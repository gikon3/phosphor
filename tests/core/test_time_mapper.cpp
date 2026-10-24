#include <gtest/gtest.h>

#include <limits>

#include "core/time_mapper.hpp"

TEST(TimeMapper, ZoomKeepsPivotFraction) {
    const auto w = Phosphor::zoomAround({0, 1000}, 250, 0.5);
    EXPECT_EQ(w, (Phosphor::TimeWindow{125, 625}));
}

TEST(TimeMapper, ZoomStopsAtMinSpan) {
    const auto w = Phosphor::zoomAround({0, 2}, 1, 1e-6);
    EXPECT_GE(w.end - w.begin, Phosphor::kMinWindowSpan);
}

TEST(TimeMapper, ZoomOutStaysInsideInt64) {
    constexpr auto kMax = std::numeric_limits<Phosphor::GlobalTime>::max();
    const auto w = Phosphor::zoomAround({0, kMax / 2}, kMax / 4, 1e6);
    EXPECT_GE(w.end, w.begin);
    EXPECT_GT(w.end - w.begin, kMax / 2);
    EXPECT_LE(w.begin, kMax / 4);
    EXPECT_GE(w.end, kMax / 4);
}

TEST(TimeMapper, ScrollKeepsSpanAndSaturates) {
    constexpr auto kMax = std::numeric_limits<Phosphor::GlobalTime>::max();
    EXPECT_EQ(Phosphor::scrollBy({10, 110}, 5), (Phosphor::TimeWindow{15, 115}));
    EXPECT_EQ(Phosphor::scrollBy({10, 110}, kMax), (Phosphor::TimeWindow{kMax - 100, kMax}));
}

TEST(TimeMapper, ZeroWidthIsSafe) {
    EXPECT_EQ(Phosphor::timeToPixel({0, 100}, 0, 50), 0.0);
    EXPECT_EQ(Phosphor::pixelToTime({0, 100}, 0, 10.0), 0);
}

TEST(TimeMapper, PixelRoundTrip) {
    EXPECT_EQ(Phosphor::pixelToTime({1000, 2000}, 1000, Phosphor::timeToPixel({1000, 2000}, 1000, 1500)), 1500);
}

TEST(TimeMapper, LargeOffsetsKeepPrecision) {
    constexpr Phosphor::GlobalTime kBase = 1'000'000'000'000'000'000;
    EXPECT_EQ(Phosphor::timeToPixel({kBase, kBase + 1000}, 1000, kBase + 500), 500.0);
}
