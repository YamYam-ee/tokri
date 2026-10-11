#include "basketorder.h"

#include "filepathprovider.h"
#include "loghelpers.h"
#include "standardpaths.h"

#include <QDateTime>
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
    setDelivered(fileNames, true);
}

void BasketOrder::setDelivered(const QStringList &fileNames, bool delivered)
{
    bool changed = false;
    for (const QString &name : fileNames) {
        Entry &e = mEntries[name];
        if (e.delivered != delivered) {
            e.delivered = delivered;
            changed = true;
        }
    }
    if (!changed)
        return;

    save();
    emit deliveredChanged();
}

void BasketOrder::setManualOrder(const QStringList &fileNames)
{
    for (int i = 0; i < fileNames.size(); ++i)
        mEntries[fileNames[i]].batch = i;

    save();
    emit orderChanged();
}

void BasketOrder::rename(const QString &oldPath, const QString &newPath)
{
    if (!mEntries.contains(oldPath))
        return;

    mEntries.insert(newPath, mEntries.take(oldPath));
    save();
    emit orderChanged();
}

QStringList BasketOrder::moveFiles(const QStringList &paths, const QString &dir)
{
    const qint64 key = QDateTime::currentMSecsSinceEpoch();
    QStringList moved;
    for (const QString &src : paths) {
        const QString dst = FilePathProvider::uniquePath(
            QDir(dir).filePath(QFileInfo(src).fileName()));
        if (!QDir().rename(src, dst)) {
            Logger::instance().log() << "moveFiles failed src=" << src << "dst=" << dst;
            continue;
        }
        Entry e = mEntries.take(src);
        e.batch = key;
        mEntries.insert(dst, e);
        moved << dst;
    }
    Logger::instance().log() << "moveFiles to=" << dir << "moved=" << moved;
    save();
    emit orderChanged();
    return moved;
}

void BasketOrder::load()
{
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly))
        return;

    const QDir tokriDir(StandardPaths::getPath(StandardPaths::TokriDir));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        // Older files stored bare names of items in the Tokri directory.
        const QString path = QFileInfo(it.key()).isAbsolute()
                                 ? it.key()
                                 : tokriDir.filePath(it.key());
        // Drop entries for items that were deleted while we were not running.
        if (!QFileInfo::exists(path))
            continue;
        const QJsonObject o = it.value().toObject();
        mEntries.insert(path,
                        Entry{qint64(o.value("batch").toDouble(-1)),
                              o.value("delivered").toBool()});
    }
}

void BasketOrder::save() const
{
    QJsonObject root;
    for (auto it = mEntries.cbegin(); it != mEntries.cend(); ++it) {
        if (!QFileInfo::exists(it.key()))
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
