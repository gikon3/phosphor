#include <gtest/gtest.h>

#include <QAction>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDropEvent>
#include <QImage>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointF>
#include <QScrollBar>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QWheelEvent>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "app/shortcuts.hpp"
#include "app/signal_mime.hpp"
#include "app/wave_canvas.hpp"
#include "app/wave_panel.hpp"
#include "core/digital_frame.hpp"
#include "core/error.hpp"
#include "core/renderer.hpp"
#include "core/session.hpp"
#include "core/store_cache.hpp"
#include "core/time_axis.hpp"
#include "core/time_mapper.hpp"
#include "core/view.hpp"
#include "support/temp_files.hpp"

namespace {

constexpr int kWaitMs = 5000;
constexpr int kPanelWidth = 900;
constexpr int kPanelHeight = 400;
constexpr int kWheelStep = 120;

// Сессия с маленьким тестовым VCD: копия БД отдана рендереру, окно вида — весь дамп. Каталог
// объявлен первым, потому что store читается лениво и должен пережить сессию и рендерер.
class Workspace {
public:
    Workspace() {
        QSignalSpy ready{&session, &Phosphor::Session::sourceReady};
        id = session.openDump(Phosphor::Test::writeFile(tmp.path() / "small.vcd", Phosphor::Test::kSmallVcd));
        if (!ready.wait(kWaitMs))
            throw Phosphor::Error{"test dump did not load"};
        axis = session.axis(id);
        renderer.addSource(id, session.takeRenderDatabase(id), *axis);
        view.setWindow(*session.totalRange());
    }

    [[nodiscard]] Phosphor::GlobalTime at(WaSafe::TimeStamp t) const { return axis->toGlobal(t); }

    void addRows(const std::vector<std::string>& paths) {
        std::vector<Phosphor::Row> rows;
        rows.reserve(paths.size());
        for (const auto& path : paths)
            rows.push_back(Phosphor::Row{Phosphor::SignalRef{id, path}, Phosphor::RowFormat{}});
        view.insertRows(view.rows().size(), std::move(rows));
    }

public:
    Phosphor::Test::TempDir tmp;
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    Phosphor::View view;
    Phosphor::Renderer renderer;
    Phosphor::SourceId id = 0;
    std::optional<Phosphor::TimeAxis> axis;
};

Phosphor::WaveCanvas* canvasOf(const Phosphor::WavePanel& panel) {
    return panel.findChild<Phosphor::WaveCanvas*>();
}

QWidget* namesOf(const Phosphor::WavePanel& panel) {
    return panel.findChild<QWidget*>(QStringLiteral("waveNames"));
}

QScrollBar* scrollBar(const Phosphor::WavePanel& panel, Qt::Orientation orientation) {
    for (auto* bar : panel.findChildren<QScrollBar*>()) {
        if (bar->orientation() == orientation)
            return bar;
    }
    return nullptr;
}

QAction* actionFor(const Phosphor::WavePanel& panel, const QKeySequence& shortcut) {
    for (auto* action : panel.actions()) {
        if (action->shortcuts().contains(shortcut))
            return action;
    }
    return nullptr;
}

// Середина строки row в координатах колонки или холста: строки идут под линейкой.
double rowCenterY(std::size_t row) {
    return Phosphor::WaveCanvas::kRulerHeight + static_cast<int>(row) * Phosphor::WaveCanvas::kRowHeight +
            Phosphor::WaveCanvas::kRowHeight / 2.0;
}

void sendWheel(QWidget* target, QPointF pos, int delta, Qt::KeyboardModifiers modifiers) {
    QWheelEvent wheel{pos, target->mapToGlobal(pos), QPoint{}, QPoint{0, delta}, Qt::NoButton, modifiers,
            Qt::NoScrollPhase, false};
    QCoreApplication::sendEvent(target, &wheel);
}

void sendMouse(QWidget* target, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons,
        Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event{type, pos, target->mapToGlobal(pos), button, buttons, modifiers};
    QCoreApplication::sendEvent(target, &event);
}

void click(QWidget* target, QPointF pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    sendMouse(target, QEvent::MouseButtonPress, pos, button, button, modifiers);
    sendMouse(target, QEvent::MouseButtonRelease, pos, button, Qt::NoButton, modifiers);
}

std::vector<std::string> rowPaths(const Phosphor::View& view) {
    std::vector<std::string> paths;
    for (const auto& row : view.rows())
        paths.push_back(row.signal.path);
    return paths;
}

// Ждёт, пока кадры перестанут приходить, и возвращает поколение последнего (0, если кадров не было).
std::uint64_t lastGenerationWhenQuiet(QSignalSpy& frames) {
    constexpr int kQuietMs = 200;
    constexpr int kMaxRounds = 25;
    for (int round = 0; round < kMaxRounds && frames.wait(kQuietMs); ++round) {
    }
    return frames.isEmpty() ? 0 : frames.last().at(0).value<Phosphor::FrameResult>().generation;
}

Phosphor::GlobalTime span(Phosphor::TimeWindow w) {
    return w.end - w.begin;
}

Phosphor::GlobalTime middle(Phosphor::TimeWindow w) {
    return w.begin + span(w) / 2;
}

}  // namespace

