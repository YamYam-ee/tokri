#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include <QDialog>
#include <QPoint>
#include <QPointer>
#include <QString>

class NavButton;
class ShellPreviewHost;

// Quick-look style window for one file: images and text are shown directly;
// other types (PDF, Office, Outlook .msg, ...) use the Windows preview
// handler that Explorer's preview pane uses, falling back to a large shell
// thumbnail or the file icon.
class PreviewDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreviewDialog(QWidget *parent = nullptr);

    // Can be called again on an open preview to switch file in place.
    void showFile(const QString &path);
    // "position / count" in the basket; enables the arrows.
    void setNavigation(int position, int count);
    static PreviewDialog *current();

signals:
    // -1 = previous (up the list), +1 = next (down the list).
    void navigateRequested(int delta);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *e) override;
    void moveEvent(QMoveEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    QWidget *makeImageView(const QImage &image);
    QWidget *makeTextView(const QString &path);
    QWidget *makeMessage(const QString &path, const QString &text);
    void setContent(QWidget *w);
    void sizeForAspect(qreal aspect, int extraWidth);
    void installClickToClose(QWidget *nativeHost);
    void removeClickToClose();
    void rememberIfMoved();
    void closeOnClick(QWidget *w);
    void placeNavButtons();

    static QPointer<PreviewDialog> sCurrent;
    static QPoint sUserCenter;  // where the user last moved a preview to
    QPoint mPlacedCenter;

    QWidget *mContent = nullptr;
    NavButton *mPrev = nullptr;
    NavButton *mNext = nullptr;
    QString mFileName;
    QString mPositionText;
    bool mHasPrev = false;
    bool mHasNext = false;
    QPoint mPressPos;
};

#endif // PREVIEWDIALOG_H
