#include <gtest/gtest.h>

#include <QSignalSpy>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/view.hpp"

namespace {

using Phosphor::kMinWindowSpan;
using Phosphor::Row;
using Phosphor::SignalRef;
using Phosphor::TimeWindow;
using Phosphor::View;

Row makeRow(const std::string& path) {
    return Row{SignalRef{1, path}, {}};
}

std::vector<std::string> paths(const View& v) {
    std::vector<std::string> out;
    for (const Row& r : v.rows())
        out.push_back(r.signal.path);
    return out;
}

}  // namespace

TEST(View, DefaultState) {
    const View v;
    EXPECT_TRUE(v.rows().empty());
    EXPECT_EQ(v.window(), (TimeWindow{0, kMinWindowSpan}));
    EXPECT_FALSE(v.cursorA().has_value());
    EXPECT_FALSE(v.cursorB().has_value());
}

TEST(View, InsertMoveRemove) {
    View v;
    const QSignalSpy spy{&v, &View::rowsChanged};
    v.insertRows(0, {makeRow("a"), makeRow("b"), makeRow("c")});
    v.moveRow(0, 2);
    EXPECT_EQ(paths(v), (std::vector<std::string>{"b", "c", "a"}));
    v.removeRows(std::array<std::size_t, 2>{0, 2});
    EXPECT_EQ(paths(v), (std::vector<std::string>{"c"}));
    EXPECT_EQ(spy.count(), 3);
}

TEST(View, InsertClampsPositionAndIgnoresEmpty) {
    View v;
    const QSignalSpy spy{&v, &View::rowsChanged};
    v.insertRows(0, {});
    EXPECT_EQ(spy.count(), 0);
    v.insertRows(0, {makeRow("a")});
    v.insertRows(100, {makeRow("b")});
    v.insertRows(1, {makeRow("x")});
    EXPECT_EQ(paths(v), (std::vector<std::string>{"a", "x", "b"}));
    EXPECT_EQ(spy.count(), 3);
}

TEST(View, RemoveIgnoresDuplicatesAndOutOfRange) {
    View v;
    v.insertRows(0, {makeRow("a"), makeRow("b"), makeRow("c")});
    const QSignalSpy spy{&v, &View::rowsChanged};
    v.removeRows(std::array<std::size_t, 4>{2, 0, 2, 7});
    EXPECT_EQ(paths(v), (std::vector<std::string>{"b"}));
    EXPECT_EQ(spy.count(), 1);
    v.removeRows(std::array<std::size_t, 1>{5});
    v.removeRows(std::span<const std::size_t>{});
    EXPECT_EQ(spy.count(), 1);
}

TEST(View, MoveRowEdgeCases) {
    View v;
    v.insertRows(0, {makeRow("a"), makeRow("b"), makeRow("c")});
    const QSignalSpy spy{&v, &View::rowsChanged};
    v.moveRow(3, 0);
    v.moveRow(1, 1);
    v.moveRow(2, 50);
    EXPECT_EQ(spy.count(), 0);
    v.moveRow(0, 50);
    EXPECT_EQ(paths(v), (std::vector<std::string>{"b", "c", "a"}));
    v.moveRow(2, 0);
    EXPECT_EQ(paths(v), (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_EQ(spy.count(), 2);
}

TEST(View, SetFormat) {
    View v;
    v.insertRows(0, {makeRow("a")});
    const QSignalSpy spy{&v, &View::rowsChanged};
    Phosphor::RowFormat f;
    f.isSigned = true;
    v.setFormat(5, f);
    EXPECT_EQ(spy.count(), 0);
    v.setFormat(0, f);
    EXPECT_EQ(v.rows()[0].format, f);
    EXPECT_EQ(spy.count(), 1);
    v.setFormat(0, f);
    EXPECT_EQ(spy.count(), 1);
}

TEST(View, WindowClampsMinSpan) {
    View v;
    v.setWindow({10, 10});
    EXPECT_EQ(v.window().begin, 10);
    EXPECT_EQ(v.window().end - v.window().begin, kMinWindowSpan);
    v.setWindow({20, 5});
    EXPECT_EQ(v.window(), (TimeWindow{20, 20 + kMinWindowSpan}));
}

TEST(View, WindowSaturatesAtMax) {
    View v;
    constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
    v.setWindow({kMax, kMax});
    EXPECT_EQ(v.window(), (TimeWindow{kMax - kMinWindowSpan, kMax}));
    constexpr auto kMin = std::numeric_limits<std::int64_t>::min();
    v.setWindow({kMin, kMax});
    EXPECT_EQ(v.window(), (TimeWindow{kMin, kMax}));
}

TEST(View, WindowSignalOnlyOnChange) {
    View v;
    const QSignalSpy spy{&v, &View::windowChanged};
    v.setWindow({0, kMinWindowSpan});
    EXPECT_EQ(spy.count(), 0);
    v.setWindow({0, 100});
    EXPECT_EQ(spy.count(), 1);
    v.setWindow({0, 100});
    EXPECT_EQ(spy.count(), 1);
    v.setWindow({100, 100});
    v.setWindow({100, 100 + kMinWindowSpan});
    EXPECT_EQ(spy.count(), 2);
}

TEST(View, NoSignalOnSameValue) {
    View v;
    const QSignalSpy spy{&v, &View::cursorsChanged};
    v.setCursorA(5);
    EXPECT_EQ(spy.count(), 1);
    v.setCursorA(5);
    EXPECT_EQ(spy.count(), 1);
    v.setCursorB(7);
    EXPECT_EQ(spy.count(), 2);
    EXPECT_EQ(v.cursorA(), std::optional<Phosphor::GlobalTime>{5});
    EXPECT_EQ(v.cursorB(), std::optional<Phosphor::GlobalTime>{7});
    v.setCursorA(std::nullopt);
    EXPECT_EQ(spy.count(), 3);
    v.setCursorA(std::nullopt);
    EXPECT_EQ(spy.count(), 3);
}
