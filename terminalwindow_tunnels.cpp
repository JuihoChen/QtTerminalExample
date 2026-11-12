// SSH port forwarding: which tunnels a new SSH tab starts, and the status bar
// button and tab menu that show them with "Open in browser" / "Copy address"
#include "terminalwindow.h"
#include "bmcpower.h"
#include "commandsafety.h"
#include "inwindowdialog.h"
#include "proxybrowser.h"
#include "tunnelsdialog.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QHash>
#include <QHostAddress>
#include <QMenu>
#include <QNetworkProxy>
#include <QProcess>
#include <QStatusBar>
#include <QTcpServer>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidgetItem>
#include <QUrl>

namespace {

// Whether this PC can open a listening socket on the port, the way ssh does
// for -L and -D (it binds the loopback address unless told otherwise)
bool localPortFree(int port, QString *error = nullptr)
{
    QTcpServer probe;
    probe.setProxy(QNetworkProxy::NoProxy);
    if (!probe.listen(QHostAddress::LocalHost, static_cast<quint16>(port))) {
        if (error) *error = probe.errorString();
        return false;
    }
    probe.close();
    return true;
}

// WSL usually has no Linux browser for xdg-open to start, so hand the URL to
// Windows there; elsewhere use the desktop's default browser
void openInBrowser(const QString &url)
{
    if (!qEnvironmentVariableIsEmpty("WSL_DISTRO_NAME")
        && QProcess::startDetached("explorer.exe", {url}))
        return;
    QDesktopServices::openUrl(QUrl(url));
}

QString webUrl(const SSHTunnel &tunnel, bool https)
{
    return QString("%1://localhost:%2%3").arg(https ? "https" : "http").arg(tunnel.listenPort).arg(tunnel.urlPath());
}

// Asks which address the proxy browser opens first: the ones used before on
// this port (and BMCs the scan routed through it) are in the list. Returns
// false on Cancel; an empty *url means a blank page.
bool askStartUrl(QWidget *parent, int port, QString *url)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("Open Browser Through Proxy");
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *label = new QLabel(QString(
        "Starts a separate browser window whose traffic goes through the SOCKS proxy on "
        "port %1, so it can open any address the server can reach, such as a BMC by its IP.\n\n"
        "Address to open (optional):").arg(port), &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    QComboBox *combo = new QComboBox(&dialog);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->addItems(ProxyBrowser::recentUrls(port));
    combo->lineEdit()->setPlaceholderText("e.g. 10.0.0.5 or https://bmc-01/");
    combo->setMinimumWidth(360);
    layout->addWidget(combo);

    QLabel *note = new QLabel("It uses its own profile, so your normal browser and its tabs are left "
                              "alone. BMCs often have self-signed certificates: the browser will ask "
                              "you to proceed past a warning.", &dialog);
    note->setWordWrap(true);
    note->setStyleSheet("QLabel { color: #666; }");
    layout->addWidget(note);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Open Browser");
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    combo->setFocus();
    combo->lineEdit()->selectAll();

    if (InWindowDialog::exec(&dialog) != QDialog::Accepted) return false;
    *url = combo->currentText().trimmed();
    return true;
}

// The BMC host of a plain https://<host>/ address, or empty for anything else
QString bmcHostOf(const QString &url)
{
    QUrl u(url);
    if (u.scheme() != "https" || u.host().isEmpty() || (u.port() != -1 && u.port() != 443)) return QString();
    if (!u.path().isEmpty() && u.path() != "/") return QString();
    return u.host();
}

} // namespace

