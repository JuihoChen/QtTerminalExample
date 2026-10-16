// SFTP access through libssh, run on a worker thread
#include "sftpworker.h"
#include "commandsafety.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QQueue>

#include <libssh/libssh.h>
#include <libssh/callbacks.h>
#include <libssh/sftp.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace {

const int kConnectTimeoutSeconds = 15;
const uint32_t kReadChunk = 32 * 1024;   // Every SFTP server accepts this size
const int kReadsInFlight = 16;           // Pipelined requests per file
const uint32_t kOpenSshWriteChunk = 128 * 1024;

// libssh 0.9 reads ~/.ssh/config and ~/.ssh/known_hosts with strtok(), which
// is not thread-safe: two panes connecting at once can corrupt each other's
// parsing and crash or hang. Only one worker at a time may run those parts.
QMutex g_configParseMutex;

QString joinRemotePath(const QString &dir, const QString &name)
{
    return dir.endsWith('/') ? dir + name : dir + '/' + name;
}

// Answers libssh's passphrase requests with "no passphrase". Without it,
// loading a passphrase-protected PEM key makes OpenSSL ask on the terminal
// the app was started from and wait there forever.
int refusePassphrase(const char *, char *, size_t, int, int, void *)
{
    return -1;
}

ssh_callbacks passphraseCallbacks()
{
    static ssh_callbacks_struct callbacks = [] {
        ssh_callbacks_struct cb;
        memset(&cb, 0, sizeof(cb));
        ssh_callbacks_init(&cb);
        cb.auth_function = refusePassphrase;
        return cb;
    }();
    return &callbacks;
}

// Offers the standard ~/.ssh keys without asking ssh-agent. libssh's
// ssh_userauth_publickey_auto() always uses a running agent, and the option
// to turn that off (SSH_OPTIONS_IDENTITY_AGENT) only exists from libssh 0.10.
bool authenticateWithDefaultKeys(ssh_session session)
{
    static const char *const names[] = { "id_ed25519", "id_ecdsa", "id_rsa" };
    QDir sshDir(QDir::home().filePath(".ssh"));
    for (const char *name : names) {
        QString path = sshDir.filePath(name);
        if (!QFileInfo::exists(path)) continue;

        ssh_key key = nullptr;
        if (ssh_pki_import_privkey_file(QFile::encodeName(path).constData(),
                                        nullptr, refusePassphrase, nullptr, &key) != SSH_OK) {
            continue;  // Unreadable, or needs a passphrase
        }
        int rc = ssh_userauth_publickey(session, nullptr, key);
        ssh_key_free(key);
        if (rc == SSH_AUTH_SUCCESS) return true;
        if (rc == SSH_AUTH_ERROR) return false;
    }
    return false;
}

struct PendingFile {
    RemoteEntry entry;
    QString localPath;
};

} // namespace

// Brackets one request with busy(true) / busy(false)
class BusyScope
{
public:
    explicit BusyScope(SftpWorker *worker) : m_worker(worker)
    {
        {
            QMutexLocker lock(&worker->m_socketMutex);
            worker->m_aborted = false;
        }
        emit worker->busy(true);
    }
    ~BusyScope() { emit m_worker->busy(false); }

private:
    SftpWorker *m_worker;
};

SftpWorker::SftpWorker(const SSHConnection &connection, QObject *parent)
    : QObject(parent), m_connection(connection), m_session(nullptr), m_sftp(nullptr),
      m_cancelAll(false), m_writeChunk(kReadChunk), m_socket(-1), m_aborted(false)
{
}

void SftpWorker::abortConnection()
{
    QMutexLocker lock(&m_socketMutex);
    m_aborted = true;
    // Holding the mutex keeps the worker from freeing (and the system from
    // reusing) the socket until shutdown() is done with it
    if (m_socket >= 0) ::shutdown(m_socket, SHUT_RDWR);
}

SftpWorker::~SftpWorker()
{
    disconnectFromServer();
}

void SftpWorker::cancelTransfer(int jobId)
{
    QMutexLocker lock(&m_cancelMutex);
    m_cancelledJobs.insert(jobId);
}

void SftpWorker::cancelAll()
{
    QMutexLocker lock(&m_cancelMutex);
    m_cancelAll = true;
}

bool SftpWorker::wasAborted()
{
    QMutexLocker lock(&m_socketMutex);
    return m_aborted;
}

bool SftpWorker::isCancelled(int jobId)
{
    QMutexLocker lock(&m_cancelMutex);
    return m_cancelAll || m_cancelledJobs.contains(jobId);
}

