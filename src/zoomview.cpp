#include "zoomview.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QWheelEvent>
#include <QtMath>

namespace {

constexpr qreal PageGap = 12;     // points between pages
constexpr qreal Margin = 10;      // points around the content
constexpr qreal MaxZoomOverFit = 25;
constexpr int DragThreshold = 5;  // pixels before a press becomes a pan

}

ZoomView::ZoomView(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
    setCursor(Qt::OpenHandCursor);
    setAttribute(Qt::WA_OpaquePaintEvent);

    mRenderTimer.setSingleShot(true);
    mRenderTimer.setInterval(90);
    connect(&mRenderTimer, &QTimer::timeout, this, &ZoomView::renderVisible);
}

ZoomView::~ZoomView() = default;

void ZoomView::setImage(const QImage &image)
{
    mImage = image;
    mPageSizes = {QSizeF(image.size())};
    layoutPages();
}

bool ZoomView::setPdf(const QString &path)
{
    mPdf = new QPdfDocument(this);
    if (mPdf->load(path) != QPdfDocument::Error::None || mPdf->pageCount() < 1) {
        delete mPdf;
        mPdf = nullptr;
        return false;
    }
    mPageSizes.clear();
    for (int i = 0; i < mPdf->pageCount(); ++i)
        mPageSizes << mPdf->pagePointSize(i);
    layoutPages();
    return true;
}

qreal ZoomView::firstPageAspect() const
{
    if (mPageSizes.isEmpty() || mPageSizes.first().height() <= 0)
        return 210.0 / 297.0;
    return mPageSizes.first().width() / mPageSizes.first().height();
}

void ZoomView::layoutPages()
{
    mPageRects.clear();
    qreal maxW = 0;
    for (const QSizeF &s : std::as_const(mPageSizes))
        maxW = qMax(maxW, s.width());

    qreal y = Margin;
    for (const QSizeF &s : std::as_const(mPageSizes)) {
        mPageRects << QRectF(Margin + (maxW - s.width()) / 2, y, s.width(), s.height());
        y += s.height() + PageGap;
    }
    mContentSize = QSizeF(maxW + 2 * Margin, y - PageGap + Margin);
    mTiles.clear();
    mFitted = false;
    update();
}

qreal ZoomView::fitScale() const
{
    if (mContentSize.isEmpty() || width() <= 0)
        return 1.0;
    // Whole page width visible; for a single image also its whole height.
    qreal s = width() / mContentSize.width();
    if (!mPdf)
        s = qMin(s, height() / mContentSize.height());
    return s;
}

void ZoomView::fitWidth()
{
    mScale = fitScale();
    mOffset = QPointF(0, 0);
    clampOffset();
    mFitted = true;
    scheduleRender();
    update();
}

void ZoomView::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    if (!mFitted || qFuzzyCompare(mScale, fitScale()) || mScale < fitScale())
        fitWidth();
    else {
        clampOffset();
        scheduleRender();
    }
}

void ZoomView::clampOffset()
{
    const qreal viewW = width() / mScale;
    const qreal viewH = height() / mScale;

    // Content narrower/shorter than the view is centred.
    if (mContentSize.width() <= viewW)
        mOffset.setX((mContentSize.width() - viewW) / 2);
    else
        mOffset.setX(qBound(0.0, mOffset.x(), mContentSize.width() - viewW));

    if (mContentSize.height() <= viewH)
        mOffset.setY((mContentSize.height() - viewH) / 2);
    else
        mOffset.setY(qBound(0.0, mOffset.y(), mContentSize.height() - viewH));
}

void ZoomView::zoomAt(const QPointF &viewPos, qreal factor)
{
    const qreal minScale = fitScale() * (mPdf ? 0.25 : 1.0);
    const qreal maxScale = fitScale() * MaxZoomOverFit;
    const qreal newScale = qBound(minScale, mScale * factor, maxScale);
    if (qFuzzyCompare(newScale, mScale))
        return;

    // Keep the content point under the cursor in place.
    const QPointF anchor = mOffset + viewPos / mScale;
    mScale = newScale;
    mOffset = anchor - viewPos / mScale;
    clampOffset();
    scheduleRender();
    update();
}

QRectF ZoomView::pageRectOnScreen(int page) const
{
    const QRectF r = mPageRects[page];
    return QRectF((r.topLeft() - mOffset) * mScale, r.size() * mScale);
}

