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
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QTabBar>
#include <QToolButton>
#include <functional>
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

namespace {

// "+" next to the tabs: click for a new tab, or drop files on it to put
// them in a new tab.
class AddTabButton : public QToolButton
{
public:
    explicit AddTabButton(QWidget *parent) : QToolButton(parent)
    {
        setText("+");
        setAutoRaise(true);
        setAcceptDrops(true);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setToolTip("Ny flik – eller släpp filer här för att lägga dem i en ny flik");
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 1.4);
        f.setBold(true);
        setFont(f);
    }

    std::function<void(QDropEvent *)> onDrop;

protected:
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (DropAwareFileSystemModel::isPasteable(e->mimeData())) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
            setDown(true);
        }
    }
    void dragMoveEvent(QDragMoveEvent *e) override
    {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    }
    void dragLeaveEvent(QDragLeaveEvent *) override { setDown(false); }
    void dropEvent(QDropEvent *e) override
    {
        setDown(false);
        e->setDropAction(Qt::CopyAction);
        e->accept();
        if (onDrop)
            onDrop(e);
    }
};

// In-place editor for a tab's name.
class TabNameEdit : public QLineEdit
{
public:
    using QLineEdit::QLineEdit;
    std::function<void(bool accepted)> onDone;

protected:
    bool event(QEvent *e) override
    {
        // Keep Escape/Enter here rather than the window's shortcuts.
        if (e->type() == QEvent::ShortcutOverride) {
            const int key = static_cast<QKeyEvent *>(e)->key();
            if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter) {
                e->accept();
                return true;
            }
        }
        return QLineEdit::event(e);
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Escape) { finish(false); return; }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { finish(true); return; }
        QLineEdit::keyPressEvent(e);
    }
    void focusOutEvent(QFocusEvent *e) override
    {
        QLineEdit::focusOutEvent(e);
        finish(true);
    }

private:
    void finish(bool accepted)
    {
        if (mDone)
            return;
        mDone = true;
        if (onDone)
            onDone(accepted);
    }
    bool mDone = false;
};

}

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
    clearButton->setStyleSheet(
        "QPushButton { background: #c62828; color: white; border: none;"
        "  border-radius: 4px; padding: 3px 14px; }"
        "QPushButton:hover { background: #e53935; }"
        "QPushButton:pressed { background: #8e1c1c; }"
        "QPushButton:disabled { background: #5a3434; color: #b8a0a0; }");
    connect(clearButton, &QPushButton::clicked, this, &TokriWindow::clearAll);
    bar->addWidget(clearButton);

    bar->addStretch();

    connect(ui->listView, &NoInternalDragListView::previewRequested, this,
            &TokriWindow::previewIndex);

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

    setupTabs();

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

    previewIndex(idx);
}

void TokriWindow::previewIndex(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    auto *view = ui->listView;
    const QFileInfo fi =
        index.data(QFileSystemModel::FileInfoRole).value<QFileInfo>();

    // Reuse an open preview so it stays where it is while browsing.
    if (!mPreview) {
        // New preview session: remember where it started.
        mPreviewStartIndex = index;
        view->setPreviewStart(QModelIndex());

        mPreview = new PreviewDialog(this);
        // However it is closed (click, X, Esc): select the file last shown
        // and tone the row the session started from.
        connect(mPreview, &QObject::destroyed, this, [this, view] {
            if (mPreviewIndex.isValid()) {
                view->selectionModel()->setCurrentIndex(
                    mPreviewIndex, QItemSelectionModel::ClearAndSelect);
                view->scrollTo(mPreviewIndex);
            }
            if (mPreviewStartIndex.isValid()
                && mPreviewStartIndex != mPreviewIndex)
                view->setPreviewStart(mPreviewStartIndex);
            activateWindow();
            view->setFocus();
        });
        connect(mPreview, &PreviewDialog::navigateRequested, this,
                [this, view](int delta) {
                    if (!mPreviewIndex.isValid())
                        return;
                    const int row = mPreviewIndex.row() + delta;
                    const QModelIndex next =
                        view->model()->index(row, 0, view->rootIndex());
                    if (!next.isValid())
                        return;
                    view->setCurrentIndex(next); // also selects it
                    view->scrollTo(next);
                    previewIndex(next);
                });
    }

    mPreviewIndex = index;
    mPreview->showFile(fi.absoluteFilePath());
    mPreview->setNavigation(index.row() + 1,
                            view->model()->rowCount(view->rootIndex()));
}

