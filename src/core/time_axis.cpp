#include "core/time_axis.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <optional>
#include <string_view>

#include "core/error.hpp"

namespace Phosphor {

namespace {

constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
constexpr int kMinExponent = -15;

// Максимальный показатель, при котором 10^(exponent + 15) ещё помещается в int64.
constexpr int kMaxExponent = 3;

// Произведение a * b или std::nullopt, если оно не помещается в int64.
// Обходится без 128-битных типов: границы проверяются до умножения.
std::optional<std::int64_t> multiplyChecked(std::int64_t a, std::int64_t b) noexcept {
    if (a == 0 || b == 0)
        return 0;
    const bool negative = (a < 0) != (b < 0);
    const std::uint64_t ua = a < 0 ? 0 - static_cast<std::uint64_t>(a) : static_cast<std::uint64_t>(a);
    const std::uint64_t ub = b < 0 ? 0 - static_cast<std::uint64_t>(b) : static_cast<std::uint64_t>(b);
    const std::uint64_t limit = negative ? static_cast<std::uint64_t>(kMax) + 1 : static_cast<std::uint64_t>(kMax);
    if (ua > limit / ub)
        return std::nullopt;
    const std::uint64_t product = ua * ub;
    return negative ? static_cast<std::int64_t>(0 - product) : static_cast<std::int64_t>(product);
}

struct Unit {
    std::uint64_t femtoseconds;
    std::string_view name;
};

constexpr std::array<Unit, 6> kUnits{{
        {1'000'000'000'000'000, "s"},
        {1'000'000'000'000, "ms"},
        {1'000'000'000, "us"},
        {1'000'000, "ns"},
        {1000, "ps"},
        {1, "fs"},
}};

// Время в единице, выбранной по модулю t. placeOf(unitFs) — вес последнего печатаемого знака
// в fs: степень десяти от 1 до unitFs. Значение округляется до этого знака, хвостовые нули
// дробной части отбрасываются.
template <typename PlaceOf>
std::string formatTimeWith(GlobalTime t, PlaceOf placeOf) {
    const bool negative = t < 0;
    const std::uint64_t magnitude = negative ? 0 - static_cast<std::uint64_t>(t) : static_cast<std::uint64_t>(t);

    std::size_t index = kUnits.size() - 1;
    if (magnitude == 0) {
        index = 0;
    }
    else {
        for (std::size_t i = 0; i < kUnits.size(); ++i) {
            if (magnitude >= kUnits[i].femtoseconds) {
                index = i;
                break;
            }
        }
    }

    const std::uint64_t unitFs = kUnits[index].femtoseconds;
    const std::uint64_t place = placeOf(unitFs);
    std::uint64_t whole = magnitude / unitFs;
    // Дробная часть в единицах последнего знака; остаток меньше unitFs, поэтому сумма не
    // переполняет uint64.
    std::uint64_t fraction = (magnitude % unitFs + place / 2) / place;
    if (fraction >= unitFs / place) {
        ++whole;
        fraction = 0;
    }
    // Округление могло довести значение до 1000 единиц: переходим к следующей единице.
    if (whole >= 1000 && index > 0) {
        --index;
        whole = 1;
        fraction = 0;
    }

    std::string text;
    if (negative)
        text += '-';
    text += std::to_string(whole);
    if (fraction != 0) {
        std::size_t digits = 0;
        for (auto p = place; p < unitFs; p *= 10)
            ++digits;
        std::string frac{std::to_string(fraction)};
        frac.insert(0, digits - frac.size(), '0');
        while (frac.back() == '0')
            frac.pop_back();
        text += '.';
        text += frac;
    }
    text += ' ';
    text += kUnits[index].name;
    return text;
}

}  // namespace

TimeAxis TimeAxis::fromScale(WaSafe::TimeScale scale) {
    if (scale.exponent < kMinExponent)
        throw Error{"time scale finer than 1 fs is not supported"};
    if (scale.scale <= 0)
        throw Error{"time scale multiplier must be positive"};
    if (scale.exponent > kMaxExponent)
        throw Error{"time scale too coarse for the femtosecond axis"};
    std::int64_t result = scale.scale;
    for (int i = 0; i < scale.exponent - kMinExponent; ++i) {
        const auto next = multiplyChecked(result, 10);
        if (!next)
            throw Error{"time scale too coarse for the femtosecond axis"};
        result = *next;
    }
    return TimeAxis{result};
}

GlobalTime TimeAxis::toGlobal(WaSafe::TimeStamp t) const noexcept {
    if (t == WaSafe::kNoTime)
        return kMin;
    const auto result = multiplyChecked(t, femtosecondsPerUnit_);
    if (!result)
        return t < 0 ? kMin : kMax;
    return *result;
}

WaSafe::TimeStamp TimeAxis::toLocalFloor(GlobalTime t) const noexcept {
    const std::int64_t q = t / femtosecondsPerUnit_;
    return t % femtosecondsPerUnit_ < 0 ? q - 1 : q;
}

WaSafe::TimeStamp TimeAxis::toLocalCeil(GlobalTime t) const noexcept {
    const std::int64_t q = t / femtosecondsPerUnit_;
    return t % femtosecondsPerUnit_ > 0 ? q + 1 : q;
}

std::int64_t TimeAxis::femtosecondsPerUnit() const noexcept {
    return femtosecondsPerUnit_;
}

TimeAxis::TimeAxis(std::int64_t femtosecondsPerUnit) noexcept : femtosecondsPerUnit_{femtosecondsPerUnit} {
}

std::string formatTime(GlobalTime t) {
    // Три знака после точки: вес последнего знака — тысячная доля единицы, но не мельче 1 fs.
    return formatTimeWith(t, [](std::uint64_t unitFs) { return std::max<std::uint64_t>(unitFs / 1000, 1); });
}

std::string formatTime(GlobalTime t, GlobalTime resolution) {
    const auto finest = resolution > 0 ? static_cast<std::uint64_t>(resolution) : std::uint64_t{1};
    return formatTimeWith(t, [finest](std::uint64_t unitFs) {
        std::uint64_t place = unitFs;
        while (place > finest && place > 1)
            place /= 10;
        return place;
    });
}

}  // namespace Phosphor
