// ============ Updated terminalwindow.cpp with Split Left Pane ============
#include "terminalwindow.h"
#include "connectiondialog.h"
#include "enhanced_qtermwidget.h"

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QAction>
#include <QMenuBar>
#include <QLabel>
#include <QStatusBar>
#include <QFont>
#include <QFontDialog>
#include <QColorDialog>
#include <QFileDialog>
#include <QSettings>
#include <QCloseEvent>
#include <QMessageBox>
#include <QKeySequence>
#include <QTabWidget>
#include <QTabBar>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QInputDialog>
#include <QTimer>
#include <QProcess>
#include <QGroupBox>
#include <QFormLayout>
#include <QPushButton>
#include <QTextEdit>
#include <QListWidget>
#include <QClipboard>

// Update the constructor to load connections:
TerminalWindow::TerminalWindow(QWidget *parent) 
    : QMainWindow(parent), tabWidget(nullptr), tabCounter(1), hasSelectedConnection(false)
{
    setupUI();
    setupMenus();
    loadSettings();
    
    // Load connections from file
    loadConnections();
    
    // Create first tab
    newTab();
}

// Update destructor to save connections on exit:
TerminalWindow::~TerminalWindow()
{
    saveConnections();  // Save connections before exit
    saveSettings();
}

void TerminalWindow::closeEvent(QCloseEvent *event)
{
    // Always ask for confirmation when closing the window
    QString message;
    if (tabWidget->count() > 1) {
        message = QString("Close all %1 terminal tabs?").arg(tabWidget->count());
    } else {
        message = "Close terminal?";
    }
    
    int ret = QMessageBox::question(this, "Close Terminal", message,
                                  QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::No) {
        event->ignore();
        return;
    }
    event->accept();
}

void TerminalWindow::openFontDialog()
{
    QTermWidget *terminal = getCurrentTerminal();
    if (!terminal) return;
    
    bool ok;
    QFont font = QFontDialog::getFont(&ok, terminal->getTerminalFont(), this);
    if (ok) {
        // Apply font to all terminals
        for (int i = 0; i < tabWidget->count(); ++i) {
            QTermWidget *term = qobject_cast<QTermWidget*>(tabWidget->widget(i));
            if (term) {
                term->setTerminalFont(font);
            }
        }
        updateStatusBar();
    }
}

void TerminalWindow::changeColorScheme()
{
    static QStringList schemes = {"Linux", "GreenOnBlack", "WhiteOnBlack", "BlackOnWhite"};
    static int currentScheme = 0;
    
    currentScheme = (currentScheme + 1) % schemes.size();
    
    // Apply color scheme to all terminals
    for (int i = 0; i < tabWidget->count(); ++i) {
        QTermWidget *term = qobject_cast<QTermWidget*>(tabWidget->widget(i));
        if (term) {
            term->setColorScheme(schemes.at(currentScheme));
        }
    }
    
    statusBar()->showMessage(QString("Color scheme: %1").arg(schemes.at(currentScheme)), 2000);
}

void TerminalWindow::increaseFont()
{
    QTermWidget *terminal = getCurrentTerminal();
    if (!terminal) return;
    
    QFont font = terminal->getTerminalFont();
    font.setPointSize(font.pointSize() + 1);
    
    // Apply to all terminals
    for (int i = 0; i < tabWidget->count(); ++i) {
        QTermWidget *term = qobject_cast<QTermWidget*>(tabWidget->widget(i));
        if (term) {
            term->setTerminalFont(font);
        }
    }
    updateStatusBar();
}

void TerminalWindow::decreaseFont()
{
    QTermWidget *terminal = getCurrentTerminal();
    if (!terminal) return;
    
    QFont font = terminal->getTerminalFont();
    if (font.pointSize() > 6) {
        font.setPointSize(font.pointSize() - 1);
        
        // Apply to all terminals
        for (int i = 0; i < tabWidget->count(); ++i) {
            QTermWidget *term = qobject_cast<QTermWidget*>(tabWidget->widget(i));
            if (term) {
                term->setTerminalFont(font);
            }
        }
        updateStatusBar();
    }
}

void TerminalWindow::resetFont()
{
    QFont defaultFont("Monospace", 12);
    
    // Apply to all terminals
    for (int i = 0; i < tabWidget->count(); ++i) {
        QTermWidget *term = qobject_cast<QTermWidget*>(tabWidget->widget(i));
        if (term) {
            term->setTerminalFont(defaultFont);
        }
    }
    updateStatusBar();
}

void TerminalWindow::updateStatusBar()
{
    QTermWidget *terminal = getCurrentTerminal();
    if (!terminal) return;
    
    QFont font = terminal->getTerminalFont();
    statusBar()->showMessage(QString("Font: %1 %2pt | Tabs: %3 | Connections: %4")
                            .arg(font.family())
                            .arg(font.pointSize())
                            .arg(tabWidget->count())
                            .arg(connections.count()));
}

void TerminalWindow::newTab()
{
    QTermWidget *terminal = createTerminal();
    QString tabTitle = getNextTabTitle();
    
    int index = tabWidget->addTab(terminal, tabTitle);
    tabWidget->setCurrentIndex(index);
    
    // Focus the new terminal
    terminal->setFocus();
    
    updateStatusBar();
}

