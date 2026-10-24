#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <iterator>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <tuple>

#include "core/cancelling_builder.hpp"
#include "core/error.hpp"
#include "core/loader.hpp"
#include "core/store_cache.hpp"
#include "support/temp_files.hpp"
#include "wasafe/core/exception.hpp"
#include "wasafe/io/builder.hpp"
#include "wasafe/model/scope.hpp"
#include "wasafe/types/type.hpp"
#include "wasafe/types/value.hpp"

namespace {

std::size_t countFiles(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir))
        return 0;
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator{dir}, {}));
}

}  // namespace

TEST(Loader, IndexesThenReusesStore) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    const Phosphor::StoreCache cache{tmp.path() / "cache"};
    const auto store = cache.storePathFor(dump);

    {
        const auto db = Phosphor::loadDump(dump, cache, {});
        EXPECT_TRUE(std::filesystem::exists(store));
        EXPECT_TRUE(db.find("top.clk").has_value());
        EXPECT_GE(db.timeRange().end, 20);
    }
    const auto written = std::filesystem::last_write_time(store);

    const auto db = Phosphor::loadDump(dump, cache, {});

    EXPECT_EQ(std::filesystem::last_write_time(store), written);
    EXPECT_TRUE(db.find("top.clk").has_value());
}

TEST(Loader, CancelLeavesNoStore) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    const Phosphor::StoreCache cache{tmp.path() / "cache"};
    const auto store = cache.storePathFor(dump);
    const std::stop_source source;
    source.request_stop();

    EXPECT_THROW(std::ignore = Phosphor::loadDump(dump, cache, source.get_token()), Phosphor::Cancelled);

    EXPECT_FALSE(std::filesystem::exists(store));
    EXPECT_FALSE(std::filesystem::exists(store.string() + ".part"));
    const auto db = Phosphor::loadDump(dump, cache, {});
    EXPECT_TRUE(db.find("top.clk").has_value());
}

TEST(Loader, BrokenVcdThrowsWaSafe) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "bad.vcd",
            "$timescale 1ns $end\n$scope module top $end\n$var wire 1 ! a $end\n$upscope $end\n"
            "$enddefinitions $end\n#abc\n1!\n");
    const Phosphor::StoreCache cache{tmp.path() / "cache"};

    EXPECT_THROW(std::ignore = Phosphor::loadDump(dump, cache, {}), WaSafe::Exception);

    EXPECT_EQ(countFiles(cache.directory()), 0U);
}

TEST(Loader, CorruptStoreReindexes) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    const Phosphor::StoreCache cache{tmp.path() / "cache"};
    Phosphor::Test::writeFile(cache.storePathFor(dump), "garbage, not a store");

    const auto db = Phosphor::loadDump(dump, cache, {});

    EXPECT_TRUE(db.find("top.clk").has_value());
}

TEST(Loader, ConcurrentLoadsOfSameDump) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    const Phosphor::StoreCache cache{tmp.path() / "cache"};
    std::optional<WaSafe::Database> first;
    std::optional<WaSafe::Database> second;

    {
        const std::jthread a{[&] { first.emplace(Phosphor::loadDump(dump, cache, {})); }};
        const std::jthread b{[&] { second.emplace(Phosphor::loadDump(dump, cache, {})); }};
    }

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(first->find("top.clk").has_value());
    EXPECT_TRUE(second->find("top.clk").has_value());
    for (const auto& entry : std::filesystem::directory_iterator{cache.directory()})
        EXPECT_NE(entry.path().extension(), ".part") << entry.path();
}

TEST(CancellingBuilder, ChecksValueChanges) {
    auto memory = WaSafe::makeMemoryBuilder();
    const std::stop_source source;
    auto cancelling = Phosphor::makeCancellingBuilder(*memory, source.get_token());
    std::ignore = cancelling->beginScope("top", WaSafe::ScopeKind::MODULE);
    const auto id = cancelling->declareVar("a", WaSafe::makeScalar());
    cancelling->endScope();
    cancelling->headerDone();
    cancelling->setTime(0);
    source.request_stop();
    const WaSafe::Value value{WaSafe::LogicVector{1}};

    std::size_t calls = 0;
    bool cancelled = false;
    try {
        for (; calls < 8192 && !cancelled; ++calls)
            cancelling->valueChange(id, value);
    }
    catch (const Phosphor::Cancelled&) {
        cancelled = true;
    }

    EXPECT_TRUE(cancelled);
    EXPECT_LE(calls, 4096U);
}
