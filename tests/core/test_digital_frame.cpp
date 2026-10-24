#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "core/digital_frame.hpp"
#include "core/loader.hpp"
#include "core/store_cache.hpp"
#include "support/temp_files.hpp"

namespace {

using Phosphor::DigitalSpan;
using Phosphor::SpanKind;

// Загруженный из текста VCD дамп вместе с осью времени.
class Dump {
public:
    explicit Dump(std::string_view vcd) :
            cache_{tmp_.path() / "cache"},
            db_{Phosphor::loadDump(Phosphor::Test::writeFile(tmp_.path() / "a.vcd", vcd), cache_, {})},
            axis_{Phosphor::TimeAxis::fromScale(db_.timeScale())} {}

    [[nodiscard]] const WaSafe::Database& db() const { return db_; }
    [[nodiscard]] const Phosphor::TimeAxis& axis() const { return axis_; }

private:
    Phosphor::Test::TempDir tmp_;
    Phosphor::StoreCache cache_;
    WaSafe::Database db_;
    Phosphor::TimeAxis axis_;
};

// Быстрые часы (переключение каждую наносекунду) и медленная шина.
std::string fastVcd() {
    std::string s =
            "$timescale 1ns $end\n$scope module top $end\n$var wire 1 ! clk $end\n"
            "$var wire 8 \" bus [7:0] $end\n$upscope $end\n$enddefinitions $end\n";
    for (int t = 0; t <= 20'000; ++t) {
        s += std::format("#{}\n{}!\n", t, t % 2);
        if (t % 137 == 0)
            s += std::format("b{:08b} \"\n", (t / 137) % 256);
    }
    return s;
}

struct Cell {
    SpanKind kind = SpanKind::NONE;
    std::string label;

    friend bool operator==(const Cell&, const Cell&) = default;
};

Cell classify(WaSafe::ValueView v) {
    if (!v.valid())
        return {SpanKind::NONE, {}};
    if (v.logic().width() == 1)
        return {v.logic()[0] == WaSafe::Logic::ONE ? SpanKind::HIGH : SpanKind::LOW, {}};
    return {SpanKind::VALUE, Phosphor::formatValue(v, {})};
}

// Прямолинейный эталон по всем изменениям: пиксель без изменений держит значение, с одним
// принадлежит новому, с двумя и более плотный.
std::vector<DigitalSpan> reference(const Dump& d, const char* path, Phosphor::TimeWindow w, int width) {
    const auto node = d.db().find(path)->node();
    Cell cur = classify(d.db().valueAt(node, d.axis().toLocalFloor(w.begin)));
    std::vector<int> count(static_cast<std::size_t>(width), 0);
    std::vector<Cell> last(static_cast<std::size_t>(width));
    auto cursor = d.db().changes(node, {d.axis().toLocalFloor(w.begin), d.axis().toLocalCeil(w.end)});
    while (cursor.next()) {
        const auto t = d.axis().toGlobal(cursor->time);
        if (t >= w.end)
            break;
        const Cell cell = classify(cursor->value);
        if (t <= w.begin) {
            cur = cell;
            continue;
        }
        const auto x = static_cast<std::size_t>(
                std::clamp(static_cast<int>(std::floor(Phosphor::timeToPixel(w, width, t))), 0, width - 1));
        ++count[x];
        last[x] = cell;
    }
    std::vector<DigitalSpan> spans;
    for (int x = 0; x < width; ++x) {
        const auto i = static_cast<std::size_t>(x);
        Cell cell = cur;
        if (count[i] >= 1)
            cur = last[i];
        if (count[i] == 1)
            cell = cur;
        else if (count[i] >= 2)
            cell = {SpanKind::DENSE, {}};
        if (!spans.empty() && spans.back().kind == cell.kind && spans.back().label == cell.label)
            spans.back().x1 = x + 1;
        else
            spans.push_back({x, x + 1, cell.kind, cell.label});
    }
    return spans;
}

class DigitalFrame : public testing::Test {
protected:
    DigitalFrame() : dump_{std::make_unique<Dump>(Phosphor::Test::kSmallVcd)} {}

    void use(std::string_view vcd) { dump_ = std::make_unique<Dump>(vcd); }

    Phosphor::DigitalFrame frame(const char* path, Phosphor::TimeWindow w, int width) const {
        return Phosphor::buildDigitalFrame(dump_->db(), dump_->db().find(path)->node(), dump_->axis(), w, width, {});
    }

