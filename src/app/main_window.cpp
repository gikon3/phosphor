#include "app/main_window.hpp"

#include <QAction>
#include <QByteArray>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QStringList>
#include <QTreeView>
#include <algorithm>
#include <filesystem>
#include <utility>
#include <vector>

#include "app/hierarchy_model.hpp"
#include "app/shortcuts.hpp"
#include "core/renderer.hpp"
#include "core/session.hpp"
#include "core/view.hpp"

namespace Phosphor {

namespace {

constexpr auto kSettingsOrganization = "phosphor";
constexpr auto kSettingsApplication = "phosphor";
constexpr auto kGeometryKey = "mainWindow/geometry";
constexpr auto kStateKey = "mainWindow/state";

constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 800;
constexpr int kProgressWidth = 120;

}  // namespace

MainWindow::MainWindow(Session& session, View& view, Renderer& renderer, QWidget* parent) :
        QMainWindow{parent}, session_{&session}, view_{&view}, renderer_{&renderer} {
    setWindowTitle(QStringLiteral("Phosphor"));
    createHierarchyDock();
    createMenus();
    restoreSettings();

    connect(&session, &Session::sourceAdded, this, [this](SourceId id) {
        revealHierarchy();
        showLoading(id);
    });
    connect(&session, &Session::sourceReady, this, &MainWindow::handleReady);
    connect(&session, &Session::sourceFailed, this, [this](SourceId id, const QString& error) {
        hideLoading(id);
        reportFailure(id, error);
    });
    connect(&session, &Session::sourceRemoved, this, [this](SourceId id) {
        hideLoading(id);
        renderer_->removeSource(id);
    });
    adoptExistingSources();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::createMenus() {
    auto* file = menuBar()->addMenu(tr("&File"));

    auto* open = file->addAction(tr("&Open…"));
    open->setShortcut(Shortcuts::kOpen);
    connect(open, &QAction::triggered, this, &MainWindow::openDumps);

    file->addSeparator();

    auto* exit = file->addAction(tr("E&xit"));
    exit->setShortcut(Shortcuts::kExit);
    connect(exit, &QAction::triggered, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(hierarchyDock_->toggleViewAction());
}

void MainWindow::createHierarchyDock() {
    model_ = new HierarchyModel{*session_, this};

    hierarchyDock_ = new QDockWidget{tr("Hierarchy"), this};
    // Без имени saveState не может сохранить положение дока.
    hierarchyDock_->setObjectName(QStringLiteral("hierarchyDock"));

    tree_ = new QTreeView{hierarchyDock_};
    tree_->setModel(model_);
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(true);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setDragEnabled(true);
    tree_->setDragDropMode(QAbstractItemView::DragOnly);
    connect(tree_, &QTreeView::doubleClicked, this, &MainWindow::addSelectedSignals);

    // Действие привязано к дереву, а не к окну: Enter в других виджетах значит своё.
    auto* add = new QAction{tr("Add to Waveform"), tree_};
    add->setShortcuts({Shortcuts::kAddSignals, Shortcuts::kAddSignalsKeypad});
    add->setShortcutContext(Qt::WidgetShortcut);
    tree_->addAction(add);
    connect(add, &QAction::triggered, this, &MainWindow::addSelectedSignals);

    hierarchyDock_->setWidget(tree_);
    addDockWidget(Qt::LeftDockWidgetArea, hierarchyDock_);
}

void MainWindow::openDumps() {
    const auto files = QFileDialog::getOpenFileNames(this, tr("Open Dumps"), QString{}, tr("VCD files (*.vcd)"));
    for (const auto& file : files)
        session_->openDump(std::filesystem::path{file.toStdU16String()});
}

void MainWindow::addSelectedSignals() {
    std::vector<Row> rows;
    for (const auto& index : tree_->selectionModel()->selectedRows()) {
        if (auto ref = model_->signalRef(index))
            rows.push_back(Row{std::move(*ref), RowFormat{}});
    }
    view_->insertRows(view_->rows().size(), std::move(rows));
}

void MainWindow::adoptExistingSources() {
    const auto ids = session_->sourceIds();
    if (!ids.empty())
        revealHierarchy();
    bool anyReady = false;
    for (const auto id : ids) {
        const auto state = session_->info(id)->state;
        if (state == SourceState::LOADING) {
            showLoading(id);
        }
        else if (state == SourceState::READY) {
            attachToRenderer(id);
            anyReady = true;
        }
    }
    if (anyReady)
        fitWindowToSources();
}

void MainWindow::revealHierarchy() {
    hierarchyDock_->show();
    hierarchyDock_->raise();
}

void MainWindow::showLoading(SourceId id) {
    const auto* info = session_->info(id);
    if (info == nullptr)
        return;
    auto* box = new QWidget{statusBar()};
    auto* layout = new QHBoxLayout{box};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel{info->label, box});
    auto* progress = new QProgressBar{box};
    // Ход индексации неизвестен, поэтому индикатор неопределённый.
    progress->setRange(0, 0);
    progress->setMaximumWidth(kProgressWidth);
    layout->addWidget(progress);
    auto* cancel = new QPushButton{tr("Cancel"), box};
    connect(cancel, &QPushButton::clicked, this, [this, id] { session_->cancelLoad(id); });
    layout->addWidget(cancel);
    statusBar()->addWidget(box);
    loading_[id] = box;
}

void MainWindow::hideLoading(SourceId id) {
    const auto it = loading_.find(id);
    if (it == loading_.end())
        return;
    statusBar()->removeWidget(it->second);
    // Отложенно: удаление может прийти из обработчика нажатия кнопки этого же виджета.
    it->second->deleteLater();
    loading_.erase(it);
}

void MainWindow::handleReady(SourceId id) {
    hideLoading(id);
    const auto ids = session_->sourceIds();
    const bool firstReady = std::ranges::none_of(ids, [&](SourceId other) {
        const auto* info = session_->info(other);
        return other != id && info != nullptr && info->state == SourceState::READY;
    });
    attachToRenderer(id);
    if (firstReady)
        fitWindowToSources();
}

void MainWindow::attachToRenderer(SourceId id) {
    const auto axis = session_->axis(id);
    if (!axis)
        return;
    renderer_->addSource(id, session_->takeRenderDatabase(id), *axis);
}

void MainWindow::fitWindowToSources() {
    if (const auto range = session_->totalRange())
        view_->setWindow(*range);
}

void MainWindow::reportFailure(SourceId id, const QString& error) {
    const auto* info = session_->info(id);
    const auto label = info != nullptr ? info->label : QString{};
    // Окно сообщения крутит свой цикл событий; из обработчика сигнала сессии его запускать
    // нельзя, иначе сессия получит новые события посреди своего вызова.
    QMetaObject::invokeMethod(
            this,
            [this, label, error] {
                QMessageBox::warning(this, tr("Cannot Open Dump"), tr("Cannot open %1:\n%2").arg(label, error));
            },
            Qt::QueuedConnection);
}

void MainWindow::restoreSettings() {
    const QSettings settings{kSettingsOrganization, kSettingsApplication};
    if (!restoreGeometry(settings.value(kGeometryKey).toByteArray()))
        resize(kDefaultWidth, kDefaultHeight);
    restoreState(settings.value(kStateKey).toByteArray());
}

void MainWindow::saveSettings() const {
    QSettings settings{kSettingsOrganization, kSettingsApplication};
    settings.setValue(kGeometryKey, saveGeometry());
    settings.setValue(kStateKey, saveState());
}

}  // namespace Phosphor
