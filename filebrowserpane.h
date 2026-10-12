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

private:
    bool accepts(const QMimeData *data) const;
    QString dropFolder(const QPoint &pos) const;

    quintptr m_source;
};

// WinSCP-style file browser for one SSH connection: the remote folder, a
// local folder to drop downloads into, and the transfer list.
class FileBrowserPane : public QWidget
{
    Q_OBJECT

public:
    explicit FileBrowserPane(const SSHConnection &connection, QWidget *parent = nullptr);
    ~FileBrowserPane() override;

    SSHConnection connection() const { return m_connection; }
    bool hasActiveTransfers() const;

private slots:
    void onConnected(const QString &homePath);
    void onConnectionFailed(const QString &error);
    void onDirectoryListed(const QString &path, const QVector<RemoteEntry> &entries);
    void onListingFailed(const QString &path, const QString &error);
    void onDownloadProgress(int jobId, quint64 bytesDone, quint64 bytesTotal);
    void onDownloadFinished(int jobId, bool success, const QString &error);
    void showRemoteContextMenu(const QPoint &pos);
    void showTransferContextMenu(const QPoint &pos);

private:
    struct Transfer {
        QTreeWidgetItem *item;
        QProgressBar *bar;
        bool finished;
    };

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
    void cancelTransfer(int jobId);
    void clearFinishedTransfers();
    SftpWorker *transferWorker();

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
    QTreeView *m_remoteView;
    QLineEdit *m_remotePathEdit;
    QLabel *m_remoteStatus;

    QFileSystemModel *m_localModel;
    LocalDropView *m_localView;
    QLineEdit *m_localPathEdit;

    QTreeWidget *m_transferList;
    QHash<int, Transfer> m_transfers;
    int m_nextJobId;
};

#endif // FILEBROWSERPANE_H