void TokriWindow::setSelectionDelivered(bool delivered)
{
    QStringList names;
    const auto selected = ui->listView->selectionModel()->selectedIndexes();
    for (const QModelIndex &idx : selected)
        names << idx.data(QFileSystemModel::FileInfoRole).value<QFileInfo>().absoluteFilePath();
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

QString TokriWindow::tabDir(int id)
{
    if (id == 0)
        return StandardPaths::getPath(StandardPaths::TokriDir);
    const QString dir = QDir::home().filePath("Tokri-flikar/" + QString::number(id));
    QDir().mkpath(dir);
    return QDir(dir).absolutePath();
}

void TokriWindow::setupTabs()
{
    auto *row = new QWidget(ui->centralwidget);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(2);

    mTabBar = new QTabBar(row);
    mTabBar->setDocumentMode(true);
    mTabBar->setExpanding(false);
    mTabBar->setMovable(true);
    mTabBar->setElideMode(Qt::ElideRight);
    mTabBar->setUsesScrollButtons(true);
    mTabBar->setFocusPolicy(Qt::NoFocus);
    // Hovering a tab while dragging switches to it, so files can be dropped
    // (or moved from another tab) into it.
    mTabBar->setAcceptDrops(true);
    mTabBar->setChangeCurrentOnDrag(true);
    mTabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    h->addWidget(mTabBar, 1);

    auto *add = new AddTabButton(row);
    h->addWidget(add);
    ui->verticalLayout->insertWidget(0, row);

    connect(add, &QToolButton::clicked, this, [this] {
        renameBasketTab(addBasketTab());
    });
    add->onDrop = [this](QDropEvent *e) {
        const bool internal = e->source() == ui->listView;
        const QString fromDir = currentBasketDir();
        const int index = addBasketTab();
        if (internal) {
            // Move the dragged items from their tab into the new one.
            ui->listView->acceptInternalDrop();
            QStringList paths;
            const auto urls = e->mimeData()->urls();
            for (const QUrl &url : urls)
                if (url.isLocalFile())
                    paths << QFileInfo(url.toLocalFile()).absoluteFilePath();
            Q_UNUSED(fromDir);
            BasketOrder::instance().moveFiles(paths, currentBasketDir());
        } else {
            emit dropIntoBasket(e->mimeData());
        }
        renameBasketTab(index);
    };

    // Load tabs (the first, id 0, is the original Tokri folder).
    QSettings settings;
    const int n = settings.beginReadArray("tabs");
    for (int i = 0; i < n; ++i) {
        settings.setArrayIndex(i);
        mTabs.append({settings.value("id").toInt(), settings.value("name").toString()});
    }
    settings.endArray();
    if (mTabs.isEmpty())
        mTabs.append({0, QStringLiteral("Korg")});
    for (const BasketTab &t : std::as_const(mTabs))
        mNextTabId = qMax(mNextTabId, t.id + 1);

    {
        const QSignalBlocker block(mTabBar);
        for (const BasketTab &t : std::as_const(mTabs))
            mTabBar->addTab(t.name);
        mTabBar->setCurrentIndex(
            qBound(0, settings.value("tabs/current").toInt(), int(mTabs.size()) - 1));
    }
    StandardPaths::setBasketDir(currentBasketDir());

    connect(mTabBar, &QTabBar::currentChanged, this, &TokriWindow::onTabChanged);
    connect(mTabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
        mTabs.move(from, to);
        saveTabs();
    });
    connect(mTabBar, &QTabBar::tabBarDoubleClicked, this, [this](int index) {
        if (index < 0)
            renameBasketTab(addBasketTab());
        else
            renameBasketTab(index);
    });
    connect(mTabBar, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                const int index = mTabBar->tabAt(pos);
                QMenu menu;
                menu.setPalette(palette());
                QAction *rename = nullptr, *remove = nullptr;
                if (index >= 0)
                    rename = menu.addAction("Byt namn");
                QAction *add = menu.addAction("Ny flik");
                if (index >= 0) {
                    menu.addSeparator();
                    remove = menu.addAction("Ta bort flik");
                    remove->setEnabled(mTabs.size() > 1);
                }
                QAction *chosen = menu.exec(mTabBar->mapToGlobal(pos));
                if (!chosen)
                    return;
                if (chosen == rename)
                    renameBasketTab(index);
                else if (chosen == add)
                    renameBasketTab(addBasketTab());
                else if (chosen == remove)
                    removeBasketTab(index);
            });
}

