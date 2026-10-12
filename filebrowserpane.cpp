// WinSCP-style file browser pane for one SSH connection
#include "filebrowserpane.h"
#include "remotefilemodel.h"

#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QProgressBar>
#include <QSettings>
#include <QSplitter>
#include <QStyle>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// A request that shows no progress for this long is cut off
const int kStallSeconds = 30;

const char *const kEditorSetting = "fileBrowser/editor";

QString joinRemotePath(const QString &dir, const QString &name)
{
    return dir.endsWith('/') ? dir + name : dir + '/' + name;
}

// Null when the file can't be read (an editor may be halfway through saving)
QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray();
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&file)) return QByteArray();
    return hash.result();
}

QToolButton *makeToolButton(QWidget *parent, QStyle::StandardPixmap icon, const QString &tip)
{
    QToolButton *button = new QToolButton(parent);
    button->setIcon(parent->style()->standardIcon(icon));
    button->setToolTip(tip);
    button->setAutoRaise(true);
    return button;
}

QLabel *makeSectionLabel(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

} // namespace

// ============ LocalDropView ============

LocalDropView::LocalDropView(QWidget *parent)
    : QTreeView(parent), m_source(0)
{
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
}

// Only ever offer a copy, so a file manager can't move (delete) local files
void LocalDropView::startDrag(Qt::DropActions)
{
    QTreeView::startDrag(Qt::CopyAction);
}

bool LocalDropView::accepts(const QMimeData *data) const
{
    if (!data->hasFormat(REMOTE_ENTRIES_MIME)) return false;
    quintptr source = 0;
    RemoteFileModel::decodeEntries(data, &source);
    return source == m_source;
}

QString LocalDropView::dropFolder(const QPoint &pos) const
{
    QFileSystemModel *fs = qobject_cast<QFileSystemModel *>(model());
    QModelIndex index = indexAt(pos);
    if (fs && index.isValid() && fs->isDir(index)) return fs->filePath(index);
    return fs ? fs->filePath(rootIndex()) : QString();
}

void LocalDropView::dragEnterEvent(QDragEnterEvent *event)
{
    if (accepts(event->mimeData())) event->acceptProposedAction();
    else event->ignore();
}