void TerminalWindow::closeTab(int index)
{
    if (tabWidget->count() <= 1) {
        // For the last tab, just close the window directly without asking
        // The closeEvent() will handle the confirmation
        close();
        return;
    }
    
    QWidget *widget = tabWidget->widget(index);
    tabWidget->removeTab(index);
    widget->deleteLater();
    
    updateStatusBar();
}

void TerminalWindow::closeCurrentTab()
{
    closeTab(tabWidget->currentIndex());
}

void TerminalWindow::copyClipboard()
{
        QTermWidget *terminal = getCurrentTerminal();
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

void TerminalWindow::onTabChanged(int index)
{
    Q_UNUSED(index)
    updateStatusBar();
    
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
        // Update the tab title to reflect it's now a local terminal
        QString currentTitle = tabWidget->tabText(tabIndex);
        
        // Remove SSH-specific parts from title and make it a normal terminal title
        QString newTitle = "Terminal";  // or extract base name if you prefer
        tabWidget->setTabText(tabIndex, newTitle);
        
        // Mark as normal terminal now (the shell is still running locally)
        finishedTerminal->setProperty("isSSHTerminal", false);
        finishedTerminal->setProperty("sshConnection", QVariant());
        
        // Show message in status bar
        statusBar()->showMessage("SSH connection closed - returned to local shell", 3000);
        
        // Don't return here - let the terminal continue running as local
        // The terminal process itself hasn't finished, just the SSH connection
        // So we don't want to close or recreate anything
        return;
    }
    
    // For local terminals that actually finished, continue with normal closure logic
    if (tabWidget->count() <= 1) {
        QApplication::quit();
        return;
    }
    
    // For multiple tabs, just close this tab
    QWidget *widget = tabWidget->widget(tabIndex);
    tabWidget->removeTab(tabIndex);
    widget->deleteLater();
    
    updateStatusBar();
}

// Add this new helper method to terminalwindow.cpp:
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
    
    // Common signal connections
    connect(terminal, &QWidget::customContextMenuRequested, 
            this, &TerminalWindow::showContextMenu);
    connect(terminal, &QTermWidget::finished, this, &TerminalWindow::onTerminalFinished);
    
    return terminal;
}

// Updated createTerminal() method - for local terminals
QTermWidget* TerminalWindow::createTerminal()
{
    QTermWidget *terminal = createTerminalWidget();  // Use helper method
    
    // Local terminal specific settings
    terminal->setShellProgram("/bin/bash");
    
    return terminal;
}

// Feature 4: Create SSH terminal with connection parameters and password support
QTermWidget* TerminalWindow::createSSHTerminal(const SSHConnection &connection)
{
    QTermWidget *terminal = createTerminalWidget();

    // Mark this as an SSH terminal
    terminal->setProperty("isSSHTerminal", true);
    terminal->setProperty("sshConnection", QVariant::fromValue(connection));

    // SSH terminal specific setup
    terminal->setShellProgram("/bin/bash");
    terminal->startShellProgram();

    // Build safe SSH command using the helper
    QString sshCommand = CommandSafetyHelper::buildSafeSSHCommand(connection);

    // Monitor terminal output to detect when SSH connection ends
    connect(terminal, &QTermWidget::receivedData, this, [this, terminal](const QString &text) {
        if (text.contains("Connection to") && text.contains("closed.")) {
            bool isSSH = terminal->property("isSSHTerminal").toBool();
            if (isSSH) {
                int tabIndex = tabWidget->indexOf(terminal);
                if (tabIndex != -1) {
                    tabWidget->setTabText(tabIndex, "Terminal");
                    terminal->setProperty("isSSHTerminal", false);
                    terminal->setProperty("sshConnection", QVariant());
                    statusBar()->showMessage("SSH connection closed - returned to local shell", 3000);
                }
            }
        }
    });

    // Wait for terminal to be ready and send command cleanly
    QTimer::singleShot(300, [terminal, sshCommand]() {
        terminal->sendText("clear\n");
        QTimer::singleShot(100, [terminal, sshCommand]() {
            terminal->sendText(sshCommand + "\n");
        });
    });

    return terminal;
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

void TerminalWindow::setupUI()
{
    setWindowTitle("Advanced Qt Terminal with SSH Connections");

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(2, 2, 2, 2);

    // Main horizontal splitter (left panel | terminal area)
    mainSplitter = new GripSplitter(Qt::Horizontal, this);

    // Create left panel with vertical splitter (tree | config)
    leftPanelSplitter = new GripSplitter(Qt::Vertical, this);

    // Setup connection tree
    setupConnectionTree();

    // Make connection tree narrower
    connectionTree->setMaximumWidth(300);
    connectionTree->setMinimumWidth(100);

    leftPanelSplitter->addWidget(connectionTree);

    // Setup connection config panel
    setupConnectionConfigPanel();
    leftPanelSplitter->addWidget(connectionConfigGroup);

    // Set left panel proportions (tree larger than config)
    leftPanelSplitter->setStretchFactor(0, 3);  // Tree takes 75%
    leftPanelSplitter->setStretchFactor(1, 1);  // Config takes 25%
    leftPanelSplitter->setCollapsible(0, false);
    leftPanelSplitter->setCollapsible(1, false);

    // Make left panel narrower
    leftPanelSplitter->setMaximumWidth(300);
    leftPanelSplitter->setMinimumWidth(100);

    mainSplitter->addWidget(leftPanelSplitter);

    // Create tab widget
    tabWidget = new QTabWidget(this);
    tabWidget->setTabsClosable(true);
    tabWidget->setMovable(true);
    tabWidget->setDocumentMode(true);

    connect(tabWidget, &QTabWidget::tabCloseRequested, this, &TerminalWindow::closeTab);
    connect(tabWidget, &QTabWidget::currentChanged, this, &TerminalWindow::onTabChanged);

    // Enable context menu for tab bar
    tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabWidget->tabBar(), &QTabBar::customContextMenuRequested, 
            this, &TerminalWindow::showTabContextMenu);

    mainSplitter->addWidget(tabWidget);
    
    // Use stretch factors for better proportion control
    mainSplitter->setStretchFactor(0, 1);   // Left panel: small
    mainSplitter->setStretchFactor(1, 20);  // Terminal: large
    mainSplitter->setCollapsible(0, false);
    
    mainLayout->addWidget(mainSplitter);
    
    statusBar()->showMessage("Ready");
    resize(1400, 800);
}