// "⚡ BMC power" for one BMC the tab's tunnels lead to. The commands run on
// the tab's server, which reaches the BMC by its own address.
void TerminalWindow::addBmcPowerMenu(QMenu *menu, QTermWidget *terminal, const QString &text,
                                     const QString &host, const QString &name)
{
    SSHConnection via = terminal->property("sshConnection").value<SSHConnection>();
    QMenu *power = menu->addMenu(text);
    if (via.host.isEmpty()) {
        power->setEnabled(false);
        return;
    }
    BmcPower::Hooks hooks;
    QPointer<TerminalWindow> self = this;
    hooks.status = [self](const QString &message) {
        if (self) self->statusBar()->showMessage(message, 8000);
    };
    hooks.openConsole = [self, via, host, name]() {
        if (self) self->openBmcConsole(via, true, host, name);
    };
    BmcPower::addActions(power, this, via, true, host, name, hooks);
}

// A tab running the BMC's serial console: "ssh -t" to via with the console
// script, or the script right here. ssh's own ~ escapes are turned off
// (-e none) so ~. reaches ipmitool and leaves the console, not the login.
void TerminalWindow::openBmcConsole(const SSHConnection &via, bool throughVia, const QString &host,
                                    const QString &name)
{
    QPointer<TerminalWindow> self = this;
    BmcPower::Hooks hooks;
    hooks.status = [self](const QString &message) {
        if (self) self->statusBar()->showMessage(message, 8000);
    };
    BmcPower::prepareConsole(this, via, throughVia, host, name, [self, via, throughVia, host, name](const QString &consoleCommand) {
        if (!self) return;
        QTermWidget *terminal = self->createTerminalWidget();
        // Not an SSH tab of via: no file browser or tunnels, and the
        // hand-typed ssh check leaves it alone
        terminal->setProperty("bmcConsole", true);
        QString command;
        if (throughVia) {
            terminal->setEnvironment(CommandSafetyHelper::passwordEnvironment(via));
            command = CommandSafetyHelper::buildPasswordPrefix(via) + "ssh -t -e none "
                    + CommandSafetyHelper::buildCommonOptions(via);
            if (via.port != 22) command += QString("-p %1 ").arg(via.port);
            command += QString("%1@%2 %3").arg(CommandSafetyHelper::escapeShellArgument(via.username),
                                               CommandSafetyHelper::escapeShellArgument(via.host),
                                               CommandSafetyHelper::escapeShellArgument(consoleCommand));
            if (!via.password.isEmpty()) command += "; unset SSHPASS";
        } else {
            command = consoleCommand;
        }
        // When the console ends the tab waits for Enter, so its last words stay readable
        command += "; echo; read -r -p 'Press Enter to close this tab.' _; exit";
        terminal->setShellProgram("/bin/bash");
        terminal->startShellProgram();
        QString label = name.isEmpty() ? host : name;
        self->addTerminalTab(terminal, QString("SOL: %1").arg(label));
        QTimer::singleShot(300, terminal, [terminal, command]() {
            terminal->sendText("clear\n");
            QTimer::singleShot(100, terminal, [terminal, command]() { terminal->sendText(command + "\n"); });
        });
        self->statusBar()->showMessage(QString("Serial console of %1 - leave it with ~. at the start of a line")
                                           .arg(label), 8000);
    }, hooks);
}

// The tunnels a new SSH tab for this connection can start. A local port that
// is already taken (by another program, or by another tab of the same
// connection) is left out and described in *skipped, so the login still works.
QList<SSHTunnel> TerminalWindow::startableTunnels(const SSHConnection &connection, QStringList *skipped)
{
    QList<SSHTunnel> result;
    for (const SSHTunnel &tunnel : connection.tunnels) {
        if (!tunnel.enabled) continue;
        QString problem = tunnel.problem();
        if (!problem.isEmpty()) {
            if (skipped) *skipped << QString("%1: %2").arg(tunnel.describe(), problem);
            continue;
        }
        QString bindError;
        if (tunnel.listensLocally() && !localPortFree(tunnel.listenPort, &bindError)) {
            if (skipped) {
                *skipped << QString("%1: local port %2 can't be opened (%3). %4")
                    .arg(tunnel.describe()).arg(tunnel.listenPort).arg(bindError)
                    .arg(tunnel.listenPort < 1024
                         ? QString("Ports below 1024 need admin rights.")
                         : QString("To see what holds it, run: ss -ltnp | grep :%1").arg(tunnel.listenPort));
            }
            continue;
        }
        result << tunnel;
    }
    return result;
}