TEST(WavePanel, AddsRowsAndShowsValues) {
    Workspace ws;
    const QSignalSpy frames{&ws.renderer, &Phosphor::Renderer::frameReady};
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();

    ws.addRows({"top.clk", "top.data"});
    ws.view.setCursorA(ws.at(12));

    EXPECT_TRUE(QTest::qWaitFor([&] { return panel.valueText(1).contains("a5"); }, kWaitMs))
            << panel.valueText(1).toStdString();
    EXPECT_TRUE(panel.nameText(1).contains("top.data"));
    EXPECT_FALSE(panel.nameText(1).contains("not in dump"));
    // Рендерер отбрасывает запрос с неувеличенным поколением, поэтому кадры идут по возрастанию.
    ASSERT_GT(frames.count(), 0);
    std::uint64_t previous = 0;
    for (const auto& args : frames) {
        const auto generation = args.at(0).value<Phosphor::FrameResult>().generation;
        EXPECT_GT(generation, previous);
        previous = generation;
    }
}

TEST(WavePanel, MissingSignalMarked) {
    Workspace ws;
    const Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};

    ws.view.insertRows(0, {Phosphor::Row{Phosphor::SignalRef{ws.id, "top.nope"}, Phosphor::RowFormat{}}});

    EXPECT_TRUE(panel.nameText(0).contains("not in dump")) << panel.nameText(0).toStdString();
}

TEST(WavePanel, RemovedSourceMarksRowsMissing) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    ws.addRows({"top.data"});
    ASSERT_FALSE(panel.nameText(0).contains("not in dump"));
    QSignalSpy frames{&ws.renderer, &Phosphor::Renderer::frameReady};
    ASSERT_TRUE(frames.wait(kWaitMs));
    frames.clear();

    ws.session.removeSource(ws.id);

    EXPECT_TRUE(panel.nameText(0).contains("not in dump"));
    EXPECT_TRUE(frames.wait(kWaitMs));
}