void TerminalWindow::setupMenus()
{
    QMenuBar *menuBar = this->menuBar();

    // File menu
    QMenu *fileMenu = menuBar->addMenu("&File");
    fileMenu->addAction("&New Tab", this, &TerminalWindow::newTab, QKeySequence::New);
    fileMenu->addAction("&Close Tab", this, &TerminalWindow::closeCurrentTab, QKeySequence::Close);
    fileMenu->addSeparator();
    fileMenu->addAction("&Quit", this, &QWidget::close, QKeySequence::Quit);

    // Edit menu
    QMenu *editMenu = menuBar->addMenu("&Edit");
    editMenu->addAction("&Copy", this, &TerminalWindow::copyClipboard, QKeySequence::Copy);
    
    editMenu->addAction("&Paste", this, [this]() {
        QTermWidget *terminal = getCurrentTerminal();
        if (terminal) terminal->pasteClipboard();
    }, QKeySequence::Paste);
    
    editMenu->addAction("Select &All", this, &TerminalWindow::selectAllText, QKeySequence::SelectAll);
    editMenu->addSeparator();
    
    editMenu->addAction("&Clear", this, [this]() {
        QTermWidget *terminal = getCurrentTerminal();
        if (terminal) terminal->clear();
    });

    // View menu
    QMenu *viewMenu = menuBar->addMenu("&View");
    viewMenu->addAction("&Font...", this, &TerminalWindow::openFontDialog);
    viewMenu->addAction("&Color Scheme", this, &TerminalWindow::changeColorScheme);
    viewMenu->addSeparator();
    viewMenu->addAction("Zoom &In", this, &TerminalWindow::increaseFont, QKeySequence::ZoomIn);
    viewMenu->addAction("Zoom &Out", this, &TerminalWindow::decreaseFont, QKeySequence::ZoomOut);
    viewMenu->addAction("&Reset Zoom", this, &TerminalWindow::resetFont, QKeySequence(Qt::CTRL + Qt::Key_0));
    
    // Connections menu (Feature 3)
    QMenu *connectionsMenu = menuBar->addMenu("&Connections");
    connectionsMenu->addAction("&New Connection...", this, &TerminalWindow::addNewConnection, QKeySequence(Qt::CTRL + Qt::Key_N));
    connectionsMenu->addSeparator();
    connectionsMenu->addAction("&Refresh", this, [this]() {
        loadConnections();
    }, QKeySequence::Refresh);
}

// Update setupConnectionTree() method:
void TerminalWindow::setupConnectionTree()
{
    connectionTree = new QTreeWidget(this);
    connectionTree->setHeaderLabel("SSH Connections");
    
    // Enable context menu
    connectionTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(connectionTree, &QTreeWidget::customContextMenuRequested,
            this, &TerminalWindow::showConnectionContextMenu);
    
    // Connect double-click signal
    connect(connectionTree, &QTreeWidget::itemDoubleClicked,
            this, &TerminalWindow::onConnectionDoubleClicked);
    
    // Connect selection changed signal
    connect(connectionTree, &QTreeWidget::itemSelectionChanged,
            this, &TerminalWindow::onConnectionSelectionChanged);
}

