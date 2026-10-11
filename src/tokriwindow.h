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
class QTabBar;

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

    // Tabs: each tab is its own directory of items.
    void initTabs();               // call once the models are connected
    QString currentBasketDir() const;
    int addBasketTab(const QString &name = QString());
    void renameBasketTab(int index);
    void removeBasketTab(int index);
    void bringToFront();
    // Merges the selected PDFs (in list order) into one new PDF at the
    // bottom of the list; the originals go to the recycle bin.
    void mergeSelectedPdfs();

signals:
    void pasteRequested();
    void basketDirChanged(const QString &dir);
    void dropIntoBasket(const QMimeData *data);

private:
    Ui::TokriWindow *ui;
    QLabel *mCounter = nullptr;
    QWidget *mBottomBar = nullptr;
    QPointer<PreviewDialog> mPreview;
    QPersistentModelIndex mPreviewIndex;
    QPersistentModelIndex mPreviewStartIndex;

    struct BasketTab {
        int id;
        QString name;
    };
    QList<BasketTab> mTabs;
    int mNextTabId = 1;
    QTabBar *mTabBar = nullptr;

    void setupTabs();
    void saveTabs() const;
    void onTabChanged(int index);
    static QString tabDir(int id);

    void init();
    void applyColumnSizing();
    void fitHeight(bool anywhereOnScreen);
    void moveNearCursor();
    void closeEvent(QCloseEvent *e);
    void openItem(QString filePath);
    QList<QUrl> selectedUrls() const;
};
#endif // TOKRIWINDOW_H
