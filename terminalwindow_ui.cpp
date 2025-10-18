// UI setup and menu-related functionality
#include "terminalwindow.h"
#include "gripsplitter.h"
#include "AboutDialog.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabBar>
#include <QHeaderView>
#include <QFormLayout>
#include <QSpacerItem>

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

    // Help menu
    QMenu* helpMenu = menuBar->addMenu("&Help");
    QAction* aboutAction = new QAction("&About", this);
    aboutAction->setStatusTip("Show application information");
    connect(aboutAction, &QAction::triggered, this, &TerminalWindow::showAbout);
    helpMenu->addAction(aboutAction);
}

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

void TerminalWindow::showAbout() {
    AboutDialog dialog(this);
    dialog.exec();
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
