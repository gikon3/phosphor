#include "core/digital_frame.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace Phosphor {

namespace {

// Копия значения, переживающая шаг курсора. Буферы переиспользуются, поэтому копирование
// после прогрева не выделяет память.
class HeldValue {
public:
    void assign(const WaSafe::ValueView& v) {
        kind_ = v.kind();
        switch (kind_) {
            case WaSafe::ValueKind::LOGIC: {
                const auto logic = v.logic();
                width_ = logic.width();
                const auto a = logic.aval();
                const auto b = logic.bval();
                a_.assign(a.begin(), a.end());
                b_.assign(b.begin(), b.end());
                break;
            }
            case WaSafe::ValueKind::REAL:
                real_ = v.real();
                break;
            case WaSafe::ValueKind::STRING:
                string_.assign(v.string());
                break;
            default:
                kind_ = WaSafe::ValueKind::NONE;
                break;
        }
    }

    [[nodiscard]] WaSafe::ValueKind kind() const noexcept { return kind_; }

    // Побитовое равенство значений; отсутствующий b-план равен нулевому.
    [[nodiscard]] bool sameAs(const HeldValue& o) const noexcept {
        if (kind_ != o.kind_)
            return false;
        switch (kind_) {
            case WaSafe::ValueKind::LOGIC:
                return width_ == o.width_ && a_ == o.a_ && sameB(o);
            case WaSafe::ValueKind::REAL:
                return real_ == o.real_;
            case WaSafe::ValueKind::STRING:
                return string_ == o.string_;
            default:
                return true;
        }
    }

    // Только для kind() == LOGIC.
    [[nodiscard]] WaSafe::LogicVectorView logic() const noexcept {
        return {a_.data(), b_.empty() ? nullptr : b_.data(), width_};
    }

    [[nodiscard]] WaSafe::ValueView view() const noexcept {
        switch (kind_) {
            case WaSafe::ValueKind::LOGIC:
                return logic();
            case WaSafe::ValueKind::REAL:
                return real_;
            case WaSafe::ValueKind::STRING:
                return std::string_view{string_};
            default:
                return {};
        }
    }

private:
    [[nodiscard]] bool sameB(const HeldValue& o) const noexcept {
        if (b_.size() == o.b_.size())
            return b_ == o.b_;
        const auto& withB = b_.empty() ? o.b_ : b_;
        return std::ranges::all_of(withB, [](std::uint64_t w) { return w == 0; });
    }

private:
    WaSafe::ValueKind kind_ = WaSafe::ValueKind::NONE;
    std::uint32_t width_ = 0;
    std::vector<std::uint64_t> a_;
    std::vector<std::uint64_t> b_;
    double real_ = 0.0;
    std::string string_;
};

SpanKind bitKind(WaSafe::Logic bit) noexcept {
    switch (bit) {
        case WaSafe::Logic::ZERO:
            return SpanKind::LOW;
        case WaSafe::Logic::ONE:
            return SpanKind::HIGH;
        case WaSafe::Logic::Z:
            return SpanKind::Z;
        default:
            return SpanKind::X;
    }
}

class FrameBuilder {
public:
    FrameBuilder(const RowFormat& fmt, bool isBit) : fmt_{fmt} { frame_.isBit = isBit; }

    // Значение cur действует на [x0, x1). Сливается с предыдущим спаном по сырому значению,
    // подпись формируется только для нового спана.
    void emitValue(int x0, int x1, const HeldValue& cur) {
        if (x0 >= x1)
            return;
        auto& spans = frame_.spans;
        if (lastIsValue_ && !spans.empty() && spans.back().x1 == x0 && last_.sameAs(cur)) {
            spans.back().x1 = x1;
            return;
        }
        last_ = cur;
        lastIsValue_ = true;
        if (cur.kind() == WaSafe::ValueKind::NONE) {
            spans.push_back({x0, x1, SpanKind::NONE, {}});
        }
        else if (cur.kind() == WaSafe::ValueKind::LOGIC) {
            const auto logic = cur.logic();
            if (logic.width() == 1)
                spans.push_back({x0, x1, bitKind(logic[0]), {}});
            else
                spans.push_back(
                        {x0, x1, logic.isTwoState() ? SpanKind::VALUE : SpanKind::X, formatValue(cur.view(), fmt_)});
        }
        else {
            spans.push_back({x0, x1, SpanKind::VALUE, formatValue(cur.view(), fmt_)});
        }
    }

    void emitDense(int x0, int x1) {
        auto& spans = frame_.spans;
        lastIsValue_ = false;
        if (!spans.empty() && spans.back().kind == SpanKind::DENSE && spans.back().x1 == x0)
            spans.back().x1 = x1;
        else
            spans.push_back({x0, x1, SpanKind::DENSE, {}});
    }

