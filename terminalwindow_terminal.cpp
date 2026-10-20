// Terminal creation and management functionality
#include "terminalwindow.h"
#include "enhanced_qtermwidget.h"
#include "commandsafety.h"
#include "hostscantab.h"

#include <QFont>
#include <QTimer>
#include <QApplication>
#include <QStatusBar>
#include <QClipboard>
#include <QDockWidget>
#include <QDir>
#include <QFileDialog>
#include <QSettings>
#include <QFile>
#include <QFileInfo>
#include <QHash>

static const char *kStartFolderSetting = "terminal/startFolder";

// Command lines (argv) of every process running under the given one
static QList<QStringList> descendantCommandLines(int rootPid)
{
    QHash<int, QList<int>> children;
    const QStringList entries = QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool ok = false;
        int pid = entry.toInt(&ok);
        if (!ok) continue;
        QFile stat(QString("/proc/%1/stat").arg(pid));
        if (!stat.open(QIODevice::ReadOnly)) continue;
        // "pid (name) state ppid ...", where the name itself may hold spaces or ")"
        QByteArray line = stat.readAll();
        QList<QByteArray> fields = line.mid(line.lastIndexOf(')') + 2).split(' ');
        if (fields.size() > 1) children[fields.at(1).toInt()].append(pid);
    }

    QList<QStringList> commandLines;
    QList<int> pending = children.value(rootPid);
    while (!pending.isEmpty()) {
        int pid = pending.takeFirst();
        pending += children.value(pid);
        QFile cmdline(QString("/proc/%1/cmdline").arg(pid));
        if (!cmdline.open(QIODevice::ReadOnly)) continue;
        QStringList args;
        for (const QByteArray &arg : cmdline.readAll().split('\0')) {
            if (!arg.isEmpty()) args << QString::fromLocal8Bit(arg);
        }
        if (!args.isEmpty()) commandLines << args;
    }
    return commandLines;
}

// Where a hand-typed "ssh" command logs in to
struct SSHTarget {
    QString user;
    QString host;
    int port = 22;
};

// Reads the user, host and port from an ssh command line; false if argv isn't one
static bool parseSSHCommand(const QStringList &args, SSHTarget *target)
{
    if (args.isEmpty() || QFileInfo(args.first()).fileName() != "ssh") return false;

    // ssh options that take a value (see "man ssh")
    static const QString optionsWithValue = "BbcDEeFIiJLlmOoPpRSWw";
    QString user;
    QString destination;
    int port = 22;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == "--") {
            if (i + 1 < args.size()) destination = args.at(i + 1);
            break;
        }
        if (!arg.startsWith('-') || arg.size() < 2) {
            destination = arg;
            break;
        }
        // Flags can be grouped ("-tt"); the first one taking a value ends the group
        for (int j = 1; j < arg.size(); ++j) {
            QChar option = arg.at(j);
            if (!optionsWithValue.contains(option)) continue;
            QString value = arg.mid(j + 1);
            if (value.isEmpty() && i + 1 < args.size()) value = args.at(++i);
            if (option == 'l') user = value;
            else if (option == 'p') port = value.toInt();
            break;
        }
    }
    if (destination.isEmpty()) return false;

    if (destination.startsWith("ssh://")) {
        destination = destination.mid(6);
        int portAt = destination.lastIndexOf(':');
        if (portAt > destination.lastIndexOf(']')) {
            port = destination.mid(portAt + 1).toInt();
            destination.truncate(portAt);
        }
    }
    int at = destination.lastIndexOf('@');
    if (at != -1) {
        user = destination.left(at);
        destination = destination.mid(at + 1);
    }
    if (destination.startsWith('[') && destination.endsWith(']')) {
        destination = destination.mid(1, destination.size() - 2);
    }
    // No user given: ssh logs in as the local user
    if (user.isEmpty()) user = QString::fromLocal8Bit(qgetenv("USER"));

    target->user = user;
    target->host = destination;
    target->port = port;
    return true;
}

static bool targetIsConnection(const SSHTarget &target, const SSHConnection &connection)
{
    return target.host.compare(connection.host, Qt::CaseInsensitive) == 0 &&
           target.user == connection.username && target.port == connection.port;
}

