// ============ Updated terminalwindow.h with Split Left Pane ============
#ifndef TERMINALWINDOW_H
#define TERMINALWINDOW_H

#include <QMainWindow>
#include <QTreeWidget>
#include <QSplitter>
#include <QGroupBox>
#include <QProgressDialog>

#include "qtermwidget.h"
#include "sshconnection.h"

QT_BEGIN_NAMESPACE
class QVBoxLayout;
class QMenu;
class QAction;
class QMenuBar;
class QStatusBar;
class QCloseEvent;
QT_END_NAMESPACE

class TerminalWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit TerminalWindow(QWidget *parent = nullptr);
    ~TerminalWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void showAbout();
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
    void copyClipboard();
    void selectAllText();
    void onTabChanged(int index);
    void onTerminalFinished();

    // Connection tree slots
    void onConnectionDoubleClicked(QTreeWidgetItem *item, int column);
    void onConnectionSelectionChanged();
    void showConnectionContextMenu(const QPoint &pos);

    // Feature 3: Connection management slots
    void addNewConnection();
    void addConnectionToFolder(const QString &folderName);
    void editConnection(QTreeWidgetItem *item);
    void deleteConnection(QTreeWidgetItem *item);

    // Feature 4: SSH connection slots
    void connectToSSH(QTreeWidgetItem *item);

    // Connection config panel slots
    void onQuickConnectClicked();
    void onEditConnectionClicked();
    void onDeleteConnectionClicked();

    // File transfer slots
    void showTabContextMenu(const QPoint &pos);
    void uploadFileToSSH(const SSHConnection &connection);
    void downloadFileFromSSH(const SSHConnection &connection);
    void browseRemoteFiles(const SSHConnection &connection);

private:
    void setupUI();
    void setupMenus();
    void setupConnectionTree();
    void setupConnectionConfigPanel();
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

    // Connection config panel methods
    void updateConnectionConfig(const SSHConnection &connection);
    void clearConnectionConfig();
    SSHConnection getCurrentSelectedConnection() const;

    // File transfer methods
    void performSCPUpload(const SSHConnection &connection, const QString &localFile, const QString &remotePath);
    void performSCPDownload(const SSHConnection &connection, const QString &remoteFile, const QString &localFile);
    QString getDefaultRemotePath(const SSHConnection &connection);
    void detectRemoteWorkingDirectory(const SSHConnection &connection,
        std::function<void(const QString&)> callback);
    QString getCurrentRemoteDirectory(QTermWidget *terminal);
    void showRemoteFileBrowser(const SSHConnection &connection, const QString &remotePath,
        std::function<void(const QString&)> callback);
    QProgressDialog *scpProgressDialog;

    QTabWidget *tabWidget;
    QTreeWidget *connectionTree;
    QSplitter *mainSplitter;        // Main horizontal splitter (left panel | terminal area)
    QSplitter *leftPanelSplitter;   // Vertical splitter for left panel (tree | config)
    int tabCounter;

    // Connection config panel widgets
    QGroupBox *connectionConfigGroup;
    QLabel *configNameLabel;
    QLabel *configHostLabel;
    QLabel *configUsernameLabel;
    QLabel *configPortLabel;
    QLabel *configFolderLabel;
    QLabel *configPasswordLabel;
    QPushButton *quickConnectButton;
    QPushButton *editConnectionButton;
    QPushButton *deleteConnectionButton;

    // Store connections
    QList<SSHConnection> connections;

    // Current selected connection for config panel
    SSHConnection selectedConnection;
    bool hasSelectedConnection;
};

#endif // TERMINALWINDOW_H