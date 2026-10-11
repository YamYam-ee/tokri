#include "standardpaths.h"
#include "standardnames.h"

#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

static QString ensure(const QString& p)
{
    QDir dir(p);
    if (!dir.exists())
        dir.mkpath(".");
    return dir.absolutePath();
}

namespace {
QMutex basketMutex;
QString basketDir;
}

StandardPaths::StandardPaths() {}

void StandardPaths::setBasketDir(const QString &dir)
{
    QMutexLocker lock(&basketMutex);
    basketDir = dir;
}

QString StandardPaths::getPath(Paths path)
{
    switch (path)
    {
    case TokriDir:
        return ensure(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
            + "/" + StandardNames::get(StandardNames::Directory));
    case BasketDir: {
        QString dir;
        {
            QMutexLocker lock(&basketMutex);
            dir = basketDir;
        }
        return dir.isEmpty() ? getPath(TokriDir) : ensure(dir);
    }
    case LinuxApplicationDir:
        return ensure(QStandardPaths::writableLocation(
            QStandardPaths::ApplicationsLocation
            ));

    case LinuxIconsDir:
        return ensure(QStandardPaths::writableLocation(
                   QStandardPaths::GenericDataLocation
                          ) + "/icons/hicolor/256x256/apps");
    default:
        return QString();
    }
}
