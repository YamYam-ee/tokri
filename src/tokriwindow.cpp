#include "tokriwindow.h"
#include "./ui_tokriwindow.h"
#include "dropawarefilesystemmodel.h"
#include "filereveal.h"
#include "basketorder.h"
#include "loghelpers.h"
#include "previewdialog.h"
#include "sortfilterproxy.h"
#include "standardpaths.h"
#include "thumbnaildelegate.h"
#include "thumbnaillayout.h"
#include <QDir>
#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFrame>
#include <QApplication>
#include <QClipboard>
#include <QScreen>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

TokriWindow::TokriWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::TokriWindow)
{
    ui->setupUi(this);

    init();
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);

    ui->listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->listView->setItemDelegate(new ThumbnailDelegate(ui->listView));

    ui->listView->setAcceptDrops(true);
    ui->listView->setFrameShape(QFrame::NoFrame);
    ui->listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->listView->setDragDropMode(QAbstractItemView::DragDrop);
    ui->listView->setDragEnabled(true);
    ui->listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->listView->setTextElideMode(Qt::ElideMiddle);
    ui->listView->setResizeMode(QListView::Adjust);
    ui->listView->setLayoutMode(QListView::Batched);

    connect(ui->listView, &QListView::doubleClicked,
            this, [this](const QModelIndex &idx){
                if (!idx.isValid())
                    return;

                const QString filePath =
                    idx.data(QFileSystemModel::FileInfoRole)
                        .value<QFileInfo>()
                        .filePath();
                openItem(filePath);
    });

    ui->listView->setViewMode(QListView::ListMode);
    ui->listView->setIconSize({ThumbnailLayout::ListIconSize,
                               ThumbnailLayout::ListIconSize});
    ui->listView->setFlow(QListView::TopToBottom);
    ui->listView->setWrapping(false);
    ui->listView->setUniformItemSizes(true);
    ui->listView->setSpacing(0);
    ui->listView->setMouseTracking(true);
    ui->listView->viewport()->setAttribute(Qt::WA_Hover);
    ui->listView->setFocusPolicy(Qt::StrongFocus);
    ui->listView->setDropIndicatorShown(false);
    ui->listView->setContextMenuPolicy(Qt::CustomContextMenu);

    // Bottom bar: [Rensa]  ...  "12 filer · 5 avbockade"
    mBottomBar = new QWidget(ui->centralwidget);
    auto *bar = new QHBoxLayout(mBottomBar);
    bar->setContentsMargins(0, 2, 4, 0);

    auto *clearButton = new QPushButton("Rensa", mBottomBar);
    clearButton->setToolTip("Flytta alla filer i korgen till papperskorgen");
    clearButton->setFocusPolicy(Qt::NoFocus);
    connect(clearButton, &QPushButton::clicked, this, &TokriWindow::clearAll);
    bar->addWidget(clearButton);

    auto *previewButton = new QPushButton("Förhandsvisa", mBottomBar);
    previewButton->setToolTip("Snabbtitt på markerad fil (Mellanslag)");
    previewButton->setFocusPolicy(Qt::NoFocus);
    connect(previewButton, &QPushButton::clicked, this,
            &TokriWindow::previewSelection);
    bar->addWidget(previewButton);
    bar->addStretch();

    auto *previewShortcut = new QShortcut(QKeySequence(Qt::Key_Space), ui->listView);
    previewShortcut->setContext(Qt::WidgetShortcut);
    connect(previewShortcut, &QShortcut::activated, this,
            &TokriWindow::previewSelection);

    mCounter = new QLabel(mBottomBar);
    mCounter->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    bar->addWidget(mCounter);

    ui->verticalLayout->addWidget(mBottomBar);
    // Kept visible (only disabled when empty) so the window's height math
    // does not shift when the basket empties or fills.
    mBottomBar->setEnabled(false);

    applyColumnSizing();

    connect(ui->listView, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                Logger &log = Logger::instance();
                log.push("contextMenu");

                auto *view = ui->listView;
                auto *sel  = view->selectionModel();
                const auto selected = sel->selectedIndexes();
                const int count = selected.size();

                QMenu menu;
                menu.setPalette(this->palette());

                QAction *open = nullptr, *reveal = nullptr, *rename = nullptr;
                QAction *copy = nullptr, *del = nullptr, *selectAll = nullptr;
                QAction *paste = nullptr;
                QAction *preview = nullptr, *tick = nullptr, *untick = nullptr;

                int deliveredCount = 0;
                for (const QModelIndex &idx : selected)
                    if (idx.data(FSSortFilterProxy::DeliveredRole).toBool())
                        ++deliveredCount;

                if (count > 0) {
                    preview = menu.addAction("Förhandsvisa\tMellanslag");
                    if (deliveredCount > 0)
                        untick = menu.addAction("Ta bort bock");
                    if (deliveredCount < count)
                        tick = menu.addAction("Bocka av");
                    menu.addSeparator();
                    open = menu.addAction("&Open");
                    copy = menu.addAction("&Copy");
#ifdef Q_OS_WIN
                    del  = menu.addAction("&Delete");
#else
                    del  = menu.addAction("Move to &Trash");
#endif
                }
                if (count == 1) {
#ifdef Q_OS_MACOS
                    reveal= menu.addAction("Reveal in &Finder");
#elif defined(Q_OS_WIN)
                    reveal= menu.addAction("Open &file location");
#else
                    reveal= menu.addAction("Open Containing &Folder");
#endif
                    rename= menu.addAction("&Rename");
                }

                paste = menu.addAction("&Paste");
                paste->setEnabled(DropAwareFileSystemModel::isPasteable(
                    QGuiApplication::clipboard()->mimeData()));
                selectAll = menu.addAction("Select &All");

                QAction *chosen = menu.exec(view->viewport()->mapToGlobal(pos));
                log.log() << "count=" << count
                          << "chosen=" << (chosen ? chosen->text() : QString("<none>"));
                if (!chosen) {
                    log.pop();
                    return;
                }

                auto fileInfoAt = [](const QModelIndex &idx) {
                    return idx.data(QFileSystemModel::FileInfoRole).value<QFileInfo>();
                };

                if (chosen == preview) {
                    previewSelection();
                    log.pop();
                    return;
                }

                if (chosen == tick || chosen == untick) {
                    setSelectionDelivered(chosen == tick);
                    log.pop();
                    return;
                }

                if (chosen == paste) {
                    emit pasteRequested();
                    log.pop();
                    return;
                }

                if (chosen == selectAll) {
                    this->selectAll();
                    log.pop();
                    return;
                }

                if (chosen == open) {
                    openSelection();
                    log.pop();
                    return;
                }

                if (count == 1 && chosen == reveal) {
                    FileReveal::revealInFileManager(
                        fileInfoAt(selected[0]).absoluteFilePath());
                    log.pop();
                    return;
                }

                if (count == 1 && chosen == rename) {
                    renameSelection();
                    log.pop();
                    return;
                }

                if (chosen == copy) {
                    copySelection();
                    log.pop();
                    return;
                }

                if (chosen == del) {
                    deleteSelection();
                }

                log.pop();
            });

}