// Строка, добавленная до готовности источника, разрешается, когда источник готов.
TEST(WavePanel, RowsResolveWhenSourceBecomesReady) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    QSignalSpy ready{&ws.session, &Phosphor::Session::sourceReady};
    const auto second =
            ws.session.openDump(Phosphor::Test::writeFile(ws.tmp.path() / "second.vcd", Phosphor::Test::kSmallVcd));
    ws.view.insertRows(0, {Phosphor::Row{Phosphor::SignalRef{second, "top.data"}, Phosphor::RowFormat{}}});
    EXPECT_TRUE(panel.nameText(0).contains("loading")) << panel.nameText(0).toStdString();
    EXPECT_FALSE(panel.nameText(0).contains("not in dump"));

    ASSERT_TRUE(ready.wait(kWaitMs));
    ws.renderer.addSource(second, ws.session.takeRenderDatabase(second), *ws.session.axis(second));
    ws.view.setCursorA(ws.at(12));

    EXPECT_TRUE(QTest::qWaitFor([&] { return !panel.nameText(0).contains("loading"); }, kWaitMs));
    EXPECT_FALSE(panel.nameText(0).contains("not in dump"));
    EXPECT_TRUE(QTest::qWaitFor([&] { return panel.valueText(0).contains("a5"); }, kWaitMs));
}

TEST(WavePanel, FailedSourceRowsMarkedMissing) {
    Workspace ws;
    const Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    QSignalSpy failed{&ws.session, &Phosphor::Session::sourceFailed};
    const auto broken =
            ws.session.openDump(Phosphor::Test::writeFile(ws.tmp.path() / "broken.vcd", Phosphor::Test::kBrokenVcd));
    ws.view.insertRows(0, {Phosphor::Row{Phosphor::SignalRef{broken, "top.a"}, Phosphor::RowFormat{}}});

    ASSERT_TRUE(failed.wait(kWaitMs));

    EXPECT_TRUE(panel.nameText(0).contains("not in dump")) << panel.nameText(0).toStdString();
}

// Имя показывает дамп, только когда дампов несколько; метка появляется уже при открытии второго.
TEST(WavePanel, NamesCarrySourceLabelWithSeveralSources) {
    Workspace ws;
    const Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    ws.addRows({"top.data"});
    EXPECT_EQ(panel.nameText(0), "top.data");

    QSignalSpy ready{&ws.session, &Phosphor::Session::sourceReady};
    const auto second =
            ws.session.openDump(Phosphor::Test::writeFile(ws.tmp.path() / "second.vcd", Phosphor::Test::kSmallVcd));
    EXPECT_EQ(panel.nameText(0), "top.data [small.vcd]");

    ws.view.insertRows(1, {Phosphor::Row{Phosphor::SignalRef{second, "top.data"}, Phosphor::RowFormat{}}});
    ASSERT_TRUE(ready.wait(kWaitMs));
    EXPECT_TRUE(QTest::qWaitFor([&] { return panel.nameText(1) == "top.data [second.vcd]"; }, kWaitMs))
            << panel.nameText(1).toStdString();
    EXPECT_EQ(panel.nameText(0), "top.data [small.vcd]");
}

TEST(WavePanel, CtrlWheelZooms) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    auto* canvas = canvasOf(panel);
    ASSERT_NE(canvas, nullptr);
    ASSERT_GT(canvas->width(), 0);
    const auto before = ws.view.window();

    sendWheel(canvas, QPointF{canvas->width() / 2.0, canvas->height() / 2.0}, kWheelStep, Qt::ControlModifier);

    const auto after = ws.view.window();
    EXPECT_LT(span(after), span(before));
    EXPECT_LE(std::abs(middle(after) - middle(before)), span(before) / 100);
}

TEST(WavePanel, ShiftWheelScrollsTenPercent) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    auto* canvas = canvasOf(panel);
    ASSERT_NE(canvas, nullptr);
    const auto before = ws.view.window();

    // Колесо «от себя» — к более позднему времени.
    sendWheel(canvas, QPointF{10, 10}, -kWheelStep, Qt::ShiftModifier);

    const auto after = ws.view.window();
    EXPECT_EQ(span(after), span(before));
    EXPECT_NEAR(static_cast<double>(after.begin - before.begin), span(before) / 10.0, 1.0);
}

