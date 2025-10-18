// Connection management (CRUD: Create、Read、Update、Delete) functionality
#include "terminalwindow.h"
#include "connectiondialog.h"

#include <QMessageBox>
#include <QTreeWidgetItem>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QStatusBar>

QString TerminalWindow::getConnectionsFilePath() const
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir dir(configDir);
    if (!dir.exists("QtTerminalExample")) {
        dir.mkpath("QtTerminalExample");
    }
    return configDir + "/QtTerminalExample/connections.json";
}

void TerminalWindow::createDefaultConnections()
{
    // Create some default connections if no file exists
    connections.clear();
    
    // Production folder connections
    connections.append(SSHConnection("Web Server", "192.168.1.10", "user", 22, "Production"));
    connections.append(SSHConnection("Database Server", "192.168.1.20", "admin", 22, "Production"));
    
    // Development folder connections
    connections.append(SSHConnection("Dev Box", "10.0.0.5", "dev", 22, "Development"));
    connections.append(SSHConnection("Test Server", "10.0.0.6", "test", 2222, "Development"));
    
    // Personal folder connections
    connections.append(SSHConnection("My VPS", "example.com", "myuser", 22, "Personal"));
    
    // Save the default connections
    saveConnections();
    
    // ADD THIS LINE: Refresh the tree to show the default connections
    refreshConnectionTree();
}

void TerminalWindow::loadConnections()
{
    QString filePath = getConnectionsFilePath();
    QFile file(filePath);
    
    if (!file.exists()) {
        qDebug() << "Connections file doesn't exist, creating default connections";
        createDefaultConnections();
        return;
    }
    
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open connections file for reading";
        createDefaultConnections();
        return;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    
    if (error.error != QJsonParseError::NoError) {
        qDebug() << "JSON parse error:" << error.errorString();
        createDefaultConnections();
        return;
    }
    
    connections.clear();
    QJsonObject root = doc.object();
    QJsonArray connectionsArray = root["connections"].toArray();
    
    for (const QJsonValue &value : connectionsArray) {
        QJsonObject connObj = value.toObject();
        SSHConnection conn;
        conn.name = connObj["name"].toString();
        conn.host = connObj["host"].toString();
        conn.username = connObj["username"].toString();
        conn.password = connObj["password"].toString(); // Load password field
        conn.port = connObj["port"].toInt(22);
        conn.folder = connObj["folder"].toString();
        connections.append(conn);
    }
    
    qDebug() << "Loaded" << connections.size() << "connections";
    refreshConnectionTree();
    updateStatusBar();
}

void TerminalWindow::saveConnections()
{
    QString filePath = getConnectionsFilePath();
    QFile file(filePath);
    
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Failed to open connections file for writing";
        return;
    }
    
    QJsonObject root;
    QJsonArray connectionsArray;
    
    for (const SSHConnection &conn : connections) {
        QJsonObject connObj;
        connObj["name"] = conn.name;
        connObj["host"] = conn.host;
        connObj["username"] = conn.username;
        connObj["password"] = conn.password; // Save password field
        connObj["port"] = conn.port;
        connObj["folder"] = conn.folder;
        connectionsArray.append(connObj);
    }
    
    root["connections"] = connectionsArray;
    root["version"] = "1.0";
    
    QJsonDocument doc(root);
    file.write(doc.toJson());
    file.close();
    
    qDebug() << "Saved" << connections.size() << "connections";
}

void TerminalWindow::refreshConnectionTree()
{
    connectionTree->clear();
    
    // Clear selection and config panel
    clearConnectionConfig();
    
    // Create folder map to organize connections
    QMap<QString, QTreeWidgetItem*> folders;
    
    // Create folders first
    QStringList folderNames;
    for (const SSHConnection &conn : connections) {
        if (!conn.folder.isEmpty() && !folderNames.contains(conn.folder)) {
            folderNames.append(conn.folder);
        }
    }
    folderNames.sort();
    
    // Create folder items with emoji icons
    for (const QString &folderName : folderNames) {
        QString displayName;
        if (folderName == "Production") {
            displayName = "🏢 Production";
        } else if (folderName == "Development") {
            displayName = "🔧 Development";
        } else if (folderName == "Personal") {
            displayName = "👤 Personal";
        } else if (folderName == "Testing") {
            displayName = "🧪 Testing";
        } else if (folderName == "Staging") {
            displayName = "🚀 Staging";
        } else {
            displayName = "📁 " + folderName;
        }
        
        QTreeWidgetItem *folder = new QTreeWidgetItem(connectionTree);
        folder->setText(0, displayName);
        folder->setExpanded(true);
        
        // Store original folder name for reference
        folder->setData(0, Qt::UserRole + 2, folderName);
        
        folders[folderName] = folder;
    }
    
    // Add connections to their folders
    for (const SSHConnection &conn : connections) {
        QTreeWidgetItem *parent = nullptr;
        
        if (!conn.folder.isEmpty() && folders.contains(conn.folder)) {
            parent = folders[conn.folder];
        } else {
            // Create connection at root level if no folder
            parent = connectionTree->invisibleRootItem();
        }
        
        QTreeWidgetItem *item = new QTreeWidgetItem(parent);
        
        // Add emoji icon based on connection name
        QString displayName;
        QString lowerName = conn.name.toLower();
        if (lowerName.contains("web") || lowerName.contains("www")) {
            displayName = "🖥️ " + conn.name;
        } else if (lowerName.contains("database") || lowerName.contains("db")) {
            displayName = "🗄️ " + conn.name;
        } else if (lowerName.contains("dev")) {
            displayName = "💻 " + conn.name;
        } else if (lowerName.contains("test")) {
            displayName = "🧪 " + conn.name;
        } else if (lowerName.contains("vps") || lowerName.contains("cloud")) {
            displayName = "☁️ " + conn.name;
        } else {
            displayName = "🖥️ " + conn.name;
        }
        
        item->setText(0, displayName);
        
        // Set tooltip with connection details (show if password is set)
        QString tooltip = QString("%1@%2:%3").arg(conn.username, conn.host).arg(conn.port);
        if (!conn.password.isEmpty()) {
            tooltip += " (password saved)";
        }
        item->setToolTip(0, tooltip);
        
        // Store connection data in item (Feature 4)
        item->setData(0, Qt::UserRole, QVariant::fromValue(conn));
        
        // Store connection index for editing
        item->setData(0, Qt::UserRole + 1, connections.indexOf(conn));
    }
}

