#ifndef SFTPWORKER_H
#define SFTPWORKER_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QVector>
#include <QSet>
#include <QMutex>
#include <QElapsedTimer>
#include <QMetaType>

#include "sshconnection.h"

// libssh types, kept out of this header so only sftpworker.cpp needs libssh
struct ssh_session_struct;
struct sftp_session_struct;

// One entry of a remote directory listing
struct RemoteEntry {
    QString name;
    QString path;        // Full remote path
    quint64 size;
    QDateTime modified;
    quint32 permissions; // Mode bits as returned by the server
    QString owner;
    QString group;
    bool isDir;          // True for directories and links that point to one
    bool isLink;
    bool isSpecial;      // A pipe, socket or device: never opened

    RemoteEntry() : size(0), permissions(0), isDir(false), isLink(false), isSpecial(false) {}
};

Q_DECLARE_METATYPE(RemoteEntry)
Q_DECLARE_METATYPE(QVector<RemoteEntry>)

// Talks SFTP to one server through libssh. Lives in its own QThread; every
// slot blocks that thread, so call them with queued connections only.
class QThread;

class SftpWorker : public QObject
{
    Q_OBJECT

public:
    explicit SftpWorker(const SSHConnection &connection, QObject *parent = nullptr);
    ~SftpWorker() override;

    // Thread-safe: stop a queued or running transfer as soon as possible
    void cancelTransfer(int jobId);
    void cancelAll();

    // Thread-safe: cut the connection so a call blocked on an unresponsive
    // server returns with an error (libssh's SFTP reads have no timeout)
    void abortConnection();

    // Stops the worker and lets its thread finish on its own, then frees
    // both. Never waits: the thread may be stuck on a server that doesn't
    // answer, and waiting for it would freeze the app.
    static void retire(QThread *thread, SftpWorker *worker);
    // At exit: gives retired workers up to msecs to close their connections
    static void waitForRetired(int msecs);

    static QString formatPermissions(quint32 permissions, bool isDir, bool isLink);

public slots:
    void connectToServer();
    void listDirectory(const QString &path);
    void download(int jobId, const RemoteEntry &entry, const QString &localPath);
    // Copies a local file or folder to remotePath, replacing a file there
    void upload(int jobId, const QString &localPath, const QString &remotePath);
    void disconnectFromServer();
    // Runs a shell command on the server (no SFTP needed), sending its
    // output as it arrives with commandOutput(), then commandFinished()
    void runCommand(const QString &command);

signals:
    // busy(true) when a request starts and busy(false) when it ends; step()
    // in between names what it is waiting on. Together they let the UI notice
    // a request that stops making progress.
    void busy(bool busy);
    void step(const QString &description);

    void connected(const QString &homePath);
    void connectionFailed(const QString &error);
    void directoryListed(const QString &path, const QVector<RemoteEntry> &entries);
    void listingFailed(const QString &path, const QString &error);
    void transferProgress(int jobId, quint64 bytesDone, quint64 bytesTotal);
    // A folder transfer that skipped some items still succeeds, with the
    // skipped items and the reasons in error, one per line
    void transferFinished(int jobId, bool success, const QString &error);
    void commandOutput(const QByteArray &data);
    void commandErrorOutput(const QByteArray &data);
    // error is empty when the command ran; exitStatus is then its exit code,
    // or -1 when the server sent none (the command was killed by a signal)
    void commandFinished(int exitStatus, const QString &error);

private:
    bool ensureConnected(QString *error);
    bool ensureSession(QString *error);
    bool connectSession(QString *error);
    bool authenticate(QString *error);
    bool authenticateWithKeys(int methods, QStringList *tried);
    bool authenticateWithPassword(int methods, QStringList *tried);
    bool verifyHostKey(QString *error);
    QString sftpError() const;
    bool isCancelled(int jobId);
    bool wasAborted();
    bool readDirectory(const QString &path, QVector<RemoteEntry> *entries, QString *error);
    bool downloadFile(int jobId, const RemoteEntry &entry, const QString &localPath,
                      quint64 *bytesDone, quint64 bytesTotal, QString *error);
    bool downloadTree(int jobId, const RemoteEntry &root, const QString &localRoot,
                      quint64 *bytesDone, QStringList *skipped, QString *error);
    bool copyLink(const RemoteEntry &link, const QString &localPath, QString *error);
    bool canContinue(int jobId, QString *error);
    bool uploadFile(int jobId, const QString &localPath, const QString &remotePath,
                    quint64 *bytesDone, quint64 bytesTotal, QString *error);
    bool uploadTree(int jobId, const QString &localRoot, const QString &remoteRoot,
                    quint64 *bytesDone, QStringList *skipped, QString *error);
    bool makeRemoteDir(const QString &path, QString *error);
    void reportProgress(int jobId, quint64 bytesDone, quint64 bytesTotal);

    SSHConnection m_connection;
    ssh_session_struct *m_session;
    sftp_session_struct *m_sftp;

    QMutex m_cancelMutex;
    QSet<int> m_cancelledJobs;
    bool m_cancelAll;
    QElapsedTimer m_progressTimer;
    uint32_t m_writeChunk;  // Largest write the server is known to accept

    QMutex m_socketMutex;
    int m_socket;     // Connection's socket while connected, else -1
    bool m_aborted;   // abortConnection() was called during this request

    friend class BusyScope;
};

#endif // SFTPWORKER_H