TEST(WavePanel, PlainWheelScrollsRows) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    ws.addRows(std::vector<std::string>(40, "top.clk"));
    auto* canvas = canvasOf(panel);
    auto* vertical = scrollBar(panel, Qt::Vertical);
    ASSERT_NE(canvas, nullptr);
    ASSERT_NE(vertical, nullptr);
    const auto window = ws.view.window();
    ASSERT_EQ(vertical->value(), 0);

    sendWheel(canvas, QPointF{10, 50}, -kWheelStep, Qt::NoModifier);

    EXPECT_GT(vertical->value(), 0);
    EXPECT_EQ(ws.view.window(), window);
}

// Событие сенсорной панели: смещение в пикселях вместе с углом, который к нему добавляет платформа.
// Событие без угла полоса прокрутки Qt не обрабатывает, а прокрутку панель оставляет ей.
TEST(WavePanel, TouchpadWheelOnNamesScrollsRows) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    ws.addRows(std::vector<std::string>(40, "top.clk"));
    auto* names = namesOf(panel);
    auto* vertical = scrollBar(panel, Qt::Vertical);
    ASSERT_NE(names, nullptr);
    ASSERT_NE(vertical, nullptr);
    ASSERT_EQ(vertical->value(), 0);
    const QPointF pos{10, 50};

    QWheelEvent wheel{pos, names->mapToGlobal(pos), QPoint{0, -60}, QPoint{0, -kWheelStep}, Qt::NoButton,
            Qt::NoModifier, Qt::ScrollUpdate, false};
    QCoreApplication::sendEvent(names, &wheel);

    EXPECT_GT(vertical->value(), 0);
}

TEST(WavePanel, ClicksSetCursors) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    auto* canvas = canvasOf(panel);
    ASSERT_NE(canvas, nullptr);
    const auto window = ws.view.window();
    const int width = canvas->width();
    const auto timeAt = [&](double x) {
        return Phosphor::pixelToTime(window, width, x);
    };

    click(canvas, QPointF{100, 30}, Qt::LeftButton);
    EXPECT_EQ(ws.view.cursorA(), timeAt(100));
    EXPECT_FALSE(ws.view.cursorB().has_value());

    click(canvas, QPointF{200, 30}, Qt::MiddleButton);
    EXPECT_EQ(ws.view.cursorB(), timeAt(200));

    click(canvas, QPointF{300, 30}, Qt::LeftButton, Qt::ShiftModifier);
    EXPECT_EQ(ws.view.cursorB(), timeAt(300));
    EXPECT_EQ(ws.view.cursorA(), timeAt(100));
}

namespace {

// Панель с маленьким дампом на экране; холст и его геометрия для действий мышью.
class ShownPanel {
public:
    ShownPanel() {
        panel.resize(kPanelWidth, kPanelHeight);
        panel.show();
        if (canvas == nullptr)
            throw Phosphor::Error{"wave panel has no canvas"};
    }

    [[nodiscard]] double xOf(Phosphor::GlobalTime t) const {
        return Phosphor::timeToPixel(ws.view.window(), canvas->width(), t);
    }

    [[nodiscard]] Phosphor::GlobalTime timeAt(double x) const {
        return Phosphor::pixelToTime(ws.view.window(), canvas->width(), x);
    }