void LocalDropView::dragMoveEvent(QDragMoveEvent *event)
{
    if (accepts(event->mimeData())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void LocalDropView::dropEvent(QDropEvent *event)
{
    if (!accepts(event->mimeData())) {
        event->ignore();
        return;
    }
    QVector<RemoteEntry> entries = RemoteFileModel::decodeEntries(event->mimeData(), nullptr);
    QString folder = dropFolder(event->pos());
    event->setDropAction(Qt::CopyAction);
    event->accept();
    if (!entries.isEmpty() && !folder.isEmpty()) emit remoteEntriesDropped(entries, folder);
}

// ============ RemoteDropView ============

RemoteDropView::RemoteDropView(QWidget *parent)
    : QTreeView(parent)
{
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
}

QStringList RemoteDropView::localPaths(const QMimeData *data)
{
    QStringList paths;
    if (data->hasFormat(REMOTE_ENTRIES_MIME)) return paths;  // Our own drag
    for (const QUrl &url : data->urls()) {
        if (url.isLocalFile()) paths << url.toLocalFile();
    }
    return paths;
}

QString RemoteDropView::dropFolder(const QPoint &pos) const
{
    QModelIndex index = indexAt(pos);
    if (index.isValid()) {
        RemoteEntry entry = model()->data(index, RemoteFileModel::EntryRole).value<RemoteEntry>();
        if (entry.isDir) return entry.path;
    }
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(model());
    auto *remote = proxy ? qobject_cast<RemoteFileModel *>(proxy->sourceModel()) : nullptr;
    return remote ? remote->path() : QString();
}

void RemoteDropView::dragEnterEvent(QDragEnterEvent *event)
{
    dragMoveEvent(event);
}

void RemoteDropView::dragMoveEvent(QDragMoveEvent *event)
{
    if (!localPaths(event->mimeData()).isEmpty() && !dropFolder(event->pos()).isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void RemoteDropView::dropEvent(QDropEvent *event)
{
    QStringList paths = localPaths(event->mimeData());
    QString folder = dropFolder(event->pos());
    if (paths.isEmpty() || folder.isEmpty()) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    emit localFilesDropped(paths, folder);
}

// ============ FileBrowserPane ============

FileBrowserPane::FileBrowserPane(const SSHConnection &connection, QWidget *parent)
    : QWidget(parent), m_connection(connection),
      m_browseThread(new QThread(this)), m_browseWorker(new SftpWorker(connection)),
      m_transferThread(nullptr), m_transferWorker(nullptr),
      m_connected(false), m_nextJobId(1),
      m_editDir(nullptr), m_editWatcher(new QFileSystemWatcher(this)),
      m_editTimer(new QTimer(this)), m_nextEditDir(1)
{
    qRegisterMetaType<RemoteEntry>("RemoteEntry");
    qRegisterMetaType<QVector<RemoteEntry>>("QVector<RemoteEntry>");

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);

    QSplitter *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(createRemoteSection());
    splitter->addWidget(createLocalSection());
    splitter->addWidget(createTransferSection());
    splitter->setStretchFactor(0, 5);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 1);
    splitter->setCollapsible(0, false);
    layout->addWidget(splitter);

    m_browseWorker->moveToThread(m_browseThread);
    connect(m_browseWorker, &SftpWorker::connected, this, &FileBrowserPane::onConnected);
    connect(m_browseWorker, &SftpWorker::connectionFailed, this, &FileBrowserPane::onConnectionFailed);
    connect(m_browseWorker, &SftpWorker::directoryListed, this, &FileBrowserPane::onDirectoryListed);
    connect(m_browseWorker, &SftpWorker::listingFailed, this, &FileBrowserPane::onListingFailed);
    connect(m_browseWorker, &SftpWorker::step, this, [this](const QString &step) {
        if (!m_connected) setRemoteStatus(step + "...");
    });
    watch(m_browseWorker, &m_browseWatchdog);
    m_browseThread->start();

    // Editors often save in several steps (write a new file, then rename it
    // over the old one), so wait a moment before reading the result
    m_editTimer->setSingleShot(true);
    m_editTimer->setInterval(500);
    connect(m_editWatcher, &QFileSystemWatcher::fileChanged, this, [this]() { m_editTimer->start(); });
    connect(m_editTimer, &QTimer::timeout, this, &FileBrowserPane::checkEditedFiles);

    setRemoteStatus(QString("Connecting to %1...").arg(m_connection.host));
    QMetaObject::invokeMethod(m_browseWorker, "connectToServer", Qt::QueuedConnection);
}

FileBrowserPane::~FileBrowserPane()
{
    // Stop downloads at the next chunk, and cut connections that may be
    // stuck waiting on the server, so the threads can finish quickly
    m_browseWorker->cancelAll();
    m_browseWorker->abortConnection();
    if (m_transferWorker) {
        m_transferWorker->cancelAll();
        m_transferWorker->abortConnection();
    }

    m_browseThread->quit();
    m_browseThread->wait();
    delete m_browseWorker;

    if (m_transferThread) {
        m_transferThread->quit();
        m_transferThread->wait();
        delete m_transferWorker;
    }

    delete m_editDir;  // Removes the local copies of edited files
}

void FileBrowserPane::watch(SftpWorker *worker, Watchdog *watchdog)
{
    watchdog->timer = new QTimer(this);
    watchdog->timer->setSingleShot(true);
    watchdog->timer->setInterval(kStallSeconds * 1000);

    connect(worker, &SftpWorker::busy, this, [watchdog](bool busy) {
        if (busy) {
            watchdog->step.clear();
            watchdog->stalledStep.clear();
            watchdog->timer->start();
        } else {
            watchdog->timer->stop();
        }
    });
    connect(worker, &SftpWorker::step, this, [watchdog](const QString &step) {
        watchdog->step = step;
        if (watchdog->timer->isActive()) watchdog->timer->start();
    });
    connect(worker, &SftpWorker::transferProgress, this, [watchdog]() {
        if (watchdog->timer->isActive()) watchdog->timer->start();
    });
    connect(watchdog->timer, &QTimer::timeout, this, [worker, watchdog]() {
        watchdog->stalledStep = watchdog->step.isEmpty() ? QStringLiteral("Waiting for the server")
                                                         : watchdog->step;
        worker->abortConnection();
    });
}

QString FileBrowserPane::stalledMessage(const QString &step) const
{
    QString message = QString("The server stopped answering. No response for %1 seconds at: %2.")
                          .arg(kStallSeconds).arg(step);
    if (step == "Starting SFTP") {
        message += QString("\n\nThe login worked, but the server's SFTP service never replied. "
                           "This usually means a script that runs at login on the server "
                           "(such as ~/.bashrc) waits for input or runs a long command even "
                           "without a terminal. Running \"sftp %1@%2\" in a terminal "
                           "should hang the same way.")
                       .arg(m_connection.username, m_connection.host);
    }
    return message;
}

QWidget *FileBrowserPane::createRemoteSection()
{
    QWidget *section = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    layout->addWidget(makeSectionLabel(
        QString("Remote: %1@%2").arg(m_connection.username, m_connection.host), section));

    QHBoxLayout *bar = new QHBoxLayout;
    QToolButton *upButton = makeToolButton(section, QStyle::SP_FileDialogToParent, "Parent folder (Backspace)");
    QToolButton *homeButton = makeToolButton(section, QStyle::SP_DirHomeIcon, "Home folder");
    QToolButton *refreshButton = makeToolButton(section, QStyle::SP_BrowserReload, "Refresh");
    m_remotePathEdit = new QLineEdit(section);
    m_remotePathEdit->setPlaceholderText("Remote folder");
    bar->addWidget(upButton);
    bar->addWidget(homeButton);
    bar->addWidget(refreshButton);
    bar->addWidget(m_remotePathEdit);
    layout->addLayout(bar);

    m_remoteModel = new RemoteFileModel(this);
    m_remoteProxy = new RemoteSortProxy(this);
    m_remoteProxy->setSourceModel(m_remoteModel);

    m_remoteView = new RemoteDropView(section);
    m_remoteView->setModel(m_remoteProxy);
    m_remoteView->setRootIsDecorated(false);
    m_remoteView->setUniformRowHeights(true);
    m_remoteView->setAlternatingRowColors(true);
    m_remoteView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_remoteView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_remoteView->setSortingEnabled(true);
    m_remoteView->sortByColumn(RemoteFileModel::NameColumn, Qt::AscendingOrder);
    m_remoteView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_remoteView->header()->setSectionResizeMode(RemoteFileModel::NameColumn, QHeaderView::Stretch);
    m_remoteView->header()->setStretchLastSection(false);
    layout->addWidget(m_remoteView);

    m_remoteStatus = new QLabel(section);
    m_remoteStatus->setWordWrap(true);
    m_remoteStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_remoteStatus);

    connect(upButton, &QToolButton::clicked, this, [this]() {
        if (!m_remoteModel->path().isEmpty()) navigateRemote(RemoteFileModel::parentPath(m_remoteModel->path()));
    });
    connect(homeButton, &QToolButton::clicked, this, [this]() {
        if (!m_homePath.isEmpty()) navigateRemote(m_homePath);
    });
    connect(refreshButton, &QToolButton::clicked, this, &FileBrowserPane::refreshRemote);
    connect(m_remotePathEdit, &QLineEdit::returnPressed, this, [this]() {
        QString path = m_remotePathEdit->text().trimmed();
        if (!path.isEmpty()) navigateRemote(path);
    });
    connect(m_remoteView, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        openRemoteEntry(m_remoteProxy->data(index, RemoteFileModel::EntryRole).value<RemoteEntry>());
    });
    connect(m_remoteView, &QTreeView::customContextMenuRequested,
            this, &FileBrowserPane::showRemoteContextMenu);
    connect(m_remoteView, &RemoteDropView::localFilesDropped, this, &FileBrowserPane::uploadPaths);

    QAction *upAction = new QAction(m_remoteView);
    upAction->setShortcut(Qt::Key_Backspace);
    upAction->setShortcutContext(Qt::WidgetShortcut);
    connect(upAction, &QAction::triggered, upButton, &QToolButton::click);
    m_remoteView->addAction(upAction);

    QAction *openAction = new QAction(m_remoteView);
    openAction->setShortcuts(QList<QKeySequence>() << Qt::Key_Return << Qt::Key_Enter);
    openAction->setShortcutContext(Qt::WidgetShortcut);
    connect(openAction, &QAction::triggered, this, [this]() {
        QVector<RemoteEntry> entries = selectedRemoteEntries();
        if (entries.size() == 1) openRemoteEntry(entries.first());
        else if (!entries.isEmpty()) downloadEntries(entries, localFolder());
    });
    m_remoteView->addAction(openAction);

    QAction *viewAction = new QAction(m_remoteView);
    viewAction->setShortcut(Qt::Key_F3);
    viewAction->setShortcutContext(Qt::WidgetShortcut);
    connect(viewAction, &QAction::triggered, this, [this]() {
        QVector<RemoteEntry> entries = selectedRemoteEntries();
        if (entries.size() == 1 && !entries.first().isDir) viewRemoteFile(entries.first());
    });
    m_remoteView->addAction(viewAction);

    QAction *editAction = new QAction(m_remoteView);
    editAction->setShortcut(Qt::Key_F4);
    editAction->setShortcutContext(Qt::WidgetShortcut);
    connect(editAction, &QAction::triggered, this, [this]() {
        QVector<RemoteEntry> entries = selectedRemoteEntries();
        if (entries.size() == 1 && !entries.first().isDir) editRemoteFile(entries.first());
    });
    m_remoteView->addAction(editAction);

    return section;
}

