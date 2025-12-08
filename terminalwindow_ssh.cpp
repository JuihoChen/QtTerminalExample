// SSH connection and interaction functionality
#include "terminalwindow.h"

#include <QTreeWidgetItem>
#include <QStatusBar>
#include <QPushButton>
#include <QLabel>

// Updated double-click handler for actual SSH connection
void TerminalWindow::onConnectionDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    
    // Check if this is a connection item (has connection data stored)
    QVariant connectionData = item->data(0, Qt::UserRole);
    if (!connectionData.canConvert<SSHConnection>()) {
        return; // This is a folder, not a connection
    }
    
    SSHConnection connection = qvariant_cast<SSHConnection>(connectionData);
    
    // Create SSH terminal with the connection
    QTermWidget *terminal = createSSHTerminal(connection);
    
    // Create appropriate tab title
    QString tabTitle = QString("SSH: %1").arg(connection.name);
    
    int index = tabWidget->addTab(terminal, tabTitle);
    tabWidget->setCurrentIndex(index);
    
    // Focus the new terminal
    terminal->setFocus();
    updateStatusBar();
    
    // Show connection status in status bar
    QString statusMessage = QString("Connecting to %1@%2:%3...")
                           .arg(connection.username, connection.host)
                           .arg(connection.port);
    if (!connection.password.isEmpty()) {
        statusMessage += " (using saved password)";
    }
    statusBar()->showMessage(statusMessage, 5000);
}

// Connect to SSH from context menu
void TerminalWindow::connectToSSH(QTreeWidgetItem *item)
{
    if (!item) return;
    
    // Trigger the same action as double-click
    onConnectionDoubleClicked(item, 0);
}

void TerminalWindow::onConnectionSelectionChanged()
{
    QList<QTreeWidgetItem*> selectedItems = connectionTree->selectedItems();
    
    if (selectedItems.isEmpty()) {
        clearConnectionConfig();
        return;
    }
    
    QTreeWidgetItem *item = selectedItems.first();
    
    // Check if this is a connection item (has connection data stored)
    QVariant connectionData = item->data(0, Qt::UserRole);
    if (!connectionData.canConvert<SSHConnection>()) {
        clearConnectionConfig();
        return; // This is a folder, not a connection
    }
    
    SSHConnection connection = qvariant_cast<SSHConnection>(connectionData);
    updateConnectionConfig(connection);
}

void TerminalWindow::updateConnectionConfig(const SSHConnection &connection)
{
    selectedConnection = connection;
    hasSelectedConnection = true;
    
    configNameLabel->setText(connection.name);
    configHostLabel->setText(connection.host);
    configUsernameLabel->setText(connection.username);
    configPortLabel->setText(QString::number(connection.port));
    configFolderLabel->setText(connection.folder.isEmpty() ? "None" : connection.folder);
    
    // Show password status without revealing the actual password
    if (connection.password.isEmpty()) {
        configPasswordLabel->setText("Not set");
        configPasswordLabel->setStyleSheet("QLabel { color: #999; font-style: italic; }");
    } else {
        configPasswordLabel->setText("••••••••");
        configPasswordLabel->setStyleSheet("QLabel { color: #333; }");
    }
    
    // Enable action buttons
    quickConnectButton->setEnabled(true);
    editConnectionButton->setEnabled(true);
    deleteConnectionButton->setEnabled(true);
}

void TerminalWindow::clearConnectionConfig()
{
    hasSelectedConnection = false;
    
    configNameLabel->setText("No connection selected");
    configHostLabel->setText("-");
    configUsernameLabel->setText("-");
    configPortLabel->setText("-");
    configPasswordLabel->setText("-");
    configFolderLabel->setText("-");
    
    // Reset password label style
    configPasswordLabel->setStyleSheet("QLabel { color: #333; }");
    
    // Disable action buttons
    quickConnectButton->setEnabled(false);
    editConnectionButton->setEnabled(false);
    deleteConnectionButton->setEnabled(false);
}

SSHConnection TerminalWindow::getCurrentSelectedConnection() const
{
    return selectedConnection;
}

void TerminalWindow::onQuickConnectClicked()
{
    if (!hasSelectedConnection) return;
    
    // Create SSH terminal with the selected connection
    QTermWidget *terminal = createSSHTerminal(selectedConnection);
    
    // Create appropriate tab title
    QString tabTitle = QString("SSH: %1").arg(selectedConnection.name);
    
    int index = tabWidget->addTab(terminal, tabTitle);
    tabWidget->setCurrentIndex(index);
    
    // Focus the new terminal
    terminal->setFocus();
    updateStatusBar();
    
    // Show connection status in status bar
    QString statusMessage = QString("Connecting to %1@%2:%3...")
                           .arg(selectedConnection.username, selectedConnection.host)
                           .arg(selectedConnection.port);
    if (!selectedConnection.password.isEmpty()) {
        statusMessage += " (using saved password)";
    }
    statusBar()->showMessage(statusMessage, 5000);
}

void TerminalWindow::onEditConnectionClicked()
{
    if (!hasSelectedConnection) return;
    
    // Find the corresponding tree item and edit it
    QList<QTreeWidgetItem*> selectedItems = connectionTree->selectedItems();
    if (!selectedItems.isEmpty()) {
        editConnection(selectedItems.first());
    }
}

void TerminalWindow::onDeleteConnectionClicked()
{
    if (!hasSelectedConnection) return;
    
    // Find the corresponding tree item and delete it
    QList<QTreeWidgetItem*> selectedItems = connectionTree->selectedItems();
    if (!selectedItems.isEmpty()) {
        deleteConnection(selectedItems.first());
    }
}