QString SftpWorker::formatPermissions(quint32 permissions, bool isDir, bool isLink)
{
    QString text(10, '-');
    if (isLink) text[0] = 'l';
    else if (isDir) text[0] = 'd';

    const char flags[] = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i) {
        if (permissions & (1u << (8 - i))) {
            text[i + 1] = flags[i];
        }
    }
    return text;
}

void SftpWorker::connectToServer()
{
    BusyScope scope(this);
    QString error;
    if (!ensureConnected(&error)) {
        emit connectionFailed(error);
        return;
    }

    char *home = sftp_canonicalize_path(m_sftp, ".");
    QString homePath = home ? QString::fromUtf8(home) : QStringLiteral("/");
    ssh_string_free_char(home);
    emit connected(homePath);
}

void SftpWorker::runCommand(const QString &command)
{
    BusyScope scope(this);
    QString error;
    if (!ensureSession(&error)) {
        emit commandFinished(-1, error);
        return;
    }

    emit step("Running command");
    ssh_channel channel = ssh_channel_new(m_session);
    if (!channel || ssh_channel_open_session(channel) != SSH_OK
        || ssh_channel_request_exec(channel, command.toUtf8().constData()) != SSH_OK) {
        error = QString("The server would not run the command: %1").arg(ssh_get_error(m_session));
        if (channel) ssh_channel_free(channel);
        emit commandFinished(-1, error);
        return;
    }

    char buffer[4096];
    char errBuffer[4096];
    while (true) {
        // Short reads so abortConnection() is noticed quickly
        int n = ssh_channel_read_timeout(channel, buffer, sizeof(buffer), 0, 500);
        if (n > 0) {
            emit commandOutput(QByteArray(buffer, n));
        }
        int errBytes = ssh_channel_read_nonblocking(channel, errBuffer, sizeof(errBuffer), 1);
        if (errBytes > 0) emit commandErrorOutput(QByteArray(errBuffer, errBytes));
        if (wasAborted()) {
            error = QStringLiteral("Cancelled");
            break;
        }
        if (n == SSH_ERROR || errBytes == SSH_ERROR) {
            error = QString("Lost the connection: %1").arg(ssh_get_error(m_session));
            break;
        }
        if (n <= 0 && errBytes <= 0 && ssh_channel_is_eof(channel)) break;
    }

    int status = -1;
    if (error.isEmpty()) {
        int errBytes;
        while ((errBytes = ssh_channel_read_nonblocking(channel, errBuffer, sizeof(errBuffer), 1)) > 0) {
            emit commandErrorOutput(QByteArray(errBuffer, errBytes));
        }
        ssh_channel_send_eof(channel);
        status = ssh_channel_get_exit_status(channel);
    }
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    if (!error.isEmpty()) disconnectFromServer();
    emit commandFinished(status, error);
}

void SftpWorker::disconnectFromServer()
{
    {
        QMutexLocker lock(&m_socketMutex);
        m_socket = -1;
    }
    if (m_sftp) {
        sftp_free(m_sftp);
        m_sftp = nullptr;
    }
    if (m_session) {
        ssh_disconnect(m_session);
        ssh_free(m_session);
        m_session = nullptr;
    }
}

bool SftpWorker::ensureConnected(QString *error)
{
    if (m_sftp) return true;
    if (!ensureSession(error)) return false;

    emit step("Starting SFTP");
    m_sftp = sftp_new(m_session);
    if (!m_sftp || sftp_init(m_sftp) != SSH_OK) {
        *error = QString("The server did not start SFTP: %1").arg(ssh_get_error(m_session));
        disconnectFromServer();
        return false;
    }

    // libssh waits for each write's reply before sending the next, so fewer,
    // larger writes upload much faster. OpenSSH accepts up to 255 KB per
    // write; other servers are only required to take 32 KB.
    const char *banner = ssh_get_serverbanner(m_session);
    m_writeChunk = banner && strstr(banner, "OpenSSH") ? kOpenSshWriteChunk : kReadChunk;
    return true;
}