// Ends with a login question (password, passphrase or host key), so not logged in yet
static bool waitingForLoginAnswer(const QString &output)
{
    QString tail = output.trimmed().right(80).toLower();
    return (tail.endsWith(':') && (tail.contains("password") || tail.contains("passphrase") ||
                                   tail.contains("verification code"))) ||
           tail.contains("(yes/no");
}

QTermWidget* TerminalWindow::createTerminalWidget()
{
    EnhancedQTermWidget* terminal = new EnhancedQTermWidget(this);
    
    // Common settings for ALL terminals
    terminal->setHistorySize(200000);  // 200k lines - unlimited-like experience
    terminal->setColorScheme("Linux");
    terminal->setTerminalFont(QFont("Monospace", 12));
    terminal->setScrollBarPosition(QTermWidget::ScrollBarRight);
    terminal->setMotionAfterPasting(2);
    terminal->setContextMenuPolicy(Qt::CustomContextMenu);
    terminal->setWorkingDirectory(startFolder());
    
    // Common signal connections
    connect(terminal, &QWidget::customContextMenuRequested, 
            this, &TerminalWindow::showContextMenu);
    connect(terminal, &QTermWidget::finished, this, &TerminalWindow::onTerminalFinished);

    // Watch the output to notice when an SSH session in this tab ends, and,
    // once the output goes quiet, whether "ssh" was typed by hand in the tab
    QTimer *sshCheck = new QTimer(terminal);
    sshCheck->setSingleShot(true);
    connect(sshCheck, &QTimer::timeout, this, [this, terminal]() {
        checkHandTypedSSH(terminal);
    });
    connect(terminal, &QTermWidget::receivedData, this, [this, terminal, sshCheck](const QString &text) {
        if (terminal->property("isSSHTerminal").toBool()) {
            if (text.contains("Connection to") && text.contains("closed.")) {
                returnToLocalShell(terminal);
            } else if (terminal->property("handTypedSSH").toBool()) {
                // A hand-typed session can also end without that message
                sshCheck->start(500);
            }
            return;
        }
        QString recent = terminal->property("recentOutput").toString() + text;
        terminal->setProperty("recentOutput", recent.right(200));
        sshCheck->start(500);
    });
    
    return terminal;
}

// Updated createTerminal() method - for local terminals
QTermWidget* TerminalWindow::createTerminal()
{
    QTermWidget *terminal = createTerminalWidget();  // Use helper method
    
    // Local terminal specific settings
    terminal->setShellProgram("/bin/bash");
    terminal->startShellProgram();
    
    return terminal;
}

// Create SSH terminal with connection parameters and password support
QTermWidget* TerminalWindow::createSSHTerminal(const SSHConnection &connection)
{
    QTermWidget *terminal = createTerminalWidget();

    // Mark this as an SSH terminal
    terminal->setProperty("isSSHTerminal", true);
    terminal->setProperty("sshConnection", QVariant::fromValue(connection));

    // SSH terminal specific setup. A saved password reaches sshpass through
    // the shell's environment, never through the text typed into the terminal.
    terminal->setEnvironment(CommandSafetyHelper::passwordEnvironment(connection));
    terminal->setShellProgram("/bin/bash");
    terminal->startShellProgram();

    // Build safe SSH command using the helper; once ssh exits, drop the
    // password from the local shell so later commands in this tab don't get it
    QString sshCommand = CommandSafetyHelper::buildSafeSSHCommand(connection);
    if (!connection.password.isEmpty()) {
        sshCommand += "; unset SSHPASS";
    }

    // Wait for terminal to be ready and send command cleanly
    QTimer::singleShot(300, [terminal, sshCommand]() {
        terminal->sendText("clear\n");
        QTimer::singleShot(100, [terminal, sshCommand]() {
            terminal->sendText(sshCommand + "\n");
        });
    });

    return terminal;
}

// Folder new terminals start in: the saved choice, or home if unset or no longer there
QString TerminalWindow::startFolder() const
{
    QString folder = QSettings().value(kStartFolderSetting).toString();
    if (folder.isEmpty() || !QDir(folder).exists()) return QDir::homePath();
    return folder;
}

void TerminalWindow::chooseStartFolder()
{
    QString folder = QFileDialog::getExistingDirectory(this, "Start New Terminals In", startFolder());
    if (folder.isEmpty()) return;

    QSettings().setValue(kStartFolderSetting, folder);
    statusBar()->showMessage("New terminals will start in " + folder, 5000);
}

