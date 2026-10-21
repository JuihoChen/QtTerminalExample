// Table model for one remote folder listing
#include "remotefilemodel.h"

#include <QFont>

#include <QDataStream>
#include <QLocale>
#include <QMimeData>
#include <QSet>

RemoteFileModel::RemoteFileModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

QString RemoteFileModel::parentPath(const QString &path)
{
    QString parent = path;
    while (parent.length() > 1 && parent.endsWith('/')) parent.chop(1);
    int slash = parent.lastIndexOf('/');
    return slash > 0 ? parent.left(slash) : QStringLiteral("/");
}

void RemoteFileModel::setEntries(const QString &path, const QVector<RemoteEntry> &entries)
{
    beginResetModel();
    m_path = path;
    m_entries.clear();
    if (path != "/") {
        RemoteEntry up;
        up.name = "..";
        up.path = parentPath(path);
        up.isDir = true;
        m_entries.append(up);
    }
    m_entries += entries;
    endResetModel();
}

RemoteEntry RemoteFileModel::entry(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_entries.size()) return RemoteEntry();
    return m_entries.at(index.row());
}

int RemoteFileModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

int RemoteFileModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant RemoteFileModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size()) return QVariant();
    const RemoteEntry &e = m_entries.at(index.row());
    bool up = isParentEntry(e);

    if (role == EntryRole) return QVariant::fromValue(e);

    switch (index.column()) {
    case NameColumn:
        if (role == Qt::DisplayRole || role == SortRole) return e.name;
        if (role == Qt::DecorationRole) {
            return m_icons.icon(e.isDir ? QFileIconProvider::Folder : QFileIconProvider::File);
        }
        if (role == Qt::ToolTipRole && !up) {
            return e.isLink ? QString("%1 (link)").arg(e.path) : e.path;
        }
        if (role == Qt::FontRole && e.isLink) {
            QFont font;
            font.setItalic(true);
            return font;
        }
        break;
    case SizeColumn:
        if (role == SortRole) return e.size;
        if (role == Qt::DisplayRole && !e.isDir) return QLocale().formattedDataSize(e.size);
        if (role == Qt::TextAlignmentRole) return int(Qt::AlignRight | Qt::AlignVCenter);
        break;
    case ModifiedColumn:
        if (role == SortRole) return e.modified;
        if (role == Qt::DisplayRole && !up) return e.modified.toString("yyyy-MM-dd HH:mm");
        break;
    case PermissionsColumn:
        if ((role == Qt::DisplayRole || role == SortRole) && !up) {
            return SftpWorker::formatPermissions(e.permissions, e.isDir, e.isLink);
        }
        break;
    case OwnerColumn:
        if ((role == Qt::DisplayRole || role == SortRole) && !up) {
            return QString("%1:%2").arg(e.owner, e.group);
        }
        break;
    }
    return QVariant();
}

QVariant RemoteFileModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
    switch (section) {
    case NameColumn: return "Name";
    case SizeColumn: return "Size";
    case ModifiedColumn: return "Modified";
    case PermissionsColumn: return "Permissions";
    case OwnerColumn: return "Owner";
    }
    return QVariant();
}

Qt::ItemFlags RemoteFileModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.isValid() && !isParentEntry(entry(index))) f |= Qt::ItemIsDragEnabled;
    return f;
}

QStringList RemoteFileModel::mimeTypes() const
{
    return QStringList() << REMOTE_ENTRIES_MIME;
}

Qt::DropActions RemoteFileModel::supportedDragActions() const
{
    return Qt::CopyAction;
}

QMimeData *RemoteFileModel::mimeData(const QModelIndexList &indexes) const
{
    QSet<int> rows;
    QVector<RemoteEntry> entries;
    for (const QModelIndex &index : indexes) {
        if (rows.contains(index.row())) continue;
        rows.insert(index.row());
        RemoteEntry e = entry(index);
        if (!isParentEntry(e)) entries.append(e);
    }
    return encodeEntries(entries, reinterpret_cast<quintptr>(this));
}

QMimeData *RemoteFileModel::encodeEntries(const QVector<RemoteEntry> &entries, quintptr source)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out << quint64(source) << quint32(entries.size());
    for (const RemoteEntry &e : entries) {
        out << e.name << e.path << e.size << e.modified << e.permissions << e.isDir << e.isLink;
    }

    QMimeData *data = new QMimeData;
    data->setData(REMOTE_ENTRIES_MIME, bytes);
    return data;
}

QVector<RemoteEntry> RemoteFileModel::decodeEntries(const QMimeData *data, quintptr *source)
{
    QVector<RemoteEntry> entries;
    QByteArray bytes = data->data(REMOTE_ENTRIES_MIME);
    QDataStream in(&bytes, QIODevice::ReadOnly);
    quint64 from = 0;
    quint32 count = 0;
    in >> from >> count;
    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
        RemoteEntry e;
        in >> e.name >> e.path >> e.size >> e.modified >> e.permissions >> e.isDir >> e.isLink;
        entries.append(e);
    }
    if (source) *source = quintptr(from);
    return entries;
}

RemoteSortProxy::RemoteSortProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    m_collator.setNumericMode(true);
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    setSortRole(RemoteFileModel::SortRole);
}

void RemoteSortProxy::setShowHidden(bool show)
{
    if (show == m_showHidden) return;
    m_showHidden = show;
    invalidateFilter();
}

bool RemoteSortProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (m_showHidden) return true;
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    RemoteEntry entry = index.data(RemoteFileModel::EntryRole).value<RemoteEntry>();
    return RemoteFileModel::isParentEntry(entry) || !entry.name.startsWith('.');
}

bool RemoteSortProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    RemoteEntry a = left.data(RemoteFileModel::EntryRole).value<RemoteEntry>();
    RemoteEntry b = right.data(RemoteFileModel::EntryRole).value<RemoteEntry>();
    bool ascending = sortOrder() == Qt::AscendingOrder;

    // Qt flips the result for descending order; flip these back so they stay on top
    if (RemoteFileModel::isParentEntry(a)) return ascending;
    if (RemoteFileModel::isParentEntry(b)) return !ascending;
    if (a.isDir != b.isDir) return ascending ? a.isDir : b.isDir;

    QVariant l = left.data(sortRole());
    QVariant r = right.data(sortRole());
    switch (left.column()) {
    case RemoteFileModel::SizeColumn:
        if (l.toULongLong() != r.toULongLong()) return l.toULongLong() < r.toULongLong();
        break;
    case RemoteFileModel::ModifiedColumn:
        if (l.toDateTime() != r.toDateTime()) return l.toDateTime() < r.toDateTime();
        break;
    default: {
        int c = m_collator.compare(l.toString(), r.toString());
        if (c != 0) return c < 0;
    }
    }
    return m_collator.compare(a.name, b.name) < 0;
}