void TerminalWindow::setupConnectionConfigPanel()
{
    connectionConfigGroup = new QGroupBox("Connection Details", this);
    connectionConfigGroup->setMaximumHeight(350);
    
    QFormLayout *formLayout = new QFormLayout(connectionConfigGroup);
    
    // Create read-only labels for connection details
    configNameLabel = new QLabel("No connection selected", this);
    configNameLabel->setStyleSheet("QLabel { color: #666; font-weight: bold; }");
    formLayout->addRow("Name:", configNameLabel);
    
    configHostLabel = new QLabel("-", this);
    configHostLabel->setStyleSheet("QLabel { color: #333; }");
    formLayout->addRow("Host:", configHostLabel);
    
    configUsernameLabel = new QLabel("-", this);
    configUsernameLabel->setStyleSheet("QLabel { color: #333; }");
    formLayout->addRow("Username:", configUsernameLabel);
    
    configPortLabel = new QLabel("-", this);
    configPortLabel->setStyleSheet("QLabel { color: #333; }");
    formLayout->addRow("Port:", configPortLabel);
    
    configPasswordLabel = new QLabel("-", this);
    configPasswordLabel->setStyleSheet("QLabel { color: #333; }");
    formLayout->addRow("Password:", configPasswordLabel);
    
    configFolderLabel = new QLabel("-", this);
    configFolderLabel->setStyleSheet("QLabel { color: #333; }");
    formLayout->addRow("Folder:", configFolderLabel);
    
    formLayout->addItem(new QSpacerItem(0, 10));
    
    // Action buttons
    quickConnectButton = new QPushButton("🔌 Quick Connect", this);
    quickConnectButton->setEnabled(false);
    quickConnectButton->setStyleSheet("QPushButton { font-weight: bold; color: #0066cc; }");
    connect(quickConnectButton, &QPushButton::clicked, this, &TerminalWindow::onQuickConnectClicked);
    formLayout->addRow("", quickConnectButton);
    
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    
    editConnectionButton = new QPushButton("✏️ Edit", this);
    editConnectionButton->setEnabled(false);
    connect(editConnectionButton, &QPushButton::clicked, this, &TerminalWindow::onEditConnectionClicked);
    buttonLayout->addWidget(editConnectionButton);
    
    deleteConnectionButton = new QPushButton("🗑️ Delete", this);
    deleteConnectionButton->setEnabled(false);
    deleteConnectionButton->setStyleSheet("QPushButton { color: #cc0000; }");
    connect(deleteConnectionButton, &QPushButton::clicked, this, &TerminalWindow::onDeleteConnectionClicked);
    buttonLayout->addWidget(deleteConnectionButton);
    
    formLayout->addRow("", buttonLayout);
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

// Feature 4: Updated double-click handler for actual SSH connection
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

// Feature 4: Connect to SSH from context menu
void TerminalWindow::connectToSSH(QTreeWidgetItem *item)
{
    if (!item) return;
    
    // Trigger the same action as double-click
    onConnectionDoubleClicked(item, 0);
}

// Feature 3 Implementation: Connection Management

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

// Updated showConnectionContextMenu to include Feature 4 functionality:
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

void TerminalWindow::uploadFileToSSH(const SSHConnection &connection)
{
    // File selection dialog
    QString localFile = QFileDialog::getOpenFileName(this, 
        QString("Upload File to %1").arg(connection.name),
        QDir::homePath(),
        "All Files (*)");
    
    if (localFile.isEmpty()) return;
    
    // Show progress while detecting remote directory
    QProgressDialog *detectDialog = new QProgressDialog("Detecting remote directory...", "Cancel", 0, 0, this);
    detectDialog->setModal(true);
    detectDialog->show();
    
    // Detect the actual current remote working directory
    detectRemoteWorkingDirectory(connection, [this, connection, localFile, detectDialog](const QString &remotePath) {
        detectDialog->hide();
        detectDialog->deleteLater();
        
        // Build default remote file path
        QString defaultRemotePath = remotePath;
        if (!defaultRemotePath.endsWith("/")) {
            defaultRemotePath += "/";
        }
        defaultRemotePath += QFileInfo(localFile).fileName();
        
        // Remote path dialog with actual remote directory info
        bool ok;
        QString dialogTitle = QString("Upload to %1").arg(connection.name);
        QString dialogLabel = QString("Remote path (current directory: %1):")
                             .arg(remotePath);
        
        QString finalRemotePath = QInputDialog::getText(this, dialogTitle, dialogLabel,
            QLineEdit::Normal, defaultRemotePath, &ok);
        
        if (!ok || finalRemotePath.isEmpty()) return;
        
        // Perform SCP upload
        performSCPUpload(connection, localFile, finalRemotePath);
    });
}

void TerminalWindow::downloadFileFromSSH(const SSHConnection &connection)
{
    // First: Detect remote directory
    QProgressDialog *detectDialog = new QProgressDialog("Detecting remote directory...", "Cancel", 0, 0, this);
    detectDialog->setModal(true);
    detectDialog->show();
    
    detectRemoteWorkingDirectory(connection, [this, connection, detectDialog](const QString &remotePath) {
        detectDialog->hide();
        detectDialog->deleteLater();
        
        // Second: Show file browser to select remote file
        showRemoteFileBrowser(connection, remotePath, [this, connection](const QString &selectedRemoteFile) {
            if (selectedRemoteFile.isEmpty()) return;
            
            // Third: Choose local save location
            QString fileName = QFileInfo(selectedRemoteFile).fileName();
            QString localFile = QFileDialog::getSaveFileName(this,
                QString("Save '%1' from %2").arg(fileName, connection.name),
                QDir::homePath() + "/" + fileName,
                "All Files (*)");
            
            if (localFile.isEmpty()) return;
            
            // Fourth: Download the file
            performSCPDownload(connection, selectedRemoteFile, localFile);
        });
    });
}

void TerminalWindow::performSCPUpload(const SSHConnection &connection, 
                                     const QString &localFile, 
                                     const QString &remotePath)
{
    // Validate connection first
    auto validation = ConnectionValidator::validateConnection(connection);
    if (!validation.isValid) {
        QMessageBox::warning(this, "Invalid Connection", validation.errorMessage);
        return;
    }
    
    // Show warnings if any
    if (!validation.warnings.isEmpty()) {
        QString warningText = "Warnings:\n" + validation.warnings.join("\n");
        int ret = QMessageBox::question(this, "Connection Warnings", 
                                       warningText + "\n\nContinue anyway?",
                                       QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::No) return;
    }
    
    // Create progress dialog
    scpProgressDialog = new QProgressDialog("Uploading file...", "Cancel", 0, 0, this);
    scpProgressDialog->setWindowTitle("SCP Upload");
    scpProgressDialog->setModal(true);
    scpProgressDialog->show();

    // Build safe SCP command for UPLOAD
    QString scpCommand;
    if (!connection.password.isEmpty()) {
        scpCommand = QString("sshpass -p %1 ").arg(CommandSafetyHelper::escapeShellArgument(connection.password));
    }

    if (connection.port == 22) {
        scpCommand += QString("scp -o ServerAliveInterval=60 -o ServerAliveCountMax=3 "
                             "-o StrictHostKeyChecking=accept-new %1 %2@%3:%4")
                     .arg(CommandSafetyHelper::escapeShellArgument(localFile),
                          CommandSafetyHelper::escapeShellArgument(connection.username),
                          CommandSafetyHelper::escapeShellArgument(connection.host),
                          CommandSafetyHelper::escapeShellArgument(remotePath));
    } else {
        scpCommand += QString("scp -o ServerAliveInterval=60 -o ServerAliveCountMax=3 "
                             "-o StrictHostKeyChecking=accept-new -P %1 %2 %3@%4:%5")
                     .arg(connection.port)
                     .arg(CommandSafetyHelper::escapeShellArgument(localFile),
                          CommandSafetyHelper::escapeShellArgument(connection.username),
                          CommandSafetyHelper::escapeShellArgument(connection.host),
                          CommandSafetyHelper::escapeShellArgument(remotePath));
    }
    
    // Execute with improved error handling
    QProcess *scpProcess = new QProcess(this);
    
    connect(scpProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
            [this, scpProcess, connection, localFile](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitStatus)
        scpProgressDialog->hide();
        scpProgressDialog->deleteLater();
        
        if (exitCode == 0) {
            statusBar()->showMessage(QString("✅ File uploaded successfully to %1").arg(connection.name), 5000);
            QMessageBox::information(this, "Upload Complete", 
                QString("File '%1' uploaded successfully to %2")
                .arg(QFileInfo(localFile).fileName(), connection.name));
        } else {
            QString error = scpProcess->readAllStandardError();
            QString friendlyError = SSHErrorHandler::getErrorDescription(exitCode);
            statusBar()->showMessage("❌ Upload failed", 5000);
            QMessageBox::warning(this, "Upload Failed", 
                QString("Failed to upload file to %1:\n%2\n\nTechnical details:\n%3")
                .arg(connection.name, friendlyError, error));
        }
        
        scpProcess->deleteLater();
    });
    
    connect(scpProgressDialog, &QProgressDialog::canceled, [scpProcess]() {
        scpProcess->kill();
    });
    
    scpProcess->start("/bin/bash", QStringList() << "-c" << scpCommand);
}

