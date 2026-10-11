#ifndef SORTFILTERPROXY_H
#define SORTFILTERPROXY_H

#include <QCollator>
#include <QSortFilterProxyModel>

class FSSortFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    // Extra role: true once the item has been dragged out of the basket.
    static constexpr int DeliveredRole = Qt::UserRole + 100;

    explicit FSSortFilterProxy(QObject *parent = nullptr);
    QVariant data(const QModelIndex &index, int role) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const;
    bool filterAcceptsRow(int row, const QModelIndex &parent) const;

    bool canDropMimeData(const QMimeData *data, Qt::DropAction action,
                         int row, int column,
                         const QModelIndex &parent) const override;

    void setSearch(const QString &string);

private:
    double score(const QString &candidate) const;
    QString mSearch;
    QCollator mCollator;
};

#endif // SORTFILTERPROXY_H