QWidget *FileBrowserPane::createLocalSection()
{
    QWidget *section = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    layout->addWidget(makeSectionLabel("Local (drag files between the lists to download or upload)", section));

    QHBoxLayout *bar = new QHBoxLayout;
    QToolButton *upButton = makeToolButton(section, QStyle::SP_FileDialogToParent, "Parent folder");
    QToolButton *homeButton = makeToolButton(section, QStyle::SP_DirHomeIcon, "Home folder");
    QToolButton *chooseButton = makeToolButton(section, QStyle::SP_DirOpenIcon, "Choose folder...");
    m_localPathEdit = new QLineEdit(section);
    bar->addWidget(upButton);
    bar->addWidget(homeButton);
    bar->addWidget(chooseButton);
    bar->addWidget(m_localPathEdit);
    layout->addLayout(bar);

    m_localModel = new QFileSystemModel(this);
    m_localModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Drives);
    m_localModel->setReadOnly(true);

    m_localView = new LocalDropView(section);
    m_localView->setModel(m_localModel);
    m_localView->setAcceptedSource(reinterpret_cast<quintptr>(m_remoteModel));
    m_localView->setRootIsDecorated(false);
    m_localView->setItemsExpandable(false);
    m_localView->setUniformRowHeights(true);
    m_localView->setAlternatingRowColors(true);
    m_localView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_localView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_localView->setSortingEnabled(true);
    m_localView->sortByColumn(0, Qt::AscendingOrder);
    m_localView->hideColumn(2);  // Type
    m_localView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_localView->header()->setStretchLastSection(false);
    layout->addWidget(m_localView);

    connect(upButton, &QToolButton::clicked, this, [this]() {
        QDir dir(localFolder());
        if (dir.cdUp()) setLocalFolder(dir.absolutePath());
    });
    connect(homeButton, &QToolButton::clicked, this, [this]() { setLocalFolder(QDir::homePath()); });
    connect(chooseButton, &QToolButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Choose Download Folder", localFolder());
        if (!dir.isEmpty()) setLocalFolder(dir);
    });
    connect(m_localPathEdit, &QLineEdit::returnPressed, this, [this]() {
        setLocalFolder(m_localPathEdit->text().trimmed());
    });
    connect(m_localView, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        if (m_localModel->isDir(index)) setLocalFolder(m_localModel->filePath(index));
    });
    connect(m_localView, &LocalDropView::remoteEntriesDropped, this, &FileBrowserPane::downloadEntries);
    connect(m_localView, &QTreeView::customContextMenuRequested,
            this, &FileBrowserPane::showLocalContextMenu);

    setLocalFolder(QDir::homePath());
    return section;
}