    [[nodiscard]] Phosphor::GlobalTime ns(WaSafe::TimeStamp t) const { return dump_->axis().toGlobal(t); }

    [[nodiscard]] const Dump& dump() const { return *dump_; }

    static void expectSpans(const std::vector<DigitalSpan>& actual, const std::vector<DigitalSpan>& expected) {
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(actual[i].x0, expected[i].x0) << i;
            EXPECT_EQ(actual[i].x1, expected[i].x1) << i;
            EXPECT_EQ(actual[i].kind, expected[i].kind) << i;
            EXPECT_EQ(actual[i].label, expected[i].label) << i;
        }
    }

private:
    std::unique_ptr<Dump> dump_;
};

}  // namespace

TEST_F(DigitalFrame, ClockSpans) {
    const auto f = frame("top.clk", {0, ns(20)}, 20);
    EXPECT_TRUE(f.isBit);
    expectSpans(f.spans,
            {{0, 5, SpanKind::LOW, ""}, {5, 10, SpanKind::HIGH, ""}, {10, 15, SpanKind::LOW, ""},
                    {15, 20, SpanKind::HIGH, ""}});
}

TEST_F(DigitalFrame, BusLabelsAndX) {
    const auto f = frame("top.data", {0, ns(20)}, 20);
    EXPECT_FALSE(f.isBit);
    expectSpans(f.spans, {{0, 10, SpanKind::VALUE, "00"}, {10, 20, SpanKind::VALUE, "a5"}});

    expectSpans(frame("top.data", {0, ns(30)}, 30).spans,
            {{0, 10, SpanKind::VALUE, "00"}, {10, 20, SpanKind::VALUE, "a5"}, {20, 30, SpanKind::X, "xx"}});
}

TEST_F(DigitalFrame, DensePixel) {
    expectSpans(frame("top.clk", {0, ns(20)}, 1).spans, {{0, 1, SpanKind::DENSE, ""}});

    const auto f = frame("top.clk", {0, ns(20)}, 4);
    for (const auto& s : f.spans)
        EXPECT_NE(s.kind, SpanKind::DENSE);
    expectSpans(f.spans,
            {{0, 1, SpanKind::LOW, ""}, {1, 2, SpanKind::HIGH, ""}, {2, 3, SpanKind::LOW, ""},
                    {3, 4, SpanKind::HIGH, ""}});
}

TEST_F(DigitalFrame, ChangeInsidePixelBelongsToNewValue) {
    expectSpans(frame("top.clk", {0, ns(20)}, 2).spans, {{0, 1, SpanKind::HIGH, ""}, {1, 2, SpanKind::DENSE, ""}});
}

TEST_F(DigitalFrame, RealValue) {
    expectSpans(frame("top.vout", {0, ns(20)}, 20).spans,
            {{0, 10, SpanKind::VALUE, "0"}, {10, 20, SpanKind::VALUE, "0.5"}});
}

TEST_F(DigitalFrame, WindowOutsideData) {
    expectSpans(frame("top.data", {ns(1000), ns(2000)}, 50).spans, {{0, 50, SpanKind::X, "xx"}});

    const auto none = frame("top.clk", {-ns(10), ns(0)}, 10);
    EXPECT_TRUE(none.isBit);
    expectSpans(none.spans, {{0, 10, SpanKind::NONE, ""}});

    expectSpans(frame("top.clk", {-ns(10), ns(10)}, 20).spans,
            {{0, 10, SpanKind::NONE, ""}, {10, 15, SpanKind::LOW, ""}, {15, 20, SpanKind::HIGH, ""}});
}

TEST_F(DigitalFrame, ZeroWidthEmpty) {
    EXPECT_TRUE(frame("top.clk", {0, ns(20)}, 0).spans.empty());
    EXPECT_TRUE(frame("top.clk", {0, ns(20)}, -3).spans.empty());
    EXPECT_TRUE(frame("top.clk", {5, 5}, 20).spans.empty());
    EXPECT_TRUE(frame("top.clk", {0, ns(20)}, 0).isBit);
    EXPECT_FALSE(frame("top.data", {5, 5}, 20).isBit);
}

TEST_F(DigitalFrame, DumpWithoutTimestamps) {
    use("$timescale 1ns $end\n$scope module top $end\n$var wire 1 ! clk $end\n$upscope $end\n"
        "$enddefinitions $end\n");
    const auto f = frame("top.clk", {0, ns(20)}, 20);
    EXPECT_TRUE(f.isBit);
    expectSpans(f.spans, {{0, 20, SpanKind::NONE, ""}});
}

