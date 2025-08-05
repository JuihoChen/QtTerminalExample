// ============ Updated terminalwindow.h with Password Field ============
#ifndef TERMINALWINDOW_H
#define TERMINALWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QTreeWidget>
#include <QSplitter>
#include <qtermwidget.h>
#include <QPainter>

QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QMenu;
class QAction;
class QMenuBar;
class QStatusBar;
class QCloseEvent;
QT_END_NAMESPACE

// Structure to hold connection data
struct SSHConnection {
    QString name;
    QString host;
    QString username;
    QString password;  // Added password field
    int port;
    QString folder;  // Which folder this connection belongs to
    
    SSHConnection() : port(22) {}
    SSHConnection(const QString &n, const QString &h, const QString &u, int p = 22, const QString &f = "", const QString &pass = "")
        : name(n), host(h), username(u), password(pass), port(p), folder(f) {}
    
    // Add equality operator for QList::indexOf()
    bool operator==(const SSHConnection &other) const {
        return name == other.name && 
               host == other.host && 
               username == other.username && 
               password == other.password &&
               port == other.port && 
               folder == other.folder;
    }
};

// Make SSHConnection available to QVariant system for Feature 4
Q_DECLARE_METATYPE(SSHConnection)

class TerminalWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit TerminalWindow(QWidget *parent = nullptr);
    ~TerminalWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void openFontDialog();
    void changeColorScheme();
    void increaseFont();
    void decreaseFont();
    void resetFont();
    void updateStatusBar();
    void newTab();
    void closeTab(int index);
    void closeCurrentTab();
    void showContextMenu(const QPoint &pos);
    void selectAllText();
    void onTabChanged(int index);
    void onTerminalFinished();
    
    // Connection tree slots
    void onConnectionDoubleClicked(QTreeWidgetItem *item, int column);
    void showConnectionContextMenu(const QPoint &pos);
    
    // Feature 3: Connection management slots
    void addNewConnection();
    void addConnectionToFolder(const QString &folderName);
    void editConnection(QTreeWidgetItem *item);
    void deleteConnection(QTreeWidgetItem *item);
    
    // Feature 4: SSH connection slots
    void connectToSSH(QTreeWidgetItem *item);

private:
    void setupUI();
    void setupMenus();
    void setupConnectionTree();
    void saveSettings();
    void loadSettings();
    QTermWidget* createTerminalWidget();
    QTermWidget* createTerminal();
    QTermWidget* getCurrentTerminal();
    QString getNextTabTitle();
    
    // Feature 4: SSH terminal creation
    QTermWidget* createSSHTerminal(const SSHConnection &connection);
    
    // Connection management methods
    void loadConnections();
    void saveConnections();
    void refreshConnectionTree();
    void createDefaultConnections();  // Creates initial connections if file doesn't exist
    QString getConnectionsFilePath() const;
    
    // Feature 3: Helper methods
    QStringList getExistingFolders() const;
    QTreeWidgetItem* findConnectionItem(const SSHConnection &connection);
    bool connectionExists(const SSHConnection &connection, int excludeIndex = -1) const;

    QTabWidget *tabWidget;
    QTreeWidget *connectionTree;
    QSplitter *splitter;
    int tabCounter;
    
    // Store connections
    QList<SSHConnection> connections;
};

class GripSplitterHandle : public QSplitterHandle
{
public:
    GripSplitterHandle(Qt::Orientation orientation, QSplitter *parent)
        : QSplitterHandle(orientation, parent) {
        setFixedWidth(6);
        // Enable mouse tracking to detect hover changes
        setMouseTracking(true);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QPainter painter(this);

        bool hovered = underMouse();

        // Background color
        QColor bgColor = hovered ? QColor(200, 200, 200) : QColor(240, 240, 240);
        painter.fillRect(rect(), bgColor);

        // Draw borders
        painter.setPen(QColor(200, 200, 200));
        painter.drawLine(0, 0, 0, height());
        painter.drawLine(width()-1, 0, width()-1, height());

        // Draw grip dots - make them darker on hover
        painter.setPen(Qt::NoPen);
        QColor dotColor = hovered ? QColor(80, 80, 80) : QColor(120, 120, 120);
        painter.setBrush(dotColor);

        int centerX = width() / 2;
        int centerY = height() / 2;
        int dotSize = 2;
        int spacing = 6;

        for (int i = -2; i <= 2; i++) {
            int y = centerY + (i * spacing);
            painter.drawEllipse(centerX - dotSize/2, y - dotSize/2, dotSize, dotSize);
        }
    }

    void enterEvent(QEvent *event) override {
        update(); // Force redraw when mouse enters
        QSplitterHandle::enterEvent(event);
    }
    
    void leaveEvent(QEvent *event) override {
        update(); // Force redraw when mouse leaves
        QSplitterHandle::leaveEvent(event);
    }
};

class GripSplitter : public QSplitter
{
public:
    GripSplitter(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSplitter(orientation, parent) {}

protected:
    QSplitterHandle *createHandle() override {
        return new GripSplitterHandle(orientation(), this);
    }
};

#endif // TERMINALWINDOW_H