TokriWindow::~TokriWindow()
{
    delete ui;
}

Ui::TokriWindow *TokriWindow::uiHandle()
{
    return ui;
}

void TokriWindow::sleep()
{
    Logger &log = Logger::instance();
    log.push("sleep");
    log.log() << "-> hide";

    hide();

    log.pop();
}

void TokriWindow::wakeUp()
{
    Logger &log = Logger::instance();
    log.push("wakeUp");

    const bool minimized = isMinimized();
    const bool hidden = !isVisible();

    log.log() << "minimized=" << minimized
              << "hidden=" << hidden;

    if (minimized || hidden) {
        fitHeight(true);
        moveNearCursor();
    }
    fitHeight(false);

    if (minimized) {
        showNormal();
    } else if (hidden) {
        show();
    }

    raise();
    activateWindow();
    ui->listView->setFocus();

    log.pop();
}

void TokriWindow::init()
{
    QString tokriDir = StandardPaths::getPath(StandardPaths::TokriDir);
    QDir dir(tokriDir);
    if (!dir.exists()){
        bool success = dir.mkpath(tokriDir);
        if (!success){
            // FIXME handle error
        }
    }
}

void TokriWindow::applyColumnSizing()
{
    setMinimumWidth(ThumbnailLayout::ListMinWidth);
    resize(ThumbnailLayout::ListDefaultWidth, height());
}