// Implementation: Connection Management

void TerminalWindow::addNewConnection()
{
    ConnectionDialog dialog(this);
    dialog.setAvailableFolders(getExistingFolders());
    
    if (dialog.exec() == QDialog::Accepted) {
        SSHConnection newConnection = dialog.getConnection();
        
        // Check if connection already exists
        if (connectionExists(newConnection)) {
            QMessageBox::warning(this, "Duplicate Connection", 
                "A connection with this name already exists in the same folder.");
            return;
        }
        
        // Add the connection
        connections.append(newConnection);
        saveConnections();
        refreshConnectionTree();
        updateStatusBar();
        
        statusBar()->showMessage(QString("Added connection: %1").arg(newConnection.name), 3000);
    }
}

void TerminalWindow::editConnection(QTreeWidgetItem *item)
{
    if (!item) return;
    
    // Get connection index
    int connectionIndex = item->data(0, Qt::UserRole + 1).toInt();
    if (connectionIndex < 0 || connectionIndex >= connections.size()) {
        QMessageBox::warning(this, "Error", "Connection not found.");
        return;
    }
    
    SSHConnection originalConnection = connections[connectionIndex];
    
    ConnectionDialog dialog(originalConnection, this);
    dialog.setAvailableFolders(getExistingFolders());
    
    if (dialog.exec() == QDialog::Accepted) {
        SSHConnection editedConnection = dialog.getConnection();
        
        // Check if the edited connection conflicts with existing ones (excluding the current one)
        if (connectionExists(editedConnection, connectionIndex)) {
            QMessageBox::warning(this, "Duplicate Connection", 
                "A connection with this name already exists in the same folder.");
            return;
        }
        
        // Update the connection
        connections[connectionIndex] = editedConnection;
        saveConnections();
        refreshConnectionTree();
        updateStatusBar();
        
        statusBar()->showMessage(QString("Updated connection: %1").arg(editedConnection.name), 3000);
    }
}

void TerminalWindow::deleteConnection(QTreeWidgetItem *item)
{
    if (!item) return;
    
    // Get connection index
    int connectionIndex = item->data(0, Qt::UserRole + 1).toInt();
    if (connectionIndex < 0 || connectionIndex >= connections.size()) {
        QMessageBox::warning(this, "Error", "Connection not found.");
        return;
    }
    
    SSHConnection connection = connections[connectionIndex];
    
    int ret = QMessageBox::question(this, "Delete Connection",
        QString("Are you sure you want to delete the connection '%1'?").arg(connection.name),
        QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        connections.removeAt(connectionIndex);
        saveConnections();
        refreshConnectionTree();
        updateStatusBar();
        
        statusBar()->showMessage(QString("Deleted connection: %1").arg(connection.name), 3000);
    }
}

// Helper methods for Feature 3

QStringList TerminalWindow::getExistingFolders() const
{
    QStringList folders;
    folders << "Production" << "Development" << "Personal" << "Testing" << "Staging";
    
    // Add any custom folders from existing connections
    for (const SSHConnection &conn : connections) {
        if (!conn.folder.isEmpty() && !folders.contains(conn.folder)) {
            folders.append(conn.folder);
        }
    }
    
    folders.sort();
    return folders;
}

QTreeWidgetItem* TerminalWindow::findConnectionItem(const SSHConnection &connection)
{
    // This method would help find a specific connection item in the tree
    // Implementation depends on how you want to match connections
    Q_UNUSED(connection)
    return nullptr; // Placeholder implementation
}

bool TerminalWindow::connectionExists(const SSHConnection &connection, int excludeIndex) const
{
    for (int i = 0; i < connections.size(); ++i) {
        if (i == excludeIndex) continue; // Skip the connection being edited
        
        const SSHConnection &existing = connections[i];
        if (existing.name == connection.name && existing.folder == connection.folder) {
            return true;
        }
    }
    return false;
}

void TerminalWindow::addConnectionToFolder(const QString &folderName)
{
    // Create connection with pre-selected folder
    SSHConnection conn;
    conn.folder = folderName;
    
    // Create dialog with the pre-configured connection
    ConnectionDialog dialog(conn, this);
    dialog.setWindowTitle("New Connection in " + folderName);
    dialog.setAvailableFolders(getExistingFolders());
    
    if (dialog.exec() == QDialog::Accepted) {
        SSHConnection newConnection = dialog.getConnection();
        
        // Check if connection already exists
        if (connectionExists(newConnection)) {
            QMessageBox::warning(this, "Duplicate Connection", 
                "A connection with this name already exists in the same folder.");
            return;
        }
        
        // Add the connection
        connections.append(newConnection);
        saveConnections();
        refreshConnectionTree();
        updateStatusBar();
        
        statusBar()->showMessage(QString("Added connection: %1 to %2").arg(newConnection.name, folderName), 3000);
    }
}
