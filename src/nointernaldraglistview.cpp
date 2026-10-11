#include "nointernaldraglistview.h"

#include "basketorder.h"
#include "loghelpers.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QGuiApplication>
#include <QMouseEvent>

namespace {

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

    const Qt::DropAction result = drag->exec(Qt::CopyAction, Qt::CopyAction);
    log.log() << "result=" << result;

    if (result != Qt::IgnoreAction) {
        QStringList names;
        for (const QModelIndex &idx : std::as_const(indexes))
            names << idx.data(QFileSystemModel::FileInfoRole)
                         .value<QFileInfo>().fileName();
        BasketOrder::instance().markDelivered(names);
    }

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
    if (isSelf)
        e->ignore();
    else {
        emit dropping(true);
        QListView::dragEnterEvent(e);
        preferCopy(e);
    }
}

void NoInternalDragListView::dragMoveEvent(QDragMoveEvent *e)
{
    const bool isSelf = e->source() == this;
    if (isSelf)
        e->ignore();
    else {
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

    e->setDropAction(Qt::CopyAction);

    QListView::dropEvent(e);

    log.log() << "accepted=" << e->isAccepted()
              << "finalDropAction=" << e->dropAction();

    log.pop();
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