void TokriWindow::updateCounter()
{
    auto *view = ui->listView;
    auto *m = view->model();
    if (!m || !mCounter)
        return;

    const QModelIndex root = view->rootIndex();
    const int total = m->rowCount(root);
    int delivered = 0;
    for (int r = 0; r < total; ++r)
        if (m->index(r, 0, root).data(FSSortFilterProxy::DeliveredRole).toBool())
            ++delivered;

    if (total == 0) {
        mCounter->clear();
        mBottomBar->setEnabled(false);
        return;
    }

    mCounter->setText(QString("%1 %2  ·  %3 avbockade")
                          .arg(total)
                          .arg(total == 1 ? "fil" : "filer")
                          .arg(delivered));
    mBottomBar->setEnabled(true);
}

void TokriWindow::previewSelection()
{
    auto *view = ui->listView;
    QModelIndex idx = view->currentIndex();
    if (!idx.isValid() || !view->selectionModel()->isSelected(idx)) {
        const auto selected = view->selectionModel()->selectedIndexes();
        if (selected.isEmpty())
            return;
        idx = selected.first();
    }

    const QFileInfo fi =
        idx.data(QFileSystemModel::FileInfoRole).value<QFileInfo>();
    auto *dlg = new PreviewDialog(this);
    dlg->showFile(fi.absoluteFilePath());
}

void TokriWindow::setSelectionDelivered(bool delivered)
{
    QStringList names;
    const auto selected = ui->listView->selectionModel()->selectedIndexes();
    for (const QModelIndex &idx : selected)
        names << idx.data(QFileSystemModel::FileInfoRole).value<QFileInfo>().fileName();
    BasketOrder::instance().setDelivered(names, delivered);
    if (delivered)
        ui->listView->clearSelection();
}

void TokriWindow::clearAll()
{
    Logger &log = Logger::instance();
    log.push("clearAll");

    // Same as Delete on everything: items go to the recycle bin.
    ui->listView->selectAll();
    deleteSelection();

    log.pop();
}

void TokriWindow::fitHeightToContents()
{
    fitHeight(false);
}

// Grows (or shrinks) the window so every row is visible. The height is capped
// at the bottom of the screen (or, when about to be shown elsewhere, at the
// screen height); beyond that the list scrolls.
void TokriWindow::fitHeight(bool anywhereOnScreen)
{
    using namespace ThumbnailLayout;

    auto *view = ui->listView;
    auto *m = view->model();
    if (!m)
        return;

    const int rows = m->rowCount(view->rootIndex());
    const int rowHeight = rows > 0 ? view->sizeHintForRow(0) : 0;
    const int viewportHeight =
        rows > 0 ? qMax(rows * rowHeight, MinViewportHeight)
                 : EmptyViewportHeight;

    const int chrome = height() - view->viewport()->height();
    int desired = chrome + viewportHeight;

    QScreen *scr = isVisible() ? screen()
                               : QGuiApplication::screenAt(QCursor::pos());
    if (!scr)
        scr = screen();
    const QRect avail = scr->availableGeometry();
    const int titleBar = frameGeometry().height() - height();
    const int top = anywhereOnScreen ? avail.top() : frameGeometry().top();
    const int maxHeight = avail.bottom() + 1 - top - titleBar;

    desired = qMin(desired, maxHeight);
    desired = qMax(desired, minimumSizeHint().height());
    if (desired != height())
        resize(width(), desired);
}

