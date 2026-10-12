#ifndef REMOTEFILEMODEL_H
#define REMOTEFILEMODEL_H

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QCollator>
#include <QFileIconProvider>

#include "sftpworker.h"

// Mime type for remote entries dragged out of the remote file list
#define REMOTE_ENTRIES_MIME "application/x-qtterminal-remote-entries"

// One remote folder's contents, as shown by the remote file list
class RemoteFileModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { NameColumn, SizeColumn, ModifiedColumn, PermissionsColumn, OwnerColumn, ColumnCount };
    enum Role { SortRole = Qt::UserRole, EntryRole };

    explicit RemoteFileModel(QObject *parent = nullptr);

    void setEntries(const QString &path, const QVector<RemoteEntry> &entries);
    QString path() const { return m_path; }
    RemoteEntry entry(const QModelIndex &index) const;

    static QString parentPath(const QString &path);
    static bool isParentEntry(const RemoteEntry &entry) { return entry.name == ".."; }

    // Encodes entries for drag and drop; decodeEntries() reverses it
    static QMimeData *encodeEntries(const QVector<RemoteEntry> &entries, quintptr source);
    static QVector<RemoteEntry> decodeEntries(const QMimeData *data, quintptr *source);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDragActions() const override;

private:
    QString m_path;
    QVector<RemoteEntry> m_entries;
    QFileIconProvider m_icons;
};

// Keeps ".." on top and folders above files, whichever way a column sorts
class RemoteSortProxy : public QSortFilterProxyModel
{
public:
    explicit RemoteSortProxy(QObject *parent = nullptr);

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QCollator m_collator;
};

#endif // REMOTEFILEMODEL_H
