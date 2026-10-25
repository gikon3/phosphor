#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "core/loader.hpp"
#include "core/renderer.hpp"
#include "core/store_cache.hpp"
#include "core/time_axis.hpp"
#include "support/temp_files.hpp"

namespace {

constexpr int kWaitMs = 5000;
constexpr Phosphor::SourceId kSource = 1;

// Тот же состав, что у маленького тестового VCD, но clk держит 1 до 30 нс.
constexpr std::string_view kHighClockVcd =
        "$timescale 1ns $end\n$scope module top $end\n$var wire 1 ! clk $end\n$upscope $end\n"
        "$enddefinitions $end\n#0\n1!\n#30\n0!\n";

// Дамп, загруженный в tmp, вместе с осью и узлами top.clk и top.data (невалидный узел, если
// сигнала нет). Каталог должен пережить БД: значения читаются из store лениво.
struct Dump {
    WaSafe::Database db;
    Phosphor::TimeAxis axis;
    WaSafe::NodeId clk;
    WaSafe::NodeId data;
};

Dump loadVcd(const Phosphor::Test::TempDir& tmp, std::string_view fileName, std::string_view vcd) {
    const Phosphor::StoreCache cache{tmp.path() / "cache"};
    auto db = Phosphor::loadDump(Phosphor::Test::writeFile(tmp.path() / fileName, vcd), cache, {});
    const auto axis = Phosphor::TimeAxis::fromScale(db.timeScale());
    const auto node = [&db](std::string_view path) {
        const auto signal = db.find(path);
        return signal ? signal->node() : WaSafe::NodeId{};
    };
    const auto clk = node("top.clk");
    const auto data = node("top.data");
    return {std::move(db), axis, clk, data};
}

// Маленький тестовый VCD.
Dump loadSmall(const Phosphor::Test::TempDir& tmp) {
    return loadVcd(tmp, "a.vcd", Phosphor::Test::kSmallVcd);
}

Phosphor::FrameRequest frameRequest(const Dump& d, std::uint64_t generation, std::vector<Phosphor::RowRequest> rows) {
    Phosphor::FrameRequest r;
    r.generation = generation;
    r.window = {d.axis.toGlobal(0), d.axis.toGlobal(25)};
    r.widthPx = 100;
    r.rows = std::move(rows);
    return r;
}

Phosphor::FrameResult frameArg(const QSignalSpy& spy, qsizetype i) {
    return spy.at(i).at(0).value<Phosphor::FrameResult>();
}

}  // namespace

TEST(Renderer, ProducesFrameAndCursorValues) {
    const Phosphor::Test::TempDir tmp;
    auto dump = loadSmall(tmp);
    Phosphor::Renderer renderer;
    QSignalSpy ready{&renderer, &Phosphor::Renderer::frameReady};
    auto request = frameRequest(dump, 1, {{kSource, dump.clk, {}}, {kSource, dump.data, {}}});
    request.cursorA = dump.axis.toGlobal(12);
    request.cursorB = dump.axis.toGlobal(16);

    renderer.addSource(kSource, std::move(dump.db), dump.axis);
    renderer.request(request);

    ASSERT_TRUE(ready.wait(kWaitMs));
    ASSERT_EQ(ready.count(), 1);
    const auto frame = frameArg(ready, 0);
    EXPECT_EQ(frame.generation, 1U);
    EXPECT_EQ(frame.window, request.window);
    EXPECT_EQ(frame.widthPx, request.widthPx);
    ASSERT_EQ(frame.rows.size(), 2U);
    const auto& clk = frame.rows[0];
    const auto& data = frame.rows[1];
    EXPECT_EQ(clk.valueA, "0");
    EXPECT_EQ(clk.valueB, "1");
    EXPECT_EQ(data.valueA, "a5");
    EXPECT_FALSE(clk.frame.spans.empty());
    EXPECT_FALSE(data.frame.spans.empty());
}

TEST(Renderer, NoCursorsEmptyValues) {
    const Phosphor::Test::TempDir tmp;
    auto dump = loadSmall(tmp);
    Phosphor::Renderer renderer;
    QSignalSpy ready{&renderer, &Phosphor::Renderer::frameReady};
    const auto request = frameRequest(dump, 1, {{kSource, dump.data, {}}});

    renderer.addSource(kSource, std::move(dump.db), dump.axis);
    renderer.request(request);

    ASSERT_TRUE(ready.wait(kWaitMs));
    const auto frame = frameArg(ready, 0);
    ASSERT_EQ(frame.rows.size(), 1U);
    EXPECT_FALSE(frame.rows[0].frame.spans.empty());
    EXPECT_TRUE(frame.rows[0].valueA.empty());
    EXPECT_TRUE(frame.rows[0].valueB.empty());
}

TEST(Renderer, AddSourceReplacesExisting) {
    const Phosphor::Test::TempDir tmp;
    auto small = loadSmall(tmp);
    auto high = loadVcd(tmp, "high.vcd", kHighClockVcd);
    Phosphor::Renderer renderer;
    QSignalSpy ready{&renderer, &Phosphor::Renderer::frameReady};
    auto request = frameRequest(high, 1, {{kSource, high.clk, {}}});
    request.cursorA = high.axis.toGlobal(12);

    renderer.addSource(kSource, std::move(small.db), small.axis);
    renderer.addSource(kSource, std::move(high.db), high.axis);
    renderer.request(request);

    ASSERT_TRUE(ready.wait(kWaitMs));
    const auto frame = frameArg(ready, 0);
    ASSERT_EQ(frame.rows.size(), 1U);
    // В маленьком дампе clk в 12 нс равен 0.
    EXPECT_EQ(frame.rows[0].valueA, "1");
}