QList<SSHTunnel> TerminalWindow::activeTunnels(QTermWidget *terminal) const
{
    if (!terminal || !terminal->property("isSSHTerminal").toBool()) return QList<SSHTunnel>();
    return terminal->property("activeTunnels").value<QList<SSHTunnel>>();
}

void TerminalWindow::setupTunnelsButton()
{
    tunnelsButton = new QToolButton(this);
    tunnelsButton->setAutoRaise(true);
    tunnelsButton->setPopupMode(QToolButton::InstantPopup);
    tunnelsButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    QMenu *menu = new QMenu(tunnelsButton);
    connect(menu, &QMenu::aboutToShow, this, [this, menu]() {
        menu->clear();
        addTunnelActions(menu, getCurrentTerminal());
    });
    tunnelsButton->setMenu(menu);
    tunnelsButton->hide();
    statusBar()->addPermanentWidget(tunnelsButton);
}

// Shows the current tab's tunnels in the status bar, or hides the button
void TerminalWindow::updateTunnelsButton()
{
    if (!tunnelsButton) return;
    QList<SSHTunnel> tunnels = activeTunnels(getCurrentTerminal());
    if (tunnels.isEmpty()) {
        tunnelsButton->hide();
        return;
    }
    tunnelsButton->setText(tunnels.size() == 1 ? QString("🔀 1 tunnel")
                                               : QString("🔀 %1 tunnels").arg(tunnels.size()));
    QStringList lines;
    for (const SSHTunnel &t : tunnels) lines << t.describeNamed();
    tunnelsButton->setToolTip("Tunnels of this SSH tab:\n" + lines.join("\n") +
                              "\n\nClick to open or copy their addresses.");
    tunnelsButton->show();
}

