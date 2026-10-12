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

    RemoteEntry() : size(0), permissions(0), isDir(false), isLink(false) {}
};

Q_DECLARE_METATYPE(RemoteEntry)
Q_DECLARE_METATYPE(QVector<RemoteEntry>)

// Talks SFTP to one server through libssh. Lives in its own QThread; every
// slot blocks that thread, so call them with queued connections only.
class SftpWorker : public QObject
{
    Q_OBJECT

public:
    explicit SftpWorker(const SSHConnection &connection, QObject *parent = nullptr);
    ~SftpWorker() override;

    // Thread-safe: stop a queued or running download as soon as possible
    void cancelDownload(int jobId);
    void cancelAll();

    static QString formatPermissions(quint32 permissions, bool isDir, bool isLink);

public slots:
    void connectToServer();
    void listDirectory(const QString &path);
    void download(int jobId, const RemoteEntry &entry, const QString &localPath);
    void disconnectFromServer();

signals:
    void connected(const QString &homePath);
    void connectionFailed(const QString &error);
    void directoryListed(const QString &path, const QVector<RemoteEntry> &entries);
    void listingFailed(const QString &path, const QString &error);
    void downloadProgress(int jobId, quint64 bytesDone, quint64 bytesTotal);
    void downloadFinished(int jobId, bool success, const QString &error);

private:
    bool ensureConnected(QString *error);
    bool authenticate(QString *error);
    bool verifyHostKey(QString *error);
    QString sftpError() const;
    bool isCancelled(int jobId);
    bool readDirectory(const QString &path, QVector<RemoteEntry> *entries, QString *error);
    bool downloadFile(int jobId, const RemoteEntry &entry, const QString &localPath,
                      quint64 *bytesDone, quint64 bytesTotal, QString *error);
    bool downloadTree(int jobId, const RemoteEntry &root, const QString &localRoot,
                      quint64 *bytesDone, QString *error);
    void reportProgress(int jobId, quint64 bytesDone, quint64 bytesTotal);

    SSHConnection m_connection;
    ssh_session_struct *m_session;
    sftp_session_struct *m_sftp;

    QMutex m_cancelMutex;
    QSet<int> m_cancelledJobs;
    bool m_cancelAll;
    QElapsedTimer m_progressTimer;
};

#endif // SFTPWORKER_H