TEST_F(DigitalFrame, SubFemtosecondWindow) {
    use("$timescale 1fs $end\n$scope module top $end\n$var wire 1 ! clk $end\n$upscope $end\n"
        "$enddefinitions $end\n#0\n0!\n#1\n1!\n#2\n0!\n");
    expectSpans(frame("top.clk", {0, 2}, 1920).spans, {{0, 960, SpanKind::LOW, ""}, {960, 1920, SpanKind::HIGH, ""}});
}

TEST_F(DigitalFrame, DenseRunMergesIntoOneSpan) {
    use(fastVcd());
    expectSpans(frame("top.clk", {0, ns(20'000)}, 7).spans, {{0, 7, SpanKind::DENSE, ""}});
    expectSpans(frame("top.clk", {0, ns(20'000)}, 1920).spans, {{0, 1920, SpanKind::DENSE, ""}});
}

TEST_F(DigitalFrame, DenseSkipMatchesFullScan) {
    use(fastVcd());
    const std::vector<Phosphor::TimeWindow> windows = {
            {0, ns(20'000)},
            {ns(4000), ns(4300)},
            {ns(3001) + 500'000, ns(9000)},
            {ns(100), ns(103)},
    };
    for (const int width : {1000, 2000}) {
        const auto f = frame("top.clk", windows[0], width);
        expectSpans(f.spans, reference(dump(), "top.clk", windows[0], width));
    }
    for (const char* path : {"top.clk", "top.bus"}) {
        for (const auto& w : windows) {
            for (const int width : {1, 7, 100, 1920}) {
                const auto f = frame(path, w, width);
                SCOPED_TRACE(std::format("{} [{}, {}) x {}", path, w.begin, w.end, width));
                expectSpans(f.spans, reference(dump(), path, w, width));
            }
        }
    }
}

TEST_F(DigitalFrame, BoundaryChangesCountAfterSkip) {
    use("$timescale 1ns $end\n$scope module top $end\n$var wire 1 ! s $end\n$upscope $end\n"
        "$enddefinitions $end\n#0\n0!\n#1\n1!\n#2\n0!\n#10\n1!\n#12\n0!\n#20\n1!\n#35\n0!\n#100\n0!\n");
    const auto f = frame("top.s", {0, ns(100)}, 10);
    expectSpans(f.spans, {{0, 2, SpanKind::DENSE, ""}, {2, 3, SpanKind::HIGH, ""}, {3, 10, SpanKind::LOW, ""}});
    expectSpans(f.spans, reference(dump(), "top.s", {0, ns(100)}, 10));
}

TEST_F(DigitalFrame, QuietPixelAfterDenseKeepsLastValue) {
    std::string vcd =
            "$timescale 1ns $end\n$scope module top $end\n$var wire 1 ! clk $end\n$upscope $end\n"
            "$enddefinitions $end\n";
    for (int t = 0; t <= 95; ++t)
        vcd += std::format("#{}\n{}!\n", t, t % 2);
    use(vcd);
    const auto f = frame("top.clk", {0, ns(1000)}, 100);
    expectSpans(f.spans, {{0, 10, SpanKind::DENSE, ""}, {10, 100, SpanKind::HIGH, ""}});
    for (const int width : {10, 100, 333})
        expectSpans(frame("top.clk", {0, ns(1000)}, width).spans, reference(dump(), "top.clk", {0, ns(1000)}, width));
}

TEST_F(DigitalFrame, WindowSpanBeyondInt64) {
    const Phosphor::TimeWindow w{-1'000'000'000'000'000'000, std::numeric_limits<Phosphor::GlobalTime>::max()};
    for (const char* path : {"top.clk", "top.data"}) {
        const auto f = frame(path, w, 1920);
        ASSERT_FALSE(f.spans.empty());
        EXPECT_EQ(f.spans.front().x0, 0);
        EXPECT_EQ(f.spans.back().x1, 1920);
        for (std::size_t i = 1; i < f.spans.size(); ++i)
            EXPECT_EQ(f.spans[i].x0, f.spans[i - 1].x1);
        if (std::string_view{path} == "top.clk")
            expectSpans(f.spans, reference(dump(), path, w, 1920));
    }
    use(fastVcd());
    expectSpans(frame("top.clk", w, 1920).spans, reference(dump(), "top.clk", w, 1920));
}
