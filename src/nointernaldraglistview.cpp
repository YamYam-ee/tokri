#include "nointernaldraglistview.h"

#include "basketorder.h"
#include "loghelpers.h"
#include "sortfilterproxy.h"
#include "thumbnaillayout.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>

namespace {

QString nameOf(const QModelIndex &idx)
{
    return idx.data(QFileSystemModel::FileInfoRole).value<QFileInfo>().fileName();
}

void preferCopy(QDropEvent *e)
{
    if (e->isAccepted()) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    }
}

}

NoInternalDragListView::NoInternalDragListView() {}

void NoInternalDragListView::startDrag(Qt::DropActions supportedActions)
{
    Q_UNUSED(supportedActions);
    Logger &log = Logger::instance();
    log.push("startDrag");

    // Items are always copied out, so they stay in the basket until deleted.
    // A completed drop marks them as delivered.
    QModelIndexList indexes;
    const auto selected = selectionModel()->selectedIndexes();
    for (const QModelIndex &idx : selected)
        if (idx.isValid() && (model()->flags(idx) & Qt::ItemIsDragEnabled))
            indexes << idx;
    if (indexes.isEmpty()) {
        log.pop();
        return;
    }

    QMimeData *mime = model()->mimeData(indexes);
    if (!mime) {
        log.pop();
        return;
    }

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    const QIcon icon = indexes.first().data(Qt::DecorationRole).value<QIcon>();
    if (!icon.isNull())
        drag->setPixmap(icon.pixmap(iconSize(), devicePixelRatioF()));

    mDroppedOnSelf = false;
    const Qt::DropAction result = drag->exec(Qt::CopyAction, Qt::CopyAction);
    log.log() << "result=" << result << "onSelf=" << mDroppedOnSelf;

    // Dropping back into the list only reorders; anywhere else delivers.
    if (result != Qt::IgnoreAction && !mDroppedOnSelf) {
        QStringList names;
        for (const QModelIndex &idx : std::as_const(indexes))
            names << nameOf(idx);
        BasketOrder::instance().markDelivered(names);
        // Deselect so the green "delivered" row is visible right away.
        clearSelection();
    }
    mDroppedOnSelf = false;

    log.pop();
}

QItemSelectionModel::SelectionFlags
NoInternalDragListView::selectionCommand(const QModelIndex &index,
                                         const QEvent *event) const
{
    if (event && event->type() == QEvent::MouseButtonPress) {
        const auto *mouseEvent = static_cast<const QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::RightButton
            && selectionModel()
            && selectionModel()->hasSelection())
            return QItemSelectionModel::NoUpdate;
    }

    return QListView::selectionCommand(index, event);
}

void NoInternalDragListView::dragEnterEvent(QDragEnterEvent *e)
{
    const bool isSelf = e->source() == this;
    if (isSelf) {
        // Reordering within the list.
        e->setDropAction(Qt::CopyAction);
        e->accept();
        setDropRow(dropRowAt(e->position().toPoint()));
    } else {
        emit dropping(true);
        QListView::dragEnterEvent(e);
        preferCopy(e);
    }
}

void NoInternalDragListView::dragMoveEvent(QDragMoveEvent *e)
{
    const bool isSelf = e->source() == this;
    if (isSelf) {
        QListView::dragMoveEvent(e); // keeps auto-scroll near the edges
        e->setDropAction(Qt::CopyAction);
        e->accept();
        setDropRow(dropRowAt(e->position().toPoint()));
    } else {
        QListView::dragMoveEvent(e);
        preferCopy(e);
    }
}

void NoInternalDragListView::dragLeaveEvent(QDragLeaveEvent *e)
{
    Logger &log = Logger::instance();
    log.push("dragLeave");
    log.log() << "leave";

    emit dropping(false);
    setDropRow(-1);

    QListView::dragLeaveEvent(e);

    log.pop();
}

void NoInternalDragListView::dropEvent(QDropEvent *e)
{
    Logger &log = Logger::instance();
    log.push("dropEvent");

    log.log() << "source==this=" << (e->source() == this)
              << "dropAction=" << e->dropAction()
              << "possibleActions=" << e->possibleActions()
              << "formats=" << e->mimeData()->formats();
    emit dropping(false);

    if (e->source() == this) {
        const int row = dropRowAt(e->position().toPoint());
        setDropRow(-1);
        mDroppedOnSelf = true;
        e->setDropAction(Qt::CopyAction);
        e->accept();
        moveSelectionTo(row);
        log.pop();
        return;
    }

    e->setDropAction(Qt::CopyAction);

    QListView::dropEvent(e);

    log.log() << "accepted=" << e->isAccepted()
              << "finalDropAction=" << e->dropAction();

    log.pop();
}

int NoInternalDragListView::dropRowAt(const QPoint &pos) const
{
    const int rows = model() ? model()->rowCount(rootIndex()) : 0;
    const QModelIndex idx = indexAt(pos);
    if (!idx.isValid())
        return rows;
    const QRect r = visualRect(idx);
    return pos.y() < r.center().y() ? idx.row() : idx.row() + 1;
}

void NoInternalDragListView::setDropRow(int row)
{
    if (row == mDropRow)
        return;
    mDropRow = row;
    viewport()->update();
}