void TokriWindow::moveNearCursor()
{
    const QPoint cursor = QCursor::pos();
    const QSize  winSize = size();
    QPoint p(cursor.x() + 20, cursor.y() + 20);

    const QRect screen = QGuiApplication::screenAt(cursor)->availableGeometry();

    if (p.x() + winSize.width() > screen.right())
        p.setX(screen.right() - winSize.width());
    if (p.y() + winSize.height() > screen.bottom())
        p.setY(screen.bottom() - winSize.height());
    if (p.x() < screen.left())
        p.setX(screen.left());
    if (p.y() < screen.top())
        p.setY(screen.top());

    move(p);
}

void TokriWindow::onShakeDetect()
{
    Logger &log = Logger::instance();
    log.push("onShakeDetect");
    log.log() << "shake";

    wakeUp();

    log.pop();
}


void TokriWindow::openItem(QString filePath) {
    Logger &log = Logger::instance();
    log.push("openItem");
    log.log() << "path=" << filePath;

    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));

    log.pop();
}

void TokriWindow::deleteSelection()
{
    Logger &log = Logger::instance();
    log.push("delete");

    auto *view = ui->listView;
    auto *sel = view->selectionModel();
    if (!sel) {
        log.pop();
        return;
    }

    const auto selected = sel->selectedIndexes();
    for (const auto &idx : selected) {
        if (!idx.isValid())
            continue;

        const QFileInfo fi =
            idx.data(QFileSystemModel::FileInfoRole).value<QFileInfo>();
        if (!fi.exists())
            continue;

        log.log() << "trash path=" << fi.absoluteFilePath()
                  << "ok=" << QFile::moveToTrash(fi.absoluteFilePath());
    }

    log.pop();
}

void TokriWindow::renameSelection()
{
    Logger &log = Logger::instance();
    log.push("rename");

    auto *view = ui->listView;
    auto *sel = view->selectionModel();
    if (!sel) {
        log.pop();
        return;
    }

    const auto selected = sel->selectedIndexes();
    if (selected.size() != 1 || !selected[0].isValid()) {
        log.log() << "rename skipped count=" << selected.size();
        log.pop();
        return;
    }

    log.log() << "rename path="
              << selected[0].data(QFileSystemModel::FileInfoRole)
                     .value<QFileInfo>().filePath();
    view->edit(selected[0]);

    log.pop();
}

void TokriWindow::selectAll()
{
    Logger &log = Logger::instance();
    log.push("selectAll");

    ui->listView->selectAll();

    log.pop();
}

QList<QUrl> TokriWindow::selectedUrls() const
{
    QList<QUrl> urls;

    auto *sel = ui->listView->selectionModel();
    if (!sel)
        return urls;

    const auto selected = sel->selectedIndexes();
    for (const auto &idx : selected) {
        if (!idx.isValid())
            continue;

        const QFileInfo fi =
            idx.data(QFileSystemModel::FileInfoRole).value<QFileInfo>();
        if (fi.exists())
            urls << QUrl::fromLocalFile(fi.absoluteFilePath());
    }

    return urls;
}

void TokriWindow::copySelection()
{
    Logger &log = Logger::instance();
    log.push("copySelection");

    const QList<QUrl> urls = selectedUrls();
    log.log() << "urls=" << urls;
    if (!urls.isEmpty()) {
        auto *mime = new QMimeData;
        mime->setUrls(urls);
        QGuiApplication::clipboard()->setMimeData(mime);
    }

    log.pop();
}

void TokriWindow::openSelection()
{
    Logger &log = Logger::instance();
    log.push("openSelection");

    auto *sel = ui->listView->selectionModel();
    if (!sel) {
        log.pop();
        return;
    }

    const auto selected = sel->selectedIndexes();
    log.log() << "selection count=" << selected.size();

    for (const QModelIndex &idx : selected) {
        if (!idx.isValid())
            continue;

        const QString filePath =
            idx.data(QFileSystemModel::FileInfoRole).value<QFileInfo>().filePath();
        log.log() << "path=" << filePath;
        openItem(filePath);
    }

    log.pop();
}

void TokriWindow::closeEvent(QCloseEvent *e)
{
    Logger &log = Logger::instance();
    log.push("closeEvent");
    log.log() << "-> ignore & sleep";

    e->ignore();
    sleep();

    log.pop();
}

