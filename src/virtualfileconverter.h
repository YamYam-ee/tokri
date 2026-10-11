#ifndef VIRTUALFILECONVERTER_H
#define VIRTUALFILECONVERTER_H

#include "virtualfilemime.h"

#include <qt_windows.h>
#include <objidl.h>

#include <QList>
#include <QWindowsMimeConverter>

// Outlook (and some other apps) drag "virtual" files: a FileGroupDescriptorW
// with names plus one FileContents entry per file, but no file paths. This
// converter exposes them as VirtualFileMime::MimeType; reading that format
// extracts the files into a temporary directory and returns their paths.
class VirtualFileMimeConverter : public QWindowsMimeConverter
{
public:
    bool canConvertFromMime(const FORMATETC &, const QMimeData *) const override
    { return false; }
    bool convertFromMime(const FORMATETC &, const QMimeData *,
                         STGMEDIUM *) const override
    { return false; }
    QList<FORMATETC> formatsForMime(const QString &,
                                    const QMimeData *) const override
    { return {}; }

    bool canConvertToMime(const QString &mimeType,
                          IDataObject *pDataObj) const override;
    QVariant convertToMime(const QString &mimeType, IDataObject *pDataObj,
                           QMetaType preferredType) const override;
    QString mimeForFormat(const FORMATETC &formatetc) const override;
};

#endif // VIRTUALFILECONVERTER_H
