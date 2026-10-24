#include <gtest/gtest.h>

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <tuple>

#include "app/main_window.hpp"
#include "core/error.hpp"
#include "core/renderer.hpp"
#include "core/session.hpp"
#include "core/store_cache.hpp"
#include "core/view.hpp"
#include "support/temp_files.hpp"

namespace {

constexpr int kWaitMs = 5000;

qsizetype progressBars(const Phosphor::MainWindow& window) {
    // Виджеты статуса снимаются через deleteLater.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return window.findChildren<QProgressBar*>().size();
}

// Настройки окна изолированы от пользователя, но общие для тестов одного процесса.
void clearWindowSettings() {
    QSettings settings{"phosphor", "phosphor"};
    settings.clear();
    settings.sync();
}

QDockWidget* hierarchyDock(const Phosphor::MainWindow& window) {
    return window.findChild<QDockWidget*>();
}

// Оставляет в настройках док, закрытый пользователем: следующее окно восстановит его скрытым.
// Сессия должна быть пуста, иначе окно заберёт копии БД её готовых источников.
void saveClosedDock(Phosphor::Session& session, Phosphor::View& view, Phosphor::Renderer& renderer) {
    Phosphor::MainWindow window{session, view, renderer};
    window.show();
    auto* dock = hierarchyDock(window);
    ASSERT_NE(dock, nullptr);
    dock->close();
    window.close();
}

QMenu* viewMenu(const Phosphor::MainWindow& window) {
    for (const auto* action : window.menuBar()->actions()) {
        if (action->text() == "&View")
            return action->menu();
    }
    return nullptr;
}

}  // namespace

// Окно читает и пишет настройки пользователя; тесты не должны их касаться.
TEST(MainWindow, SettingsAreIsolatedFromUser) {
    EXPECT_TRUE(QStandardPaths::isTestModeEnabled());
#ifndef Q_OS_WIN
    const QSettings settings{"phosphor", "phosphor"};
    EXPECT_TRUE(settings.fileName().startsWith(QDir::tempPath())) << settings.fileName().toStdString();
#endif
}

TEST(MainWindow, FitsWindowOnFirstReady) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "small.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    Phosphor::View view;
    Phosphor::Renderer renderer;
    const Phosphor::MainWindow window{session, view, renderer};
    QSignalSpy ready{&session, &Phosphor::Session::sourceReady};

    const auto id = session.openDump(dump);
    ASSERT_TRUE(ready.wait(kWaitMs));

    const auto range = session.totalRange();
    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(view.window(), *range);
    // Копию БД для чтения значений забрал рендерер.
    EXPECT_THROW(std::ignore = session.takeRenderDatabase(id), Phosphor::Error);
}

TEST(MainWindow, LoadingIndicatorLifecycle) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "small.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    Phosphor::View view;
    Phosphor::Renderer renderer;
    const Phosphor::MainWindow window{session, view, renderer};
    QSignalSpy ready{&session, &Phosphor::Session::sourceReady};

    session.openDump(dump);
    EXPECT_EQ(progressBars(window), 1);

    ASSERT_TRUE(ready.wait(kWaitMs));
    EXPECT_EQ(progressBars(window), 0);
    clearWindowSettings();
}

// Окно, созданное над непустой сессией, подхватывает её источники так же, как новые.
TEST(MainWindow, PicksUpExistingSources) {
    clearWindowSettings();
    const Phosphor::Test::TempDir tmp;
    const auto first = Phosphor::Test::writeFile(tmp.path() / "first.vcd", Phosphor::Test::kSmallVcd);
    const auto second = Phosphor::Test::writeFile(tmp.path() / "second.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    Phosphor::View view;
    Phosphor::Renderer renderer;
    saveClosedDock(session, view, renderer);
    QSignalSpy ready{&session, &Phosphor::Session::sourceReady};
    const auto readyId = session.openDump(first);
    ASSERT_TRUE(ready.wait(kWaitMs));
    session.openDump(second);

    Phosphor::MainWindow window{session, view, renderer};
    window.show();

    const auto* dock = hierarchyDock(window);
    ASSERT_NE(dock, nullptr);
    EXPECT_TRUE(dock->isVisible());
    EXPECT_EQ(progressBars(window), 1);
    EXPECT_THROW(std::ignore = session.takeRenderDatabase(readyId), Phosphor::Error);
    const auto range = session.totalRange();
    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(view.window(), *range);

    ASSERT_TRUE(ready.wait(kWaitMs));
    EXPECT_EQ(progressBars(window), 0);
    clearWindowSettings();
}

// Док, закрытый в прошлом сеансе, восстанавливается скрытым, но открытый дамп его показывает.
TEST(MainWindow, ClosedDockReopensOnOpen) {
    clearWindowSettings();
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "small.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    Phosphor::View view;
    Phosphor::Renderer renderer;
    saveClosedDock(session, view, renderer);

    Phosphor::MainWindow window{session, view, renderer};
    window.show();
    auto* dock = hierarchyDock(window);
    ASSERT_NE(dock, nullptr);
    EXPECT_FALSE(dock->isVisible());

    session.openDump(dump);

    EXPECT_TRUE(dock->isVisible());
    clearWindowSettings();
}

TEST(MainWindow, ViewMenuTogglesDock) {
    clearWindowSettings();
    const Phosphor::Test::TempDir tmp;
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    Phosphor::View view;
    Phosphor::Renderer renderer;
    Phosphor::MainWindow window{session, view, renderer};
    window.show();
    auto* dock = hierarchyDock(window);
    ASSERT_NE(dock, nullptr);
    ASSERT_TRUE(dock->isVisible());

    auto* menu = viewMenu(window);
    ASSERT_NE(menu, nullptr);
    QAction* toggle = nullptr;
    for (auto* action : menu->actions()) {
        if (action->text() == "Hierarchy")
            toggle = action;
    }
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->isCheckable());
    EXPECT_TRUE(toggle->isChecked());

    toggle->trigger();
    EXPECT_FALSE(dock->isVisible());
    toggle->trigger();
    EXPECT_TRUE(dock->isVisible());
}
