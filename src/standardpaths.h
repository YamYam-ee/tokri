#ifndef STANDARDPATHS_H
#define STANDARDPATHS_H

#include <QString>

class StandardPaths
{
public:
    enum Paths {
        TokriDir,
        BasketDir,      // directory of the current tab (drops land here)
        LinuxApplicationDir,
        LinuxIconsDir
    };

    StandardPaths();

    static QString getPath(Paths path);
    static void setBasketDir(const QString &dir);
};

#endif // STANDARDPATHS_H
