#include <gtest/gtest.h>

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTest>
#include <string_view>
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

// Заголовок без единой метки времени: диапазон времени дампа пуст.
constexpr std::string_view kNoTimeVcd =
        "$timescale 1ns $end\n$scope module top $end\n$var wire 1 ! a $end\n$upscope $end\n$enddefinitions $end\n";

qsizetype progressBars(const Phosphor::MainWindow& window) {
    // Виджеты статуса снимаются через deleteLater.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return window.findChildren<QProgressBar*>().size();
}

// Настройки окна изолированы от пользователя, но общие для тестов одного процесса.
void clearWindowSettings() {
    QSettings settings;
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

QAction* menuAction(const QMenu& menu, const QString& text) {
    for (auto* action : menu.actions()) {
        if (action->text() == text)
            return action;
    }
    return nullptr;
}

}  // namespace

// Окно читает и пишет настройки пользователя; тесты не должны их касаться. На всех платформах
// настройки тестов — INI-файл во временном каталоге, а не реестр Windows.
TEST(MainWindow, SettingsAreIsolatedFromUser) {
    EXPECT_TRUE(QStandardPaths::isTestModeEnabled());
    const QSettings settings;
    EXPECT_EQ(settings.format(), QSettings::IniFormat);
    EXPECT_TRUE(settings.fileName().startsWith(QDir::tempPath())) << settings.fileName().toStdString();
}

// Окно пишет в настройки приложения по умолчанию: только их тесты уводят от пользователя.
TEST(MainWindow, SavesIntoDefaultSettings) {
    clearWindowSettings();
    const Phosphor::Test::TempDir tmp;
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    Phosphor::View view;
    Phosphor::Renderer renderer;
    {
        Phosphor::MainWindow window{session, view, renderer};
        window.show();
        window.close();
    }

    const QSettings settings;
    EXPECT_TRUE(settings.contains("mainWindow/geometry"));
    EXPECT_TRUE(settings.contains("mainWindow/state"));
    clearWindowSettings();
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

// Первый готовый дамп без диапазона показывать нечего: окно подгоняется под первый дамп, у
// которого диапазон есть, а выбранное после этого окно следующие дампы уже не сбивают.
TEST(MainWindow, FitsWindowWhenFirstRangeAppears) {
    const Phosphor::Test::TempDir tmp;
    const auto empty = Phosphor::Test::writeFile(tmp.path() / "empty.vcd", kNoTimeVcd);
    const auto small = Phosphor::Test::writeFile(tmp.path() / "small.vcd", Phosphor::Test::kSmallVcd);
    const auto second = Phosphor::Test::writeFile(tmp.path() / "second.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    Phosphor::View view;
    Phosphor::Renderer renderer;
    const Phosphor::MainWindow window{session, view, renderer};
    QSignalSpy ready{&session, &Phosphor::Session::sourceReady};

    session.openDump(empty);
    ASSERT_TRUE(ready.wait(kWaitMs));
    ASSERT_FALSE(session.totalRange().has_value());

    session.openDump(small);
    ASSERT_TRUE(ready.wait(kWaitMs));
    const auto range = session.totalRange();
    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(view.window(), *range);

    const Phosphor::TimeWindow chosen{range->begin + 1, range->end - 1};
    view.setWindow(chosen);
    session.openDump(second);
    ASSERT_TRUE(ready.wait(kWaitMs));
    EXPECT_EQ(view.window(), chosen);
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

// Действия масштаба и курсоров видны в меню, а не только доступны с клавиатуры.
TEST(MainWindow, ViewMenuShowsTimeActions) {
    const Phosphor::Test::TempDir tmp;
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    Phosphor::View view;
    Phosphor::Renderer renderer;
    const Phosphor::MainWindow window{session, view, renderer};

    const auto* menu = viewMenu(window);
    ASSERT_NE(menu, nullptr);
    for (const auto* text :
            {"Zoom In", "Zoom Out", "Zoom Fit", "Cursor A to Window Start", "Cursor B to Window Start"}) {
        const auto* action = menuAction(*menu, QString::fromLatin1(text));
        ASSERT_NE(action, nullptr) << text;
        EXPECT_FALSE(action->shortcut().isEmpty()) << text;
    }
}

// Клавиши масштаба действуют только в панели волн: «+» цифрового блока в поле ввода вне панели —
// это текст, а в других виджетах окна — не масштаб.
TEST(MainWindow, KeypadZoomKeysStayInPanel) {
    clearWindowSettings();
    const Phosphor::Test::TempDir tmp;
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    Phosphor::View view;
    Phosphor::Renderer renderer;
    Phosphor::MainWindow window{session, view, renderer};
    // Объявлены после окна и уничтожаются раньше него, снимаясь со статус-бара.
    QLineEdit edit{window.statusBar()};
    window.statusBar()->addWidget(&edit);
    window.show();
    window.activateWindow();
    ASSERT_TRUE(QTest::qWaitForWindowActive(&window));
    edit.setFocus();
    ASSERT_TRUE(edit.hasFocus());
    // Окно, которое масштаб заметно изменил бы в обе стороны.
    view.setWindow({1'000'000, 2'000'000});
    const auto before = view.window();

    QTest::keyClick(&edit, Qt::Key_Plus, Qt::KeypadModifier);

    EXPECT_EQ(edit.text(), "+");
    EXPECT_EQ(view.window(), before);

    // Виджет без ввода текста клавишу не перехватывает, но и тогда масштаб не меняется.
    QPushButton button{QStringLiteral("button"), window.statusBar()};
    window.statusBar()->addWidget(&button);
    button.setFocus();
    ASSERT_TRUE(button.hasFocus());
    QTest::keyClick(&button, Qt::Key_Plus, Qt::KeypadModifier);
    EXPECT_EQ(view.window(), before);
    QTest::keyClick(&button, Qt::Key_Minus, Qt::KeypadModifier);
    EXPECT_EQ(view.window(), before);
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
