#ifndef BASKETORDER_H
#define BASKETORDER_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

// Remembers, per item in the Tokri directory, which drop ("batch") it arrived
// in and whether it has been dragged out ("delivered"). Persisted as JSON so
// the order and check marks survive restarts.
class BasketOrder : public QObject
{
    Q_OBJECT
public:
    static BasketOrder &instance();

    // Sort key: the batch timestamp (ms) for known items, otherwise -1.
    qint64 batchKey(const QString &fileName) const;
    bool isDelivered(const QString &fileName) const;

public slots:
    void addBatch(qint64 key, const QStringList &fileNames);
    void markDelivered(const QStringList &fileNames);
    void rename(const QString &oldName, const QString &newName);

signals:
    void orderChanged();
    void deliveredChanged();

private:
    explicit BasketOrder(QObject *parent = nullptr);
    void load();
    void save() const;

    struct Entry {
        qint64 batch = -1;
        bool delivered = false;
    };

    QHash<QString, Entry> mEntries;
};

#endif // BASKETORDER_H
