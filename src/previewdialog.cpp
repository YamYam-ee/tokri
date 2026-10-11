#include "previewdialog.h"

#include "loghelpers.h"

#include <QDir>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeDatabase>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <thumbcache.h>
#endif

namespace {

constexpr qint64 MaxTextBytes = 2 * 1024 * 1024;

// Shows an image scaled down to fit, never up-scaled.
class FitImageLabel : public QLabel
{
public:
    explicit FitImageLabel(const QImage &image, QWidget *parent = nullptr)
        : QLabel(parent), mPixmap(QPixmap::fromImage(image))
    {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(1, 1);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QLabel::resizeEvent(e);
        const qreal dpr = devicePixelRatioF();
        const QSize avail = size() * dpr;
        QPixmap pm = mPixmap;
        if (pm.width() > avail.width() || pm.height() > avail.height())
            pm = pm.scaled(avail, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pm.setDevicePixelRatio(dpr);
        setPixmap(pm);
    }

private:
    QPixmap mPixmap;
};

}

#ifdef Q_OS_WIN

// Hosts a shell IPreviewHandler inside a native child widget.
class ShellPreviewHost : public QWidget
{
public:
    explicit ShellPreviewHost(QWidget *parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_DontCreateNativeAncestors);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    ~ShellPreviewHost() override { unload(); }

    bool load(const QString &path)
    {
        const std::wstring ext = ("." + QFileInfo(path).suffix()).toStdWString();
        wchar_t clsidText[64] = {};
        DWORD len = 64;
        if (FAILED(AssocQueryStringW(ASSOCF_INIT_DEFAULTTOSTAR, ASSOCSTR_SHELLEXTENSION,
                                     ext.c_str(),
                                     L"{8895b1c6-b41f-4c1c-a562-0d564250836f}",
                                     clsidText, &len)))
            return false;

        CLSID clsid;
        if (FAILED(CLSIDFromString(clsidText, &clsid)))
            return false;

        if (FAILED(CoCreateInstance(clsid, nullptr,
                                    CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                                    IID_PPV_ARGS(&mHandler))))
            return false;

        const std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
        bool initialized = false;

        IInitializeWithFile *withFile = nullptr;
        if (SUCCEEDED(mHandler->QueryInterface(IID_PPV_ARGS(&withFile)))) {
            initialized = SUCCEEDED(withFile->Initialize(wpath.c_str(), STGM_READ));
            withFile->Release();
        }
        if (!initialized) {
            IInitializeWithStream *withStream = nullptr;
            if (SUCCEEDED(mHandler->QueryInterface(IID_PPV_ARGS(&withStream)))) {
                IStream *stream = nullptr;
                if (SUCCEEDED(SHCreateStreamOnFileEx(wpath.c_str(),
                                                     STGM_READ | STGM_SHARE_DENY_NONE,
                                                     0, FALSE, nullptr, &stream))) {
                    initialized = SUCCEEDED(withStream->Initialize(stream, STGM_READ));
                    stream->Release();
                }
                withStream->Release();
            }
        }
        if (!initialized) {
            IInitializeWithItem *withItem = nullptr;
            if (SUCCEEDED(mHandler->QueryInterface(IID_PPV_ARGS(&withItem)))) {
                IShellItem *item = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(wpath.c_str(), nullptr,
                                                          IID_PPV_ARGS(&item)))) {
                    initialized = SUCCEEDED(withItem->Initialize(item, STGM_READ));
                    item->Release();
                }
                withItem->Release();
            }
        }

        if (!initialized) {
            unload();
            return false;
        }

        const RECT rc = nativeRect();
        if (FAILED(mHandler->SetWindow(reinterpret_cast<HWND>(winId()), &rc))
            || FAILED(mHandler->DoPreview())) {
            unload();
            return false;
        }
        return true;
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        if (mHandler) {
            const RECT rc = nativeRect();
            mHandler->SetRect(&rc);
        }
    }

private:
    RECT nativeRect() const
    {
        const QSize s = size() * devicePixelRatioF();
        return RECT{0, 0, s.width(), s.height()};
    }

    void unload()
    {
        if (!mHandler)
            return;
        mHandler->Unload();
        mHandler->Release();
        mHandler = nullptr;
    }

    IPreviewHandler *mHandler = nullptr;
};

namespace {

QImage shellThumbnail(const QString &path, int size)
{
    IShellItemImageFactory *factory = nullptr;
    const std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
    if (FAILED(SHCreateItemFromParsingName(wpath.c_str(), nullptr,
                                           IID_PPV_ARGS(&factory))))
        return {};

    QImage image;
    HBITMAP bmp = nullptr;
    if (SUCCEEDED(factory->GetImage(SIZE{size, size}, SIIGBF_THUMBNAILONLY, &bmp))) {
        image = QImage::fromHBITMAP(bmp);
        DeleteObject(bmp);
    }
    factory->Release();
    return image;
}

}

#endif

PreviewDialog::PreviewDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
}

void PreviewDialog::setContent(QWidget *w)
{
    if (mContent)
        mContent->deleteLater();
    mContent = w;
    layout()->addWidget(w);
}

QWidget *PreviewDialog::makeImageView(const QImage &image)
{
    return new FitImageLabel(image, this);
}

QWidget *PreviewDialog::makeTextView(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return nullptr;
    const QByteArray bytes = f.read(MaxTextBytes);

    auto *edit = new QPlainTextEdit(this);
    edit->setReadOnly(true);
    edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    edit->setPlainText(QString::fromUtf8(bytes));
    if (f.size() > MaxTextBytes)
        edit->appendPlainText("\n…");
    return edit;
}

