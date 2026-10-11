#include "sortfilterproxy.h"
#include "basketorder.h"
// #include <rapidfuzz/fuzz.hpp>

#include <QFileSystemModel>
#include <QMimeData>

FSSortFilterProxy::FSSortFilterProxy(QObject *parent)
    : QSortFilterProxyModel{parent}
{
    mCollator.setNumericMode(true);
    mCollator.setCaseSensitivity(Qt::CaseInsensitive);
}

QVariant FSSortFilterProxy::data(const QModelIndex &index, int role) const
{
    if (role == DeliveredRole) {
        const QFileInfo fi =
            index.data(QFileSystemModel::FileInfoRole).value<QFileInfo>();
        return BasketOrder::instance().isDelivered(fi.absoluteFilePath());
    }
    return QSortFilterProxyModel::data(index, role);
}

bool FSSortFilterProxy::canDropMimeData(const QMimeData *data,
                                        Qt::DropAction action,
                                        int row, int column,
                                        const QModelIndex &parent) const
{
    QAbstractItemModel *src = sourceModel();
    if (!src)
        return false;
    return src->canDropMimeData(data, action, row, column,
                                mapToSource(parent));
}

bool FSSortFilterProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const auto leftFileInfo = left.data(QFileSystemModel::FileInfoRole).value<QFileInfo>();
    const auto rightFileInfo = right.data(QFileSystemModel::FileInfoRole).value<QFileInfo>();

    // Items dropped together share a batch key; batches stay in the order
    // they were dropped, and each batch is sorted by name ("2" before "10").
    // Items without a recorded batch fall back to their creation time.
    const auto key = [](const QFileInfo &fi) {
        const qint64 batch = BasketOrder::instance().batchKey(fi.absoluteFilePath());
        return batch >= 0 ? batch : fi.birthTime().toMSecsSinceEpoch();
    };
    const qint64 l = key(leftFileInfo);
    const qint64 r = key(rightFileInfo);
    if (l != r)
        return l < r;
    return mCollator.compare(leftFileInfo.fileName(),
                             rightFileInfo.fileName()) < 0;

    // if (mSearch.isEmpty()){
    // }

    // return score(leftFileInfo.fileName()) < score(rightFileInfo.fileName());
}

bool FSSortFilterProxy::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    if (mSearch.isEmpty())
        return true;

    auto *src = sourceModel();
    if (!src)
        return false;

    const QModelIndex idx = src->index(row, 0, parent);
    if (!idx.isValid())
        return false;

    const QFileInfo info =
        idx.data(QFileSystemModel::FileInfoRole).value<QFileInfo>();

    if (!info.exists())
        return false;

    if (info.isDir())
        return true;

    const QString name = info.fileName();
    return true;
    // return score(name) > 30;
}

// void FSSortFilterProxy::setSearch(const QString &string)
// {
//     if (mSearch != string){
//         beginFilterChange();
//         mSearch = string;
//         endFilterChange();
//     }
// }

// double FSSortFilterProxy::score(const QString &candidate) const
// {
//     const auto result = rapidfuzz::fuzz::ratio(
//         mSearch.toStdString(),
//         candidate.toLower().toStdString());
//     return result;
// }