    void drag(double from, double to, Qt::MouseButton button, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const {
        sendMouse(canvas, QEvent::MouseButtonPress, QPointF{from, 30}, button, button, modifiers);
        sendMouse(canvas, QEvent::MouseMove, QPointF{to, 30}, Qt::NoButton, button, modifiers);
        sendMouse(canvas, QEvent::MouseButtonRelease, QPointF{to, 30}, button, Qt::NoButton, modifiers);
    }

public:
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    Phosphor::WaveCanvas* canvas = canvasOf(panel);
};

}  // namespace

// Левая кнопка тянет только A, даже если рядом B.
TEST(WavePanel, LeftDragMovesOnlyCursorA) {
    ShownPanel p;
    p.ws.view.setCursorA(p.ws.at(5));
    p.ws.view.setCursorB(p.ws.at(15));
    const auto a = p.xOf(*p.ws.view.cursorA());
    const auto b = p.xOf(*p.ws.view.cursorB());

    p.drag(a + 3, 150, Qt::LeftButton);
    EXPECT_EQ(p.ws.view.cursorA(), p.timeAt(150));
    EXPECT_EQ(p.ws.view.cursorB(), p.ws.at(15));

    p.drag(b + 2, 300, Qt::LeftButton);
    EXPECT_EQ(p.ws.view.cursorA(), p.timeAt(300));
    EXPECT_EQ(p.ws.view.cursorB(), p.ws.at(15));
}

// Средняя кнопка и Shift+левая тянут только B, даже если рядом A.
TEST(WavePanel, MiddleDragMovesOnlyCursorB) {
    ShownPanel p;
    p.ws.view.setCursorA(p.ws.at(5));
    const auto a = p.xOf(*p.ws.view.cursorA());

    p.drag(a + 2, 150, Qt::MiddleButton);
    EXPECT_EQ(p.ws.view.cursorA(), p.ws.at(5));
    EXPECT_EQ(p.ws.view.cursorB(), p.timeAt(150));
    ASSERT_TRUE(p.ws.view.cursorB().has_value());

    p.drag(p.xOf(*p.ws.view.cursorB()) + 2, 250, Qt::LeftButton, Qt::ShiftModifier);
    EXPECT_EQ(p.ws.view.cursorA(), p.ws.at(5));
    EXPECT_EQ(p.ws.view.cursorB(), p.timeAt(250));
}

// Повторный щелчок по B убирает его; A не убирается ничем.
TEST(WavePanel, ClickOnCursorBRemovesIt) {
    ShownPanel p;
    p.ws.view.setCursorA(p.ws.at(5));
    p.ws.view.setCursorB(p.ws.at(15));

    click(p.canvas, QPointF{p.xOf(*p.ws.view.cursorB()) + 2, 30}, Qt::MiddleButton);
    EXPECT_FALSE(p.ws.view.cursorB().has_value());
    EXPECT_EQ(p.ws.view.cursorA(), p.ws.at(5));

    click(p.canvas, QPointF{200, 30}, Qt::MiddleButton);
    EXPECT_EQ(p.ws.view.cursorB(), p.timeAt(200));

    click(p.canvas, QPointF{201, 30}, Qt::LeftButton, Qt::ShiftModifier);
    EXPECT_FALSE(p.ws.view.cursorB().has_value());
    EXPECT_EQ(p.ws.view.cursorA(), p.ws.at(5));

    click(p.canvas, QPointF{p.xOf(*p.ws.view.cursorA()) + 1, 30}, Qt::LeftButton);
    EXPECT_EQ(p.ws.view.cursorA(), p.ws.at(5));
}

// Окно не уходит левее начала данных: ни прокруткой, ни полосой, ни отдалением у начала.
TEST(WavePanel, WindowStopsAtDataStart) {
    ShownPanel p;
    const auto total = *p.ws.session.totalRange();
    const Phosphor::TimeWindow first{total.begin, middle(total)};
    p.ws.view.setWindow(first);

    sendWheel(p.canvas, QPointF{10, 50}, kWheelStep, Qt::ShiftModifier);
    EXPECT_EQ(p.ws.view.window(), first);

    p.ws.view.setWindow({total.begin + p.ws.at(1), middle(total) + p.ws.at(1)});
    auto* horizontal = scrollBar(p.panel, Qt::Horizontal);
    ASSERT_NE(horizontal, nullptr);
    horizontal->triggerAction(QAbstractSlider::SliderPageStepSub);
    EXPECT_EQ(p.ws.view.window(), first);

    sendWheel(p.canvas, QPointF{p.canvas->width() / 2.0, 50}, -kWheelStep, Qt::ControlModifier);
    EXPECT_EQ(p.ws.view.window().begin, total.begin);
    EXPECT_GT(span(p.ws.view.window()), span(first));

    p.ws.view.setWindow(first);
    auto* zoomOut = actionFor(p.panel, Phosphor::Shortcuts::kZoomOut);
    ASSERT_NE(zoomOut, nullptr);
    zoomOut->trigger();
    EXPECT_EQ(p.ws.view.window().begin, total.begin);
    EXPECT_GT(span(p.ws.view.window()), span(first));
}

TEST(WavePanel, ZoomActions) {
    Workspace ws;
    const Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    const auto total = *ws.session.totalRange();
    const Phosphor::TimeWindow part{ws.at(4), ws.at(14)};
    ws.view.setWindow(part);

    auto* zoomIn = actionFor(panel, Phosphor::Shortcuts::kZoomIn);
    auto* zoomOut = actionFor(panel, Phosphor::Shortcuts::kZoomOut);
    auto* zoomFit = actionFor(panel, Phosphor::Shortcuts::kZoomFit);
    ASSERT_NE(zoomIn, nullptr);
    ASSERT_NE(zoomOut, nullptr);
    ASSERT_NE(zoomFit, nullptr);

    zoomIn->trigger();
    EXPECT_LT(span(ws.view.window()), span(part));
    EXPECT_LE(std::abs(middle(ws.view.window()) - middle(part)), 1);

    zoomOut->trigger();
    EXPECT_LE(std::abs(span(ws.view.window()) - span(part)), 1);

    zoomFit->trigger();
    EXPECT_EQ(ws.view.window(), total);
}

TEST(WavePanel, HorizontalScrollBarMovesWindow) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    const auto total = *ws.session.totalRange();
    const Phosphor::TimeWindow firstHalf{total.begin, middle(total)};
    ws.view.setWindow(firstHalf);
    auto* horizontal = scrollBar(panel, Qt::Horizontal);
    ASSERT_NE(horizontal, nullptr);
    EXPECT_EQ(horizontal->value(), 0);
    ASSERT_GT(horizontal->maximum(), 0);