QWidget *PreviewDialog::makeMessage(const QString &path, const QString &text)
{
    auto *w = new QWidget(this);
    auto *l = new QVBoxLayout(w);
    l->addStretch();
    auto *icon = new QLabel(w);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(QFileIconProvider().icon(QFileInfo(path)).pixmap(96, 96));
    l->addWidget(icon);
    auto *label = new QLabel(text, w);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    l->addWidget(label);
    l->addStretch();
    return w;
}

void PreviewDialog::sizeForAspect(qreal aspect, int extraWidth)
{
    // Window shaped like the content (aspect = width / height), as tall as
    // comfortably fits, so a whole page is visible at its proper width.
    QScreen *scr = parentWidget() ? parentWidget()->screen() : screen();
    const QRect avail = scr->availableGeometry();

    int h = int(avail.height() * 0.88);
    int w = int(h * aspect) + extraWidth;
    const int maxW = int(avail.width() * 0.95);
    if (w > maxW) {
        w = maxW;
        h = int((w - extraWidth) / aspect);
    }
    resize(qMax(w, 240), qMax(h, 200));
    move(avail.center() - rect().center());
}

void PreviewDialog::showFile(const QString &path)
{
    Logger &log = Logger::instance();
    log.push("preview");

    const QFileInfo fi(path);
    setWindowTitle(fi.fileName());

    // Only one preview at a time.
    if (sCurrent && sCurrent != this)
        sCurrent->close();
    sCurrent = this;

    constexpr qreal A4 = 210.0 / 297.0;
    constexpr int DocScrollbar = 24;

    QWidget *view = nullptr;
    bool useShell = false;
    QImage shellThumb;
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(fi);

    if (fi.isDir()) {
        view = makeMessage(path, "Mapp: " + fi.fileName());
        sizeForAspect(1.0, 0);
    } else if (mime.name().startsWith("image/")) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (!image.isNull()) {
            view = makeImageView(image);
            sizeForAspect(qreal(image.width()) / qMax(1, image.height()), 0);
        }
    } else if (mime.inherits("text/plain")) {
        view = makeTextView(path);
        sizeForAspect(A4, DocScrollbar);
    }

#ifdef Q_OS_WIN
    if (!view) {
        // Documents: shape the window like the first page so the preview
        // shows the full page width.
        shellThumb = shellThumbnail(path, 1024);
        const qreal aspect = shellThumb.isNull()
            ? A4
            : qreal(shellThumb.width()) / qMax(1, shellThumb.height());
        sizeForAspect(aspect, DocScrollbar);
        useShell = true;
    }
#endif

    show();
    raise();
    activateWindow();
    installClickToClose();

#ifdef Q_OS_WIN
    if (useShell) {
        auto *host = new ShellPreviewHost(this);
        setContent(host);
        host->show();
        if (host->load(path)) {
            log.log() << "shell preview path=" << path;
            log.pop();
            return;
        }
        mContent = nullptr;
        host->deleteLater();
        if (!shellThumb.isNull())
            view = makeImageView(shellThumb);
    }
#endif

    if (!view) {
        view = makeMessage(path, "Ingen förhandsvisning för " + fi.fileName());
        sizeForAspect(1.0, 0);
    }

    log.log() << "path=" << path << "mime=" << mime.name();
    setContent(view);
    log.pop();
}

#ifdef Q_OS_WIN
namespace {
HHOOK sMouseHook = nullptr;

// A left click anywhere inside the preview's client area closes it. A
// low-level hook is used because document previews are native windows,
// often owned by another process, that Qt never sees events for.
LRESULT CALLBACK previewMouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && wParam == WM_LBUTTONDOWN && PreviewDialog::current()) {
        const auto *info = reinterpret_cast<const MSLLHOOKSTRUCT *>(lParam);
        const HWND hwnd = reinterpret_cast<HWND>(PreviewDialog::current()->winId());
        RECT rc;
        GetClientRect(hwnd, &rc);
        POINT tl{rc.left, rc.top}, br{rc.right, rc.bottom};
        ClientToScreen(hwnd, &tl);
        ClientToScreen(hwnd, &br);
        const POINT pt = info->pt;
        if (pt.x >= tl.x && pt.x < br.x && pt.y >= tl.y && pt.y < br.y
            && WindowFromPoint(pt) != nullptr
            && GetAncestor(WindowFromPoint(pt), GA_ROOT) == hwnd) {
            QMetaObject::invokeMethod(PreviewDialog::current(), "close",
                                      Qt::QueuedConnection);
            return 1; // swallow the click
        }
    }
    return CallNextHookEx(sMouseHook, code, wParam, lParam);
}
}
#endif

void PreviewDialog::installClickToClose()
{
#ifdef Q_OS_WIN
    if (!sMouseHook)
        sMouseHook = SetWindowsHookExW(WH_MOUSE_LL, previewMouseProc,
                                       GetModuleHandleW(nullptr), 0);
#endif
}

void PreviewDialog::closeEvent(QCloseEvent *e)
{
    if (sCurrent == this) {
        sCurrent = nullptr;
#ifdef Q_OS_WIN
        if (sMouseHook) {
            UnhookWindowsHookEx(sMouseHook);
            sMouseHook = nullptr;
        }
#endif
    }
    QDialog::closeEvent(e);
}

PreviewDialog *PreviewDialog::current()
{
    return sCurrent;
}

QPointer<PreviewDialog> PreviewDialog::sCurrent;

void PreviewDialog::keyPressEvent(QKeyEvent *e)
{
    // Space closes, like Quick Look / Explorer's preview.
    if (e->key() == Qt::Key_Space || e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QDialog::keyPressEvent(e);
}