QWidget *FileBrowserPane::createTransferSection()
{
    QWidget *section = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    QHBoxLayout *header = new QHBoxLayout;
    header->addWidget(makeSectionLabel("Transfers", section));
    header->addStretch();
    QToolButton *clearButton = new QToolButton(section);
    clearButton->setText("Clear Finished");
    clearButton->setAutoRaise(true);
    header->addWidget(clearButton);
    layout->addLayout(header);

    m_transferList = new QTreeWidget(section);
    m_transferList->setColumnCount(3);
    m_transferList->setHeaderLabels(QStringList() << "File" << "Progress" << "Status");
    m_transferList->setRootIsDecorated(false);
    m_transferList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_transferList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_transferList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_transferList->header()->setStretchLastSection(false);
    m_transferList->setColumnWidth(1, 140);
    m_transferList->setColumnWidth(2, 110);
    layout->addWidget(m_transferList);

    connect(clearButton, &QToolButton::clicked, this, &FileBrowserPane::clearFinishedTransfers);
    connect(m_transferList, &QTreeWidget::customContextMenuRequested,
            this, &FileBrowserPane::showTransferContextMenu);
    return section;
}

// ---------- Remote side ----------

void FileBrowserPane::setRemoteStatus(const QString &text, bool isError)
{
    m_remoteStatus->setText(text);
    m_remoteStatus->setStyleSheet(isError ? "QLabel { color: #c0392b; }" : QString());
    m_remoteStatus->setVisible(!text.isEmpty());
}

void FileBrowserPane::onConnected(const QString &homePath)
{
    m_connected = true;
    m_homePath = homePath;
    navigateRemote(homePath);
}

void FileBrowserPane::onConnectionFailed(const QString &error)
{
    QString message = m_browseWatchdog.stalledStep.isEmpty()
        ? error : stalledMessage(m_browseWatchdog.stalledStep);
    setRemoteStatus(message + "\n\nPress Refresh to try again.", true);
}

void FileBrowserPane::navigateRemote(const QString &path)
{
    if (!m_connected) {
        refreshRemote();
        return;
    }
    m_requestedPath = path;
    setRemoteStatus(QString("Loading %1...").arg(path));
    QMetaObject::invokeMethod(m_browseWorker, "listDirectory", Qt::QueuedConnection,
                              Q_ARG(QString, path));
}

void FileBrowserPane::refreshRemote()
{
    if (!m_connected) {
        setRemoteStatus(QString("Connecting to %1...").arg(m_connection.host));
        QMetaObject::invokeMethod(m_browseWorker, "connectToServer", Qt::QueuedConnection);
        return;
    }
    navigateRemote(m_remoteModel->path().isEmpty() ? m_homePath : m_remoteModel->path());
}

void FileBrowserPane::onDirectoryListed(const QString &path, const QVector<RemoteEntry> &entries)
{
    if (path != m_requestedPath) return;  // A newer request is on its way

    bool samePath = path == m_remoteModel->path();
    m_remoteModel->setEntries(path, entries);
    m_remotePathEdit->setText(path);
    if (!samePath && m_remoteProxy->rowCount() > 0) {
        m_remoteView->setCurrentIndex(m_remoteProxy->index(0, 0));
    }
    m_remoteView->resizeColumnToContents(RemoteFileModel::SizeColumn);
    m_remoteView->resizeColumnToContents(RemoteFileModel::ModifiedColumn);
    m_remoteView->resizeColumnToContents(RemoteFileModel::PermissionsColumn);
    setRemoteStatus(QString());
}

void FileBrowserPane::onListingFailed(const QString &path, const QString &error)
{
    if (path != m_requestedPath) return;
    m_requestedPath = m_remoteModel->path();
    m_remotePathEdit->setText(m_remoteModel->path());
    QString reason = m_browseWatchdog.stalledStep.isEmpty()
        ? error : stalledMessage(m_browseWatchdog.stalledStep);
    setRemoteStatus(QString("Could not open %1: %2").arg(path, reason), true);
}

