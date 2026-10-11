#include "basketorder.h"

#include "loghelpers.h"
#include "standardpaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

QString storePath()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/basket.json";
}

}

BasketOrder &BasketOrder::instance()
{
    static BasketOrder order;
    return order;
}

BasketOrder::BasketOrder(QObject *parent)
    : QObject{parent}
{
    load();
}

qint64 BasketOrder::batchKey(const QString &fileName) const
{
    return mEntries.value(fileName).batch;
}

bool BasketOrder::isDelivered(const QString &fileName) const
{
    return mEntries.value(fileName).delivered;
}

void BasketOrder::addBatch(qint64 key, const QStringList &fileNames)
{
    if (fileNames.isEmpty())
        return;

    Logger::instance().log() << "addBatch key=" << key
                             << "names=" << fileNames;
    for (const QString &name : fileNames)
        mEntries[name] = Entry{key, false};

    save();
    emit orderChanged();
}

void BasketOrder::markDelivered(const QStringList &fileNames)
{
    bool changed = false;
    for (const QString &name : fileNames) {
        Entry &e = mEntries[name];
        if (!e.delivered) {
            e.delivered = true;
            changed = true;
        }
    }
    if (!changed)
        return;

    save();
    emit deliveredChanged();
}

void BasketOrder::rename(const QString &oldName, const QString &newName)
{
    if (!mEntries.contains(oldName))
        return;

    mEntries.insert(newName, mEntries.take(oldName));
    save();
    emit orderChanged();
}

void BasketOrder::load()
{
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly))
        return;

    const QDir tokriDir(StandardPaths::getPath(StandardPaths::TokriDir));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        // Drop entries for items that were deleted while we were not running.
        if (!QFileInfo::exists(tokriDir.filePath(it.key())))
            continue;
        const QJsonObject o = it.value().toObject();
        mEntries.insert(it.key(),
                        Entry{qint64(o.value("batch").toDouble(-1)),
                              o.value("delivered").toBool()});
    }
}

void BasketOrder::save() const
{
    const QDir tokriDir(StandardPaths::getPath(StandardPaths::TokriDir));
    QJsonObject root;
    for (auto it = mEntries.cbegin(); it != mEntries.cend(); ++it) {
        if (!QFileInfo::exists(tokriDir.filePath(it.key())))
            continue;
        root.insert(it.key(), QJsonObject{{"batch", double(it->batch)},
                                          {"delivered", it->delivered}});
    }

    QSaveFile f(storePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.commit();
    }
}
