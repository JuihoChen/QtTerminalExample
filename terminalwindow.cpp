// ============ terminalwindow.cpp ============
#include "terminalwindow.h"

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

// Update the constructor to load connections:
TerminalWindow::TerminalWindow(QWidget *parent) 
    : QMainWindow(parent), tabWidget(nullptr), tabCounter(1)
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
    statusBar()->showMessage(QString("Font: %1 %2pt | Tabs: %3")
                            .arg(font.family())
                            .arg(font.pointSize())
                            .arg(tabWidget->count()));
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

void TerminalWindow::selectAllText()
{
    QTermWidget *terminal = getCurrentTerminal();
    if (terminal) {
        // Send Ctrl+A to select all text (universal terminal shortcut)
        terminal->sendText("\x01"); // Ctrl+A ASCII code
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
    
    // If this is the last tab, quit the application directly without confirmation
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

QTermWidget* TerminalWindow::createTerminal()
{
    QTermWidget *terminal = new QTermWidget(this);
    
    // Configure terminal
    terminal->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(terminal, &QWidget::customContextMenuRequested, 
            this, &TerminalWindow::showContextMenu);

    terminal->setShellProgram("/bin/bash");
    terminal->setColorScheme("Linux");
    terminal->setTerminalFont(QFont("Monospace", 12));
    terminal->setScrollBarPosition(QTermWidget::ScrollBarRight);
    terminal->setMotionAfterPasting(2);

    // Connect terminal finished signal
    connect(terminal, &QTermWidget::finished, this, &TerminalWindow::onTerminalFinished);

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

    // Create central widget and main layout
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(2, 2, 2, 2);

    // Create splitter for tree and tabs
    splitter = new QSplitter(Qt::Horizontal, this);
    
    // Setup connection tree
    setupConnectionTree();
    splitter->addWidget(connectionTree);
    
    // Create tab widget (existing code)
    tabWidget = new QTabWidget(this);
    tabWidget->setTabsClosable(true);
    tabWidget->setMovable(true);
    tabWidget->setDocumentMode(true);
    
    // Connect tab signals
    connect(tabWidget, &QTabWidget::tabCloseRequested, this, &TerminalWindow::closeTab);
    connect(tabWidget, &QTabWidget::currentChanged, this, &TerminalWindow::onTabChanged);

    splitter->addWidget(tabWidget);
    
    // Set splitter proportions (18% tree, 82% tabs)
    splitter->setSizes({180, 820});
    splitter->setCollapsible(0, false);  // Don't allow tree to be collapsed completely
    
    mainLayout->addWidget(splitter);

    // Status bar
    statusBar()->showMessage("Ready");

    // Set initial size
    resize(1400, 800);  // Slightly wider to accommodate tree
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
    editMenu->addAction("&Copy", this, [this]() {
        QTermWidget *terminal = getCurrentTerminal();
        if (terminal) terminal->copyClipboard();
    }, QKeySequence::Copy);
    
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
}

// Update setupConnectionTree() method:
void TerminalWindow::setupConnectionTree()
{
    connectionTree = new QTreeWidget(this);
    connectionTree->setHeaderLabel("SSH Connections");
    connectionTree->setMinimumWidth(200);
    connectionTree->setMaximumWidth(400);
    
    // Enable context menu
    connectionTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(connectionTree, &QTreeWidget::customContextMenuRequested,
            this, &TerminalWindow::showConnectionContextMenu);
    
    // Connect double-click signal
    connect(connectionTree, &QTreeWidget::itemDoubleClicked,
            this, &TerminalWindow::onConnectionDoubleClicked);
    
    // Don't create dummy connections here anymore - loadConnections() will handle it
}

// Replace createDummyConnections() with these new methods:

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
        conn.port = connObj["port"].toInt(22);
        conn.folder = connObj["folder"].toString();
        connections.append(conn);
    }
    
    qDebug() << "Loaded" << connections.size() << "connections";
    refreshConnectionTree();
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
        } else {
            displayName = "📁 " + folderName;
        }
        
        QTreeWidgetItem *folder = new QTreeWidgetItem(connectionTree);
        folder->setText(0, displayName);
        folder->setExpanded(true);
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
        
        // Set tooltip with connection details
        QString tooltip = QString("%1@%2:%3").arg(conn.username, conn.host).arg(conn.port);
        item->setToolTip(0, tooltip);
        
        // Store SSH command in item data
        QString sshCommand;
        if (conn.port == 22) {
            sshCommand = QString("ssh %1@%2").arg(conn.username, conn.host);
        } else {
            sshCommand = QString("ssh %1@%2 -p %3").arg(conn.username, conn.host).arg(conn.port);
        }
        item->setData(0, Qt::UserRole, sshCommand);
        
        // Store connection index for editing (we'll use this in Feature 3)
        item->setData(0, Qt::UserRole + 1, connections.indexOf(conn));
    }
}

// New slot for double-click on connection:
void TerminalWindow::onConnectionDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    
    // Check if this is a connection item (has SSH command stored)
    QString sshCommand = item->data(0, Qt::UserRole).toString();
    if (sshCommand.isEmpty()) {
        return; // This is a folder, not a connection
    }
    
    // For now, just create a new tab and show the SSH command
    // In Feature 4, we'll actually execute the SSH connection
    QTermWidget *terminal = createTerminal();
    QString tabTitle = QString("SSH: %1").arg(item->text(0).remove(0, 2)); // Remove emoji
    
    int index = tabWidget->addTab(terminal, tabTitle);
    tabWidget->setCurrentIndex(index);
    
    // Show the SSH command that would be executed
    terminal->sendText(QString("# Would execute: %1\n").arg(sshCommand));
    terminal->sendText("# (SSH connection will be implemented in Feature 4)\n");
    
    terminal->setFocus();
    updateStatusBar();
}

// Update showConnectionContextMenu to include refresh functionality:
void TerminalWindow::showConnectionContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = connectionTree->itemAt(pos);
    
    QMenu menu;
    
    if (item) {
        QString sshCommand = item->data(0, Qt::UserRole).toString();
        if (!sshCommand.isEmpty()) {
            // This is a connection item
            menu.addAction("🔌 Connect", [this, item]() {
                onConnectionDoubleClicked(item, 0);
            });
            menu.addSeparator();
            menu.addAction("✏️ Edit Connection", []() {
                // Will implement in Feature 3
            });
            menu.addAction("🗑️ Delete Connection", []() {
                // Will implement in Feature 3
            });
        } else {
            // This is a folder
            menu.addAction("➕ Add Connection", []() {
                // Will implement in Feature 3
            });
            menu.addAction("📁 Add Folder", []() {
                // Will implement in Feature 5
            });
        }
        menu.addSeparator();
    }
    
    menu.addAction("➕ New Connection", []() {
        // Will implement in Feature 3
    });
    menu.addAction("📁 New Folder", []() {
        // Will implement in Feature 5
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
    
    menu.addAction("Copy", terminal, &QTermWidget::copyClipboard);
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