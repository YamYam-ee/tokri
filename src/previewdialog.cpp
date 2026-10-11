#include "previewdialog.h"

#include "loghelpers.h"
#include "zoomview.h"

#include <QDir>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QMimeDatabase>
#include <QPlainTextEdit>
#include <QEnterEvent>
#include <QPainter>
#include <functional>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
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

// Big, very transparent previous/next arrow floating over the left or right
// edge of the preview; clearly visible while hovered. It is its own
// frameless translucent window so it can float above native document
// previews too (child widgets cannot be transparent over those).
class NavButton : public QWidget
{
public:
    NavButton(bool forward, QWidget *owner)
        : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint
                             | Qt::WindowDoesNotAcceptFocus
                             | Qt::NoDropShadowWindowHint)
        , mForward(forward)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(64, 128);
        setToolTip(forward ? QStringLiteral("Nästa fil (nedåt i listan)")
                           : QStringLiteral("Föregående fil (uppåt i listan)"));
    }

    std::function<void()> onClick;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, mHover ? (mPressed ? 170 : 130) : 35));
        p.drawRoundedRect(r, 14, 14);

        // Chevron
        const qreal cx = r.center().x(), cy = r.center().y();
        const qreal dx = 10, dy = 20;
        QPainterPath chevron;
        if (mForward) {
            chevron.moveTo(cx - dx / 2, cy - dy);
            chevron.lineTo(cx + dx, cy);
            chevron.lineTo(cx - dx / 2, cy + dy);
        } else {
            chevron.moveTo(cx + dx / 2, cy - dy);
            chevron.lineTo(cx - dx, cy);
            chevron.lineTo(cx + dx / 2, cy + dy);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, mHover ? 255 : 80), 5,
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(chevron);
    }

    void enterEvent(QEnterEvent *) override { mHover = true; update(); }
    void leaveEvent(QEvent *) override { mHover = mPressed = false; update(); }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            mPressed = true;
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        const bool fire = mPressed && rect().contains(e->position().toPoint());
        mPressed = false;
        update();
        if (fire && onClick)
            onClick();
    }

private:
    bool mForward;
    bool mHover = false;
    bool mPressed = false;
};

PreviewDialog::PreviewDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    mPrev = new NavButton(false, this);
    mNext = new NavButton(true, this);
    mPrev->onClick = [this] { emit navigateRequested(-1); };
    mNext->onClick = [this] { emit navigateRequested(+1); };
}

void PreviewDialog::setNavigation(int position, int count)
{
    mPositionText = QStringLiteral("%1 / %2").arg(position).arg(count);
    setWindowTitle(mFileName + QStringLiteral("   —   ") + mPositionText);
    mHasPrev = position > 1;
    mHasNext = position < count;
    placeNavButtons();
}

void PreviewDialog::placeNavButtons()
{
    if (!mPrev || !mNext)
        return;
    const bool visible = isVisible() && !isMinimized();
    const QRect area(mapToGlobal(QPoint(0, 0)), size());
    const int margin = 12;
    const int y = area.center().y() - mPrev->height() / 2;

    mPrev->move(area.left() + margin, y);
    mNext->move(area.right() - margin - mNext->width() + 1, y);
    mPrev->setVisible(visible && mHasPrev);
    mNext->setVisible(visible && mHasNext);
    if (visible) {
        mPrev->raise();
        mNext->raise();
    }
}

void PreviewDialog::moveEvent(QMoveEvent *e)
{
    QDialog::moveEvent(e);
    placeNavButtons();
}

void PreviewDialog::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    placeNavButtons();
}

void PreviewDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    placeNavButtons();
}

void PreviewDialog::hideEvent(QHideEvent *e)
{
    mPrev->hide();
    mNext->hide();
    QDialog::hideEvent(e);
}

void PreviewDialog::changeEvent(QEvent *e)
{
    QDialog::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange)
        placeNavButtons();
}

void PreviewDialog::closeOnClick(QWidget *w)
{
    w->installEventFilter(this);
}

bool PreviewDialog::eventFilter(QObject *watched, QEvent *e)
{
    // Text and message views: a click (not a drag/selection) closes.
    if (e->type() == QEvent::MouseButtonPress) {
        const auto *me = static_cast<QMouseEvent *>(e);
        if (me->button() == Qt::LeftButton)
            mPressPos = me->globalPosition().toPoint();
    } else if (e->type() == QEvent::MouseButtonRelease) {
        const auto *me = static_cast<QMouseEvent *>(e);
        if (me->button() == Qt::LeftButton
            && (me->globalPosition().toPoint() - mPressPos).manhattanLength() < 5) {
            auto *edit = qobject_cast<QPlainTextEdit *>(watched->parent());
            if (!edit || !edit->textCursor().hasSelection())
                QMetaObject::invokeMethod(this, "close", Qt::QueuedConnection);
        }
    }
    return QDialog::eventFilter(watched, e);
}

void PreviewDialog::setContent(QWidget *w)
{
    if (mContent)
        mContent->deleteLater();
    mContent = w;
    static_cast<QVBoxLayout *>(layout())->insertWidget(0, w, 1);
}

QWidget *PreviewDialog::makeImageView(const QImage &image)
{
    auto *view = new ZoomView(this);
    view->setImage(image);
    connect(view, &ZoomView::clicked, this, &QWidget::close);
    return view;
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
    closeOnClick(edit->viewport());
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
    closeOnClick(w);
    return w;
}

