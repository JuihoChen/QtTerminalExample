// File browser dock: a WinSCP-style SFTP browser for the current SSH tab
#include "terminalwindow.h"
#include "filebrowserpane.h"
#include "hostscantab.h"
#include "inwindowdialog.h"

#include <QDockWidget>
#include <QMessageBox>
#include <QStackedWidget>
#include <QVBoxLayout>

static const char *kPlaceholderText =
    "Open or switch to an SSH tab to browse its files.\n\n"
    "Double-click a connection on the left to start one.";

void TerminalWindow::setupFileBrowserDock()
{
    fileBrowserDock = new QDockWidget("File Browser", this);
    fileBrowserDock->setObjectName("FileBrowserDock");
    fileBrowserDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea |
                                     Qt::BottomDockWidgetArea);
    // Qt's own float and close buttons are tiny, so give them bundled icons
    // drawn larger and round grey backs, like the About dialog's close button
    fileBrowserDock->setStyleSheet(
        "QDockWidget { titlebar-close-icon: url(:/icons/dock-close.png);"
        "  titlebar-normal-icon: url(:/icons/dock-float.png); }"
        "QDockWidget::close-button, QDockWidget::float-button {"
        "  subcontrol-origin: padding; subcontrol-position: right center;"
        "  min-width: 22px; min-height: 22px; icon-size: 16px;"
        "  border: none; border-radius: 11px; background: rgba(0, 0, 0, 0.06); }"
        "QDockWidget::close-button { right: 3px; }"
        "QDockWidget::float-button { right: 29px; }"
        "QDockWidget::close-button:hover, QDockWidget::float-button:hover {"
        "  background: rgba(0, 0, 0, 0.13); }"
        "QDockWidget::close-button:pressed, QDockWidget::float-button:pressed {"
        "  background: rgba(0, 0, 0, 0.20); }");

    fileBrowserStack = new QStackedWidget(fileBrowserDock);
    // Tabs without a server still get the local folder, under a note
    fileBrowserPlaceholder = new FileBrowserPane(fileBrowserStack);
    fileBrowserPlaceholder->setMessage(kPlaceholderText);
    fileBrowserStack->addWidget(fileBrowserPlaceholder);
    fileBrowserScanPage = new QWidget(fileBrowserStack);
    QVBoxLayout *scanLayout = new QVBoxLayout(fileBrowserScanPage);
    scanLayout->addStretch();  // Import options are inserted above, at the top
    fileBrowserStack->addWidget(fileBrowserScanPage);

    fileBrowserDock->setWidget(fileBrowserStack);
    addDockWidget(Qt::RightDockWidgetArea, fileBrowserDock);
    fileBrowserDock->hide();

    // Panes connect to the server only while the dock is open: closing it
    // logs them out, and the shown tab's pane logs back in when it reopens
    connect(fileBrowserDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            updateFileBrowser();
            return;
        }
        for (FileBrowserPane *pane : qAsConst(fileBrowserPanes)) pane->suspendConnection();
        if (hostScanTab) hostScanTab->takeBackImportSection();  // Keep them reachable
    });
}

void TerminalWindow::showFileBrowser()
{
    fileBrowserDock->show();
    fileBrowserDock->raise();

    // Start wide enough for the remote columns; later the user's width sticks
    if (!fileBrowserDock->property("initialSizeSet").toBool() && !fileBrowserDock->isFloating()) {
        fileBrowserDock->setProperty("initialSizeSet", true);
        resizeDocks(QList<QDockWidget *>() << fileBrowserDock, QList<int>() << 600, Qt::Horizontal);
    }
    updateFileBrowser();
}

// Shows the pane belonging to the current tab, creating it on first use
void TerminalWindow::updateFileBrowser()
{
    // The scan tab has no files: its import options use the panel instead,
    // so the panel keeps its width and nothing moves when switching tabs
    if (hostScanTab) {
        if (tabWidget->currentWidget() == hostScanTab && !fileBrowserDock->isHidden()) {
            hostScanTab->lendImportSection(fileBrowserScanPage);
            fileBrowserStack->setCurrentWidget(fileBrowserScanPage);
            fileBrowserDock->setWindowTitle("Import Scanned Hosts - SSH Scan");
            return;
        }
        hostScanTab->takeBackImportSection();
    }

    QTermWidget *terminal = getCurrentTerminal();
    bool isSSH = terminal && terminal->property("isSSHTerminal").toBool();
    QVariant data = terminal ? terminal->property("sshConnection") : QVariant();

    if (!isSSH || !data.canConvert<SSHConnection>()) {
        if (terminal) removeFileBrowserPane(terminal);
        QString unsavedHost = terminal ? terminal->property("unsavedSSHHost").toString() : QString();
        fileBrowserPlaceholder->setMessage(unsavedHost.isEmpty() ? kPlaceholderText
            : QString("This tab is logged in to %1, which isn't a saved connection, "
                      "so there is no login to browse its files with.\n\n"
                      "Save it as a connection on the left to browse it.").arg(unsavedHost));
        fileBrowserStack->setCurrentWidget(fileBrowserPlaceholder);
        fileBrowserDock->setWindowTitle("File Browser");
        return;
    }

    SSHConnection connection = qvariant_cast<SSHConnection>(data);
    fileBrowserDock->setWindowTitle(QString("File Browser - %1").arg(connection.name));

    FileBrowserPane *pane = fileBrowserPanes.value(terminal);
    if (pane && !(pane->connection() == connection)) {
        removeFileBrowserPane(terminal);
        pane = nullptr;
    }
    if (!pane) {
        if (!fileBrowserDock->isVisible()) return;
        pane = new FileBrowserPane(connection, fileBrowserStack);
        fileBrowserStack->addWidget(pane);
        fileBrowserPanes.insert(terminal, pane);
        // Tied to the pane, so it can't fire once the window has deleted the panes
        connect(terminal, &QObject::destroyed, pane, [this, terminal]() {
            removeFileBrowserPane(terminal);
        });
    }
    if (fileBrowserDock->isVisible()) pane->resumeConnection();
    fileBrowserStack->setCurrentWidget(pane);
}

void TerminalWindow::removeFileBrowserPane(QWidget *terminal)
{
    FileBrowserPane *pane = fileBrowserPanes.take(terminal);
    if (!pane) return;
    fileBrowserStack->removeWidget(pane);
    delete pane;
}

bool TerminalWindow::confirmCloseWithTransfers(QWidget *terminal)
{
    FileBrowserPane *pane = fileBrowserPanes.value(terminal);
    if (!pane) return true;
    if (pane->hasActiveTransfers()) {
        return InWindowDialog::question(this, "Transfers in Progress",
            "This tab still has transfers running. Closing it cancels them.\n\nClose anyway?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    }
    if (pane->hasOpenEdits()) {
        return InWindowDialog::question(this, "Files Open for Editing",
            "Files from this server are open in your editor. After the tab closes, "
            "saving them no longer updates the server.\n\nClose anyway?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    }
    return true;
}
