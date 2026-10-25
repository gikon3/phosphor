#include "core/time_mapper.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Phosphor {

namespace {

constexpr GlobalTime kMax = std::numeric_limits<GlobalTime>::max();
constexpr GlobalTime kMin = std::numeric_limits<GlobalTime>::min();

// Точная разность a - b без переполнения int64 (a >= b).
std::uint64_t unsignedDiff(GlobalTime a, GlobalTime b) noexcept {
    return static_cast<std::uint64_t>(a) - static_cast<std::uint64_t>(b);
}

// Разность в double без потери точности до преобразования: метки в фемтосекундах
// быстро выходят за 2^53, а вычитание в double теряет младшие разряды.
double signedDiff(GlobalTime a, GlobalTime b) noexcept {
    return a >= b ? static_cast<double>(unsignedDiff(a, b)) : -static_cast<double>(unsignedDiff(b, a));
}

// Ширина корректного окна, не превышающая kMax.
GlobalTime spanOf(TimeWindow w) noexcept {
    const std::uint64_t span = unsignedDiff(w.end, w.begin);
    return span > static_cast<std::uint64_t>(kMax) ? kMax : static_cast<GlobalTime>(span);
}

GlobalTime saturatingAdd(GlobalTime a, GlobalTime b) noexcept {
    if (b > 0 && a > kMax - b)
        return kMax;
    if (b < 0 && a < kMin - b)
        return kMin;
    return a + b;
}

// Приведение double к int64 с округлением и насыщением.
GlobalTime roundSaturating(double v) noexcept {
    if (std::isnan(v))
        return 0;
    // 2^63 точно представимо в double; kMax как double округляется до него же.
    constexpr double kLimit = 9223372036854775808.0;
    if (v >= kLimit)
        return kMax;
    if (v <= -kLimit)
        return kMin;
    return static_cast<GlobalTime>(std::llround(v));
}

// Окно заданной ширины, начинающееся не раньше begin и целиком лежащее в int64.
TimeWindow windowWithSpan(GlobalTime begin, GlobalTime span) noexcept {
    begin = std::min(begin, kMax - span);
    return {begin, begin + span};
}

}  // namespace

double timeToPixel(TimeWindow w, int widthPx, GlobalTime t) noexcept {
    if (widthPx <= 0 || w.end <= w.begin)
        return 0.0;
    return signedDiff(t, w.begin) / signedDiff(w.end, w.begin) * widthPx;
}

GlobalTime pixelToTime(TimeWindow w, int widthPx, double x) noexcept {
    if (widthPx <= 0 || w.end <= w.begin)
        return w.begin;
    const double offset = x / widthPx * signedDiff(w.end, w.begin);
    return saturatingAdd(w.begin, roundSaturating(offset));
}

TimeWindow zoomAround(TimeWindow w, GlobalTime pivot, double factor) noexcept {
    if (w.end <= w.begin || !(factor > 0.0) || !std::isfinite(factor))
        return w;
    const double span = signedDiff(w.end, w.begin);
    const double fraction = signedDiff(pivot, w.begin) / span;
    GlobalTime newSpan = roundSaturating(span * factor);
    newSpan = std::max(newSpan, kMinWindowSpan);
    const GlobalTime pivotOffset = roundSaturating(fraction * static_cast<double>(newSpan));
    const GlobalTime begin = saturatingAdd(pivot, pivotOffset == kMin ? kMax : -pivotOffset);
    return windowWithSpan(begin, newSpan);
}

TimeWindow scrollBy(TimeWindow w, GlobalTime delta) noexcept {
    const GlobalTime span = w.end > w.begin ? spanOf(w) : 0;
    return windowWithSpan(saturatingAdd(w.begin, delta), span);
}

}  // namespace Phosphor
