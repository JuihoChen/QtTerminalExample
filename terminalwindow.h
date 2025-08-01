// ============ Updated terminalwindow.h ============
#ifndef TERMINALWINDOW_H
#define TERMINALWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QTreeWidget>
#include <QSplitter>
#include <qtermwidget.h>

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
    int port;
    QString folder;  // Which folder this connection belongs to
    
    SSHConnection() : port(22) {}
    SSHConnection(const QString &n, const QString &h, const QString &u, int p = 22, const QString &f = "")
        : name(n), host(h), username(u), port(p), folder(f) {}
    
    // Add equality operator for QList::indexOf()
    bool operator==(const SSHConnection &other) const {
        return name == other.name && 
               host == other.host && 
               username == other.username && 
               port == other.port && 
               folder == other.folder;
    }
};

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

private:
    void setupUI();
    void setupMenus();
    void setupConnectionTree();
    void saveSettings();
    void loadSettings();
    QTermWidget* createTerminal();
    QTermWidget* getCurrentTerminal();
    QString getNextTabTitle();
    
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

#endif // TERMINALWINDOW_H