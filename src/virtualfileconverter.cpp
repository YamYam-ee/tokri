#include "virtualfileconverter.h"

#include "loghelpers.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QVariant>

#include <windows.h>
#include <objidl.h>
#include <shlobj.h>

namespace {

CLIPFORMAT descriptorFormat()
{
    static const auto cf = CLIPFORMAT(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW));
    return cf;
}

CLIPFORMAT contentsFormat()
{
    static const auto cf = CLIPFORMAT(RegisterClipboardFormatW(CFSTR_FILECONTENTS));
    return cf;
}

FORMATETC descriptorEtc()
{
    return FORMATETC{descriptorFormat(), nullptr, DVASPECT_CONTENT, -1,
                     TYMED_HGLOBAL};
}

// Keeps a descriptor's relative path ("dir\\file") but makes each part a
// valid file name.
QString sanitizeRelative(const QString &name)
{
    QStringList parts = QString(name).replace('/', '\\').split('\\', Qt::SkipEmptyParts);
    for (QString &p : parts) {
        for (QChar &c : p)
            if (QStringLiteral("<>:\"|?*").contains(c) || c.unicode() < 32)
                c = '_';
        p = p.trimmed();
        if (p.isEmpty() || p == "." || p == "..")
            p = "_";
    }
    return parts.join('/');
}

bool writeHGlobal(HGLOBAL h, qint64 size, const QString &path)
{
    const void *p = GlobalLock(h);
    if (!p)
        return false;
    if (size < 0)
        size = qint64(GlobalSize(h));
    QFile f(path);
    const bool ok = f.open(QIODevice::WriteOnly)
                    && f.write(static_cast<const char *>(p), size) == size;
    GlobalUnlock(h);
    return ok;
}

bool writeStream(IStream *stream, const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;

    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);

    char buf[64 * 1024];
    for (;;) {
        ULONG read = 0;
        const HRESULT hr = stream->Read(buf, sizeof buf, &read);
        if (read > 0 && f.write(buf, read) != qint64(read))
            return false;
        if (FAILED(hr))
            return false;
        if (hr == S_FALSE || read == 0)
            return true;
    }
}

bool writeStorage(IStorage *src, const QString &path)
{
    IStorage *dst = nullptr;
    const std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
    if (FAILED(StgCreateDocfile(wpath.c_str(),
                                STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
                                0, &dst)))
        return false;
    const bool ok = SUCCEEDED(src->CopyTo(0, nullptr, nullptr, dst))
                    && SUCCEEDED(dst->Commit(STGC_DEFAULT));
    dst->Release();
    return ok;
}

}

bool VirtualFileMimeConverter::canConvertToMime(const QString &mimeType,
                                                IDataObject *pDataObj) const
{
    if (mimeType != VirtualFileMime::MimeType || !pDataObj)
        return false;
    FORMATETC fe = descriptorEtc();
    return pDataObj->QueryGetData(&fe) == S_OK;
}

QString VirtualFileMimeConverter::mimeForFormat(const FORMATETC &formatetc) const
{
    return formatetc.cfFormat == descriptorFormat() ? VirtualFileMime::MimeType
                                                    : QString();
}

QVariant VirtualFileMimeConverter::convertToMime(const QString &mimeType,
                                                 IDataObject *pDataObj,
                                                 QMetaType preferredType) const
{
    Q_UNUSED(preferredType);
    if (!canConvertToMime(mimeType, pDataObj))
        return {};

    Logger &log = Logger::instance();
    log.push("virtualFiles");

    FORMATETC fe = descriptorEtc();
    STGMEDIUM medium{};
    if (FAILED(pDataObj->GetData(&fe, &medium))) {
        log.log() << "no descriptor";
        log.pop();
        return {};
    }

    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + "/tokri-virtual-" + QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(root);

    QStringList topLevel;
    const auto *group = static_cast<const FILEGROUPDESCRIPTORW *>(
        GlobalLock(medium.hGlobal));
    const UINT count = group ? group->cItems : 0;

    for (UINT i = 0; i < count; ++i) {
        const FILEDESCRIPTORW &d = group->fgd[i];
        const QString rel = sanitizeRelative(QString::fromWCharArray(d.cFileName));
        if (rel.isEmpty())
            continue;
        const QString path = root + "/" + rel;
        const QString top = rel.section('/', 0, 0);

        const bool isDir = (d.dwFlags & FD_ATTRIBUTES)
                           && (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        bool ok = false;
        if (isDir) {
            ok = QDir().mkpath(path);
        } else {
            QDir().mkpath(QFileInfo(path).path());

            FORMATETC ce{contentsFormat(), nullptr, DVASPECT_CONTENT, LONG(i),
                         TYMED_HGLOBAL | TYMED_ISTREAM | TYMED_ISTORAGE};
            STGMEDIUM content{};
            if (SUCCEEDED(pDataObj->GetData(&ce, &content))) {
                const qint64 size = (d.dwFlags & FD_FILESIZE)
                    ? (qint64(d.nFileSizeHigh) << 32) | d.nFileSizeLow
                    : -1;
                switch (content.tymed) {
                case TYMED_HGLOBAL: ok = writeHGlobal(content.hGlobal, size, path); break;
                case TYMED_ISTREAM: ok = writeStream(content.pstm, path); break;
                case TYMED_ISTORAGE: ok = writeStorage(content.pstg, path); break;
                default: break;
                }
                ReleaseStgMedium(&content);
            }
        }

        log.log() << "item" << i << "name=" << rel << "dir=" << isDir << "ok=" << ok;
        if (ok && !topLevel.contains(top))
            topLevel << top;
    }

    if (group)
        GlobalUnlock(medium.hGlobal);
    ReleaseStgMedium(&medium);

    QStringList paths;
    for (const QString &top : std::as_const(topLevel))
        paths << root + "/" + top;

    log.log() << "extracted=" << paths;
    log.pop();
    return paths.join('\n').toUtf8();
}

