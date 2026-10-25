#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

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

// Store в каталоге кэша, имя которых начинается с prefix.
std::vector<std::filesystem::path> storesWithPrefix(const std::filesystem::path& dir, const std::string& prefix) {
    std::vector<std::filesystem::path> stores;
    for (const auto& entry : std::filesystem::directory_iterator{dir}) {
        const auto name = entry.path().filename().string();
        if (name.starts_with(prefix) && name.ends_with(".wsfstore"))
            stores.push_back(entry.path());
    }
    return stores;
}

// Общая часть имён всех store одного дампа: `<stem>-<хеш пути>-`.
std::string dumpPrefix(const std::filesystem::path& store) {
    const auto name = store.filename().string();
    return name.substr(0, name.size() - std::string_view{"0123456789abcdef.wsfstore"}.size());
}

// Перезаписывает дамп и сдвигает mtime: store дампа после этого устаревает.
void rewrite(const std::filesystem::path& dump) {
    Phosphor::Test::writeFile(dump, Phosphor::Test::kSmallVcd);
    std::filesystem::last_write_time(dump, std::filesystem::last_write_time(dump) + std::chrono::seconds{2});
}

void makeOlder(const std::filesystem::path& p, std::chrono::hours age) {
    std::filesystem::last_write_time(p, std::filesystem::file_time_type::clock::now() - age);
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

TEST(Loader, ReloadAfterRewriteKeepsOneStoreOfDump) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    const Phosphor::StoreCache cache{tmp.path() / "cache"};
    std::ignore = Phosphor::loadDump(dump, cache, {});
    const auto old = cache.storePathFor(dump);

    rewrite(dump);
    std::ignore = Phosphor::loadDump(dump, cache, {});

    const auto stores = storesWithPrefix(cache.directory(), dumpPrefix(old));
    ASSERT_EQ(stores.size(), 1U);
    EXPECT_EQ(stores.front(), cache.storePathFor(dump));
    EXPECT_FALSE(std::filesystem::exists(old));
}

// Устаревший store дампа удаляется и тогда, когда свежий store уже был в кэше.
TEST(Loader, OpeningFreshStoreRemovesOutdatedOfDump) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    const Phosphor::StoreCache cache{tmp.path() / "cache"};
    std::ignore = Phosphor::loadDump(dump, cache, {});
    const auto store = cache.storePathFor(dump);
    const auto outdated = cache.directory() / (dumpPrefix(store) + "0000000000000000.wsfstore");
    Phosphor::Test::writeFile(outdated, "outdated store");

    std::ignore = Phosphor::loadDump(dump, cache, {});

    EXPECT_FALSE(std::filesystem::exists(outdated));
    EXPECT_TRUE(std::filesystem::exists(store));
}

TEST(Loader, CleanupLeavesOtherDumpsStores) {
    const Phosphor::Test::TempDir tmp;
    std::filesystem::create_directories(tmp.path() / "x");
    std::filesystem::create_directories(tmp.path() / "y");
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "x" / "a.vcd", Phosphor::Test::kSmallVcd);
    const auto twin = Phosphor::Test::writeFile(tmp.path() / "y" / "a.vcd", Phosphor::Test::kSmallVcd);
    const auto other = Phosphor::Test::writeFile(tmp.path() / "x" / "b.vcd", Phosphor::Test::kSmallVcd);
    const Phosphor::StoreCache cache{tmp.path() / "cache"};
    std::ignore = Phosphor::loadDump(twin, cache, {});
    std::ignore = Phosphor::loadDump(other, cache, {});
    std::ignore = Phosphor::loadDump(dump, cache, {});

    rewrite(dump);
    std::ignore = Phosphor::loadDump(dump, cache, {});

    EXPECT_TRUE(std::filesystem::exists(cache.storePathFor(twin)));
    EXPECT_TRUE(std::filesystem::exists(cache.storePathFor(other)));
}

// Свежий `.part` может принадлежать идущей параллельной загрузке, а давний остался от
// прерванного процесса.
TEST(Loader, CleanupRemovesOnlyStaleParts) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    const Phosphor::StoreCache cache{tmp.path() / "cache"};
    std::ignore = Phosphor::loadDump(dump, cache, {});
    const auto old = cache.storePathFor(dump);
    auto freshPart = old;
    freshPart += ".0123456789abcdef.part";
    auto stalePart = old;
    stalePart += ".fedcba9876543210.part";
    Phosphor::Test::writeFile(freshPart, "parallel load");
    Phosphor::Test::writeFile(stalePart, "crashed load");
    makeOlder(stalePart, std::chrono::hours{2});

    rewrite(dump);
    std::ignore = Phosphor::loadDump(dump, cache, {});

    EXPECT_TRUE(std::filesystem::exists(freshPart));
    EXPECT_FALSE(std::filesystem::exists(stalePart));
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
