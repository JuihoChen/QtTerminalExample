// ============ Updated terminalwindow.h with Split Left Pane ============
#ifndef TERMINALWINDOW_H
#define TERMINALWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QTreeWidget>
#include <QSplitter>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <qtermwidget.h>
#include <QPainter>
#include <QProgressDialog>
#include <QProcess>
#include <QUuid>

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

class GripSplitterHandle : public QSplitterHandle
{
public:
    GripSplitterHandle(Qt::Orientation orientation, QSplitter *parent)
        : QSplitterHandle(orientation, parent) {
        if (orientation == Qt::Horizontal) {
            setFixedWidth(6);
        } else {
            setFixedHeight(8);
        }
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
        if (orientation() == Qt::Horizontal) {
            painter.drawLine(0, 0, 0, height());
            painter.drawLine(width()-1, 0, width()-1, height());
        } else {
            painter.drawLine(0, 0, width(), 0);
            painter.drawLine(0, height()-1, width(), height()-1);
        }

        // Draw grip dots - make them darker on hover
        painter.setPen(Qt::NoPen);
        QColor dotColor = hovered ? QColor(80, 80, 80) : QColor(120, 120, 120);
        painter.setBrush(dotColor);

        int centerX = width() / 2;
        int centerY = height() / 2;
        int dotSize = 2;
        int spacing = 6;

        if (orientation() == Qt::Horizontal) {
            // Vertical dots for horizontal splitter
            for (int i = -2; i <= 2; i++) {
                int y = centerY + (i * spacing);
                painter.drawEllipse(centerX - dotSize/2, y - dotSize/2, dotSize, dotSize);
            }
        } else {
            // Horizontal dots for vertical splitter
            for (int i = -2; i <= 2; i++) {
                int x = centerX + (i * spacing);
                painter.drawEllipse(x - dotSize/2, centerY - dotSize/2, dotSize, dotSize);
            }
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

// Suggested improvements for your terminal application

// 1. Password Encryption Helper (add to header)
class PasswordManager {
public:
    static QString encryptPassword(const QString &plainText);
    static QString decryptPassword(const QString &encrypted);
private:
    static const QString ENCRYPTION_KEY;
};

// 2. Command Safety Helper
class CommandSafetyHelper {
public:
    // Safely escape arguments for shell commands - Qt version compatible
    static QString escapeShellArgument(const QString &argument) {
        if (argument.isEmpty()) {
            return "''";
        }
        
        // Check if argument needs quoting
        bool needsQuoting = false;
        static const QString specialChars = " \t\n\r\"'`$\\|&;<>(){}[]?*~#";
        
        for (const QChar &ch : argument) {
            if (specialChars.contains(ch)) {
                needsQuoting = true;
                break;
            }
        }
        
        if (!needsQuoting) {
            return argument;
        }
        
        // Escape single quotes and wrap in single quotes
        QString escaped = argument;
        escaped.replace("'", "'\"'\"'");
        return "'" + escaped + "'";
    }
    
    // Build safe SSH command
    static QString buildSafeSSHCommand(const SSHConnection &connection, 
                                      const QString &remoteCommand = QString()) {
        QString cmd;
        
        if (!connection.password.isEmpty()) {
            cmd = QString("sshpass -p %1 ").arg(escapeShellArgument(connection.password));
        }
        
        cmd += QString("ssh -o ServerAliveInterval=60 -o ServerAliveCountMax=3 "
                      "-o StrictHostKeyChecking=accept-new ");
        
        if (connection.port != 22) {
            cmd += QString("-p %1 ").arg(connection.port);
        }
        
        cmd += QString("%1@%2").arg(
            escapeShellArgument(connection.username),
            escapeShellArgument(connection.host)
        );
        
        if (!remoteCommand.isEmpty()) {
            cmd += QString(" %1").arg(escapeShellArgument(remoteCommand));
        }
        
        return cmd;
    }
};

// 3. Enhanced Error Handling for SSH Operations
class SSHErrorHandler {
public:
    enum ErrorType {
        Success = 0,
        GeneralError = 1,
        AuthenticationFailed = 255,
        ConnectionRefused = 61,
        HostUnreachable = 113,
        TimeoutError = 124
    };
    
    static QString getErrorDescription(int exitCode) {
        switch (exitCode) {
        case Success:
            return "Operation completed successfully";
        case AuthenticationFailed:
            return "Authentication failed - check username/password";
        case ConnectionRefused:
            return "Connection refused - check host and port";
        case HostUnreachable:
            return "Host unreachable - check network connection";
        case TimeoutError:
            return "Operation timed out";
        default:
            return QString("Unknown error (code: %1)").arg(exitCode);
        }
    }
};

// 4. Configuration Validation
class ConnectionValidator {
public:
    struct ValidationResult {
        bool isValid;
        QString errorMessage;
        QStringList warnings;
    };
    
    static ValidationResult validateConnection(const SSHConnection &connection) {
        ValidationResult result;
        result.isValid = true;
        
        // Basic validation
        if (connection.name.trimmed().isEmpty()) {
            result.isValid = false;
            result.errorMessage = "Connection name cannot be empty";
            return result;
        }
        
        if (connection.host.trimmed().isEmpty()) {
            result.isValid = false;
            result.errorMessage = "Host cannot be empty";
            return result;
        }
        
        if (connection.username.trimmed().isEmpty()) {
            result.isValid = false;
            result.errorMessage = "Username cannot be empty";
            return result;
        }
        
        if (connection.port < 1 || connection.port > 65535) {
            result.isValid = false;
            result.errorMessage = "Port must be between 1 and 65535";
            return result;
        }
        
        // Warnings for common issues
        if (connection.password.isEmpty()) {
            result.warnings.append("No password set - you'll need to enter it manually");
        }
        
        if (connection.port != 22) {
            result.warnings.append(QString("Using non-standard SSH port: %1").arg(connection.port));
        }
        
        return result;
    }
};

#endif // TERMINALWINDOW_H