void TerminalWindow::performSCPDownload(const SSHConnection &connection, const QString &remoteFile, const QString &localFile)
{
    // Validate connection first
    auto validation = ConnectionValidator::validateConnection(connection);
    if (!validation.isValid) {
        QMessageBox::warning(this, "Invalid Connection", validation.errorMessage);
        return;
    }
    
    // Show warnings if any
    if (!validation.warnings.isEmpty()) {
        QString warningText = "Warnings:\n" + validation.warnings.join("\n");
        int ret = QMessageBox::question(this, "Connection Warnings", 
                                       warningText + "\n\nContinue anyway?",
                                       QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::No) return;
    }

    // Create progress dialog
    scpProgressDialog = new QProgressDialog("Downloading file...", "Cancel", 0, 0, this);
    scpProgressDialog->setWindowTitle("SCP Download");
    scpProgressDialog->setModal(true);
    scpProgressDialog->show();
    
    // Build safe SCP command for DOWNLOAD
    QString scpCommand;
    if (!connection.password.isEmpty()) {
        scpCommand = QString("sshpass -p %1 ").arg(CommandSafetyHelper::escapeShellArgument(connection.password));
    }

    if (connection.port == 22) {
        scpCommand += QString("scp -o ServerAliveInterval=60 -o ServerAliveCountMax=3 "
                             "-o StrictHostKeyChecking=accept-new %1@%2:%3 %4")
                     .arg(CommandSafetyHelper::escapeShellArgument(connection.username),
                          CommandSafetyHelper::escapeShellArgument(connection.host),
                          CommandSafetyHelper::escapeShellArgument(remoteFile),
                          CommandSafetyHelper::escapeShellArgument(localFile));
    } else {
        scpCommand += QString("scp -o ServerAliveInterval=60 -o ServerAliveCountMax=3 "
                             "-o StrictHostKeyChecking=accept-new -P %1 %2@%3:%4 %5")
                     .arg(connection.port)
                     .arg(CommandSafetyHelper::escapeShellArgument(connection.username),
                          CommandSafetyHelper::escapeShellArgument(connection.host),
                          CommandSafetyHelper::escapeShellArgument(remoteFile),
                          CommandSafetyHelper::escapeShellArgument(localFile));
    }

    // Execute with improved error handling
    QProcess *scpProcess = new QProcess(this);
    
    connect(scpProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
            [this, scpProcess, connection, remoteFile](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitStatus)
        scpProgressDialog->hide();
        scpProgressDialog->deleteLater();
        
        if (exitCode == 0) {
            statusBar()->showMessage(QString("✅ File downloaded successfully from %1").arg(connection.name), 5000);
            QMessageBox::information(this, "Download Complete", 
                QString("File '%1' downloaded successfully from %2")
                .arg(QFileInfo(remoteFile).fileName(), connection.name));
        } else {
            QString error = scpProcess->readAllStandardError();
            QString friendlyError = SSHErrorHandler::getErrorDescription(exitCode);
            statusBar()->showMessage("❌ Download failed", 5000);
            QMessageBox::warning(this, "Download Failed", 
                QString("Failed to download file from %1:\n%2\n\nTechnical details:\n%3")
                .arg(connection.name, friendlyError, error));
        }
        
        scpProcess->deleteLater();
    });
    
    connect(scpProgressDialog, &QProgressDialog::canceled, [scpProcess]() {
        scpProcess->kill();
    });
    
    scpProcess->start("/bin/bash", QStringList() << "-c" << scpCommand);
}

