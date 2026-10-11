#ifndef TOKRIWINDOW_H
#define TOKRIWINDOW_H

#include <QLabel>
#include <QListView>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QMainWindow>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QMimeData>
#include <QCloseEvent>
#include <QList>
#include <QUrl>

QT_BEGIN_NAMESPACE
namespace Ui {
class TokriWindow;
}
QT_END_NAMESPACE

class PreviewDialog;

class TokriWindow : public QMainWindow
{
    Q_OBJECT

public:
    TokriWindow(QWidget *parent = nullptr);
    ~TokriWindow();
    Ui::TokriWindow* uiHandle();
    void sleep();
    void wakeUp();

public slots:
    void onShakeDetect();
    void deleteSelection();
    void renameSelection();
    void copySelection();
    void openSelection();
    void selectAll();
    void fitHeightToContents();
    void updateCounter();
    void clearAll();
    void previewSelection();
    void previewIndex(const QModelIndex &index);
    void setSelectionDelivered(bool delivered);

signals:
    void pasteRequested();

private:
    Ui::TokriWindow *ui;
    QLabel *mCounter = nullptr;
    QWidget *mBottomBar = nullptr;
    QPointer<PreviewDialog> mPreview;
    QPersistentModelIndex mPreviewIndex;

    void init();
    void applyColumnSizing();
    void fitHeight(bool anywhereOnScreen);
    void moveNearCursor();
    void closeEvent(QCloseEvent *e);
    void openItem(QString filePath);
    QList<QUrl> selectedUrls() const;
};
#endif // TOKRIWINDOW_H
