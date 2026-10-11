#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include <QDialog>
#include <QString>

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

    void showFile(const QString &path);

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    QWidget *makeImageView(const QImage &image);
    QWidget *makeTextView(const QString &path);
    QWidget *makeMessage(const QString &path, const QString &text);
    void setContent(QWidget *w);

    QWidget *mContent = nullptr;
};

#endif // PREVIEWDIALOG_H