void TerminalWindow::browseRemoteFiles(const SSHConnection &connection)
{
    // Show progress while detecting remote directory
    QProgressDialog *detectDialog = new QProgressDialog("Detecting remote directory...", "Cancel", 0, 0, this);
    detectDialog->setModal(true);
    detectDialog->show();
    
    // Detect the actual current remote working directory
    detectRemoteWorkingDirectory(connection, [this, connection, detectDialog](const QString &remotePath) {
        detectDialog->hide();
        detectDialog->deleteLater();
        
        // Directory browser dialog with actual current directory
        bool ok;
        QString dialogTitle = QString("Browse Remote Directory on %1").arg(connection.name);
        QString dialogLabel = QString("Directory path (current: %1):").arg(remotePath);
        
        QString browsePath = QInputDialog::getText(this, dialogTitle, dialogLabel,
            QLineEdit::Normal, remotePath, &ok);
        
        if (!ok || browsePath.isEmpty()) return;
        
        // Build SSH ls command
        QString lsCommand;
        if (!connection.password.isEmpty()) {
            lsCommand = QString("sshpass -p '%1' ").arg(connection.password);
        }
        
        if (connection.port == 22) {
            lsCommand += QString("ssh %1@%2 'ls -la \"%3\"'")
                        .arg(connection.username, connection.host, browsePath);
        } else {
            lsCommand += QString("ssh -p %1 %2@%3 'ls -la \"%4\"'")
                        .arg(connection.port)
                        .arg(connection.username, connection.host, browsePath);
        }
        
        // Execute and show results
        QProcess *lsProcess = new QProcess(this);
        connect(lsProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
                [this, lsProcess, connection, browsePath](int exitCode, QProcess::ExitStatus exitStatus) {
            Q_UNUSED(exitStatus)
            if (exitCode == 0) {
                QString output = lsProcess->readAllStandardOutput();
                
                // Create a simple dialog to show file listing
                QDialog dialog(this);
                dialog.setWindowTitle(QString("Remote Files: %1:%2").arg(connection.host, browsePath));
                dialog.resize(600, 400);
                
                QVBoxLayout *layout = new QVBoxLayout(&dialog);
                QTextEdit *textEdit = new QTextEdit(&dialog);
                textEdit->setReadOnly(true);
                textEdit->setFont(QFont("Monospace", 10));
                textEdit->setPlainText(output);
                layout->addWidget(textEdit);
                
                QPushButton *closeBtn = new QPushButton("Close", &dialog);
                connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
                layout->addWidget(closeBtn);
                
                dialog.exec();
            } else {
                QString error = lsProcess->readAllStandardError();
                QMessageBox::warning(this, "Browse Failed", 
                    QString("Failed to browse directory on %1:\n%2")
                    .arg(connection.name, error));
            }
            lsProcess->deleteLater();
        });
        
        lsProcess->start("/bin/bash", QStringList() << "-c" << lsCommand);
    });
}

QString TerminalWindow::getDefaultRemotePath(const SSHConnection &connection)
{
    // Always use the user's home directory as default, not the organizational folder
    return QString("/home/%1").arg(connection.username);
}

// Add this new method to detect the actual current remote directory
void TerminalWindow::detectRemoteWorkingDirectory(const SSHConnection &connection, 
                                                std::function<void(const QString&)> callback)
{
    // Build safe SSH pwd command using the helper
    QString pwdCommand = CommandSafetyHelper::buildSafeSSHCommand(connection, "pwd");
    
    // Execute command to get current directory
    QProcess *pwdProcess = new QProcess(this);
    connect(pwdProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
            [this, pwdProcess, connection, callback](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitStatus)
        
        QString remotePath;
        if (exitCode == 0) {
            // Successfully got the remote directory
            remotePath = pwdProcess->readAllStandardOutput().trimmed();
            if (remotePath.isEmpty()) {
                remotePath = getDefaultRemotePath(connection);
            }
        } else {
            // Fallback to default if command failed
            remotePath = getDefaultRemotePath(connection);
        }
        
        callback(remotePath);
        pwdProcess->deleteLater();
    });
    
    pwdProcess->start("/bin/bash", QStringList() << "-c" << pwdCommand);
}

