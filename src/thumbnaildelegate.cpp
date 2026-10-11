#include "thumbnaildelegate.h"

#include "nointernaldraglistview.h"
#include "sortfilterproxy.h"
#include "thumbnaillayout.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyle>
#include <QWidget>

void ThumbnailDelegate::paint(QPainter *painter,
                              const QStyleOptionViewItem &option,
                              const QModelIndex &index) const
{
    using namespace ThumbnailLayout;

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    const QIcon icon = opt.icon;
    const QString text = opt.text;

    opt.icon = QIcon();
    opt.text.clear();
    opt.features &= ~(QStyleOptionViewItem::HasDisplay
                      | QStyleOptionViewItem::HasDecoration);

    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const bool delivered =
        index.data(FSSortFilterProxy::DeliveredRole).toBool();
    const bool selected = opt.state & QStyle::State_Selected;

    // Delivered rows are tinted green; lighter when selected so the
    // selection still shows through.
    if (delivered)
        painter->fillRect(opt.rect, QColor(0x2e, 0xa0, 0x43, selected ? 130 : 210));

    // Where the last preview session started: a toned-down selection with a
    // thin accent bar on the left.
    if (!selected) {
        const auto *view = qobject_cast<const NoInternalDragListView *>(opt.widget);
        if (view && view->previewStart() == index) {
            QColor tone = opt.palette.color(QPalette::Highlight);
            tone.setAlpha(75);
            painter->fillRect(opt.rect, tone);
            tone.setAlpha(200);
            painter->fillRect(QRect(opt.rect.left(), opt.rect.top(), 3,
                                    opt.rect.height()),
                              tone);
        }
    }

    // Strong hover: a highlight wash plus an accent bar and outline, so the
    // row under the mouse is unmistakable (also on green rows).
    if (opt.state & QStyle::State_MouseOver) {
        QColor accent = opt.palette.color(QPalette::Highlight);
        QColor wash = accent;
        wash.setAlpha(delivered ? 70 : 110);
        painter->fillRect(opt.rect, wash);

        painter->save();
        accent.setAlpha(255);
        painter->setPen(QPen(accent, 1));
        painter->drawRect(opt.rect.adjusted(0, 0, -1, -1));
        painter->fillRect(QRect(opt.rect.left(), opt.rect.top(), 4,
                                opt.rect.height()),
                          accent);
        painter->restore();
    }

    const QRect cell = opt.rect.adjusted(ListHorizontalPadding, 0,
                                         -ListHorizontalPadding, 0);
    int x = cell.x();

    if (!icon.isNull()) {
        const qreal dpr = opt.widget ? opt.widget->devicePixelRatioF()
                                     : qApp->devicePixelRatio();
        const QPixmap pm = icon.pixmap(QSize(ListIconSize, ListIconSize), dpr);
        if (!pm.isNull()) {
            const QSize size = pm.deviceIndependentSize().toSize();
            painter->drawPixmap(
                QPoint(x + (ListIconSize - size.width()) / 2,
                       cell.center().y() - size.height() / 2 + 1),
                pm);
        }
    }
    x += ListIconSize + ListHorizontalPadding;

    const int checkSize = QFontMetrics(opt.font).height();
    const QRect eye = eyeRect(opt.rect, checkSize);
    const int right = eye.left() - ListHorizontalPadding;

    // Preview "eye"; brighter when the mouse is on it.
    {
        const bool eyeHot = opt.widget
            && opt.widget->property("hoverEyeRow").isValid()
            && opt.widget->property("hoverEyeRow").toInt() == index.row();
        QColor c = delivered ? QColor(Qt::white)
                             : opt.palette.color(selected ? QPalette::HighlightedText
                                                          : QPalette::Text);
        c.setAlpha(eyeHot ? 255 : 120);

        const QRectF r = QRectF(eye).adjusted(1, 2, -1, -2);
        QPainterPath lid;
        lid.moveTo(r.left(), r.center().y());
        lid.quadTo(r.center().x(), r.top() - r.height() * 0.45,
                   r.right(), r.center().y());
        lid.quadTo(r.center().x(), r.bottom() + r.height() * 0.45,
                   r.left(), r.center().y());

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(c, eyeHot ? 1.8 : 1.4));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(lid);
        painter->setPen(Qt::NoPen);
        painter->setBrush(c);
        painter->drawEllipse(r.center(), r.height() * 0.28, r.height() * 0.28);
        painter->restore();
    }

    {
        const QRectF box = tickRect(opt.rect, checkSize);
        QPainterPath tick;
        tick.moveTo(box.left() + box.width() * 0.15, box.top() + box.height() * 0.55);
        tick.lineTo(box.left() + box.width() * 0.40, box.top() + box.height() * 0.80);
        tick.lineTo(box.left() + box.width() * 0.88, box.top() + box.height() * 0.22);

        const bool tickHot = opt.widget
            && opt.widget->property("hoverTickRow").isValid()
            && opt.widget->property("hoverTickRow").toInt() == index.row();

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if (delivered) {
            painter->setPen(QPen(Qt::white, checkSize * (tickHot ? 0.2 : 0.16),
                                 Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->drawPath(tick);
        } else {
            // Not delivered yet: just the outline of a tick, to click on.
            QPainterPathStroker stroker;
            stroker.setWidth(checkSize * 0.18);
            stroker.setCapStyle(Qt::RoundCap);
            stroker.setJoinStyle(Qt::RoundJoin);
            const QPainterPath outline = stroker.createStroke(tick).simplified();

            QColor c = opt.palette.color(selected ? QPalette::HighlightedText
                                                  : QPalette::Text);
            if (tickHot) {
                QColor fill(0x2e, 0xa0, 0x43, 150);
                painter->fillPath(outline, fill);
            }
            c.setAlpha(tickHot ? 230 : 95);
            painter->setPen(QPen(c, 1.1));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(outline);
        }
        painter->restore();
    }

    const QRect textRect(x, cell.y(), right - x + 1, cell.height());
    const QColor color =
        delivered ? QColor(Qt::white)
                  : opt.palette.color(selected ? QPalette::HighlightedText
                                               : QPalette::Text);
    const QString elided =
        QFontMetrics(opt.font).elidedText(text, Qt::ElideMiddle, textRect.width());

    painter->save();
    painter->setFont(opt.font);
    painter->setPen(color);
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elided);
    painter->restore();
}

QSize ThumbnailDelegate::sizeHint(const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const
{
    Q_UNUSED(index);

    using namespace ThumbnailLayout;

    const QFontMetrics fm(option.font);
    // The list view stretches rows to the viewport width.
    return QSize(1, qMax(ListIconSize, fm.height()) + 2 * ListRowPadding);
}

void ThumbnailDelegate::updateEditorGeometry(QWidget *editor,
                                             const QStyleOptionViewItem &option,
                                             const QModelIndex &index) const
{
    Q_UNUSED(index);

    using namespace ThumbnailLayout;

    const int left = ListHorizontalPadding * 2 + ListIconSize;
    editor->setGeometry(option.rect.adjusted(left, 0, -ListHorizontalPadding, 0));
}
