#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "core/error.hpp"
#include "core/time_axis.hpp"

TEST(TimeAxis, ConvertsNanoseconds) {
    const auto a = Phosphor::TimeAxis::fromScale({.exponent = -9, .scale = 10});
    EXPECT_EQ(a.femtosecondsPerUnit(), 10'000'000);
    EXPECT_EQ(a.toGlobal(3), 30'000'000);
    EXPECT_EQ(a.toLocalFloor(35'000'000), 3);
    EXPECT_EQ(a.toLocalCeil(35'000'000), 4);
}

TEST(TimeAxis, FloorAndCeilOfNegativeAndExact) {
    const auto a = Phosphor::TimeAxis::fromScale({.exponent = -9, .scale = 10});
    EXPECT_EQ(a.toLocalFloor(-35'000'000), -4);
    EXPECT_EQ(a.toLocalCeil(-35'000'000), -3);
    EXPECT_EQ(a.toLocalFloor(30'000'000), 3);
    EXPECT_EQ(a.toLocalCeil(30'000'000), 3);
    EXPECT_EQ(a.toLocalCeil(std::numeric_limits<std::int64_t>::max()), 922'337'203'686);
}

TEST(TimeAxis, RejectsSubFemtosecond) {
    EXPECT_THROW(Phosphor::TimeAxis::fromScale({.exponent = -18, .scale = 1}), Phosphor::Error);
}

TEST(TimeAxis, RejectsScaleThatDoesNotFit) {
    EXPECT_THROW(Phosphor::TimeAxis::fromScale({.exponent = 4, .scale = 1}), Phosphor::Error);
    EXPECT_THROW(Phosphor::TimeAxis::fromScale({.exponent = 3, .scale = 100}), Phosphor::Error);
    EXPECT_THROW(Phosphor::TimeAxis::fromScale({.exponent = 0, .scale = 0}), Phosphor::Error);
}

TEST(TimeAxis, SaturatesOnOverflow) {
    const auto a = Phosphor::TimeAxis::fromScale({.exponent = 0, .scale = 1});
    EXPECT_EQ(a.toGlobal(std::numeric_limits<std::int64_t>::max() / 2), std::numeric_limits<std::int64_t>::max());
    EXPECT_EQ(a.toGlobal(std::numeric_limits<std::int64_t>::min() + 1), std::numeric_limits<std::int64_t>::min());
}

TEST(TimeAxis, NoTimeMapsToMin) {
    const auto a = Phosphor::TimeAxis::fromScale({.exponent = -12, .scale = 1});
    EXPECT_EQ(a.toGlobal(WaSafe::kNoTime), std::numeric_limits<std::int64_t>::min());
}

TEST(FormatTime, PicksUnit) {
    EXPECT_EQ(Phosphor::formatTime(340'000'000), "340 ns");
    EXPECT_EQ(Phosphor::formatTime(1'500'000), "1.5 ns");
    EXPECT_EQ(Phosphor::formatTime(0), "0 s");
}

TEST(FormatTime, KeepsSignAndCoversAllUnits) {
    EXPECT_EQ(Phosphor::formatTime(-340'000'000), "-340 ns");
    EXPECT_EQ(Phosphor::formatTime(7), "7 fs");
    EXPECT_EQ(Phosphor::formatTime(2000), "2 ps");
    EXPECT_EQ(Phosphor::formatTime(1'234'567'000), "1.235 us");
    EXPECT_EQ(Phosphor::formatTime(5'000'000'000'000), "5 ms");
    EXPECT_EQ(Phosphor::formatTime(1'500'000'000'000'000), "1.5 s");
    EXPECT_EQ(Phosphor::formatTime(999'999'999), "1 us");
    EXPECT_EQ(Phosphor::formatTime(-999'999'999), "-1 us");
    EXPECT_EQ(Phosphor::formatTime(std::numeric_limits<std::int64_t>::min()), "-9223.372 s");
}

// Соседние деления линейки различаются, даже если шаг намного мельче выбранной единицы.
TEST(FormatTimeWithResolution, NeighbouringTicksDiffer) {
    EXPECT_EQ(Phosphor::formatTime(39'076'500'000, 500'000), "39.0765 us");
    EXPECT_EQ(Phosphor::formatTime(39'077'000'000, 500'000), "39.077 us");
    EXPECT_EQ(Phosphor::formatTime(39'076'500'500, 500), "39.0765005 us");
    EXPECT_EQ(Phosphor::formatTime(7'000'000'000'000'003, 1), "7.000000000000003 s");
}

TEST(FormatTimeWithResolution, CoarseStepAddsNoDigits) {
    EXPECT_EQ(Phosphor::formatTime(40'000'000'000, 10'000'000'000), "40 us");
    EXPECT_EQ(Phosphor::formatTime(39'000'000'000, 1'000'000'000), "39 us");
    EXPECT_EQ(Phosphor::formatTime(1'500'000, 500'000), "1.5 ns");
    EXPECT_EQ(Phosphor::formatTime(1'200'000, 200'000), "1.2 ns");
    EXPECT_EQ(Phosphor::formatTime(0, 500'000), "0 s");
}

TEST(FormatTimeWithResolution, RoundsToStepAndCarriesUnit) {
    EXPECT_EQ(Phosphor::formatTime(39'077'400'000, 1'000'000'000), "39 us");
    EXPECT_EQ(Phosphor::formatTime(999'999'999, 1000), "1 us");
    EXPECT_EQ(Phosphor::formatTime(-39'076'500'000, 500'000), "-39.0765 us");
    // Неположительный шаг означает предельную точность — фемтосекунду.
    EXPECT_EQ(Phosphor::formatTime(1'000'001, 0), "1.000001 ns");
    EXPECT_EQ(Phosphor::formatTime(std::numeric_limits<std::int64_t>::min(), 1), "-9223.372036854775808 s");
}