void FileBrowserPane::openRemoteEntry(const RemoteEntry &entry)
{
    if (entry.path.isEmpty()) return;
    if (entry.isDir) {
        navigateRemote(entry.path);
    } else {
        downloadEntries(QVector<RemoteEntry>() << entry, localFolder());
    }
}

QVector<RemoteEntry> FileBrowserPane::selectedRemoteEntries() const
{
    QVector<RemoteEntry> entries;
    for (const QModelIndex &index : m_remoteView->selectionModel()->selectedRows()) {
        RemoteEntry e = m_remoteProxy->data(index, RemoteFileModel::EntryRole).value<RemoteEntry>();
        if (!RemoteFileModel::isParentEntry(e)) entries.append(e);
    }
    return entries;
}

void FileBrowserPane::showRemoteContextMenu(const QPoint &pos)
{
    QVector<RemoteEntry> entries = selectedRemoteEntries();
    QMenu menu(this);

    if (!entries.isEmpty()) {
        QString what = entries.size() == 1 ? QString("\"%1\"").arg(entries.first().name)
                                           : QString("%1 items").arg(entries.size());
        QString target = QDir::toNativeSeparators(localFolder());
        menu.addAction(style()->standardIcon(QStyle::SP_ArrowDown),
                       QString("Download %1 to %2").arg(what, target), this, [this, entries]() {
            downloadEntries(entries, localFolder());
        });
        menu.addAction("Download To...", this, [this, entries]() {
            QString dir = QFileDialog::getExistingDirectory(this, "Download To", localFolder());
            if (!dir.isEmpty()) downloadEntries(entries, dir);
        });
        if (entries.size() == 1 && entries.first().isDir) {
            RemoteEntry dir = entries.first();
            menu.addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), "Open", this,
                           [this, dir]() { navigateRemote(dir.path); });
        }
        if (entries.size() == 1 && !entries.first().isDir) {
            RemoteEntry file = entries.first();
            QAction *view = menu.addAction(style()->standardIcon(QStyle::SP_FileDialogContentsView),
                                           "View", this, [this, file]() { viewRemoteFile(file); });
            view->setShortcut(Qt::Key_F3);
            QAction *edit = menu.addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView),
                                           "Edit", this, [this, file]() { editRemoteFile(file); });
            edit->setShortcut(Qt::Key_F4);
            menu.addAction("Edit With...", this, [this, file]() {
                if (chooseEditor()) editRemoteFile(file);
            });
        }
        menu.addSeparator();
        menu.addAction("Copy Path", this, [entries]() {
            QStringList paths;
            for (const RemoteEntry &e : entries) paths << e.path;
            QApplication::clipboard()->setText(paths.join('\n'));
        });
    }
    if (m_connected && !m_remoteModel->path().isEmpty()) {
        QString remoteDir = m_remoteModel->path();
        menu.addAction(style()->standardIcon(QStyle::SP_ArrowUp), "Upload Files...", this,
                       [this, remoteDir]() {
            QStringList files = QFileDialog::getOpenFileNames(
                this, QString("Upload to %1").arg(remoteDir), localFolder());
            uploadPaths(files, remoteDir);
        });
        menu.addAction("Upload Folder...", this, [this, remoteDir]() {
            QString dir = QFileDialog::getExistingDirectory(
                this, QString("Upload to %1").arg(remoteDir), localFolder());
            if (!dir.isEmpty()) uploadPaths(QStringList() << dir, remoteDir);
        });
    }
    menu.addAction(style()->standardIcon(QStyle::SP_BrowserReload), "Refresh",
                   this, &FileBrowserPane::refreshRemote);

    menu.exec(m_remoteView->viewport()->mapToGlobal(pos));
}

// ---------- Local side ----------

void FileBrowserPane::showLocalContextMenu(const QPoint &pos)
{
    QStringList paths;
    for (const QModelIndex &index : m_localView->selectionModel()->selectedRows()) {
        paths << m_localModel->filePath(index);
    }

    QMenu menu(this);
    QString remoteDir = m_remoteModel->path();
    if (!paths.isEmpty() && m_connected && !remoteDir.isEmpty()) {
        QString what = paths.size() == 1 ? QString("\"%1\"").arg(QFileInfo(paths.first()).fileName())
                                         : QString("%1 items").arg(paths.size());
        menu.addAction(style()->standardIcon(QStyle::SP_ArrowUp),
                       QString("Upload %1 to %2").arg(what, remoteDir), this, [this, paths, remoteDir]() {
            uploadPaths(paths, remoteDir);
        });
    }
    if (paths.size() == 1 && QFileInfo(paths.first()).isDir()) {
        QString dir = paths.first();
        menu.addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), "Open", this,
                       [this, dir]() { setLocalFolder(dir); });
    }
    if (menu.isEmpty()) return;
    menu.exec(m_localView->viewport()->mapToGlobal(pos));
}

void FileBrowserPane::setLocalFolder(const QString &path)
{
    QFileInfo info(path);
    if (!info.isDir()) {
        m_localPathEdit->setText(QDir::toNativeSeparators(localFolder()));
        return;
    }
    QString absolute = info.absoluteFilePath();
    m_localView->setRootIndex(m_localModel->setRootPath(absolute));
    m_localPathEdit->setText(QDir::toNativeSeparators(absolute));
}

