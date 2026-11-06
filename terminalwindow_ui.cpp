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
#include <QDockWidget>
#include <QToolButton>
#include <QEvent>
#include <QTimer>
#include <QPainter>
#include <QIcon>

namespace {

// Keeps the browser-style "+" button sitting just after the last tab.
// In document mode the tab bar spans the whole width, so the button is placed
// from the last tab's rectangle. A blank corner widget reserves room at the
// right end: when there are too many tabs to fit, the button parks there
// instead of covering the tab scroll arrows.
// Blank corner widget; QTabWidget sizes corners by their size hint
class NewTabButtonRoom : public QWidget
{
public:
    NewTabButtonRoom(int width, QWidget *parent) : QWidget(parent), roomWidth(width) {}
    QSize sizeHint() const override { return QSize(roomWidth, 1); }

private:
    int roomWidth;
};

class NewTabButtonPlacer : public QObject
{
public:
    NewTabButtonPlacer(QTabWidget *tabs, QToolButton *button, int room)
        : QObject(button), tabs(tabs), bar(tabs->tabBar()), button(button), room(room)
    {
        tabs->installEventFilter(this);
        bar->installEventFilter(this);
        connect(bar, &QTabBar::tabMoved, this, [this]() { schedule(); });
        connect(bar, &QTabBar::currentChanged, this, [this]() { schedule(); });
        schedule();
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::LayoutRequest:
            schedule();
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    // Wait until Qt has finished laying out the tabs before measuring them
    void schedule()
    {
        if (pending) return;
        pending = true;
        QTimer::singleShot(0, this, [this]() { pending = false; place(); });
    }

    void place()
    {
        const QRect barRect = bar->geometry();
        const int size = qMax(barRect.height() - 6, 20);
        const int y = barRect.top() + (barRect.height() - size) / 2;

        // Centered in the reserved room on the right
        const int roomX = tabs->width() - room + (room - size) / 2;

        int x = barRect.left() + 3;
        if (bar->sizeHint().width() > bar->width()) {
            // Tabs overflow and the bar shows scroll arrows: park in the room
            x = roomX;
        } else if (bar->count() > 0) {
            const QRect last = bar->tabRect(bar->count() - 1);
            x = qMin(bar->mapTo(tabs, last.topRight()).x() + 4, roomX);
        }

        button->setFixedSize(size, size);
        button->move(x, y);
        button->raise();
    }

    QTabWidget *tabs;
    QTabBar *bar;
    QToolButton *button;
    int room;
    bool pending = false;
};

} // namespace

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

    // Browser-style "+" button right after the last tab: same as "New Tab"
    QToolButton *newTabButton = new QToolButton(tabWidget);
    newTabButton->setText("+");
    newTabButton->setToolTip("New Tab");
    newTabButton->setAutoRaise(true);
    newTabButton->setFocusPolicy(Qt::NoFocus);
    QFont plusFont = newTabButton->font();
    plusFont.setBold(true);
    plusFont.setPointSizeF(plusFont.pointSizeF() * 1.3);
    newTabButton->setFont(plusFont);
    connect(newTabButton, &QToolButton::clicked, this, &TerminalWindow::newTab);

    const int newTabButtonRoomWidth = 34;
    tabWidget->setCornerWidget(new NewTabButtonRoom(newTabButtonRoomWidth, tabWidget),
                               Qt::TopRightCorner);

    new NewTabButtonPlacer(tabWidget, newTabButton, newTabButtonRoomWidth);

    mainSplitter->addWidget(tabWidget);
    
    // Use stretch factors for better proportion control
    mainSplitter->setStretchFactor(0, 1);   // Left panel: small
    mainSplitter->setStretchFactor(1, 20);  // Terminal: large
    mainSplitter->setCollapsible(0, false);
    
    mainLayout->addWidget(mainSplitter);

    setupFileBrowserDock();
    
    statusBar()->showMessage("Ready");
    resize(1400, 800);
}

