// WinSCP-style file browser pane for one SSH connection
#include "filebrowserpane.h"
#include "remotefilemodel.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QSplitter>
#include <QStyle>
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

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
    setDragDropMode(QAbstractItemView::DropOnly);
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

// ============ FileBrowserPane ============

FileBrowserPane::FileBrowserPane(const SSHConnection &connection, QWidget *parent)
    : QWidget(parent), m_connection(connection),
      m_browseThread(new QThread(this)), m_browseWorker(new SftpWorker(connection)),
      m_transferThread(nullptr), m_transferWorker(nullptr),
      m_connected(false), m_nextJobId(1)
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
    m_browseThread->start();

    setRemoteStatus(QString("Connecting to %1...").arg(m_connection.host));
    QMetaObject::invokeMethod(m_browseWorker, "connectToServer", Qt::QueuedConnection);
}

FileBrowserPane::~FileBrowserPane()
{
    // Stop downloads at the next chunk so the threads can finish quickly
    m_browseWorker->cancelAll();
    if (m_transferWorker) m_transferWorker->cancelAll();

    m_browseThread->quit();
    m_browseThread->wait();
    delete m_browseWorker;

    if (m_transferThread) {
        m_transferThread->quit();
        m_transferThread->wait();
        delete m_transferWorker;
    }
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

    m_remoteView = new QTreeView(section);
    m_remoteView->setModel(m_remoteProxy);
    m_remoteView->setRootIsDecorated(false);
    m_remoteView->setUniformRowHeights(true);
    m_remoteView->setAlternatingRowColors(true);
    m_remoteView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_remoteView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_remoteView->setDragEnabled(true);
    m_remoteView->setDragDropMode(QAbstractItemView::DragOnly);
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

    return section;
}

QWidget *FileBrowserPane::createLocalSection()
{
    QWidget *section = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    layout->addWidget(makeSectionLabel("Local (drop remote files here to download)", section));

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
    setRemoteStatus(error + "\n\nPress Refresh to try again.", true);
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
    setRemoteStatus(QString("Could not open %1: %2").arg(path, error), true);
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
        menu.addSeparator();
        menu.addAction("Copy Path", this, [entries]() {
            QStringList paths;
            for (const RemoteEntry &e : entries) paths << e.path;
            QApplication::clipboard()->setText(paths.join('\n'));
        });
    }
    menu.addAction(style()->standardIcon(QStyle::SP_BrowserReload), "Refresh",
                   this, &FileBrowserPane::refreshRemote);

    menu.exec(m_remoteView->viewport()->mapToGlobal(pos));
}

// ---------- Local side ----------

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
        connect(m_transferWorker, &SftpWorker::downloadProgress, this, &FileBrowserPane::onDownloadProgress);
        connect(m_transferWorker, &SftpWorker::downloadFinished, this, &FileBrowserPane::onDownloadFinished);
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
        int jobId = m_nextJobId++;
        QString localPath = dir.filePath(e.name);

        QTreeWidgetItem *item = new QTreeWidgetItem(m_transferList);
        item->setText(0, e.isDir ? e.name + "/" : e.name);
        item->setToolTip(0, QString("%1\n→ %2").arg(e.path, QDir::toNativeSeparators(localPath)));
        item->setIcon(0, style()->standardIcon(e.isDir ? QStyle::SP_DirIcon : QStyle::SP_FileIcon));
        item->setText(2, "Queued");
        item->setData(0, Qt::UserRole, jobId);

        QProgressBar *bar = new QProgressBar(m_transferList);
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setTextVisible(true);
        bar->setMaximumHeight(m_transferList->fontMetrics().height() + 4);
        m_transferList->setItemWidget(item, 1, bar);

        Transfer transfer = { item, bar, false };
        m_transfers.insert(jobId, transfer);

        QMetaObject::invokeMethod(worker, "download", Qt::QueuedConnection,
                                  Q_ARG(int, jobId), Q_ARG(RemoteEntry, e), Q_ARG(QString, localPath));
    }
    m_transferList->scrollToBottom();
}

void FileBrowserPane::onDownloadProgress(int jobId, quint64 bytesDone, quint64 bytesTotal)
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

void FileBrowserPane::onDownloadFinished(int jobId, bool success, const QString &error)
{
    auto it = m_transfers.find(jobId);
    if (it == m_transfers.end()) return;

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
        it->item->setToolTip(2, error);
        it->item->setForeground(2, QBrush(QColor("#c0392b")));
    }
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
    m_transferWorker->cancelDownload(jobId);
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