    DigitalFrame take() && { return std::move(frame_); }

private:
    RowFormat fmt_;
    DigitalFrame frame_;
    HeldValue last_;
    bool lastIsValue_ = false;
};

// Пиксель момента t в пределах [0, widthPx).
int pixelOf(TimeWindow window, int widthPx, GlobalTime t) noexcept {
    const double raw = std::floor(timeToPixel(window, widthPx, t));
    return static_cast<int>(std::clamp(raw, 0.0, static_cast<double>(widthPx - 1)));
}

// Наименьший момент окна, чей пиксель не меньше x (0 < x < widthPx). Двоичный поиск по
// самому отображению; окончание окна ему удовлетворяет всегда. Оценка обратным отображением пиксель→время лишь
// сужает начальную вилку и на корректность не влияет.
GlobalTime pixelStart(TimeWindow window, int widthPx, int x) noexcept {
    const auto reaches = [&](GlobalTime t) {
        return std::floor(timeToPixel(window, widthPx, t)) >= x;
    };
    GlobalTime lo = window.begin;
    GlobalTime hi = window.end;
    const GlobalTime estimate = pixelToTime(window, widthPx, x);
    constexpr GlobalTime kMargin = 2;
    constexpr auto kUnsignedMargin = static_cast<std::uint64_t>(kMargin);
    const auto gap = [](GlobalTime from, GlobalTime to) {
        return to > from ? static_cast<std::uint64_t>(to) - static_cast<std::uint64_t>(from) : 0;
    };
    if (gap(lo, estimate) > kUnsignedMargin && gap(estimate, hi) > kUnsignedMargin && !reaches(estimate - kMargin) &&
            reaches(estimate + kMargin)) {
        lo = estimate - kMargin;
        hi = estimate + kMargin;
    }
    // Инвариант: reaches(hi) истинно, а для t < lo ложно.
    while (lo < hi) {
        const auto half =
                static_cast<GlobalTime>((static_cast<std::uint64_t>(hi) - static_cast<std::uint64_t>(lo)) / 2);
        const GlobalTime mid = lo + half;
        if (reaches(mid))
            hi = mid;
        else
            lo = mid + 1;
    }
    return lo;
}

}  // namespace

DigitalFrame buildDigitalFrame(const WaSafe::Database& db, WaSafe::NodeId node, const TimeAxis& axis, TimeWindow window,
        int widthPx, const RowFormat& fmt) {
    const bool isBit = db.signalHandle(node).width() == 1;
    if (widthPx <= 0 || window.end <= window.begin) {
        DigitalFrame empty;
        empty.isBit = isBit;
        return empty;
    }

    FrameBuilder out{fmt, isBit};
    HeldValue cur;
    const auto setValueAt = [&](GlobalTime t) {
        const auto value = db.valueAt(node, axis.toLocalFloor(t));
        cur.assign(value);
    };
    const WaSafe::TimeStamp endLocal = axis.toLocalCeil(window.end);

    // Текущее значение действует с пикселя curStart. Группа — изменения текущего пикселя
    // groupPixel, который кончается в groupEnd.
    int curStart = 0;
    int groupPixel = 0;
    int groupCount = 0;
    GlobalTime groupEnd = 0;

    setValueAt(window.begin);
    auto changes = db.changes(node, {axis.toLocalFloor(window.begin), endLocal});
    while (changes.next()) {
        const GlobalTime t = axis.toGlobal(changes->time);
        if (t >= window.end)
            break;
        if (t <= window.begin) {
            cur.assign(changes->value);
            continue;
        }
        if (groupCount > 0 && t < groupEnd) {
            // Второе изменение: пиксель плотный, остаток пропускается.
            out.emitDense(groupPixel, groupPixel + 1);
            curStart = groupPixel + 1;
            groupCount = 0;
            if (curStart >= widthPx)
                return std::move(out).take();
            // Изменение ровно в начале пикселя принадлежит этому пикселю, поэтому начальное
            // значение — строго до groupEnd, а курсор начинается с groupEnd.
            const WaSafe::TimeStamp from = axis.toLocalCeil(groupEnd);
            const auto before = db.valueAt(node, from - 1);
            cur.assign(before);
            changes = db.changes(node, {from, endLocal});
            continue;
        }
        const int x = pixelOf(window, widthPx, t);
        out.emitValue(curStart, x, cur);
        curStart = x;
        groupPixel = x;
        groupCount = 1;
        groupEnd = x + 1 < widthPx ? pixelStart(window, widthPx, x + 1) : window.end;
        cur.assign(changes->value);
    }
    out.emitValue(curStart, widthPx, cur);
    return std::move(out).take();
}

}  // namespace Phosphor