QString FileBrowserPane::localFolder() const
{
    return m_localModel->rootPath();
}

// ---------- Transfers ----------

SftpWorker *FileBrowserPane::transferWorker()
{
    if (!m_transferWorker) {
        m_transferThread = new QThread(this);
        m_transferWorker = new SftpWorker(m_connection);
        m_transferWorker->moveToThread(m_transferThread);
        connect(m_transferWorker, &SftpWorker::transferProgress, this, &FileBrowserPane::onTransferProgress);
        connect(m_transferWorker, &SftpWorker::transferFinished, this, &FileBrowserPane::onTransferFinished);
        watch(m_transferWorker, &m_transferWatchdog);
        m_transferThread->start();
    }
    return m_transferWorker;
}

void FileBrowserPane::downloadEntries(const QVector<RemoteEntry> &entries, const QString &localDir)
{
    if (entries.isEmpty() || localDir.isEmpty()) return;

    QDir dir(localDir);
    QStringList existing;
    for (const RemoteEntry &e : entries) {
        if (QFileInfo::exists(dir.filePath(e.name))) existing << e.name;
    }
    if (!existing.isEmpty()) {
        QString list = existing.mid(0, 8).join("\n");
        if (existing.size() > 8) list += QString("\n... and %1 more").arg(existing.size() - 8);
        int answer = QMessageBox::question(this, "Replace Files?",
            QString("These already exist in %1:\n\n%2\n\nReplace them?")
                .arg(QDir::toNativeSeparators(localDir), list),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }

    SftpWorker *worker = transferWorker();
    for (const RemoteEntry &e : entries) {
        QString localPath = dir.filePath(e.name);
        int jobId = addTransfer(e.name, e.isDir, false,
                                QString("%1\n→ %2").arg(e.path, QDir::toNativeSeparators(localPath)));
        QMetaObject::invokeMethod(worker, "download", Qt::QueuedConnection,
                                  Q_ARG(int, jobId), Q_ARG(RemoteEntry, e), Q_ARG(QString, localPath));
    }
}

void FileBrowserPane::uploadPaths(const QStringList &localPaths, const QString &remoteDir)
{
    if (localPaths.isEmpty() || remoteDir.isEmpty()) return;

    // Names already on the server can only be checked for the folder shown
    if (remoteDir == m_remoteModel->path()) {
        QSet<QString> remoteNames;
        for (int row = 0; row < m_remoteModel->rowCount(); ++row) {
            remoteNames.insert(m_remoteModel->entry(m_remoteModel->index(row, 0)).name);
        }
        QStringList existing;
        for (const QString &path : localPaths) {
            QString name = QFileInfo(path).fileName();
            if (remoteNames.contains(name)) existing << name;
        }
        if (!existing.isEmpty()) {
            QString list = existing.mid(0, 8).join("\n");
            if (existing.size() > 8) list += QString("\n... and %1 more").arg(existing.size() - 8);
            int answer = QMessageBox::question(this, "Replace Files?",
                QString("These already exist in %1 on the server:\n\n%2\n\n"
                        "Replace them? Folders are merged.").arg(remoteDir, list),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) return;
        }
    }

    SftpWorker *worker = transferWorker();
    for (const QString &path : localPaths) {
        QFileInfo info(path);
        if (!info.exists()) continue;
        QString remotePath = joinRemotePath(remoteDir, info.fileName());
        int jobId = addTransfer(info.fileName(), info.isDir(), true,
                                QString("%1\n→ %2").arg(QDir::toNativeSeparators(path), remotePath));
        m_transfers[jobId].refreshDir = remoteDir;
        QMetaObject::invokeMethod(worker, "upload", Qt::QueuedConnection,
                                  Q_ARG(int, jobId), Q_ARG(QString, path), Q_ARG(QString, remotePath));
    }
}

int FileBrowserPane::addTransfer(const QString &name, bool isDir, bool upload, const QString &tooltip)
{
    int jobId = m_nextJobId++;

    QTreeWidgetItem *item = new QTreeWidgetItem(m_transferList);
    item->setText(0, isDir ? name + "/" : name);
    item->setToolTip(0, tooltip);
    item->setIcon(0, style()->standardIcon(upload ? QStyle::SP_ArrowUp : QStyle::SP_ArrowDown));
    item->setText(2, "Queued");
    item->setData(0, Qt::UserRole, jobId);

    QProgressBar *bar = new QProgressBar(m_transferList);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setTextVisible(true);
    bar->setMaximumHeight(m_transferList->fontMetrics().height() + 4);
    m_transferList->setItemWidget(item, 1, bar);

    Transfer transfer = { item, bar, false, QString(), QString(), false, false };
    m_transfers.insert(jobId, transfer);
    m_transferList->scrollToBottom();
    return jobId;
}

void FileBrowserPane::onTransferProgress(int jobId, quint64 bytesDone, quint64 bytesTotal)
{
    auto it = m_transfers.find(jobId);
    if (it == m_transfers.end() || it->finished) return;

    if (bytesTotal == 0) {
        it->bar->setRange(0, 0);  // Busy: still walking a folder
    } else {
        it->bar->setRange(0, 1000);
        it->bar->setValue(int(bytesDone * 1000 / bytesTotal));
        it->bar->setFormat(QString("%p%"));
    }
    it->item->setText(2, QString("%1 / %2")
        .arg(QLocale().formattedDataSize(bytesDone), QLocale().formattedDataSize(bytesTotal)));
}

void FileBrowserPane::onTransferFinished(int jobId, bool success, const QString &error)
{
    auto it = m_transfers.find(jobId);
    if (it == m_transfers.end()) return;
    QString refreshDir = it->refreshDir;
    QString editPath = it->editPath;
    bool editUpload = it->editUpload;
    bool viewOnly = it->viewOnly;

    QString reason = success || m_transferWatchdog.stalledStep.isEmpty()
        ? error : stalledMessage(m_transferWatchdog.stalledStep);

    it->finished = true;
    it->bar->setRange(0, 100);
    if (success) {
        it->bar->setValue(100);
        it->item->setText(2, "Done");
        it->item->setToolTip(2, QString());
    } else {
        it->bar->setValue(0);
        it->bar->setFormat(error == "Cancelled" ? "Cancelled" : "Failed");
        it->item->setText(2, error == "Cancelled" ? "Cancelled" : "Failed");
        it->item->setToolTip(2, reason);
        it->item->setForeground(2, QBrush(QColor("#c0392b")));
    }

    if (viewOnly && success) {
        // Read-only, so the editor says so and a save can't be mistaken
        // for one that reaches the server
        QFile::setPermissions(editPath, QFileDevice::ReadOwner | QFileDevice::ReadUser);
        openInEditor(editPath);
    }

    if (!editPath.isEmpty() && m_edits.contains(editPath)) {
        EditedFile &edit = m_edits[editPath];
        if (!editUpload) {
            // The local copy is ready: watch it and hand it to the editor
            if (!success) {
                m_edits.remove(editPath);
            } else {
                edit.savedHash = fileHash(editPath);
                edit.ready = true;
                watchEditedFile(editPath);
                openInEditor(editPath);
            }
        } else {
            edit.uploadJob = 0;
            if (success) {
                setRemoteStatus(QString("Saved %1 to the server.").arg(edit.remotePath));
            } else {
                edit.savedHash.clear();  // Try again on the next save
                setRemoteStatus(QString("Could not save %1 to the server: %2\n"
                                        "Save it again in your editor to retry.")
                                    .arg(edit.remotePath, reason), true);
            }
            if (edit.changedAgain) {
                edit.changedAgain = false;
                checkEditedFiles();
            }
        }
    }

    // Show what an upload added, once the last one into this folder is done
    if (!refreshDir.isEmpty() && refreshDir == m_remoteModel->path()) {
        for (const Transfer &t : m_transfers) {
            if (!t.finished && t.refreshDir == refreshDir) return;
        }
        navigateRemote(refreshDir);
    }
}

// ---------- Edit ----------

// Like WinSCP: download to a private temporary folder, open it in an editor,
// and upload it again each time the editor saves it
void FileBrowserPane::editRemoteFile(const RemoteEntry &entry)
{
    for (auto it = m_edits.constBegin(); it != m_edits.constEnd(); ++it) {
        if (it->remotePath != entry.path) continue;
        if (it->ready) openInEditor(it.key());
        return;  // Already open, or still downloading
    }

    QString localPath = newLocalCopyPath(entry.name);
    if (localPath.isEmpty()) return;

    EditedFile edit;
    edit.remotePath = entry.path;
    m_edits.insert(localPath, edit);

    int jobId = addTransfer(entry.name, false, false, QString("%1\n→ editing in %2")
                                .arg(entry.path, QDir::toNativeSeparators(localPath)));
    m_transfers[jobId].editPath = localPath;
    QMetaObject::invokeMethod(transferWorker(), "download", Qt::QueuedConnection,
                              Q_ARG(int, jobId), Q_ARG(RemoteEntry, entry), Q_ARG(QString, localPath));
}

// Opens a fresh, read-only copy each time, so it shows the server's current
// version; nothing is uploaded
void FileBrowserPane::viewRemoteFile(const RemoteEntry &entry)
{
    QString localPath = newLocalCopyPath(entry.name);
    if (localPath.isEmpty()) return;

    int jobId = addTransfer(entry.name, false, false, QString("%1\n→ viewing in %2")
                                .arg(entry.path, QDir::toNativeSeparators(localPath)));
    m_transfers[jobId].editPath = localPath;
    m_transfers[jobId].viewOnly = true;
    QMetaObject::invokeMethod(transferWorker(), "download", Qt::QueuedConnection,
                              Q_ARG(int, jobId), Q_ARG(RemoteEntry, entry), Q_ARG(QString, localPath));
}

// A path in the pane's private temporary folder, deleted with the pane
QString FileBrowserPane::newLocalCopyPath(const QString &name)
{
    if (!m_editDir) {
        m_editDir = new QTemporaryDir(QDir::temp().filePath("qtterminal-edit-XXXXXX"));
        if (!m_editDir->isValid()) {
            QMessageBox::warning(this, "Edit", QString("Could not create a temporary folder: %1")
                                                   .arg(m_editDir->errorString()));
            delete m_editDir;
            m_editDir = nullptr;
            return QString();
        }
    }
    // A folder per file keeps its name, so the editor can pick the right
    // highlighting, even when two remote files share a name
    QString dir = m_editDir->filePath(QString::number(m_nextEditDir++));
    QDir().mkpath(dir);
    return QDir(dir).filePath(name);
}

bool FileBrowserPane::chooseEditor()
{
    bool ok = false;
    QString current = QSettings().value(kEditorSetting).toString();
    QString command = QInputDialog::getText(this, "Choose Editor",
        "Command that opens a file for editing, such as gedit, kate, code,\n"
        "or \"xterm -e vim\". Leave it empty to use the system's default application.",
        QLineEdit::Normal, current, &ok);
    if (!ok) return false;
    QSettings().setValue(kEditorSetting, command.trimmed());
    return true;
}

void FileBrowserPane::openInEditor(const QString &localPath)
{
    QString command = QSettings().value(kEditorSetting).toString().trimmed();
    if (command.isEmpty()) {
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(localPath))) return;
        QMessageBox::information(this, "Edit",
            "No application is set up to open this kind of file. Choose an editor to use.");
        if (chooseEditor() && !QSettings().value(kEditorSetting).toString().trimmed().isEmpty()) {
            openInEditor(localPath);
        }
        return;
    }

    QStringList args = QProcess::splitCommand(command);
    QString program = args.takeFirst();
    args << localPath;
    if (!QProcess::startDetached(program, args)) {
        QMessageBox::warning(this, "Edit", QString("Could not start \"%1\". Choose another "
                                                   "editor with Edit With...").arg(command));
    }
}

