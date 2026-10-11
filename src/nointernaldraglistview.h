#ifndef NOINTERNALDRAGLISTVIEW_H
#define NOINTERNALDRAGLISTVIEW_H

#include <QListView>
#include <QObject>
#include <QPainter>
#include <QItemSelectionModel>
#include <QPersistentModelIndex>

class NoInternalDragListView : public QListView
{
    Q_OBJECT
public:
    using QListView::QListView;

    NoInternalDragListView();

    // Row where the last preview session started; drawn with a toned
    // selection until the user clicks in the list again.
    void setPreviewStart(const QModelIndex &index);
    QModelIndex previewStart() const { return mPreviewStart; }

    // Something other than the list (e.g. the "+" tab button) took this
    // list's own drag: don't count it as delivered.
    void acceptInternalDrop() { mDroppedOnSelf = true; }

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;
    QItemSelectionModel::SelectionFlags selectionCommand(
        const QModelIndex &index, const QEvent *event = nullptr) const override;

signals:
    void dropping(bool status);
    void visibleCountChanged(int count);
    void previewRequested(const QModelIndex &index);

private:
    void emitVisibleCount();
    // Row the dragged items would be inserted before (rowCount = at the end).
    int dropRowAt(const QPoint &pos) const;
    bool hitsTick(const QModelIndex &index, const QPoint &pos) const;
    bool hitsEye(const QModelIndex &index, const QPoint &pos) const;
    void setHoverEyeRow(int row);
    void setHoverTickRow(int row);
    void moveSelectionTo(int row);
    void setDropRow(int row);

    int mLastVisibleCount = -1;
    int mDropRow = -1;
    QPersistentModelIndex mPreviewStart;
    bool mDroppedOnSelf = false;
};

#endif // NOINTERNALDRAGLISTVIEW_H