// One section per tunnel of the tab: its state, then what can be done with it
void TerminalWindow::addTunnelActions(QMenu *menu, QTermWidget *terminal)
{
    QList<SSHTunnel> tunnels = activeTunnels(terminal);
    if (tunnels.isEmpty()) {
        QAction *none = menu->addAction("No tunnels in this tab");
        none->setEnabled(false);
        return;
    }

    // A rack's worth of BMC forwards would make a menu taller than the
    // screen, so past a few tunnels each one gets a submenu instead
    const bool submenus = tunnels.size() > 5;
    QMenu *parentMenu = menu;
    for (const SSHTunnel &tunnel : tunnels) {
        menu = parentMenu;
        // Disabled menu rows are drawn grey, so the state is spelled out in words
        QString state;
        bool listening = false;
        if (tunnel.type == SSHTunnel::Remote) {
            state = "↩ On the server";  // This PC can't check a port opened over there
        } else {
            listening = !localPortFree(tunnel.listenPort);
            state = listening ? "🟢 Listening" : "⚪ Not listening";
        }
        QString titleText = QString("%1 (%2) · %3").arg(state, tunnel.flag(), tunnel.describeNamed());
        QString notListening = "Nothing is listening on this port: the login hasn't finished, "
                               "or ssh could not open the tunnel (see the terminal)";
        if (submenus) {
            menu = parentMenu->addMenu(titleText);
            menu->setToolTipsVisible(true);
            if (tunnel.type != SSHTunnel::Remote && !listening) menu->menuAction()->setToolTip(notListening);
        } else {
            QAction *title = menu->addAction(titleText);
            title->setEnabled(false);
            if (tunnel.type != SSHTunnel::Remote && !listening) title->setToolTip(notListening);
        }
        const QString indent = submenus ? QString() : QStringLiteral("    ");

        if (tunnel.type == SSHTunnel::Local) {
            bool https = tunnel.looksLikeHttps();
            menu->addAction(QString(indent + "🌐 Open %1").arg(webUrl(tunnel, https)), this, [tunnel, https]() {
                openInBrowser(webUrl(tunnel, https));
            });
            menu->addAction(QString(indent + "🌐 Open %1").arg(webUrl(tunnel, !https)), this, [tunnel, https]() {
                openInBrowser(webUrl(tunnel, !https));
            });
            QString address = QString("localhost:%1").arg(tunnel.listenPort);
            menu->addAction(QString(indent + "📋 Copy address %1").arg(address), this, [this, address]() {
                QApplication::clipboard()->setText(address);
                statusBar()->showMessage("Copied " + address, 3000);
            });
            // A forward to port 443 is most likely a BMC (the scan makes these)
            if (https && tunnel.targetPort == 443 && !tunnel.targetHost.trimmed().isEmpty()
                && tunnel.targetHost.trimmed() != "localhost") {
                addBmcPowerMenu(menu, terminal, indent + "⚡ BMC power", tunnel.targetHost.trimmed(), tunnel.name.trimmed());
            }
        } else if (tunnel.type == SSHTunnel::Dynamic) {
            int socksPort = tunnel.listenPort;
            // One click per BMC: the addresses used before on this proxy (and the
            // BMCs the scan routed through it), then what this tab forwards to
            // port 443, which the proxy reaches by its real IP as well
            QStringList targets = ProxyBrowser::recentUrls(socksPort);
            QHash<QString, QString> names;
            for (const QString &url : targets) names.insert(url, ProxyBrowser::nameFor(socksPort, url));
            for (const SSHTunnel &other : tunnels) {
                if (other.type == SSHTunnel::Local && other.targetPort == 443 && !other.targetHost.trimmed().isEmpty()) {
                    QString url = ProxyBrowser::normalizeUrl(other.targetHost.trimmed());
                    if (!targets.contains(url)) targets << url;
                    if (names.value(url).isEmpty()) names.insert(url, other.name.trimmed());
                }
            }
            // A few go straight in the menu, newest first; a rack's worth get a
            // submenu of their own, in address order
            const int kMaxDirect = 5;
            QMenu *targetMenu = menu;
            QString targetIndent = indent;
            if (targets.size() > kMaxDirect) {
                targetMenu = menu->addMenu(QString(indent + "🌐 Open through this proxy (%1)").arg(targets.size()));
                targetIndent.clear();
                targets = ProxyBrowser::sortedByAddress(targets);
            }
            for (const QString &url : targets) {
                QString text = targets.size() > kMaxDirect
                    ? ProxyBrowser::label(url, names.value(url))
                    : QString("Open %1 through this proxy").arg(ProxyBrowser::label(url, names.value(url)));
                QAction *action = targetMenu->addAction(targetIndent + "🌐 " + text, this, [this, socksPort, url]() {
                    openProxyBrowser(socksPort, url);
                });
                action->setToolTip(url);
            }
            menu->addAction(indent + (targets.isEmpty() ? "🌐 Open browser through this proxy..."
                                                        : "🌐 Open another address through this proxy..."),
                            this, [this, socksPort]() { openProxyBrowser(socksPort); });
            // Power commands for the BMCs among them, run on the tab's server
            QStringList bmcs;
            for (const QString &url : targets) {
                if (!bmcHostOf(url).isEmpty()) bmcs << url;
            }
            if (bmcs.size() == 1) {
                const QString &url = bmcs.first();
                addBmcPowerMenu(menu, terminal, indent + "⚡ BMC power · " + ProxyBrowser::label(url, names.value(url)),
                                bmcHostOf(url), names.value(url));
            } else if (!bmcs.isEmpty()) {
                QMenu *powerMenu = menu->addMenu(QString(indent + "⚡ BMC power (%1)").arg(bmcs.size()));
                for (const QString &url : ProxyBrowser::sortedByAddress(bmcs)) {
                    addBmcPowerMenu(powerMenu, terminal, ProxyBrowser::label(url, names.value(url)),
                                    bmcHostOf(url), names.value(url));
                }
            }
            QString proxy = QString("socks5://localhost:%1").arg(tunnel.listenPort);
            menu->addAction(QString(indent + "📋 Copy proxy address %1").arg(proxy), this, [this, proxy]() {
                QApplication::clipboard()->setText(proxy);
                statusBar()->showMessage("Copied " + proxy, 3000);
            });
            int port = tunnel.listenPort;
            menu->addAction(indent + "❓ How to use this SOCKS proxy...", this, [this, port]() {
                InWindowDialog::information(this, "SOCKS Proxy",
                    QString("This tab runs a SOCKS5 proxy on localhost:%1. Traffic sent through it "
                            "leaves from the server, so a browser can open any address the server "
                            "can reach, such as each BMC by its own IP.\n\n"
                            "Firefox: Settings > Network Settings > Manual proxy configuration. "
                            "Set SOCKS Host to localhost, Port to %1, choose SOCKS v5 and tick "
                            "\"Proxy DNS when using SOCKS v5\".\n\n"
                            "Chrome or Edge: the \"Open ... through this proxy\" entries in this menu do it "
                            "for you, or start a separate window with\n"
                            "  --proxy-server=\"socks5://localhost:%1\" --user-data-dir=<new folder>\n\n"
                            "Command line: curl --socks5-hostname localhost:%1 https://10.x.y.z/")
                        .arg(port));
            });
        } else {
            QString address = QString("localhost:%1").arg(tunnel.listenPort);
            menu->addAction(QString(indent + "📋 Copy server-side address %1").arg(address), this, [this, address]() {
                QApplication::clipboard()->setText(address);
                statusBar()->showMessage("Copied " + address + " (use it on the server)", 3000);
            });
        }
        if (!submenus) menu->addSeparator();
    }
    menu = parentMenu;
    if (submenus) menu->addSeparator();
    QAction *note = menu->addAction("Tunnels close when the SSH session ends");
    note->setEnabled(false);
}