    horizontal->setValue(horizontal->maximum());

    const auto moved = ws.view.window();
    EXPECT_EQ(span(moved), span(firstHalf));
    EXPECT_LE(std::abs(moved.end - total.end), span(total) / 100);
}

// Шаги полосы двигают окно на долю его ширины, даже когда окно много уже единицы полосы.
TEST(WavePanel, HorizontalStepsMoveByWindowFraction) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    const Phosphor::TimeWindow narrow{ws.at(5), ws.at(5) + 10};
    ws.view.setWindow(narrow);
    auto* horizontal = scrollBar(panel, Qt::Horizontal);
    ASSERT_NE(horizontal, nullptr);

    horizontal->triggerAction(QAbstractSlider::SliderSingleStepAdd);
    EXPECT_EQ(ws.view.window(), (Phosphor::TimeWindow{narrow.begin + 1, narrow.end + 1}));

    horizontal->triggerAction(QAbstractSlider::SliderPageStepSub);
    EXPECT_EQ(ws.view.window(), (Phosphor::TimeWindow{narrow.begin - 9, narrow.end - 9}));
}

// Несколько изменений подряд дают один запрос кадра.
TEST(WavePanel, RemovingRowsRequestsOneFrame) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    QSignalSpy frames{&ws.renderer, &Phosphor::Renderer::frameReady};
    ws.addRows(std::vector<std::string>(40, "top.clk"));
    auto* vertical = scrollBar(panel, Qt::Vertical);
    ASSERT_NE(vertical, nullptr);
    // Прокрутка в конец: удаление строк сдвинет и позицию прокрутки.
    vertical->setValue(vertical->maximum());
    const auto before = lastGenerationWhenQuiet(frames);
    ASSERT_GT(before, 0U);
    frames.clear();

    const std::vector<std::size_t> first{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    ws.view.removeRows(first);

    std::ignore = lastGenerationWhenQuiet(frames);
    ASSERT_EQ(frames.count(), 1);
    EXPECT_EQ(frames.at(0).at(0).value<Phosphor::FrameResult>().generation, before + 1);
}