QTermWidget* TerminalWindow::getCurrentTerminal()
{
    return qobject_cast<QTermWidget*>(tabWidget->currentWidget());
}

QString TerminalWindow::getNextTabTitle()
{
    QString title = QString("Terminal %1").arg(tabCounter++);
    return title;
}

void TerminalWindow::newTab()
{
    QTermWidget *terminal = createTerminal();
    addTerminalTab(terminal, getNextTabTitle());
}

// Switches to the new terminal but holds its drawing until the shell has
// printed its prompt (or a short wait passes), so the terminal area keeps the
// previous picture instead of blinking to an empty screen while the shell starts
void TerminalWindow::addTerminalTab(QTermWidget *terminal, const QString &title)
{
    terminal->setUpdatesEnabled(false);
    QTimer *reveal = new QTimer(terminal);
    reveal->setSingleShot(true);
    connect(reveal, &QTimer::timeout, terminal, [terminal, reveal]() {
        reveal->deleteLater();
        terminal->setUpdatesEnabled(true);
    });
    // Give the rest of the prompt a moment to arrive
    connect(terminal, &QTermWidget::receivedData, reveal, [reveal]() {
        if (reveal->remainingTime() > 50) reveal->start(50);
    });
    reveal->start(500);

    int index = tabWidget->addTab(terminal, title);
    tabWidget->setCurrentIndex(index);
    terminal->setFocus();
    updateStatusBar();
}

void TerminalWindow::closeTab(int index)
{
    QWidget *widget = tabWidget->widget(index);
    if (widget && widget == hostScanTab) {
        // Closing the scan tab never closes the window; a running scan stops
        tabWidget->removeTab(index);
        widget->deleteLater();
        updateStatusBar();
        return;
    }

    int terminalTabs = tabWidget->count() - (hostScanTab ? 1 : 0);
    if (terminalTabs <= 1) {
        // For the last terminal, just close the window directly without asking
        // The closeEvent() will handle the confirmation
        close();
        return;
    }
    
    if (!confirmCloseWithTransfers(widget)) return;
    removeFileBrowserPane(widget);
    tabWidget->removeTab(index);
    widget->deleteLater();
    
    updateStatusBar();
}

void TerminalWindow::closeCurrentTab()
{
    closeTab(tabWidget->currentIndex());
}

void TerminalWindow::onTabChanged(int index)
{
    Q_UNUSED(index)
    updateStatusBar();
    if (fileBrowserDock) {
        // The scan tab has no files, so the File Browser steps aside for it
        // and comes back on the next terminal tab if it was open
        bool onScanTab = hostScanTab && tabWidget->currentWidget() == hostScanTab;
        if (onScanTab && !fileBrowserDock->isHidden()) {
            fileBrowserHiddenForScan = true;
            fileBrowserDock->hide();
        } else if (!onScanTab && fileBrowserHiddenForScan) {
            fileBrowserHiddenForScan = false;
            fileBrowserDock->show();
        }
        updateFileBrowser();
    }
    
    // Focus the current terminal
    QTermWidget *terminal = getCurrentTerminal();
    if (terminal) {
        terminal->setFocus();
    }
}

void TerminalWindow::onTerminalFinished()
{
    // Find which terminal finished and close its tab
    QTermWidget *finishedTerminal = qobject_cast<QTermWidget*>(sender());
    if (!finishedTerminal) return;
    
    // Find the tab index
    int tabIndex = -1;
    for (int i = 0; i < tabWidget->count(); ++i) {
        if (tabWidget->widget(i) == finishedTerminal) {
            tabIndex = i;
            break;
        }
    }
    
    if (tabIndex == -1) return;
    
    // Check if this was an SSH terminal
    bool isSSH = finishedTerminal->property("isSSHTerminal").toBool();
    if (isSSH) {
        // When SSH connection ends, the terminal returns to local shell
        returnToLocalShell(finishedTerminal);
        
        // Don't return here - let the terminal continue running as local
        // The terminal process itself hasn't finished, just the SSH connection
        // So we don't want to close or recreate anything
        return;
    }
    
    // For local terminals that actually finished, continue with normal closure logic
    if (tabWidget->count() - (hostScanTab ? 1 : 0) <= 1) {
        QApplication::quit();
        return;
    }
    
    // For multiple tabs, just close this tab
    QWidget *widget = tabWidget->widget(tabIndex);
    tabWidget->removeTab(tabIndex);
    widget->deleteLater();
    
    updateStatusBar();
}