void TokriWindow::initTabs()
{
    emit basketDirChanged(currentBasketDir());
}

QString TokriWindow::currentBasketDir() const
{
    const int i = mTabBar ? mTabBar->currentIndex() : 0;
    return tabDir(i >= 0 && i < mTabs.size() ? mTabs[i].id : 0);
}

void TokriWindow::saveTabs() const
{
    QSettings settings;
    settings.beginWriteArray("tabs", int(mTabs.size()));
    for (int i = 0; i < mTabs.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("id", mTabs[i].id);
        settings.setValue("name", mTabs[i].name);
    }
    settings.endArray();
    settings.setValue("tabs/current", mTabBar->currentIndex());
}

void TokriWindow::onTabChanged(int index)
{
    if (index < 0)
        return;
    if (mPreview)
        mPreview->close();
    ui->listView->setPreviewStart(QModelIndex());
    ui->listView->clearSelection();

    const QString dir = currentBasketDir();
    StandardPaths::setBasketDir(dir);
    saveTabs();
    emit basketDirChanged(dir);
}

int TokriWindow::addBasketTab(const QString &name)
{
    const int id = mNextTabId++;
    const QString title = name.isEmpty()
        ? QStringLiteral("Flik %1").arg(mTabs.size() + 1)
        : name;
    mTabs.append({id, title});
    tabDir(id); // create the folder
    const int index = mTabBar->addTab(title);
    mTabBar->setCurrentIndex(index); // switches (onTabChanged saves)
    saveTabs();
    return index;
}

void TokriWindow::renameBasketTab(int index)
{
    if (index < 0 || index >= mTabs.size())
        return;
    mTabBar->setCurrentIndex(index);

    auto *edit = new TabNameEdit(mTabBar);
    edit->setText(mTabs[index].name);
    edit->selectAll();
    QRect r = mTabBar->tabRect(index).adjusted(2, 2, -2, -2);
    r.setWidth(qMax(r.width(), 120));
    edit->setGeometry(r);
    edit->show();
    edit->setFocus();

    const int id = mTabs[index].id;
    edit->onDone = [this, edit, id](bool accepted) {
        const QString name = edit->text().trimmed();
        if (accepted && !name.isEmpty()) {
            for (int i = 0; i < mTabs.size(); ++i) {
                if (mTabs[i].id == id) {
                    mTabs[i].name = name;
                    mTabBar->setTabText(i, name);
                }
            }
            saveTabs();
        }
        edit->deleteLater();
        ui->listView->setFocus();
    };
}

void TokriWindow::removeBasketTab(int index)
{
    if (index < 0 || index >= mTabs.size() || mTabs.size() <= 1)
        return;

    const BasketTab tab = mTabs[index];
    const QString dir = tabDir(tab.id);
    const QFileInfoList items = QDir(dir).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);

    if (!items.isEmpty()) {
        const auto answer = QMessageBox::question(
            this, "Ta bort flik",
            QStringLiteral("Ta bort fliken \"%1\"?\n\n%2 fil(er) i den flyttas till papperskorgen.")
                .arg(tab.name).arg(items.size()));
        if (answer != QMessageBox::Yes)
            return;
    }
    for (const QFileInfo &fi : items)
        QFile::moveToTrash(fi.absoluteFilePath());
    if (tab.id != 0)
        QDir(dir).removeRecursively();

    mTabs.removeAt(index);
    mTabBar->removeTab(index); // switches tab if it was current
    saveTabs();
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