// Connects and logs in, without starting SFTP
bool SftpWorker::ensureSession(QString *error)
{
    if (m_session && ssh_is_connected(m_session)) return true;
    disconnectFromServer();

    m_session = ssh_new();
    if (!m_session) {
        *error = "Could not create an SSH session.";
        return false;
    }

    QByteArray host = m_connection.host.toUtf8();
    QByteArray user = m_connection.username.toUtf8();
    unsigned int port = m_connection.port;
    long timeout = kConnectTimeoutSeconds;
    ssh_options_set(m_session, SSH_OPTIONS_HOST, host.constData());
    ssh_options_set(m_session, SSH_OPTIONS_USER, user.constData());
    ssh_options_set(m_session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(m_session, SSH_OPTIONS_TIMEOUT, &timeout);
    // ssh_userauth_publickey_auto() loads default keys with these callbacks
    ssh_set_callbacks(m_session, passphraseCallbacks());

    emit step(QString("Connecting to %1:%2").arg(m_connection.host).arg(m_connection.port));
    QMutexLocker parseLock(&g_configParseMutex);
    if (ssh_connect(m_session) != SSH_OK) {
        *error = QString("Could not connect to %1:%2\n%3")
                     .arg(m_connection.host).arg(m_connection.port)
                     .arg(ssh_get_error(m_session));
        disconnectFromServer();
        return false;
    }
    {
        QMutexLocker lock(&m_socketMutex);
        m_socket = ssh_get_fd(m_session);
    }

    emit step("Checking the host key");
    bool hostKeyOk = verifyHostKey(error);
    parseLock.unlock();
    if (!hostKeyOk || !authenticate(error)) {
        disconnectFromServer();
        return false;
    }
    return true;
}

// Matches "ssh -o StrictHostKeyChecking=accept-new": trust and remember a host
// seen for the first time, refuse one whose key changed.
bool SftpWorker::verifyHostKey(QString *error)
{
    switch (ssh_session_is_known_server(m_session)) {
    case SSH_KNOWN_HOSTS_OK:
        return true;
    case SSH_KNOWN_HOSTS_NOT_FOUND:
    case SSH_KNOWN_HOSTS_UNKNOWN:
        if (ssh_session_update_known_hosts(m_session) != SSH_OK) {
            // Still connect, like ssh does when known_hosts is read-only
            qWarning("Could not save host key for %s: %s",
                     qPrintable(m_connection.host), ssh_get_error(m_session));
        }
        return true;
    case SSH_KNOWN_HOSTS_CHANGED:
    case SSH_KNOWN_HOSTS_OTHER:
        *error = QString("The host key for %1 has changed since it was saved in "
                         "~/.ssh/known_hosts. This could mean someone is intercepting "
                         "the connection. Remove the old key with "
                         "\"ssh-keygen -R %1\" if the change is expected.")
                     .arg(m_connection.host);
        return false;
    case SSH_KNOWN_HOSTS_ERROR:
    default:
        *error = QString("Could not check the host key: %1").arg(ssh_get_error(m_session));
        return false;
    }
}

// Tries the same methods as the ssh command line built by CommandSafetyHelper:
// key file, ssh-agent and default keys, and the saved password. A connection
// with a saved password and key authentication off tries the password first,
// so a stray key or a slow agent can't get in its way.
bool SftpWorker::authenticate(QString *error)
{
    int rc = ssh_userauth_none(m_session, nullptr);
    if (rc == SSH_AUTH_SUCCESS) return true;
    if (rc == SSH_AUTH_ERROR) {
        *error = QString("Authentication failed: %1").arg(ssh_get_error(m_session));
        return false;
    }

    int methods = ssh_userauth_list(m_session, nullptr);
    QStringList tried;
    bool passwordFirst = !m_connection.useKeyAuth && !m_connection.password.isEmpty();

    if (passwordFirst && authenticateWithPassword(methods, &tried)) return true;
    if (authenticateWithKeys(methods, &tried)) return true;
    if (!passwordFirst && authenticateWithPassword(methods, &tried)) return true;

    if (tried.isEmpty()) {
        *error = QString("%1 accepts no login method this connection can offer. "
                         "Save a password, or set up an SSH key, in Edit Connection.")
                     .arg(m_connection.host);
    } else {
        *error = QString("The server refused the login for %1@%2.\nTried: %3")
                     .arg(m_connection.username, m_connection.host, tried.join(", "));
    }
    return false;
}

bool SftpWorker::authenticateWithKeys(int methods, QStringList *tried)
{
    if (!(methods & SSH_AUTH_METHOD_PUBLICKEY)) return false;

    QString keyFile = m_connection.useKeyAuth
        ? CommandSafetyHelper::expandHomePath(m_connection.identityFile) : QString();

    if (!keyFile.isEmpty()) {
        *tried << "key file";
        emit step("Logging in with key file");
        ssh_key key = nullptr;
        if (ssh_pki_import_privkey_file(QFile::encodeName(keyFile).constData(),
                                        nullptr, refusePassphrase, nullptr, &key) == SSH_OK) {
            int rc = ssh_userauth_publickey(m_session, nullptr, key);
            ssh_key_free(key);
            if (rc == SSH_AUTH_SUCCESS) return true;
        } else {
            tried->last() = "key file (could not be read, or needs a passphrase)";
        }
    }

    // IdentitiesOnly: with the agent turned off, offer only the key file
    bool onlyKeyFile = m_connection.useKeyAuth && !m_connection.useAgent && !keyFile.isEmpty();
    if (onlyKeyFile) return false;

    if (m_connection.useKeyAuth && !m_connection.useAgent) {
        // Same as "ssh -o IdentityAgent=none": never ask the running ssh-agent
        *tried << "default keys";
        emit step("Logging in with default keys");
        return authenticateWithDefaultKeys(m_session);
    }

    *tried << "ssh-agent and default keys";
    emit step("Logging in with ssh-agent and default keys");
    return ssh_userauth_publickey_auto(m_session, nullptr, nullptr) == SSH_AUTH_SUCCESS;
}

bool SftpWorker::authenticateWithPassword(int methods, QStringList *tried)
{
    QByteArray password = m_connection.password.toUtf8();
    if (password.isEmpty()) return false;

    if (methods & SSH_AUTH_METHOD_PASSWORD) {
        *tried << "password";
        emit step("Logging in with password");
        if (ssh_userauth_password(m_session, nullptr, password.constData()) == SSH_AUTH_SUCCESS) {
            return true;
        }
    }

    // Servers using PAM often ask for the password this way instead
    if (methods & SSH_AUTH_METHOD_INTERACTIVE) {
        *tried << "keyboard-interactive";
        emit step("Logging in with keyboard-interactive");
        int rc = ssh_userauth_kbdint(m_session, nullptr, nullptr);
        // A server that keeps asking (a password change, a second factor)
        // is not one a saved password can answer
        for (int round = 0; rc == SSH_AUTH_INFO && round < 5; ++round) {
            int prompts = ssh_userauth_kbdint_getnprompts(m_session);
            for (int i = 0; i < prompts; ++i) {
                char echo = 0;
                ssh_userauth_kbdint_getprompt(m_session, i, &echo);
                ssh_userauth_kbdint_setanswer(m_session, i, echo ? "" : password.constData());
            }
            rc = ssh_userauth_kbdint(m_session, nullptr, nullptr);
        }
        if (rc == SSH_AUTH_SUCCESS) return true;
    }
    return false;
}

QString SftpWorker::sftpError() const
{
    if (!m_sftp) return QStringLiteral("Not connected");

    switch (sftp_get_error(m_sftp)) {
    case SSH_FX_NO_SUCH_FILE:
    case SSH_FX_NO_SUCH_PATH:
        return QStringLiteral("No such file or folder");
    case SSH_FX_PERMISSION_DENIED:
        return QStringLiteral("Permission denied");
    case SSH_FX_CONNECTION_LOST:
    case SSH_FX_NO_CONNECTION:
        return QStringLiteral("Connection lost");
    default:
        return QString::fromUtf8(ssh_get_error(m_session));
    }
}

bool SftpWorker::readDirectory(const QString &path, QVector<RemoteEntry> *entries, QString *error)
{
    sftp_dir dir = sftp_opendir(m_sftp, path.toUtf8().constData());
    if (!dir) {
        *error = sftpError();
        return false;
    }

    while (sftp_attributes attr = sftp_readdir(m_sftp, dir)) {
        QString name = QString::fromUtf8(attr->name);
        if (name == "." || name == "..") {
            sftp_attributes_free(attr);
            continue;
        }

        RemoteEntry entry;
        entry.name = name;
        entry.path = joinRemotePath(path, name);
        entry.size = attr->size;
        entry.modified = QDateTime::fromSecsSinceEpoch(attr->mtime);
        entry.permissions = attr->permissions;
        entry.owner = attr->owner ? QString::fromUtf8(attr->owner) : QString::number(attr->uid);
        entry.group = attr->group ? QString::fromUtf8(attr->group) : QString::number(attr->gid);
        entry.isLink = attr->type == SSH_FILEXFER_TYPE_SYMLINK;
        entry.isDir = attr->type == SSH_FILEXFER_TYPE_DIRECTORY;
        entry.isSpecial = attr->type == SSH_FILEXFER_TYPE_SPECIAL;
        sftp_attributes_free(attr);

        if (entry.isLink) {
            // Show links by what they point to, like ls -L
            sftp_attributes target = sftp_stat(m_sftp, entry.path.toUtf8().constData());
            if (target) {
                entry.isDir = target->type == SSH_FILEXFER_TYPE_DIRECTORY;
                entry.isSpecial = target->type == SSH_FILEXFER_TYPE_SPECIAL;
                if (!entry.isDir) entry.size = target->size;
                // The target is what an upload through the link changes
                entry.modified = QDateTime::fromSecsSinceEpoch(target->mtime);
                sftp_attributes_free(target);
            }
        }
        entries->append(entry);
    }

    bool complete = sftp_dir_eof(dir);
    if (!complete) *error = sftpError();
    sftp_closedir(dir);
    return complete;
}

void SftpWorker::listDirectory(const QString &path)
{
    BusyScope scope(this);
    QString error;
    if (!ensureConnected(&error)) {
        emit listingFailed(path, error);
        return;
    }

    QVector<RemoteEntry> entries;
    if (readDirectory(path, &entries, &error)) {
        emit directoryListed(path, entries);
        return;
    }

    // A dropped connection gets one fresh attempt, unless the UI cut it
    // because the server stopped answering
    if (!wasAborted() && ssh_is_connected(m_session) == 0) {
        disconnectFromServer();
        entries.clear();
        if (ensureConnected(&error) && readDirectory(path, &entries, &error)) {
            emit directoryListed(path, entries);
            return;
        }
    }
    emit listingFailed(path, error);
}

void SftpWorker::download(int jobId, const RemoteEntry &entry, const QString &localPath)
{
    BusyScope scope(this);
    QString error;
    if (isCancelled(jobId)) {
        emit transferFinished(jobId, false, "Cancelled");
        return;
    }
    if (!ensureConnected(&error)) {
        emit transferFinished(jobId, false, error);
        return;
    }

    quint64 done = 0;
    m_progressTimer.start();
    emit transferProgress(jobId, 0, entry.isDir ? 0 : entry.size);
    bool ok;
    QStringList skipped;
    if (entry.isDir) {
        ok = downloadTree(jobId, entry, localPath, &done, &skipped, &error);
    } else if (entry.isSpecial) {
        ok = false;
        error = "Not a regular file (a pipe, socket or device), so it can't be copied";
    } else {
        ok = downloadFile(jobId, entry, localPath, &done, entry.size, &error);
    }
    if (!ok && ssh_is_connected(m_session) == 0) {
        disconnectFromServer();  // Reconnect for the next transfer
    }
    emit transferFinished(jobId, ok, ok ? skipped.join('\n') : error);
}

void SftpWorker::upload(int jobId, const QString &localPath, const QString &remotePath)
{
    BusyScope scope(this);
    QString error;
    if (isCancelled(jobId)) {
        emit transferFinished(jobId, false, "Cancelled");
        return;
    }
    if (!ensureConnected(&error)) {
        emit transferFinished(jobId, false, error);
        return;
    }

    QFileInfo info(localPath);
    quint64 done = 0;
    m_progressTimer.start();
    emit transferProgress(jobId, 0, info.isDir() ? 0 : quint64(info.size()));
    bool ok;
    QStringList skipped;
    if (info.isDir()) {
        ok = uploadTree(jobId, localPath, remotePath, &done, &skipped, &error);
    } else {
        ok = uploadFile(jobId, localPath, remotePath, &done, quint64(info.size()), &error);
    }
    if (!ok && ssh_is_connected(m_session) == 0) {
        disconnectFromServer();
    }
    emit transferFinished(jobId, ok, ok ? skipped.join('\n') : error);
}

// Progress at most ten times a second, so a fast link doesn't flood the UI
void SftpWorker::reportProgress(int jobId, quint64 bytesDone, quint64 bytesTotal)
{
    if (m_progressTimer.elapsed() >= 100 || bytesDone >= bytesTotal) {
        m_progressTimer.restart();
        emit transferProgress(jobId, bytesDone, bytesTotal);
    }
}

// Stops a folder transfer only for what ends all of it: a cancel, or a lost
// connection. Anything else skips the one item and carries on.
bool SftpWorker::canContinue(int jobId, QString *error)
{
    if (isCancelled(jobId)) {
        *error = "Cancelled";
        return false;
    }
    if (wasAborted() || ssh_is_connected(m_session) == 0) {
        if (error->isEmpty()) *error = "Connection lost";
        return false;
    }
    return true;
}

bool SftpWorker::downloadTree(int jobId, const RemoteEntry &root, const QString &localRoot,
                              quint64 *bytesDone, QStringList *skipped, QString *error)
{
    // Walk the folder first so progress can show the full size
    QStringList localDirs;
    QList<PendingFile> files;
    QList<PendingFile> links;
    quint64 total = 0;

    QQueue<PendingFile> pending;
    pending.enqueue(PendingFile{root, localRoot});
    while (!pending.isEmpty()) {
        if (isCancelled(jobId)) {
            *error = "Cancelled";
            return false;
        }
        PendingFile dir = pending.dequeue();
        emit step("Reading " + dir.entry.path);

        QVector<RemoteEntry> entries;
        QString reason;
        if (!readDirectory(dir.entry.path, &entries, &reason)) {
            if (dir.entry.path == root.path || !canContinue(jobId, &reason)) {
                *error = QString("%1: %2").arg(dir.entry.path, reason);
                return false;
            }
            *skipped << QString("%1/: %2").arg(dir.entry.path, reason);
            continue;
        }
        localDirs << dir.localPath;
        for (const RemoteEntry &child : entries) {
            QString childLocal = QDir(dir.localPath).filePath(child.name);
            if (child.isLink) {
                // Copied as links, like cp -a: following them could loop
                // forever or pull in a whole other tree (snap's "current")
                links.append(PendingFile{child, childLocal});
            } else if (child.isDir) {
                pending.enqueue(PendingFile{child, childLocal});
            } else if (child.isSpecial) {
                // Opening a pipe would wait forever for someone to write to it
                *skipped << QString("%1: not a regular file (a pipe, socket or device)")
                                .arg(child.path);
            } else {
                files.append(PendingFile{child, childLocal});
                total += child.size;
            }
        }
    }

    for (const QString &dir : localDirs) {
        if (!QDir().mkpath(dir)) {
            *error = QString("Could not create local folder %1").arg(dir);
            return false;
        }
    }

    emit transferProgress(jobId, 0, total);
    for (const PendingFile &file : files) {
        quint64 before = *bytesDone;
        QString reason;
        if (!downloadFile(jobId, file.entry, file.localPath, bytesDone, total, &reason)) {
            if (!canContinue(jobId, &reason)) {
                *error = QString("%1: %2").arg(file.entry.path, reason);
                return false;
            }
            *skipped << QString("%1: %2").arg(file.entry.path, reason);
            *bytesDone = before + file.entry.size;
            reportProgress(jobId, *bytesDone, total);
        }
    }
    for (const PendingFile &link : links) {
        QString reason;
        if (!copyLink(link.entry, link.localPath, &reason)) {
            if (!canContinue(jobId, &reason)) {
                *error = QString("%1: %2").arg(link.entry.path, reason);
                return false;
            }
            *skipped << QString("%1: %2").arg(link.entry.path, reason);
        }
    }
    return true;
}

// Recreates a remote symlink locally with the same target, even one that
// points nowhere on this machine
bool SftpWorker::copyLink(const RemoteEntry &link, const QString &localPath, QString *error)
{
    char *target = sftp_readlink(m_sftp, link.path.toUtf8().constData());
    if (!target) {
        *error = QString("Could not read the link: %1").arg(sftpError());
        return false;
    }
    QByteArray targetBytes(target);
    ssh_string_free_char(target);

    QByteArray local = QFile::encodeName(localPath);
    struct stat st;
    if (::lstat(local.constData(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            *error = "A local folder with that name is in the way";
            return false;
        }
        ::unlink(local.constData());
    }
    if (::symlink(targetBytes.constData(), local.constData()) != 0) {
        *error = QString("Could not create the link: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    return true;
}

// Reads with several requests in flight; one request per round trip is far
// too slow on a distant server.
bool SftpWorker::downloadFile(int jobId, const RemoteEntry &entry, const QString &localPath,
                              quint64 *bytesDone, quint64 bytesTotal, QString *error)
{
    sftp_file remote = sftp_open(m_sftp, entry.path.toUtf8().constData(), O_RDONLY, 0);
    if (!remote) {
        *error = sftpError();
        return false;
    }

    // Write beside the target and rename at the end, so a failed or
    // cancelled transfer never leaves a truncated file under the real name
    QString partPath = localPath + ".part";
    QFile local(partPath);
    if (!local.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QString("Could not write %1: %2").arg(partPath, local.errorString());
        sftp_close(remote);
        return false;
    }

    QByteArray buffer(kReadChunk, Qt::Uninitialized);
    QQueue<uint32_t> requests;
    quint64 requested = 0;
    quint64 received = 0;
    bool eof = false;
    bool ok = true;

    while (ok) {
        while (!eof && requested < entry.size && requests.size() < kReadsInFlight) {
            int id = sftp_async_read_begin(remote, kReadChunk);
            if (id < 0) {
                *error = sftpError();
                ok = false;
                break;
            }
            requests.enqueue(static_cast<uint32_t>(id));
            requested += kReadChunk;
        }
        if (!ok) break;

        int count;
        if (!requests.isEmpty()) {
            count = sftp_async_read(remote, buffer.data(), kReadChunk, requests.dequeue());
        } else {
            // Past the size seen at listing time (or a short read): finish
            // with plain reads until the server says end of file
            if (eof) break;
            sftp_seek64(remote, received);
            count = sftp_read(remote, buffer.data(), kReadChunk);
        }

        if (count < 0) {
            *error = sftpError();
            ok = false;
        } else if (count == 0) {
            eof = true;
        } else if (eof) {
            // Data after end of file means the file changed while reading
            continue;
        } else {
            if (local.write(buffer.constData(), count) != count) {
                *error = QString("Could not write %1: %2").arg(partPath, local.errorString());
                ok = false;
                break;
            }
            received += count;
            *bytesDone += count;
            reportProgress(jobId, *bytesDone, bytesTotal);

            if (static_cast<uint32_t>(count) < kReadChunk && received < entry.size) {
                // Short read in the middle: the queued offsets are now wrong,
                // so drop them and continue from where the data really ends
                while (!requests.isEmpty()) {
                    sftp_async_read(remote, buffer.data(), kReadChunk, requests.dequeue());
                }
                requested = entry.size;
            }
        }

        if (ok && isCancelled(jobId)) {
            *error = "Cancelled";
            ok = false;
        }
    }

    // Collect replies still in flight so the session stays in sync
    while (!requests.isEmpty()) {
        sftp_async_read(remote, buffer.data(), kReadChunk, requests.dequeue());
    }
    sftp_close(remote);
    local.close();

    if (!ok) {
        QFile::remove(partPath);
        return false;
    }

    QFile::remove(localPath);
    if (!QFile::rename(partPath, localPath)) {
        *error = QString("Could not rename %1 to %2").arg(partPath, localPath);
        QFile::remove(partPath);
        return false;
    }
    QFile done(localPath);
    if (done.open(QIODevice::ReadWrite)) {
        done.setFileTime(entry.modified, QFileDevice::FileModificationTime);
    }
    if (entry.permissions & S_IXUSR) {
        done.setPermissions(done.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    }
    return true;
}

bool SftpWorker::makeRemoteDir(const QString &path, QString *error)
{
    QByteArray remote = path.toUtf8();
    sftp_attributes attr = sftp_stat(m_sftp, remote.constData());
    if (attr) {
        bool isDir = attr->type == SSH_FILEXFER_TYPE_DIRECTORY;
        sftp_attributes_free(attr);
        if (isDir) return true;  // Uploading into a folder that exists merges
        *error = QString("%1: a file with that name already exists").arg(path);
        return false;
    }
    if (sftp_mkdir(m_sftp, remote.constData(), 0755) != SSH_OK) {
        *error = QString("Could not create %1: %2").arg(path, sftpError());
        return false;
    }
    return true;
}

bool SftpWorker::uploadTree(int jobId, const QString &localRoot, const QString &remoteRoot,
                            quint64 *bytesDone, QStringList *skipped, QString *error)
{
    // List the folder first so progress can show the full size
    QStringList remoteDirs;
    QList<QPair<QString, QString>> files;
    quint64 total = 0;

    QQueue<QPair<QString, QString>> pending;
    pending.enqueue(qMakePair(localRoot, remoteRoot));
    while (!pending.isEmpty()) {
        if (isCancelled(jobId)) {
            *error = "Cancelled";
            return false;
        }
        QPair<QString, QString> dir = pending.dequeue();
        QFileInfo dirInfo(dir.first);
        if (!dirInfo.isReadable() || !dirInfo.isExecutable()) {
            if (dir.first == localRoot) {
                *error = QString("Could not read %1: permission denied").arg(localRoot);
                return false;
            }
            *skipped << QString("%1/: permission denied").arg(dir.first);
            continue;
        }
        remoteDirs << dir.second;

        const QFileInfoList children = QDir(dir.first).entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &child : children) {
            QString childRemote = joinRemotePath(dir.second, child.fileName());
            if (child.isDir()) {
                // Following links to folders could loop forever
                if (child.isSymLink()) {
                    *skipped << QString("%1: a link to a folder (not followed)").arg(child.filePath());
                } else {
                    pending.enqueue(qMakePair(child.filePath(), childRemote));
                }
            } else if (child.isFile()) {
                files.append(qMakePair(child.filePath(), childRemote));
                total += child.size();
            } else {
                *skipped << QString("%1: not a regular file (a pipe, socket, device, "
                                    "or a link to nothing)").arg(child.filePath());
            }
        }
    }

    for (const QString &dir : remoteDirs) {
        emit step("Creating " + dir);
        if (!makeRemoteDir(dir, error)) return false;
    }

    emit transferProgress(jobId, 0, total);
    for (const auto &file : files) {
        quint64 before = *bytesDone;
        quint64 size = quint64(QFileInfo(file.first).size());
        QString reason;
        if (!uploadFile(jobId, file.first, file.second, bytesDone, total, &reason)) {
            if (!canContinue(jobId, &reason)) {
                *error = QString("%1: %2").arg(file.first, reason);
                return false;
            }
            *skipped << QString("%1: %2").arg(file.first, reason);
            *bytesDone = before + size;
            reportProgress(jobId, *bytesDone, total);
        }
    }
    return true;
}

// Writes beside the target and renames at the end, so a failed upload never
// leaves a truncated file under the real name. A file being replaced keeps
// its permissions, and its owner where the server allows it.
bool SftpWorker::uploadFile(int jobId, const QString &localPath, const QString &remotePath,
                            quint64 *bytesDone, quint64 bytesTotal, QString *error)
{
    QFile local(localPath);
    if (!local.open(QIODevice::ReadOnly)) {
        *error = QString("Could not read %1: %2").arg(localPath, local.errorString());
        return false;
    }

    QByteArray target = remotePath.toUtf8();
    QByteArray part = (remotePath + ".part").toUtf8();

    sftp_attributes existing = sftp_lstat(m_sftp, target.constData());
    bool exists = existing != nullptr;
    bool isLink = exists && existing->type == SSH_FILEXFER_TYPE_SYMLINK;
    if (isLink) {
        sftp_attributes_free(existing);
        existing = sftp_stat(m_sftp, target.constData());
    }
    if (existing && existing->type == SSH_FILEXFER_TYPE_DIRECTORY) {
        sftp_attributes_free(existing);
        *error = "A folder with that name already exists";
        return false;
    }

    mode_t mode = 0644;
    struct stat st;
    if (::stat(QFile::encodeName(localPath).constData(), &st) == 0) mode = st.st_mode & 0777;
    uint32_t uid = 0, gid = 0;
    if (existing) {
        mode = existing->permissions & 07777;
        uid = existing->uid;
        gid = existing->gid;
        sftp_attributes_free(existing);
    }

    // Write straight into a link's target so the link stays a link, and
    // into an existing file when its folder doesn't let us create the
    // temporary one (a writable file in /etc, say)
    bool inPlace = isLink;
    sftp_file remote = nullptr;
    if (!inPlace) {
        remote = sftp_open(m_sftp, part.constData(), O_WRONLY | O_CREAT | O_TRUNC, mode);
        if (!remote && exists && sftp_get_error(m_sftp) == SSH_FX_PERMISSION_DENIED) inPlace = true;
    }
    if (inPlace) remote = sftp_open(m_sftp, target.constData(), O_WRONLY | O_TRUNC, 0);
    if (!remote) {
        *error = sftpError();
        return false;
    }

    QByteArray buffer(m_writeChunk, Qt::Uninitialized);
    bool ok = true;
    while (ok) {
        qint64 count = local.read(buffer.data(), buffer.size());
        if (count < 0) {
            *error = QString("Could not read %1: %2").arg(localPath, local.errorString());
            ok = false;
        } else if (count == 0) {
            break;
        } else if (sftp_write(remote, buffer.constData(), size_t(count)) != count) {
            *error = sftpError();
            ok = false;
        } else {
            *bytesDone += count;
            reportProgress(jobId, *bytesDone, bytesTotal);
            if (isCancelled(jobId)) {
                *error = "Cancelled";
                ok = false;
            }
        }
    }
    sftp_close(remote);

    if (!ok) {
        if (!inPlace) sftp_unlink(m_sftp, part.constData());
        return false;
    }

    if (!inPlace) {
        if (exists) {
            sftp_chmod(m_sftp, part.constData(), mode);
            sftp_chown(m_sftp, part.constData(), uid, gid);  // Only works as root
        }
        // SFTP version 3 servers refuse to rename onto an existing file
        if (sftp_rename(m_sftp, part.constData(), target.constData()) != SSH_OK) {
            if (!exists || sftp_unlink(m_sftp, target.constData()) != SSH_OK
                || sftp_rename(m_sftp, part.constData(), target.constData()) != SSH_OK) {
                *error = QString("Could not replace %1: %2").arg(remotePath, sftpError());
                sftp_unlink(m_sftp, part.constData());
                return false;
            }
        }
    }

    struct timeval times[2];
    times[0].tv_sec = times[1].tv_sec = QFileInfo(local).lastModified().toSecsSinceEpoch();
    times[0].tv_usec = times[1].tv_usec = 0;
    sftp_utimes(m_sftp, target.constData(), times);
    return true;
}