TEST(WavePanel, WindowAndCursorChangesRequestOneFrame) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    QSignalSpy frames{&ws.renderer, &Phosphor::Renderer::frameReady};
    ws.addRows({"top.clk"});
    const auto before = lastGenerationWhenQuiet(frames);
    ASSERT_GT(before, 0U);
    frames.clear();

    ws.view.setWindow({ws.at(2), ws.at(18)});
    ws.view.setCursorA(ws.at(7));

    std::ignore = lastGenerationWhenQuiet(frames);
    ASSERT_EQ(frames.count(), 1);
    EXPECT_EQ(frames.at(0).at(0).value<Phosphor::FrameResult>().generation, before + 1);
}

// До прихода нового кадра строки показывают значения своих сигналов, а не тех, что стояли на их месте.
TEST(WavePanel, ShownValuesFollowMovedRows) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    ws.addRows({"top.clk", "top.data", "top.vout"});
    ws.view.setCursorA(ws.at(12));
    ASSERT_TRUE(QTest::qWaitFor([&] { return panel.valueText(1) == "a5" && panel.valueText(2) == "0.5"; }, kWaitMs));
    const auto clk = panel.valueText(0);
    ASSERT_FALSE(clk.isEmpty());

    ws.view.moveRow(0, 1);

    EXPECT_EQ(panel.valueText(0), "a5");
    EXPECT_EQ(panel.valueText(1), clk);
    EXPECT_EQ(panel.valueText(2), "0.5");
}

TEST(WavePanel, ShownValuesFollowInsertedRows) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    ws.addRows({"top.clk", "top.data"});
    ws.view.setCursorA(ws.at(12));
    ASSERT_TRUE(QTest::qWaitFor([&] { return panel.valueText(1) == "a5"; }, kWaitMs));
    const auto clk = panel.valueText(0);

    ws.view.insertRows(0, {Phosphor::Row{Phosphor::SignalRef{ws.id, "top.vout"}, Phosphor::RowFormat{}}});

    EXPECT_EQ(panel.valueText(0), "");
    EXPECT_EQ(panel.valueText(1), clk);
    EXPECT_EQ(panel.valueText(2), "a5");
}

TEST(WavePanel, DeleteRemovesSelectedRows) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    ws.addRows({"top.clk", "top.data", "top.vout"});
    auto* names = namesOf(panel);
    ASSERT_NE(names, nullptr);
    auto* remove = actionFor(panel, Phosphor::Shortcuts::kRemoveRows);
    ASSERT_NE(remove, nullptr);

    click(names, QPointF{10, rowCenterY(1)}, Qt::LeftButton);
    remove->trigger();

    EXPECT_EQ(rowPaths(ws.view), (std::vector<std::string>{"top.clk", "top.vout"}));
}

TEST(WavePanel, NameDragMovesRow) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    ws.addRows({"top.clk", "top.data", "top.vout"});
    auto* names = namesOf(panel);
    ASSERT_NE(names, nullptr);
    // Ниже середины последней строки — вставка после неё.
    const QPointF target{10, rowCenterY(2) + 5};

    sendMouse(names, QEvent::MouseButtonPress, QPointF{10, rowCenterY(0)}, Qt::LeftButton, Qt::LeftButton);
    sendMouse(names, QEvent::MouseMove, QPointF{10, rowCenterY(1)}, Qt::NoButton, Qt::LeftButton);
    sendMouse(names, QEvent::MouseMove, target, Qt::NoButton, Qt::LeftButton);
    sendMouse(names, QEvent::MouseButtonRelease, target, Qt::LeftButton, Qt::NoButton);

    EXPECT_EQ(rowPaths(ws.view), (std::vector<std::string>{"top.data", "top.vout", "top.clk"}));
}