void ZoomView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x2b, 0x2b, 0x2b));
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRectF view = rect();
    bool stale = false;

    for (int i = 0; i < mPageRects.size(); ++i) {
        const QRectF target = pageRectOnScreen(i);
        if (!target.intersects(view))
            continue;

        if (!mPdf) {
            p.drawImage(target, mImage);
            continue;
        }

        p.fillRect(target, Qt::white);
        const auto it = mTiles.constFind(i);
        if (it != mTiles.constEnd() && !it->image.isNull()) {
            const QRectF src = it->source;
            const QRectF tileTarget(
                (mPageRects[i].topLeft() + src.topLeft() - mOffset) * mScale,
                src.size() * mScale);
            p.drawImage(tileTarget, it->image);

            const QRectF visiblePage =
                QRectF(mOffset - mPageRects[i].topLeft(), view.size() / mScale)
                    .intersected(QRectF(QPointF(0, 0), mPageRects[i].size()));
            if (!qFuzzyCompare(it->scale, mScale) || !src.contains(visiblePage))
                stale = true;
        } else {
            stale = true;
        }
    }

    if (stale)
        scheduleRender();
}

void ZoomView::scheduleRender()
{
    if (mPdf && !mRenderTimer.isActive())
        mRenderTimer.start();
}

void ZoomView::renderVisible()
{
    if (!mPdf)
        return;

    const qreal dpr = devicePixelRatioF();
    const QRectF viewContent(mOffset, QSizeF(width(), height()) / mScale);
    // Render a bit beyond the view so small pans stay sharp.
    const QRectF wanted = viewContent.adjusted(-viewContent.width() * 0.25,
                                               -viewContent.height() * 0.25,
                                               viewContent.width() * 0.25,
                                               viewContent.height() * 0.25);

    QHash<int, Tile> tiles;
    for (int i = 0; i < mPageRects.size(); ++i) {
        const QRectF page = mPageRects[i];
        const QRectF part = wanted.intersected(page);
        if (part.isEmpty())
            continue;

        // In page points, relative to the page's top-left.
        const QRectF source = part.translated(-page.topLeft());
        const qreal px = mScale * dpr; // device pixels per point
        const QSize full = (page.size() * px).toSize();
        const QRect clip = QRectF(source.topLeft() * px, source.size() * px)
                               .toAlignedRect()
                               .intersected(QRect(QPoint(0, 0), full));
        if (clip.isEmpty())
            continue;

        QPdfDocumentRenderOptions options;
        options.setScaledSize(full);
        options.setScaledClipRect(clip);
        options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
        QImage img = mPdf->render(i, clip.size(), options);
        if (img.isNull())
            continue;

        Tile t;
        t.image = img;
        t.source = QRectF(QPointF(clip.topLeft()) / px, QSizeF(clip.size()) / px);
        t.scale = mScale;
        tiles.insert(i, t);
    }
    mTiles = tiles;
    update();
}

void ZoomView::wheelEvent(QWheelEvent *e)
{
    const qreal steps = e->angleDelta().y() / 120.0;
    if (steps == 0)
        return;
    zoomAt(e->position(), qPow(1.2, steps));
    e->accept();
}

void ZoomView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    mPressPos = mLastPos = e->position();
    mDragging = false;
}

void ZoomView::mouseMoveEvent(QMouseEvent *e)
{
    if (!(e->buttons() & Qt::LeftButton))
        return;
    if (!mDragging
        && (e->position() - mPressPos).manhattanLength() >= DragThreshold) {
        mDragging = true;
        setCursor(Qt::ClosedHandCursor);
    }
    if (mDragging) {
        mOffset -= (e->position() - mLastPos) / mScale;
        mLastPos = e->position();
        clampOffset();
        scheduleRender();
        update();
    }
}

void ZoomView::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    setCursor(Qt::OpenHandCursor);
    if (!mDragging)
        emit clicked();
    mDragging = false;
}

void ZoomView::keyPressEvent(QKeyEvent *e)
{
    const qreal stepY = height() / mScale * 0.15;
    const qreal pageY = height() / mScale * 0.9;
    switch (e->key()) {
    case Qt::Key_Up:       mOffset.ry() -= stepY; break;
    case Qt::Key_Down:     mOffset.ry() += stepY; break;
    case Qt::Key_Left:     mOffset.rx() -= stepY; break;
    case Qt::Key_Right:    mOffset.rx() += stepY; break;
    case Qt::Key_PageUp:   mOffset.ry() -= pageY; break;
    case Qt::Key_PageDown: mOffset.ry() += pageY; break;
    case Qt::Key_Home:     fitWidth(); return;
    case Qt::Key_Plus:     zoomAt(rect().center(), 1.25); return;
    case Qt::Key_Minus:    zoomAt(rect().center(), 1 / 1.25); return;
    default:
        QWidget::keyPressEvent(e); // Esc/Space reach the dialog
        return;
    }
    clampOffset();
    scheduleRender();
    update();
}
