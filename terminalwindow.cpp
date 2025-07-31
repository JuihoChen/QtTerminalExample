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

TerminalWindow::TerminalWindow(QWidget *parent) 
    : QMainWindow(parent), tabWidget(nullptr), tabCounter(1)
{
    setupUI();
    setupMenus();
    loadSettings();
    
    // Create first tab
    newTab();
}

TerminalWindow::~TerminalWindow()
{
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

// New method to setup the connection tree:
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
    
    // Add some dummy connections for now
    createDummyConnections();
}

// Method to create dummy connections (temporary for Feature 1):
void TerminalWindow::createDummyConnections()
{
    // Create folder items
    QTreeWidgetItem *prodFolder = new QTreeWidgetItem(connectionTree);
    prodFolder->setText(0, "🏢 Production");
    prodFolder->setExpanded(true);
    
    QTreeWidgetItem *devFolder = new QTreeWidgetItem(connectionTree);
    devFolder->setText(0, "🔧 Development");
    devFolder->setExpanded(true);
    
    QTreeWidgetItem *personalFolder = new QTreeWidgetItem(connectionTree);
    personalFolder->setText(0, "👤 Personal");
    personalFolder->setExpanded(true);
    
    // Add connection items to Production folder
    QTreeWidgetItem *webServer = new QTreeWidgetItem(prodFolder);
    webServer->setText(0, "🖥️ Web Server");
    webServer->setToolTip(0, "user@192.168.1.10:22");
    webServer->setData(0, Qt::UserRole, "ssh user@192.168.1.10");
    
    QTreeWidgetItem *dbServer = new QTreeWidgetItem(prodFolder);
    dbServer->setText(0, "🗄️ Database Server");
    dbServer->setToolTip(0, "admin@192.168.1.20:22");
    dbServer->setData(0, Qt::UserRole, "ssh admin@192.168.1.20");
    
    // Add connection items to Development folder
    QTreeWidgetItem *devBox = new QTreeWidgetItem(devFolder);
    devBox->setText(0, "💻 Dev Box");
    devBox->setToolTip(0, "dev@10.0.0.5:22");
    devBox->setData(0, Qt::UserRole, "ssh dev@10.0.0.5");
    
    QTreeWidgetItem *testServer = new QTreeWidgetItem(devFolder);
    testServer->setText(0, "🧪 Test Server");
    testServer->setToolTip(0, "test@10.0.0.6:2222");
    testServer->setData(0, Qt::UserRole, "ssh test@10.0.0.6 -p 2222");
    
    // Add connection to Personal folder
    QTreeWidgetItem *vps = new QTreeWidgetItem(personalFolder);
    vps->setText(0, "☁️ My VPS");
    vps->setToolTip(0, "myuser@example.com:22");
    vps->setData(0, Qt::UserRole, "ssh myuser@example.com");
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

// New slot for connection tree context menu:
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
        // Will refresh from saved connections in Feature 2
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