// Connection menu > Tunnels...: edit the saved connection's port forwards.
// They apply to SSH tabs opened from now on; open tabs keep theirs.
void TerminalWindow::editTunnels(QTreeWidgetItem *item)
{
    if (!item) return;
    int index = item->data(0, Qt::UserRole + 1).toInt();
    if (index < 0 || index >= connections.size()) {
        InWindowDialog::warning(this, "Error", "Connection not found.");
        return;
    }

    TunnelsDialog dialog(connections[index], this);
    if (InWindowDialog::exec(&dialog) != QDialog::Accepted) return;

    QList<SSHTunnel> tunnels = dialog.tunnels();
    if (tunnels == connections[index].tunnels) return;
    connections[index].tunnels = tunnels;
    QString name = connections[index].name;
    saveConnections();
    refreshConnectionTree();
    updateStatusBar();
    statusBar()->showMessage(QString("Saved tunnels of %1 - they start with its next SSH tab").arg(name), 5000);
}

// Tunnels menu > Open ... through this proxy: a separate Chrome-family
// window with its own profile and --proxy-server pointing at the SOCKS port
void TerminalWindow::openProxyBrowser(int port, const QString &startUrl)
{
    if (localPortFree(port)) {
        InWindowDialog::warning(this, "SOCKS Proxy Not Running",
            QString("Nothing is listening on port %1 yet: the login hasn't finished, or ssh "
                    "couldn't open the proxy (see the terminal). Try again once the SSH tab "
                    "is logged in.").arg(port));
        return;
    }
    // A menu entry names its address; otherwise ask for one
    QString url = startUrl;
    if (url.isEmpty() && !askStartUrl(this, port, &url)) return;

    QString error, browser;
    if (!ProxyBrowser::launch(port, url, &error, &browser)) {
        InWindowDialog::warning(this, "Couldn't Open Browser", error);
        return;
    }
    ProxyBrowser::remember(port, url);
    statusBar()->showMessage(QString("Started %1 through the SOCKS proxy on port %2").arg(browser).arg(port), 5000);
}
