#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <system_error>
#include <tuple>

#include "core/error.hpp"
#include "core/store_cache.hpp"
#include "support/temp_files.hpp"

TEST(StoreCache, SameDumpSamePath) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    const Phosphor::StoreCache cache{tmp.path() / "cache"};

    const auto first = cache.storePathFor(dump);
    const auto second = cache.storePathFor(dump);

    EXPECT_EQ(first, second);
    EXPECT_EQ(first.parent_path(), cache.directory());
    EXPECT_EQ(first.extension(), ".wsfstore");
}

TEST(StoreCache, RewrittenDumpNewPath) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    const Phosphor::StoreCache cache{tmp.path() / "cache"};
    const auto before = cache.storePathFor(dump);

    Phosphor::Test::writeFile(dump, "$timescale 1ps $end\n");
    std::filesystem::last_write_time(dump, std::filesystem::last_write_time(dump) + std::chrono::seconds{2});

    EXPECT_NE(cache.storePathFor(dump), before);
}

TEST(StoreCache, MissingDumpThrows) {
    const Phosphor::Test::TempDir tmp;
    const Phosphor::StoreCache cache{tmp.path() / "cache"};

    EXPECT_THROW(std::ignore = cache.storePathFor(tmp.path() / "missing.vcd"), Phosphor::Error);
}

TEST(StoreCache, TouchedDumpNewPath) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    const Phosphor::StoreCache cache{tmp.path() / "cache"};
    const auto before = cache.storePathFor(dump);

    std::filesystem::last_write_time(dump, std::filesystem::last_write_time(dump) + std::chrono::seconds{2});

    EXPECT_NE(cache.storePathFor(dump), before);
}

TEST(StoreCache, NameUsesGivenPathStem) {
    const Phosphor::Test::TempDir tmp;
    const auto target = Phosphor::Test::writeFile(tmp.path() / "run42.vcd", Phosphor::Test::kSmallVcd);
    const auto link = tmp.path() / "a.vcd";
    std::error_code ec;
    std::filesystem::create_symlink(target, link, ec);
    if (ec)
        GTEST_SKIP() << "symlinks unavailable: " << ec.message();
    const Phosphor::StoreCache cache{tmp.path() / "cache"};

    EXPECT_TRUE(cache.storePathFor(link).filename().string().starts_with("a-"));
}