void NoInternalDragListView::moveSelectionTo(int row)
{
    auto *m = model();
    if (!m)
        return;

    const QModelIndex root = rootIndex();
    const int rows = m->rowCount(root);

    QStringList order;
    for (int r = 0; r < rows; ++r)
        order << nameOf(m->index(r, 0, root));

    // Selected items, in their current top-to-bottom order.
    QList<int> selectedRows;
    const auto selected = selectionModel()->selectedIndexes();
    for (const QModelIndex &idx : selected)
        if (idx.parent() == root)
            selectedRows << idx.row();
    std::sort(selectedRows.begin(), selectedRows.end());
    if (selectedRows.isEmpty())
        return;

    QStringList moved;
    int insertAt = row;
    for (int r : std::as_const(selectedRows)) {
        moved << order[r];
        if (r < row)
            --insertAt;
    }
    for (const QString &name : std::as_const(moved))
        order.removeOne(name);

    insertAt = qBound(0, insertAt, int(order.size()));
    for (int i = 0; i < moved.size(); ++i)
        order.insert(insertAt + i, moved[i]);

    Logger::instance().log() << "reorder moved=" << moved << "to=" << insertAt;
    BasketOrder::instance().setManualOrder(order);
}

bool NoInternalDragListView::hitsTick(const QModelIndex &index,
                                      const QPoint &pos) const
{
    using namespace ThumbnailLayout;

    if (!index.isValid()
        || !index.data(FSSortFilterProxy::DeliveredRole).toBool())
        return false;
    const QRect r = visualRect(index);
    const QRect t = tickRect(r, fontMetrics().height());
    return pos.x() >= t.left() - ListHorizontalPadding / 2;
}

bool NoInternalDragListView::hitsEye(const QModelIndex &index,
                                     const QPoint &pos) const
{
    using namespace ThumbnailLayout;

    if (!index.isValid())
        return false;
    const QRect r = visualRect(index);
    const QRect e = eyeRect(r, fontMetrics().height());
    return pos.x() >= e.left() - ListHorizontalPadding / 2
           && pos.x() < e.right() + ListHorizontalPadding / 2;
}

void NoInternalDragListView::setHoverEyeRow(int row)
{
    const QVariant current = property("hoverEyeRow");
    if (current.isValid() && current.toInt() == row)
        return;
    setProperty("hoverEyeRow", row);
    viewport()->update();
}

void NoInternalDragListView::leaveEvent(QEvent *e)
{
    setHoverEyeRow(-1);
    QListView::leaveEvent(e);
}

void NoInternalDragListView::mousePressEvent(QMouseEvent *e)
{
    // Clicking the tick of a delivered row un-ticks it.
    if (e->button() == Qt::LeftButton && e->modifiers() == Qt::NoModifier) {
        const QPoint pos = e->position().toPoint();
        const QModelIndex idx = indexAt(pos);
        if (hitsTick(idx, pos)) {
            BasketOrder::instance().setDelivered({nameOf(idx)}, false);
            e->accept();
            return;
        }
        if (hitsEye(idx, pos)) {
            emit previewRequested(idx);
            e->accept();
            return;
        }
    }
    QListView::mousePressEvent(e);
}

void NoInternalDragListView::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons() == Qt::NoButton) {
        const QPoint pos = e->position().toPoint();
        const QModelIndex idx = indexAt(pos);
        const bool onTick = hitsTick(idx, pos);
        const bool onEye = !onTick && hitsEye(idx, pos);
        setHoverEyeRow(onEye ? idx.row() : -1);
        viewport()->setCursor(onTick || onEye ? Qt::PointingHandCursor
                                              : Qt::ArrowCursor);
        setToolTip(onTick  ? QStringLiteral("Klicka för att ta bort bocken")
                   : onEye ? QStringLiteral("Förhandsvisa (Mellanslag)")
                           : QString());
    }
    QListView::mouseMoveEvent(e);
}

void NoInternalDragListView::emitVisibleCount()
{
    const int rowHeight = sizeHintForRow(0);
    if (rowHeight <= 0)
        return;

    const int count = viewport()->height() / rowHeight + 1;

    if (count == mLastVisibleCount)
        return;

    mLastVisibleCount = count;
    emit visibleCountChanged(count);
}

void NoInternalDragListView::paintEvent(QPaintEvent *e) {
    emitVisibleCount();

    QListView::paintEvent(e);

    // Insertion line while reordering by drag.
    if (mDropRow >= 0 && model()) {
        const int rows = model()->rowCount(rootIndex());
        int y = 0;
        if (rows > 0 && mDropRow < rows)
            y = visualRect(model()->index(mDropRow, 0, rootIndex())).top();
        else if (rows > 0)
            y = visualRect(model()->index(rows - 1, 0, rootIndex())).bottom() + 1;

        QPainter lp(viewport());
        lp.setRenderHint(QPainter::Antialiasing);
        const QColor c = palette().color(QPalette::Highlight).lighter(170);
        const int yy = qBound(3, y, viewport()->height() - 3);
        lp.setPen(QPen(c, 3, Qt::SolidLine, Qt::RoundCap));
        lp.drawLine(10, yy, viewport()->width() - 6, yy);
        lp.setBrush(c);
        lp.drawEllipse(QPoint(7, yy), 4, 4);
    }

    auto *m = model();
    if (!m || m->rowCount(rootIndex()) > 0)
        return;

    QPainter p(viewport());

    QPixmap pm(":/background.png");
    pm = pm.scaled(150, 150,
                   Qt::KeepAspectRatio,
                   Qt::SmoothTransformation);

    const QString text =
        "Drop files, folders, text, links, or images here.";

    QFontMetrics fm(font());
    int textHeight = fm.height();
    int spacing = 8;

    int totalHeight = pm.height() + spacing + textHeight;

    int startY = (height() - totalHeight) / 3;

    QPoint iconPos(width()/2 - pm.width()/2,
                   startY);
    p.drawPixmap(iconPos, pm);

    QRect textRect(0,
                   startY + pm.height() + spacing,
                   width(),
                   textHeight);

    p.setPen(palette().color(QPalette::Text));
    p.drawText(textRect, Qt::AlignHCenter, text);
}