// Alternative approach: Track terminal working directories
// Add this method to track the current directory of active SSH terminals
QString TerminalWindow::getCurrentRemoteDirectory(QTermWidget *terminal)
{
    if (!terminal) return QString();
    
    // Check if this is an SSH terminal
    bool isSSH = terminal->property("isSSHTerminal").toBool();
    if (!isSSH) return QString();
    
    // Get the connection info
    QVariant connectionData = terminal->property("sshConnection");
    if (!connectionData.canConvert<SSHConnection>()) return QString();
    
    SSHConnection connection = qvariant_cast<SSHConnection>(connectionData);
    
    // For now, return default path - you could enhance this by:
    // 1. Parsing terminal output to track 'cd' commands
    // 2. Sending 'pwd' command and capturing output
    // 3. Using a more sophisticated terminal state tracking
    
    return getDefaultRemotePath(connection);
}

// Add this new method to show a proper remote file browser with file selection
void TerminalWindow::showRemoteFileBrowser(const SSHConnection &connection, const QString &remotePath, 
                                          std::function<void(const QString&)> callback)
{
    // Build safe SSH ls command using the helper
    QString lsCommand = CommandSafetyHelper::buildSafeSSHCommand(connection, 
        QString("ls -la %1").arg(CommandSafetyHelper::escapeShellArgument(remotePath)));
    
    // Create progress dialog for file listing
    QProgressDialog *listDialog = new QProgressDialog("Loading remote files...", "Cancel", 0, 0, this);
    listDialog->setModal(true);
    listDialog->show();
    
    // Execute command to get file listing
    QProcess *lsProcess = new QProcess(this);
    connect(lsProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
            [this, lsProcess, connection, remotePath, callback, listDialog](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitStatus)
        
        listDialog->hide();
        listDialog->deleteLater();
        
        if (exitCode != 0) {
            QString error = lsProcess->readAllStandardError();
            QString friendlyError = SSHErrorHandler::getErrorDescription(exitCode);
            QMessageBox::warning(this, "Browse Failed", 
                QString("Failed to browse directory on %1:\n%2\n\nTechnical details:\n%3")
                .arg(connection.name, friendlyError, error));
            callback(QString());
            lsProcess->deleteLater();
            return;
        }
        
        QString output = lsProcess->readAllStandardOutput();
        
        // Parse the ls output to create a file selection dialog
        QStringList lines = output.split('\n', Qt::SkipEmptyParts);
        QStringList files;
        QStringList fileDetails;
        
        // Skip the first line if it shows total
        int startIndex = (lines.size() > 0 && lines[0].startsWith("total")) ? 1 : 0;
        
        for (int i = startIndex; i < lines.size(); ++i) {
            QString line = lines[i].trimmed();
            if (line.isEmpty()) continue;
            
            // Parse ls -la output: permissions user group size date time filename
            QStringList parts = line.split(QRegExp("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() < 9) continue;
            
            QString permissions = parts[0];
            QString filename = parts.mid(8).join(" "); // Handle filenames with spaces
            
            // Skip . and .. entries
            if (filename == "." || filename == "..") continue;
            
            QString fullPath = remotePath;
            if (!fullPath.endsWith("/")) fullPath += "/";
            fullPath += filename;
            
            // For directories, ensure the path ends with /
            if (permissions.startsWith('d')) {
                if (!fullPath.endsWith("/")) fullPath += "/";
            }
            
            files.append(fullPath);
            
            // Create display text with file info
            QString displayText;
            if (permissions.startsWith('d')) {
                displayText = QString("📁 %1/").arg(filename);
            } else if (permissions.contains('x')) {
                displayText = QString("⚙️ %1").arg(filename);
            } else {
                displayText = QString("📄 %1").arg(filename);
            }
            
            // Add size info for files
            if (!permissions.startsWith('d') && parts.size() >= 5) {
                displayText += QString(" (%1)").arg(parts[4]);
            }
            
            fileDetails.append(displayText);
        }
        
        if (files.isEmpty()) {
            QMessageBox::information(this, "Empty Directory", 
                QString("No files found in %1").arg(remotePath));
            callback(QString());
            lsProcess->deleteLater();
            return;
        }
        
        // Create file selection dialog
        QDialog fileDialog(this);
        fileDialog.setWindowTitle(QString("Select File from %1:%2").arg(connection.host, remotePath));
        fileDialog.resize(600, 400);
        
        QVBoxLayout *layout = new QVBoxLayout(&fileDialog);
        
        // Add path label
        QLabel *pathLabel = new QLabel(QString("Remote path: %1").arg(remotePath), &fileDialog);
        pathLabel->setStyleSheet("QLabel { font-weight: bold; color: #666; }");
        layout->addWidget(pathLabel);
        
        // File list
        QListWidget *fileList = new QListWidget(&fileDialog);
        for (int i = 0; i < files.size(); ++i) {
            QListWidgetItem *item = new QListWidgetItem(fileDetails[i]);
            item->setData(Qt::UserRole, files[i]); // Store full path
            fileList->addItem(item);
        }
        layout->addWidget(fileList);
        
        // Manual path entry
        QHBoxLayout *pathLayout = new QHBoxLayout();
        pathLayout->addWidget(new QLabel("Or enter file path:", &fileDialog));
        QLineEdit *pathEdit = new QLineEdit(&fileDialog);
        pathEdit->setPlaceholderText("Enter full remote file path...");
        pathLayout->addWidget(pathEdit);
        layout->addLayout(pathLayout);
        
        // Buttons
        QHBoxLayout *buttonLayout = new QHBoxLayout();
        QPushButton *selectBtn = new QPushButton("Download Selected", &fileDialog);
        QPushButton *enterBtn = new QPushButton("Enter Directory", &fileDialog);
        QPushButton *upBtn = new QPushButton("📁 Up", &fileDialog);
        QPushButton *manualBtn = new QPushButton("Manual Path", &fileDialog);
        QPushButton *cancelBtn = new QPushButton("Cancel", &fileDialog);
        
        selectBtn->setEnabled(false);
        enterBtn->setEnabled(false);
        selectBtn->setStyleSheet("QPushButton { font-weight: bold; color: #0066cc; }");
        
        buttonLayout->addWidget(selectBtn);
        buttonLayout->addWidget(enterBtn);
        buttonLayout->addWidget(upBtn);
        buttonLayout->addWidget(manualBtn);
        buttonLayout->addStretch();
        buttonLayout->addWidget(cancelBtn);
        layout->addLayout(buttonLayout);
        
        // Connect signals
        connect(fileList, &QListWidget::itemSelectionChanged, [selectBtn, enterBtn, fileList]() {
            QListWidgetItem *current = fileList->currentItem();
            selectBtn->setEnabled(current != nullptr);
            
            if (current) {
                QString selectedPath = current->data(Qt::UserRole).toString();
                enterBtn->setEnabled(selectedPath.endsWith("/"));
            } else {
                enterBtn->setEnabled(false);
            }
        });
        
        connect(fileList, &QListWidget::itemDoubleClicked, [this, &fileDialog, callback, connection](QListWidgetItem *item) {
            QString selectedPath = item->data(Qt::UserRole).toString();
            
            // If it's a directory, navigate into it
            if (selectedPath.endsWith("/")) {
                fileDialog.accept(); // Close current dialog
                // Recursively open browser in the new directory
                showRemoteFileBrowser(connection, selectedPath, callback);
                return;
            }
            
            // If it's a file, select it for download
            callback(selectedPath);
            fileDialog.accept();
        });
        
        connect(selectBtn, &QPushButton::clicked, [&fileDialog, fileList, callback]() {
            QListWidgetItem *currentItem = fileList->currentItem();
            if (!currentItem) return;
            
            QString selectedFile = currentItem->data(Qt::UserRole).toString();
            
            // Check if it's a directory
            if (selectedFile.endsWith("/")) {
                QMessageBox::information(&fileDialog, "Directory Selected", 
                    "Cannot download a directory. Use 'Enter Directory' to browse or select a file.");
                return;
            }
            
            callback(selectedFile);
            fileDialog.accept();
        });
        
        connect(enterBtn, &QPushButton::clicked, [this, &fileDialog, fileList, callback, connection]() {
            QListWidgetItem *currentItem = fileList->currentItem();
            if (!currentItem) return;
            
            QString selectedDir = currentItem->data(Qt::UserRole).toString();
            if (!selectedDir.endsWith("/")) return;
            
            fileDialog.accept(); // Close current dialog
            showRemoteFileBrowser(connection, selectedDir, callback); // Open new browser in directory
        });
        
        connect(upBtn, &QPushButton::clicked, [this, &fileDialog, callback, connection, remotePath]() {
            // Go up one directory
            QString parentPath = remotePath;
            if (parentPath.endsWith("/")) parentPath.chop(1);
            
            int lastSlash = parentPath.lastIndexOf('/');
            if (lastSlash > 0) {
                parentPath = parentPath.left(lastSlash + 1);
            } else {
                parentPath = "/";
            }
            
            fileDialog.accept();
            showRemoteFileBrowser(connection, parentPath, callback);
        });
        
        connect(manualBtn, &QPushButton::clicked, [&fileDialog, pathEdit, callback]() {
            QString manualPath = pathEdit->text().trimmed();
            if (manualPath.isEmpty()) {
                QMessageBox::information(&fileDialog, "Empty Path", "Please enter a file path.");
                return;
            }
            
            callback(manualPath);
            fileDialog.accept();
        });
        
        connect(cancelBtn, &QPushButton::clicked, [&fileDialog, callback]() {
            callback(QString());
            fileDialog.reject();
        });
        
        fileDialog.exec();
        lsProcess->deleteLater();
    });
    
    connect(listDialog, &QProgressDialog::canceled, [lsProcess, callback]() {
        lsProcess->kill();
        callback(QString());
    });
    
    lsProcess->start("/bin/bash", QStringList() << "-c" << lsCommand);
}

void TerminalWindow::saveSettings()
{
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    
    QTermWidget *terminal = getCurrentTerminal();
    if (terminal) {
        settings.setValue("font", terminal->getTerminalFont());
    }
}

void TerminalWindow::loadSettings()
{
    QSettings settings;
    restoreGeometry(settings.value("geometry").toByteArray());
}