// A plain magnifier in one colour, drawn rather than an emoji so it looks the
// same on every system (Windows fonts turn 🔍 into a colour picture)
static QPixmap magnifierPixmap(const QColor &color, int size, qreal dpr)
{
    QPixmap pixmap(QSize(size, size) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal pen = size / 7.0;
    painter.setPen(QPen(color, pen, Qt::SolidLine, Qt::RoundCap));
    const qreal lens = size * 0.62;
    painter.drawEllipse(QRectF(pen / 2, pen / 2, lens - pen, lens - pen));
    painter.drawLine(QPointF(lens * 0.86, lens * 0.86), QPointF(size - pen, size - pen));
    return pixmap;
}

void TerminalWindow::setupMenus()
{
    QMenuBar *menuBar = this->menuBar();

    // File menu
    QMenu *fileMenu = menuBar->addMenu("&File");
    fileMenu->addAction("&New Tab", this, &TerminalWindow::newTab, QKeySequence::New);
    fileMenu->addAction("&Close Tab", this, &TerminalWindow::closeCurrentTab, QKeySequence::Close);
    fileMenu->addSeparator();
    fileMenu->addAction("&Start Folder...", this, &TerminalWindow::chooseStartFolder);
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
    // Not QKeySequence::Find: that is Ctrl+F, which bash uses to move forward a character
    editMenu->addAction("&Find...", this, &TerminalWindow::findInTerminal, QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_F));
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
    viewMenu->addSeparator();

    QAction *fileBrowserAction = fileBrowserDock->toggleViewAction();
    fileBrowserAction->setText("File &Browser");
    fileBrowserAction->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_B));
    connect(fileBrowserAction, &QAction::triggered, this, [this](bool checked) {
        if (checked) showFileBrowser();
    });
    viewMenu->addAction(fileBrowserAction);
    
    // Connections menu (Feature 3)
    QMenu *connectionsMenu = menuBar->addMenu("&Connections");
    connectionsMenu->addAction("&New Connection...", this, &TerminalWindow::addNewConnection, QKeySequence(Qt::CTRL + Qt::Key_N));
    connectionsMenu->addAction("&Scan for SSH Hosts...", this, [this]() { scanForHosts(); });
    connectionsMenu->addSeparator();
    connectionsMenu->addAction("&Refresh", this, [this]() {
        loadConnections();
    }, QKeySequence(Qt::Key_F5));  // Not QKeySequence::Refresh: on Linux it adds Ctrl+R, which bash needs

    // Help menu
    QMenu* helpMenu = menuBar->addMenu("&Help");
    QAction* aboutAction = new QAction("&About", this);
    aboutAction->setStatusTip("Show application information");
    connect(aboutAction, &QAction::triggered, this, &TerminalWindow::showAbout);
    helpMenu->addAction(aboutAction);

    // One-click Find on the right end of the menu bar
    // as a small outlined pill in the app's link blue, so it stands out from the menus
    QWidget *findCorner = new QWidget(menuBar);
    QHBoxLayout *findLayout = new QHBoxLayout(findCorner);
    findLayout->setContentsMargins(0, 2, 8, 2);
    findButton = new QToolButton(findCorner);
    findButton->setText("Find");
    findButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    {
        const int iconSize = 12;
        const qreal dpr = devicePixelRatioF();
        QIcon icon;
        icon.addPixmap(magnifierPixmap(QColor("#0066cc"), iconSize, dpr), QIcon::Normal, QIcon::Off);
        icon.addPixmap(magnifierPixmap(Qt::white, iconSize, dpr), QIcon::Normal, QIcon::On);
        findButton->setIcon(icon);
        findButton->setIconSize(QSize(iconSize, iconSize));
    }
    findButton->setToolTip("Find in terminal (Ctrl+Shift+F)");
    findButton->setFocusPolicy(Qt::NoFocus);
    findButton->setCursor(Qt::PointingHandCursor);
    findButton->setCheckable(true);  // stays pressed while the current tab's find bar is open
    findButton->setStyleSheet(
        "QToolButton { color: #0066cc; font-weight: bold; background: rgba(0, 102, 204, 0.07);"
        "  border: 1px solid rgba(0, 102, 204, 0.45); border-radius: 10px; padding: 1px 10px; }"
        "QToolButton:hover { background: rgba(0, 102, 204, 0.16); }"
        "QToolButton:pressed { background: rgba(0, 102, 204, 0.26); }"
        "QToolButton:checked { color: white; background: #0066cc; border-color: #0066cc; }"
        "QToolButton:checked:hover { background: #0059b3; }");
    // Some desktop styles (e.g. GTK) ignore a style sheet's text colour and
    // weight, so set them on the font and palette too; updateFindButton()
    // switches the text to white while the bar is open
    QFont findFont = findButton->font();
    findFont.setBold(true);
    findButton->setFont(findFont);
    findLayout->addWidget(findButton);
    connect(findButton, &QToolButton::clicked, this, &TerminalWindow::toggleFindBar);
    menuBar->setCornerWidget(findCorner, Qt::TopRightCorner);
    updateFindButton();
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
    
    configKeyLabel = new QLabel("-", this);
    configKeyLabel->setStyleSheet("QLabel { color: #333; }");
    formLayout->addRow("SSH Key:", configKeyLabel);
    
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
    AboutDialog::showFor(this);
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