void PreviewDialog::sizeForAspect(qreal aspect, int extraWidth)
{
    // Window shaped like the content (aspect = width / height), as tall as
    // comfortably fits, so a whole page is visible at its proper width.
    static bool loaded = false;
    if (!loaded) {
        sUserCenter = QSettings().value("preview/center").toPoint();
        loaded = true;
    }

    // Switching file in an open preview keeps it where it is; otherwise
    // centred where the user last left a preview, or over Tokri.
    const QPoint center = isVisible() ? geometry().center()
        : sUserCenter.isNull()
        ? (parentWidget() ? parentWidget()->frameGeometry().center()
                          : screen()->availableGeometry().center())
        : sUserCenter;
    QScreen *scr = QGuiApplication::screenAt(center);
    if (!scr)
        scr = parentWidget() ? parentWidget()->screen() : screen();
    const QRect avail = scr->availableGeometry();

    const int barH = 0;
    int pageH = int(avail.height() * 0.88) - barH;
    int w = int(pageH * aspect) + extraWidth;
    const int maxW = int(avail.width() * 0.95);
    if (w > maxW) {
        w = maxW;
        pageH = int((w - extraWidth) / aspect);
    }
    const int h = pageH + barH;
    QRect r(0, 0, qMax(w, 240), qMax(h, 200));
    r.moveCenter(center);
    // Keep it on screen (leaving room for the title bar).
    const int titleBar = 32;
    r.moveLeft(qBound(avail.left(), r.left(), avail.right() + 1 - r.width()));
    r.moveTop(qBound(avail.top() + titleBar, r.top(),
                     qMax(avail.top() + titleBar, avail.bottom() + 1 - r.height())));
    setGeometry(r);
    mPlacedCenter = r.center();
}

void PreviewDialog::showFile(const QString &path)
{
    Logger &log = Logger::instance();
    log.push("preview");

    const QFileInfo fi(path);
    mFileName = fi.fileName();
    setWindowTitle(mFileName);

    // Only one preview at a time.
    if (sCurrent && sCurrent != this)
        sCurrent->close();
    sCurrent = this;

    // Switching files: drop the previous view's click hook, keep position.
    removeClickToClose();
    if (isVisible())
        rememberIfMoved();

    constexpr qreal A4 = 210.0 / 297.0;
    constexpr int DocScrollbar = 24;

    QWidget *view = nullptr;
    bool useShell = false;
    QImage shellThumb;
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(fi);

    if (fi.isDir()) {
        view = makeMessage(path, "Mapp: " + fi.fileName());
        sizeForAspect(1.0, 0);
    } else if (fi.suffix().compare("pdf", Qt::CaseInsensitive) == 0) {
        // Own renderer: zoom with the wheel at the cursor, drag to pan.
        auto *zoom = new ZoomView(this);
        if (zoom->setPdf(path)) {
            connect(zoom, &ZoomView::clicked, this, &QWidget::close);
            view = zoom;
            sizeForAspect(zoom->firstPageAspect(), 0);
        } else {
            delete zoom;
        }
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

#ifdef Q_OS_WIN
    if (useShell) {
        auto *host = new ShellPreviewHost(this);
        setContent(host);
        host->show();
        if (host->load(path)) {
            installClickToClose(host);
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
    view->setFocus();
    log.pop();
}

#ifdef Q_OS_WIN
namespace {
HHOOK sMouseHook = nullptr;
HWND sHostHwnd = nullptr;

// A left click on a native document preview closes the preview window.
// A low-level hook is used because those previews are native windows,
// often owned by another process, that Qt never sees events for. Clicks on
// the dialog's own buttons are not affected.
LRESULT CALLBACK previewMouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && wParam == WM_LBUTTONDOWN && sHostHwnd
        && PreviewDialog::current()) {
        const auto *info = reinterpret_cast<const MSLLHOOKSTRUCT *>(lParam);
        const HWND hit = WindowFromPoint(info->pt);
        if (hit && (hit == sHostHwnd || IsChild(sHostHwnd, hit))) {
            QMetaObject::invokeMethod(PreviewDialog::current(), "close",
                                      Qt::QueuedConnection);
            return 1; // swallow the click
        }
    }
    return CallNextHookEx(sMouseHook, code, wParam, lParam);
}
}
#endif

void PreviewDialog::installClickToClose(QWidget *nativeHost)
{
#ifdef Q_OS_WIN
    sHostHwnd = reinterpret_cast<HWND>(nativeHost->winId());
    if (!sMouseHook)
        sMouseHook = SetWindowsHookExW(WH_MOUSE_LL, previewMouseProc,
                                       GetModuleHandleW(nullptr), 0);
#else
    Q_UNUSED(nativeHost);
#endif
}

void PreviewDialog::removeClickToClose()
{
#ifdef Q_OS_WIN
    sHostHwnd = nullptr;
    if (sMouseHook) {
        UnhookWindowsHookEx(sMouseHook);
        sMouseHook = nullptr;
    }
#endif
}

void PreviewDialog::rememberIfMoved()
{
    const QPoint c = geometry().center();
    if ((c - mPlacedCenter).manhattanLength() > 8) {
        sUserCenter = c;
        QSettings().setValue("preview/center", c);
    }
}

void PreviewDialog::closeEvent(QCloseEvent *e)
{
    // Remember the spot if the user moved the window.
    if (isVisible())
        rememberIfMoved();

    if (sCurrent == this) {
        sCurrent = nullptr;
        removeClickToClose();
    }
    QDialog::closeEvent(e);
}

PreviewDialog *PreviewDialog::current()
{
    return sCurrent;
}

QPointer<PreviewDialog> PreviewDialog::sCurrent;
QPoint PreviewDialog::sUserCenter;

void PreviewDialog::keyPressEvent(QKeyEvent *e)
{
    // Space closes, like Quick Look / Explorer's preview.
    if (e->key() == Qt::Key_Space || e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QDialog::keyPressEvent(e);
}
