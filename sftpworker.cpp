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

namespace {

const int kConnectTimeoutSeconds = 15;
const uint32_t kReadChunk = 32 * 1024;   // Every SFTP server accepts this size
const int kReadsInFlight = 16;           // Pipelined requests per file

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
      m_cancelAll(false), m_socket(-1), m_aborted(false)
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

void SftpWorker::cancelDownload(int jobId)
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

    emit step("Starting SFTP");
    m_sftp = sftp_new(m_session);
    if (!m_sftp || sftp_init(m_sftp) != SSH_OK) {
        *error = QString("The server did not start SFTP: %1").arg(ssh_get_error(m_session));
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
        sftp_attributes_free(attr);

        if (entry.isLink) {
            // Show links by what they point to, like ls -L
            sftp_attributes target = sftp_stat(m_sftp, entry.path.toUtf8().constData());
            if (target) {
                entry.isDir = target->type == SSH_FILEXFER_TYPE_DIRECTORY;
                if (!entry.isDir) entry.size = target->size;
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
        emit downloadFinished(jobId, false, "Cancelled");
        return;
    }
    if (!ensureConnected(&error)) {
        emit downloadFinished(jobId, false, error);
        return;
    }

    quint64 done = 0;
    m_progressTimer.start();
    emit downloadProgress(jobId, 0, entry.isDir ? 0 : entry.size);
    bool ok;
    if (entry.isDir) {
        ok = downloadTree(jobId, entry, localPath, &done, &error);
    } else {
        ok = downloadFile(jobId, entry, localPath, &done, entry.size, &error);
    }
    if (!ok && ssh_is_connected(m_session) == 0) {
        disconnectFromServer();  // Reconnect for the next download
    }
    emit downloadFinished(jobId, ok, ok ? QString() : error);
}

// Progress at most ten times a second, so a fast link doesn't flood the UI
void SftpWorker::reportProgress(int jobId, quint64 bytesDone, quint64 bytesTotal)
{
    if (m_progressTimer.elapsed() >= 100 || bytesDone >= bytesTotal) {
        m_progressTimer.restart();
        emit downloadProgress(jobId, bytesDone, bytesTotal);
    }
}

bool SftpWorker::downloadTree(int jobId, const RemoteEntry &root, const QString &localRoot,
                              quint64 *bytesDone, QString *error)
{
    // Walk the folder first so progress can show the full size
    QStringList localDirs;
    QList<PendingFile> files;
    quint64 total = 0;

    QQueue<PendingFile> pending;
    pending.enqueue(PendingFile{root, localRoot});
    while (!pending.isEmpty()) {
        if (isCancelled(jobId)) {
            *error = "Cancelled";
            return false;
        }
        PendingFile dir = pending.dequeue();
        localDirs << dir.localPath;
        emit step("Reading " + dir.entry.path);

        QVector<RemoteEntry> entries;
        if (!readDirectory(dir.entry.path, &entries, error)) {
            *error = QString("%1: %2").arg(dir.entry.path, *error);
            return false;
        }
        for (const RemoteEntry &child : entries) {
            QString childLocal = QDir(dir.localPath).filePath(child.name);
            if (child.isDir) {
                // Following links to folders could loop forever
                if (!child.isLink) pending.enqueue(PendingFile{child, childLocal});
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

    emit downloadProgress(jobId, 0, total);
    for (const PendingFile &file : files) {
        if (!downloadFile(jobId, file.entry, file.localPath, bytesDone, total, error)) {
            *error = QString("%1: %2").arg(file.entry.path, *error);
            return false;
        }
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
