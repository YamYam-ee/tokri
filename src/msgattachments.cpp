#include "msgattachments.h"

#include "filepathprovider.h"
#include "loghelpers.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <objbase.h>
#include <objidl.h>

namespace {

// MAPI property tags as they appear in .msg stream names / property streams.
constexpr quint16 PidAttachDataBinary   = 0x3701;
constexpr quint16 PidAttachFilename     = 0x3704;
constexpr quint16 PidAttachMethod       = 0x3705;
constexpr quint16 PidAttachLongFilename = 0x3707;
constexpr quint16 PidAttachMimeTag      = 0x370E;
constexpr quint16 PidAttachContentId    = 0x3712;
constexpr quint16 PidAttachFlags        = 0x3714;
constexpr quint16 PidDisplayName        = 0x3001;
constexpr quint16 PidAttachmentHidden   = 0x7FFE;
constexpr quint16 PidHtmlBody           = 0x1013;

constexpr quint32 AttachByValue = 1;
constexpr quint32 AttachEmbeddedMsg = 5;
constexpr quint32 AttMhtmlRef = 0x4;

// RAII for COM interfaces.
template <typename T>
struct Com {
    T *p = nullptr;
    ~Com() { if (p) p->Release(); }
    T **out() { return &p; }
    T *operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::wstring wname(const QString &s) { return s.toStdWString(); }

QString streamName(quint16 id, quint16 type)
{
    return QStringLiteral("__substg1.0_%1%2")
        .arg(id, 4, 16, QLatin1Char('0'))
        .arg(type, 4, 16, QLatin1Char('0'))
        .toUpper()
        .replace("__SUBSTG1.0_", "__substg1.0_");
}

QByteArray readStream(IStorage *stg, const QString &name)
{
    Com<IStream> s;
    if (FAILED(stg->OpenStream(wname(name).c_str(), nullptr,
                               STGM_READ | STGM_SHARE_EXCLUSIVE, 0, s.out())))
        return {};
    STATSTG st{};
    if (FAILED(s->Stat(&st, STATFLAG_NONAME)))
        return {};
    QByteArray buf(qsizetype(st.cbSize.QuadPart), Qt::Uninitialized);
    ULONG read = 0;
    if (!buf.isEmpty() && FAILED(s->Read(buf.data(), ULONG(buf.size()), &read)))
        return {};
    buf.truncate(read);
    return buf;
}

QString readString(IStorage *stg, quint16 id)
{
    // Unicode (001F) first, then 8-bit (001E).
    QByteArray b = readStream(stg, streamName(id, 0x001F));
    if (!b.isEmpty())
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(b.constData()),
                                  b.size() / 2).remove(QChar(0)).trimmed();
    b = readStream(stg, streamName(id, 0x001E));
    return QString::fromLocal8Bit(b).remove(QChar(0)).trimmed();
}

// Fixed-size properties of an attachment (8-byte header, 16-byte entries).
QHash<quint16, quint32> readFixedProps(IStorage *stg)
{
    QHash<quint16, quint32> props;
    const QByteArray b = readStream(stg, "__properties_version1.0");
    for (qsizetype off = 8; off + 16 <= b.size(); off += 16) {
        const auto *p = reinterpret_cast<const uchar *>(b.constData() + off);
        const quint16 type = quint16(p[0] | (p[1] << 8));
        const quint16 id = quint16(p[2] | (p[3] << 8));
        if (type == 0x0003 || type == 0x000B) // PT_LONG, PT_BOOLEAN
            props.insert(id, quint32(p[8] | (p[9] << 8) | (p[10] << 16) | (quint32(p[11]) << 24)));
    }
    return props;
}

bool isImage(const QString &name, const QString &mime)
{
    static const QSet<QString> exts{"png", "jpg", "jpeg", "gif", "bmp", "tif", "tiff",
                                    "emf", "wmf", "emz", "wmz", "svg", "webp", "ico"};
    return mime.startsWith("image/", Qt::CaseInsensitive)
           || exts.contains(QFileInfo(name).suffix().toLower());
}

// Saves an embedded message storage as a stand-alone .msg file.
bool saveEmbeddedMsg(IStorage *attach, const QString &path)
{
    Com<IStorage> src;
    if (FAILED(attach->OpenStorage(wname(QStringLiteral("__substg1.0_3701000D")).c_str(), nullptr,
                                   STGM_READ | STGM_SHARE_EXCLUSIVE, nullptr, 0, src.out())))
        return false;

    Com<IStorage> dst;
    if (FAILED(StgCreateDocfile(wname(QDir::toNativeSeparators(path)).c_str(),
                                STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, dst.out())))
        return false;
    if (FAILED(src->CopyTo(0, nullptr, nullptr, dst.p)))
        return false;

    // An embedded message's property header is 24 bytes; a top-level .msg
    // needs 32. Pad it so Outlook opens the file.
    {
        Com<IStream> props;
        if (SUCCEEDED(dst->OpenStream(L"__properties_version1.0", nullptr,
                                      STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, props.out()))) {
            STATSTG st{};
            props->Stat(&st, STATFLAG_NONAME);
            QByteArray b(qsizetype(st.cbSize.QuadPart), Qt::Uninitialized);
            ULONG read = 0;
            props->Read(b.data(), ULONG(b.size()), &read);
            if (read >= 24) {
                b.insert(24, QByteArray(8, '\0'));
                LARGE_INTEGER zero{};
                props->Seek(zero, STREAM_SEEK_SET, nullptr);
                ULARGE_INTEGER size;
                size.QuadPart = quint64(b.size());
                props->SetSize(size);
                ULONG written = 0;
                props->Write(b.constData(), ULONG(b.size()), &written);
            }
        }
    }
    return SUCCEEDED(dst->Commit(STGC_DEFAULT));
}

QString uniqueTarget(const QString &dir, QString name)
{
    name = FilePathProvider::sanitizeName(name);
    return FilePathProvider::uniquePath(QDir(dir).filePath(name));
}

}