void FileBrowserPane::watchEditedFile(const QString &localPath)
{
    if (!m_editWatcher->files().contains(localPath)) m_editWatcher->addPath(localPath);
}

void FileBrowserPane::checkEditedFiles()
{
    bool missing = false;
    for (auto it = m_edits.begin(); it != m_edits.end(); ++it) {
        if (!it->ready) continue;
        // Saving by renaming a new file over the old one ends the watch
        if (!QFileInfo::exists(it.key())) {
            missing = true;
            continue;
        }
        watchEditedFile(it.key());
        QByteArray hash = fileHash(it.key());
        if (!hash.isNull() && hash != it->savedHash) saveEditedFile(it.key());
    }
    if (missing) m_editTimer->start();  // Look again once the save completes
}

void FileBrowserPane::saveEditedFile(const QString &localPath)
{
    EditedFile &edit = m_edits[localPath];
    if (edit.uploadJob) {
        edit.changedAgain = true;  // Send the newest version after this one
        return;
    }

    QString name = QFileInfo(localPath).fileName();
    edit.savedHash = fileHash(localPath);
    int jobId = addTransfer(name, false, true, QString("Saving %1\n→ %2")
                                .arg(QDir::toNativeSeparators(localPath), edit.remotePath));
    m_transfers[jobId].editPath = localPath;
    m_transfers[jobId].editUpload = true;
    m_transfers[jobId].refreshDir = RemoteFileModel::parentPath(edit.remotePath);
    edit.uploadJob = jobId;
    QMetaObject::invokeMethod(transferWorker(), "upload", Qt::QueuedConnection,
                              Q_ARG(int, jobId), Q_ARG(QString, localPath),
                              Q_ARG(QString, edit.remotePath));
}

