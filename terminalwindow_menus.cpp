// Context menu functionality
#include "terminalwindow.h"

#include <QMenu>
#include <QTreeWidgetItem>

void TerminalWindow::showContextMenu(const QPoint &pos)
{
    QTermWidget *terminal = getCurrentTerminal();
    if (!terminal) return;

    QMenu menu;

    menu.addAction("Copy", this, &TerminalWindow::copyClipboard);
    menu.addAction("Paste", terminal, &QTermWidget::pasteClipboard);
    menu.addSeparator();
    menu.addAction("Select All", this, &TerminalWindow::selectAllText);
    menu.addSeparator();
    menu.addAction("Clear", terminal, &QTermWidget::clear);
    menu.addSeparator();
    
    menu.addAction("New Tab", this, &TerminalWindow::newTab);
    menu.addAction("Close Tab", this, &TerminalWindow::closeCurrentTab);
    menu.addSeparator();
    
    QMenu *fontMenu = menu.addMenu("Font Size");
    fontMenu->addAction("Increase", this, &TerminalWindow::increaseFont);
    fontMenu->addAction("Decrease", this, &TerminalWindow::decreaseFont);
    fontMenu->addAction("Reset", this, &TerminalWindow::resetFont);
    
    menu.addAction("Change Color Scheme", this, &TerminalWindow::changeColorScheme);

    menu.exec(terminal->mapToGlobal(pos));
}

void TerminalWindow::showConnectionContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = connectionTree->itemAt(pos);
    
    QMenu menu;
    
    if (item) {
        QVariant connectionData = item->data(0, Qt::UserRole);
        if (connectionData.canConvert<SSHConnection>()) {
            // This is a connection item
            menu.addAction("🔌 Connect", [this, item]() {
                connectToSSH(item);
            });
            menu.addSeparator();
            menu.addAction("✏️ Edit Connection", [this, item]() {
                editConnection(item);
            });
            menu.addAction("🗑️ Delete Connection", [this, item]() {
                deleteConnection(item);
            });
        } else {
            // This is a folder
            QString folderName = item->data(0, Qt::UserRole + 2).toString();
            if (!folderName.isEmpty()) {
                menu.addAction("➕ Add Connection to " + folderName, [this, folderName]() {
                    addConnectionToFolder(folderName);
                });
                menu.addSeparator();
            }
        }
    }
    
    menu.addAction("➕ New Connection", [this]() {
        addNewConnection();
    });
    menu.addSeparator();
    menu.addAction("🔄 Refresh", [this]() {
        loadConnections();  // Reload from file and refresh tree
    });
    
    if (!menu.isEmpty()) {
        menu.exec(connectionTree->mapToGlobal(pos));
    }
}

void TerminalWindow::showTabContextMenu(const QPoint &pos)
{
    int tabIndex = tabWidget->tabBar()->tabAt(pos);
    if (tabIndex == -1) return;
    
    QTermWidget *terminal = qobject_cast<QTermWidget*>(tabWidget->widget(tabIndex));
    if (!terminal) return;
    
    // Check if this is an SSH terminal
    bool isSSH = terminal->property("isSSHTerminal").toBool();
    
    QMenu menu;
    
    // Standard tab operations
    menu.addAction("📋 New Tab", this, &TerminalWindow::newTab);
    menu.addAction("❌ Close Tab", [this, tabIndex]() { closeTab(tabIndex); });
    menu.addSeparator();
    
    if (isSSH) {
        // Get SSH connection details
        QVariant connectionData = terminal->property("sshConnection");
        if (connectionData.canConvert<SSHConnection>()) {
            SSHConnection connection = qvariant_cast<SSHConnection>(connectionData);
            
            menu.addAction("📤 Upload File to Server...", [this, connection]() {
                uploadFileToSSH(connection);
            });
            
            menu.addAction("📥 Download File from Server...", [this, connection]() {
                downloadFileFromSSH(connection);
            });
            
            menu.addSeparator();
            menu.addAction("📂 Browse Remote Files...", [this, connection]() {
                browseRemoteFiles(connection);
            });
        }
    }
    
    menu.addSeparator();
    menu.addAction("🎨 Change Color Scheme", this, &TerminalWindow::changeColorScheme);
    menu.addAction("🔤 Font...", this, &TerminalWindow::openFontDialog);
    
    menu.exec(tabWidget->tabBar()->mapToGlobal(pos));
}