MsgAttachments::Result MsgAttachments::extract(const QString &msgPath, const QString &targetDir)
{
    Logger &log = Logger::instance();
    log.push("msgAttachments");
    Result result;

    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    {
        Com<IStorage> msg;
        if (FAILED(StgOpenStorage(wname(QDir::toNativeSeparators(msgPath)).c_str(), nullptr,
                                  STGM_READ | STGM_SHARE_DENY_WRITE, nullptr, 0, msg.out()))) {
            log.log() << "not a compound file" << msgPath;
        } else {
            result.ok = true;

            // Images the message text actually shows (cid: references).
            const QByteArray html = readStream(msg.p, streamName(PidHtmlBody, 0x0102));
            const QString htmlText = QString::fromUtf8(html);

            Com<IEnumSTATSTG> it;
            QStringList attachStorages;
            if (SUCCEEDED(msg->EnumElements(0, nullptr, 0, it.out()))) {
                STATSTG st;
                while (it->Next(1, &st, nullptr) == S_OK) {
                    const QString name = QString::fromWCharArray(st.pwcsName);
                    CoTaskMemFree(st.pwcsName);
                    if (st.type == STGTY_STORAGE && name.startsWith("__attach_version1.0_#"))
                        attachStorages << name;
                }
            }
            attachStorages.sort();

            for (const QString &storageName : std::as_const(attachStorages)) {
                Com<IStorage> att;
                if (FAILED(msg->OpenStorage(wname(storageName).c_str(), nullptr,
                                            STGM_READ | STGM_SHARE_EXCLUSIVE, nullptr, 0, att.out())))
                    continue;

                const auto props = readFixedProps(att.p);
                const quint32 method = props.value(PidAttachMethod, AttachByValue);
                const bool hidden = props.value(PidAttachmentHidden, 0) != 0;
                const quint32 flags = props.value(PidAttachFlags, 0);

                QString name = readString(att.p, PidAttachLongFilename);
                if (name.isEmpty())
                    name = readString(att.p, PidAttachFilename);
                if (name.isEmpty())
                    name = readString(att.p, PidDisplayName);
                const QString mime = readString(att.p, PidAttachMimeTag);
                const QString contentId = readString(att.p, PidAttachContentId);

                // Inline pictures: signature logos and images in the text.
                bool inlineImage = false;
                if (isImage(name, mime)) {
                    if (!htmlText.isEmpty() && !contentId.isEmpty())
                        inlineImage = htmlText.contains("cid:" + contentId, Qt::CaseInsensitive);
                    else
                        inlineImage = !contentId.isEmpty() || (flags & AttMhtmlRef);
                }

                if (hidden || inlineImage) {
                    ++result.skipped;
                    log.log() << "skip" << name << "hidden=" << hidden << "inline=" << inlineImage;
                    continue;
                }

                if (method == AttachByValue) {
                    const QByteArray data = readStream(att.p, streamName(PidAttachDataBinary, 0x0102));
                    if (name.isEmpty())
                        name = "bilaga";
                    const QString path = uniqueTarget(targetDir, name);
                    QFile f(path);
                    if (f.open(QIODevice::WriteOnly) && f.write(data) == data.size()) {
                        result.written << path;
                        log.log() << "saved" << path << "bytes=" << data.size();
                    }
                } else if (method == AttachEmbeddedMsg) {
                    if (name.isEmpty())
                        name = "Bifogat mejl";
                    if (!name.endsWith(".msg", Qt::CaseInsensitive))
                        name += ".msg";
                    const QString path = uniqueTarget(targetDir, name);
                    if (saveEmbeddedMsg(att.p, path)) {
                        result.written << path;
                        log.log() << "saved embedded msg" << path;
                    } else {
                        QFile::remove(path);
                    }
                } else {
                    log.log() << "unsupported attach method" << method << name;
                }
            }
        }
    }

    if (SUCCEEDED(coInit))
        CoUninitialize();

    log.log() << "written=" << result.written.size() << "skipped=" << result.skipped;
    log.pop();
    return result;
}

#else

MsgAttachments::Result MsgAttachments::extract(const QString &, const QString &)
{
    return {};
}

#endif