void TerminalWindow::copyClipboard()
{
        QTermWidget *terminal = getCurrentTerminal();
        if (!terminal) return;  // The scan tab has no terminal
        //if (terminal) terminal->copyClipboard();
        QString text = terminal->selectedText(true);
        if (!text.isEmpty()) QApplication::clipboard()->setText(text);

}

void TerminalWindow::selectAllText()
{
    QTermWidget *terminal = getCurrentTerminal();
    if (terminal) {
        // Send Ctrl+A to select all text (universal terminal shortcut)
        //terminal->sendText("\x01"); // Ctrl+A ASCII code
        qobject_cast<EnhancedQTermWidget*>(terminal)->selectAll();
    }
}

// The SSH session in this tab ended: the tab is a local shell again, and
// remembers its server in case the user logs back in to it by hand
void TerminalWindow::returnToLocalShell(QTermWidget *terminal)
{
    int tabIndex = tabWidget->indexOf(terminal);
    if (tabIndex == -1) return;

    QVariant connection = terminal->property("sshConnection");
    if (connection.isValid()) terminal->setProperty("lastSSHConnection", connection);
    terminal->setProperty("recentOutput", QString());
    terminal->setProperty("handTypedSSH", false);

    tabWidget->setTabText(tabIndex, "Terminal");
    terminal->setProperty("isSSHTerminal", false);
    terminal->setProperty("sshConnection", QVariant());
    if (terminal == getCurrentTerminal()) updateFileBrowser();
    else removeFileBrowserPane(terminal);
    updateStatusBar();
    statusBar()->showMessage("SSH connection closed - returned to local shell", 3000);
}

// Runs once a local tab's output goes quiet. If "ssh" typed in the tab has
// logged in to a saved connection, the tab becomes that connection's SSH tab,
// so the File Browser follows it; a host that isn't saved is noted instead,
// since the File Browser has no login for it.
void TerminalWindow::checkHandTypedSSH(QTermWidget *terminal)
{
    int tabIndex = tabWidget->indexOf(terminal);
    if (tabIndex == -1) return;
    bool isSSH = terminal->property("isSSHTerminal").toBool();
    bool handTyped = terminal->property("handTypedSSH").toBool();
    if (isSSH && !handTyped) return;  // Opened from the connection list

    QList<SSHTarget> targets;
    for (const QStringList &args : descendantCommandLines(terminal->getShellPID())) {
        SSHTarget target;
        if (parseSSHCommand(args, &target)) targets << target;
    }

    // The connection this tab last had comes first, then the others in list order
    QList<SSHConnection> candidates;
    QVariant last = terminal->property("lastSSHConnection");
    if (last.canConvert<SSHConnection>()) candidates << qvariant_cast<SSHConnection>(last);
    candidates += connections;

    const SSHConnection *match = nullptr;
    for (const SSHConnection &connection : candidates) {
        for (const SSHTarget &target : targets) {
            if (targetIsConnection(target, connection)) {
                match = &connection;
                break;
            }
        }
        if (match) break;
    }

    if (isSSH) {
        SSHConnection current = qvariant_cast<SSHConnection>(terminal->property("sshConnection"));
        if (!match || !(*match == current)) returnToLocalShell(terminal);
        return;
    }

    QString unsavedHost;
    if (!match && !targets.isEmpty()) {
        unsavedHost = QString("%1@%2").arg(targets.first().user, targets.first().host);
    }
    if (terminal->property("unsavedSSHHost").toString() != unsavedHost) {
        terminal->setProperty("unsavedSSHHost", unsavedHost);
        if (terminal == getCurrentTerminal()) updateFileBrowser();
    }

    if (!match || waitingForLoginAnswer(terminal->property("recentOutput").toString())) return;

    terminal->setProperty("recentOutput", QString());
    terminal->setProperty("handTypedSSH", true);
    terminal->setProperty("isSSHTerminal", true);
    terminal->setProperty("sshConnection", QVariant::fromValue(*match));
    tabWidget->setTabText(tabIndex, QString("SSH: %1").arg(match->name));
    if (terminal == getCurrentTerminal()) updateFileBrowser();
    updateStatusBar();
    statusBar()->showMessage(QString("Logged in to %1 - File Browser follows this tab").arg(match->name), 3000);
}
