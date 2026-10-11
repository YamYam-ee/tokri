#ifndef ZOOMVIEW_H
#define ZOOMVIEW_H

#include <QHash>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QTimer>
#include <QWidget>

class QPdfDocument;

// Zoomable, pannable view of an image or the pages of a PDF.
//  - mouse wheel zooms around the cursor
//  - left-drag pans; a click without dragging emits clicked()
//  - arrows / PageUp / PageDown / Home move around
// PDF pages are re-rendered at the current zoom (visible part only), so text
// stays sharp however far you zoom in.
class ZoomView : public QWidget
{
    Q_OBJECT
public:
    explicit ZoomView(QWidget *parent = nullptr);
    ~ZoomView() override;

    void setImage(const QImage &image);
    bool setPdf(const QString &path);

    // Width / height of the first page (or the image).
    qreal firstPageAspect() const;

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    struct Tile {
        QImage image;    // rendered pixels
        QRectF source;   // area of the page it covers, in page points
        qreal scale = 0; // logical pixels per point it was rendered at
    };

    void layoutPages();
    void fitWidth();
    qreal fitScale() const;
    void zoomAt(const QPointF &viewPos, qreal factor);
    void clampOffset();
    QRectF pageRectOnScreen(int page) const;
    void scheduleRender();
    void renderVisible();

    QPdfDocument *mPdf = nullptr;
    QImage mImage;
    QList<QSizeF> mPageSizes;   // points (or pixels for an image)
    QList<QRectF> mPageRects;   // laid out in content space (points)
    QSizeF mContentSize;
    QHash<int, Tile> mTiles;

    qreal mScale = 1.0;         // logical pixels per point
    QPointF mOffset;            // content point at the view's top-left
    bool mFitted = false;

    QPointF mPressPos;
    QPointF mLastPos;
    bool mDragging = false;
    QTimer mRenderTimer;
};

#endif // ZOOMVIEW_H