bool FileBrowserPane::hasActiveTransfers() const
{
    for (const Transfer &t : m_transfers) {
        if (!t.finished) return true;
    }
    return false;
}

void FileBrowserPane::cancelTransfer(int jobId)
{
    auto it = m_transfers.find(jobId);
    if (it == m_transfers.end() || it->finished || !m_transferWorker) return;
    m_transferWorker->cancelTransfer(jobId);
    it->item->setText(2, "Cancelling...");
}

void FileBrowserPane::clearFinishedTransfers()
{
    for (auto it = m_transfers.begin(); it != m_transfers.end();) {
        if (it->finished) {
            delete it->item;
            it = m_transfers.erase(it);
        } else {
            ++it;
        }
    }
}

void FileBrowserPane::showTransferContextMenu(const QPoint &pos)
{
    QList<int> active;
    QStringList errors;
    for (QTreeWidgetItem *item : m_transferList->selectedItems()) {
        int jobId = item->data(0, Qt::UserRole).toInt();
        auto it = m_transfers.constFind(jobId);
        if (it == m_transfers.constEnd()) continue;
        if (!it->finished) active << jobId;
        else if (!item->toolTip(2).isEmpty()) errors << item->text(0) + ": " + item->toolTip(2);
    }

    QMenu menu(this);
    if (!active.isEmpty()) {
        menu.addAction(style()->standardIcon(QStyle::SP_DialogCancelButton), "Cancel",
                       this, [this, active]() {
            for (int jobId : active) cancelTransfer(jobId);
        });
    }
    if (!errors.isEmpty()) {
        menu.addAction("Copy Error", this, [errors]() {
            QApplication::clipboard()->setText(errors.join('\n'));
        });
    }
    menu.addAction("Clear Finished", this, &FileBrowserPane::clearFinishedTransfers);
    menu.exec(m_transferList->viewport()->mapToGlobal(pos));
}
