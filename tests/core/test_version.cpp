#include <gtest/gtest.h>

#include <format>

#include "phosphor/version.hpp"

TEST(Version, MatchesProject) {
    EXPECT_EQ(Phosphor::Version::kString,
            std::format("{}.{}.{}", Phosphor::Version::kMajor, Phosphor::Version::kMinor, Phosphor::Version::kPatch));
}