TEST(WavePanel, FormatMenuSetsFormat) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    ws.addRows({"top.clk", "top.data"});
    auto* names = namesOf(panel);
    ASSERT_NE(names, nullptr);

    const QPoint pos{10, static_cast<int>(rowCenterY(1))};
    QContextMenuEvent event{QContextMenuEvent::Mouse, pos, names->mapToGlobal(pos)};
    QCoreApplication::sendEvent(names, &event);

    auto* menu = panel.findChild<QMenu*>();
    ASSERT_NE(menu, nullptr);
    QAction* signedDec = nullptr;
    for (auto* action : menu->actions()) {
        if (action->text() == "Signed Dec")
            signedDec = action;
    }
    ASSERT_NE(signedDec, nullptr);
    signedDec->trigger();
    menu->close();

    EXPECT_EQ(ws.view.rows()[1].format, (Phosphor::RowFormat{WaSafe::Radix::DEC, true}));
    EXPECT_EQ(ws.view.rows()[0].format, Phosphor::RowFormat{});
}

// Номер источника действителен только в своём процессе: перетаскивание без источника в этом
// процессе пришло извне и не принимается.
TEST(WavePanel, ForeignDropIgnored) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.resize(kPanelWidth, kPanelHeight);
    panel.show();
    const std::vector<Phosphor::SignalRef> refs{{ws.id, "top.clk"}};
    QMimeData mime;
    mime.setData(Phosphor::kSignalRefsMimeType, Phosphor::encodeSignalRefs(refs));
    const QPointF pos{kPanelWidth / 2.0, kPanelHeight / 2.0};

    QDragEnterEvent enter{pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
    QCoreApplication::sendEvent(&panel, &enter);
    QDropEvent drop{pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier};
    QCoreApplication::sendEvent(&panel, &drop);

    EXPECT_FALSE(drop.isAccepted());
    EXPECT_TRUE(ws.view.rows().empty());
}

TEST(WavePanel, HiddenCanvasNoCrash) {
    Workspace ws;
    Phosphor::WavePanel panel{ws.session, ws.view, ws.renderer};
    panel.show();
    ws.addRows({"top.clk", "top.data"});
    QSignalSpy frames{&ws.renderer, &Phosphor::Renderer::frameReady};

    panel.resize(0, 0);
    panel.requestFrame();
    EXPECT_TRUE(frames.wait(kWaitMs));
    QCoreApplication::processEvents();
    std::ignore = panel.grab();

    ws.view.setWindow({ws.at(3), ws.at(3) + 2});
    QCoreApplication::processEvents();
    std::ignore = panel.grab();
}

// Промежуточный кадр на глубоком зуме растягивается на триллионы пикселей; заливка всё равно видна.
TEST(WaveCanvas, InterimDenseSpanSurvivesHugeScale) {
    Phosphor::View view;
    Phosphor::WaveCanvas canvas{view};
    constexpr int kWidth = 500;
    canvas.resize(kWidth, Phosphor::WaveCanvas::kRulerHeight + 3 * Phosphor::WaveCanvas::kRowHeight);
    constexpr Phosphor::GlobalTime kFrameSpan = 1'000'000'000'000;
    Phosphor::FrameResult frame;
    frame.generation = 1;
    frame.window = {0, kFrameSpan};
    frame.widthPx = 100;
    Phosphor::RowResult row;
    row.frame.spans = {Phosphor::DigitalSpan{0, 100, Phosphor::SpanKind::DENSE, {}}};
    frame.rows = {row};
    canvas.setFrame(std::make_shared<const Phosphor::FrameResult>(std::move(frame)), {std::optional<std::size_t>{0}});
    view.setWindow({kFrameSpan / 2, kFrameSpan / 2 + 100});

    const auto image = canvas.grab().toImage();

    const int x = kWidth / 2;
    EXPECT_NE(image.pixelColor(x, static_cast<int>(rowCenterY(0))),
            image.pixelColor(x, static_cast<int>(rowCenterY(2))));
}