TEST(Renderer, LatestWins) {
    const Phosphor::Test::TempDir tmp;
    auto dump = loadSmall(tmp);
    Phosphor::Renderer renderer;
    std::vector<std::uint64_t> received;
    QObject::connect(&renderer, &Phosphor::Renderer::frameReady, &renderer,
            [&received](const Phosphor::FrameResult& f) { received.push_back(f.generation); });
    // Много строк, чтобы кадр строился заметное время и новые запросы приходили посреди него.
    const std::vector<Phosphor::RowRequest> rows(50, Phosphor::RowRequest{kSource, dump.clk, {}});
    renderer.addSource(kSource, std::move(dump.db), dump.axis);

    for (std::uint64_t g = 1; g <= 20; ++g)
        renderer.request(frameRequest(dump, g, rows));
    ASSERT_TRUE(QTest::qWaitFor([&received] { return std::ranges::contains(received, 20U); }, kWaitMs));
    // Кадр 21 — барьер: всё, что поток успел отдать до него, к его приходу уже получено.
    renderer.request(frameRequest(dump, 21, {}));
    ASSERT_TRUE(QTest::qWaitFor([&received] { return std::ranges::contains(received, 21U); }, kWaitMs));

    EXPECT_TRUE(std::ranges::is_sorted(received, std::ranges::less_equal{}) &&
            std::ranges::adjacent_find(received) == received.end());
    ASSERT_GE(received.size(), 2U);
    EXPECT_EQ(received[received.size() - 2], 20U);
    EXPECT_EQ(received.back(), 21U);
}

TEST(Renderer, StaleGenerationIgnored) {
    const Phosphor::Test::TempDir tmp;
    auto dump = loadSmall(tmp);
    Phosphor::Renderer renderer;
    std::vector<std::uint64_t> received;
    QObject::connect(&renderer, &Phosphor::Renderer::frameReady, &renderer,
            [&received](const Phosphor::FrameResult& f) { received.push_back(f.generation); });
    renderer.addSource(kSource, std::move(dump.db), dump.axis);

    // Принятый устаревший запрос либо вытеснил бы 6, либо отдался бы следом за ним.
    renderer.request(frameRequest(dump, 6, {}));
    renderer.request(frameRequest(dump, 3, {}));
    ASSERT_TRUE(QTest::qWaitFor([&received] { return !received.empty(); }, kWaitMs));
    // Кадр 7 — барьер: всё, что поток успел отдать до него, к его приходу уже получено.
    renderer.request(frameRequest(dump, 7, {}));
    ASSERT_TRUE(QTest::qWaitFor([&received] { return std::ranges::contains(received, 7U); }, kWaitMs));

    EXPECT_EQ(received, (std::vector<std::uint64_t>{6, 7}));
}

TEST(Renderer, UnknownSourceEmptyRow) {
    const Phosphor::Test::TempDir tmp;
    auto dump = loadSmall(tmp);
    Phosphor::Renderer renderer;
    QSignalSpy ready{&renderer, &Phosphor::Renderer::frameReady};
    auto request = frameRequest(dump, 1, {{kSource + 1, dump.clk, {}}, {kSource, WaSafe::NodeId{}, {}}});
    request.cursorA = dump.axis.toGlobal(12);
    request.cursorB = dump.axis.toGlobal(16);

    renderer.addSource(kSource, std::move(dump.db), dump.axis);
    renderer.request(request);

    ASSERT_TRUE(ready.wait(kWaitMs));
    const auto frame = frameArg(ready, 0);
    ASSERT_EQ(frame.rows.size(), 2U);
    for (const auto& row : frame.rows) {
        EXPECT_TRUE(row.frame.spans.empty());
        EXPECT_TRUE(row.valueA.empty());
        EXPECT_TRUE(row.valueB.empty());
    }
}

TEST(Renderer, RemovedSourceEmptyRow) {
    const Phosphor::Test::TempDir tmp;
    auto dump = loadSmall(tmp);
    Phosphor::Renderer renderer;
    QSignalSpy ready{&renderer, &Phosphor::Renderer::frameReady};
    auto request = frameRequest(dump, 1, {{kSource, dump.clk, {}}});
    request.cursorA = dump.axis.toGlobal(12);

    renderer.addSource(kSource, std::move(dump.db), dump.axis);
    renderer.removeSource(kSource + 1);
    renderer.removeSource(kSource);
    renderer.request(request);

    ASSERT_TRUE(ready.wait(kWaitMs));
    const auto frame = frameArg(ready, 0);
    ASSERT_EQ(frame.rows.size(), 1U);
    EXPECT_TRUE(frame.rows[0].frame.spans.empty());
    EXPECT_TRUE(frame.rows[0].valueA.empty());
}

// Для санитайзеров: проверяет лишь, что разрушение посреди работы обходится без падений и гонок.
TEST(Renderer, DestroyedWithRequestInFlight) {
    const Phosphor::Test::TempDir tmp;
    auto dump = loadSmall(tmp);
    auto renderer = std::make_unique<Phosphor::Renderer>();
    const std::vector<Phosphor::RowRequest> rows(50, Phosphor::RowRequest{kSource, dump.clk, {}});
    renderer->addSource(kSource, std::move(dump.db), dump.axis);
    renderer->request(frameRequest(dump, 1, rows));
    renderer->request(frameRequest(dump, 2, rows));

    renderer.reset();

    // Доставки, поставленные в очередь уничтоженному объекту, Qt отбрасывает.
    QCoreApplication::processEvents();
}
