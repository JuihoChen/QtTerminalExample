#ifndef FILEBROWSERPANE_H
#define FILEBROWSERPANE_H

#include <QWidget>
#include <QTreeView>
#include <QHash>

#include "sshconnection.h"
#include "sftpworker.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QFileSystemModel;
class QTreeWidget;
class QTreeWidgetItem;
class QProgressBar;
class QThread;
class QTimer;
class QFileSystemWatcher;
class QTemporaryDir;
QT_END_NAMESPACE

class RemoteFileModel;
class RemoteSortProxy;

// Local folder list that accepts remote entries dropped from its own pane
class LocalDropView : public QTreeView
{
    Q_OBJECT

public:
    explicit LocalDropView(QWidget *parent = nullptr);
    void setAcceptedSource(quintptr source) { m_source = source; }

signals:
    void remoteEntriesDropped(const QVector<RemoteEntry> &entries, const QString &localDir);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void startDrag(Qt::DropActions supportedActions) override;

private:
    bool accepts(const QMimeData *data) const;
    QString dropFolder(const QPoint &pos) const;

    quintptr m_source;
};

// Remote file list that accepts local files and folders, from the local
// pane or from a file manager, to upload
class RemoteDropView : public QTreeView
{
    Q_OBJECT

public:
    explicit RemoteDropView(QWidget *parent = nullptr);

signals:
    void localFilesDropped(const QStringList &paths, const QString &remoteDir);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    static QStringList localPaths(const QMimeData *data);
    QString dropFolder(const QPoint &pos) const;
};

// WinSCP-style file browser for one SSH connection: the remote folder, a
// local folder to drop downloads into, and the transfer list.
class FileBrowserPane : public QWidget
{
    Q_OBJECT

public:
    explicit FileBrowserPane(const SSHConnection &connection, QWidget *parent = nullptr);
    explicit FileBrowserPane(QWidget *parent);
    ~FileBrowserPane() override;

    SSHConnection connection() const { return m_connection; }
    bool hasActiveTransfers() const;
    bool hasOpenEdits() const { return !m_edits.isEmpty(); }
    void setMessage(const QString &text);  // Shown in place of the remote list (local only)
    void setLocalListVisible(bool visible);

private slots:
    void onConnected(const QString &homePath);
    void onConnectionFailed(const QString &error);
    void onDirectoryListed(const QString &path, const QVector<RemoteEntry> &entries);
    void onListingFailed(const QString &path, const QString &error);
    void onTransferProgress(int jobId, quint64 bytesDone, quint64 bytesTotal);
    void onTransferFinished(int jobId, bool success, const QString &error);
    void showRemoteContextMenu(const QPoint &pos);
    void showLocalContextMenu(const QPoint &pos);
    void showTransferContextMenu(const QPoint &pos);

private:
    // Cuts a worker's connection when a request stops making progress, so an
    // unresponsive server can't leave the pane waiting forever
    struct Watchdog {
        QTimer *timer = nullptr;
        QString step;         // What the worker is currently waiting on
        QString stalledStep;  // Set when the timer fired during this request
    };

    struct Transfer {
        QTreeWidgetItem *item;
        QProgressBar *bar;
        bool finished;
        QString refreshDir;  // Remote folder to reload when an upload ends
        QString editPath;    // Local copy, for a file opened with Edit
        bool editUpload;     // Saving an edited file back to the server
        bool viewOnly;       // Local copy for View: read-only, never uploaded
    };

    // A remote file opened with Edit: a local copy, watched for saves
    struct EditedFile {
        QString remotePath;
        QByteArray savedHash;     // Content the server has (or is being sent)
        bool ready = false;       // Downloaded and open in the editor
        int uploadJob = 0;        // Save in progress, else 0
        bool changedAgain = false;
    };

    void watch(SftpWorker *worker, Watchdog *watchdog);
    QString stalledMessage(const QString &step) const;

    void setupSections(bool localOnly);
    QWidget *createRemoteSection();
    QWidget *createLocalSection();
    QWidget *createTransferSection();

    void navigateRemote(const QString &path);
    void refreshRemote();
    void openRemoteEntry(const RemoteEntry &entry);
    QVector<RemoteEntry> selectedRemoteEntries() const;
    void setRemoteStatus(const QString &text, bool isError = false);

    void setLocalFolder(const QString &path);
    QString localFolder() const;

    void downloadEntries(const QVector<RemoteEntry> &entries, const QString &localDir);
    void uploadPaths(const QStringList &localPaths, const QString &remoteDir);
    int addTransfer(const QString &name, bool isDir, bool upload, const QString &tooltip);
    void cancelTransfer(int jobId);
    void setTransferDetails(QTreeWidgetItem *item, const QString &summary, const QString &details);
    void showTransferDetails(QTreeWidgetItem *item);
    void clearFinishedTransfers();
    SftpWorker *transferWorker();

    void editRemoteFile(const RemoteEntry &entry);
    void viewRemoteFile(const RemoteEntry &entry);
    QString newLocalCopyPath(const QString &name);
    bool chooseEditor();
    void openInEditor(const QString &localPath);
    void watchEditedFile(const QString &localPath);
    void checkEditedFiles();
    void saveEditedFile(const QString &localPath);

    SSHConnection m_connection;

    // Listing and transfers use separate connections, so browsing stays
    // responsive while a large download runs
    QThread *m_browseThread;
    SftpWorker *m_browseWorker;
    QThread *m_transferThread;
    SftpWorker *m_transferWorker;

    bool m_connected;
    QString m_homePath;
    QString m_requestedPath;

    RemoteFileModel *m_remoteModel;
    RemoteSortProxy *m_remoteProxy;
    RemoteDropView *m_remoteView;
    QLineEdit *m_remotePathEdit;
    QLabel *m_remoteStatus;
    QLabel *m_message;  // Local-only pane: why there is no remote list
    QWidget *m_localSection;

    QFileSystemModel *m_localModel;
    LocalDropView *m_localView;
    QLineEdit *m_localPathEdit;

    QTreeWidget *m_transferList;
    QHash<int, Transfer> m_transfers;
    int m_nextJobId;

    QTemporaryDir *m_editDir;
    QFileSystemWatcher *m_editWatcher;
    QTimer *m_editTimer;                  // Waits for an editor to finish saving
    QHash<QString, EditedFile> m_edits;   // By local copy's path
    int m_nextEditDir;

    Watchdog m_browseWatchdog;
    Watchdog m_transferWatchdog;
};

#endif // FILEBROWSERPANE_H
