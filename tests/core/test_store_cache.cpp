#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QStandardPaths>
#include <QString>
#include <chrono>
#include <filesystem>
#include <regex>
#include <system_error>
#include <tuple>

#include "core/error.hpp"
#include "core/store_cache.hpp"
#include "phosphor/config.hpp"
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

// Имя store: `<stem>-<хеш пути>-<хеш размера и mtime>.wsfstore`; у перезаписанного дампа
// меняется только второй хеш, у другого дампа с тем же именем файла — первый.
TEST(StoreCache, NameSeparatesPathAndVersion) {
    const Phosphor::Test::TempDir tmp;
    std::filesystem::create_directories(tmp.path() / "x");
    std::filesystem::create_directories(tmp.path() / "y");
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "x" / "a.vcd", Phosphor::Test::kSmallVcd);
    const auto twin = Phosphor::Test::writeFile(tmp.path() / "y" / "a.vcd", Phosphor::Test::kSmallVcd);
    const Phosphor::StoreCache cache{tmp.path() / "cache"};
    const std::regex pattern{"a-([0-9a-f]{16})-([0-9a-f]{16})\\.wsfstore"};

    const auto before = cache.storePathFor(dump).filename().string();
    std::filesystem::last_write_time(dump, std::filesystem::last_write_time(dump) + std::chrono::seconds{2});
    const auto after = cache.storePathFor(dump).filename().string();
    const auto other = cache.storePathFor(twin).filename().string();

    std::smatch b;
    std::smatch a;
    std::smatch o;
    ASSERT_TRUE(std::regex_match(before, b, pattern)) << before;
    ASSERT_TRUE(std::regex_match(after, a, pattern)) << after;
    ASSERT_TRUE(std::regex_match(other, o, pattern)) << other;
    EXPECT_EQ(b[1], a[1]);
    EXPECT_NE(b[2], a[2]);
    EXPECT_NE(b[1], o[1]);
}

// Каталог кэша не зависит от имён организации и приложения: их смена не бросает старые store.
TEST(DefaultCacheDirectory, IsGenericCacheLocationPlusProjectName) {
    const auto organization = QCoreApplication::organizationName();
    const auto application = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("some-org"));
    QCoreApplication::setApplicationName(QStringLiteral("some-app"));

    const auto dir = Phosphor::defaultCacheDirectory();

    QCoreApplication::setOrganizationName(organization);
    QCoreApplication::setApplicationName(application);
    const auto generic = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    ASSERT_FALSE(generic.isEmpty());
    EXPECT_EQ(dir, std::filesystem::path{generic.toStdU16String()} / Phosphor::kName);
    EXPECT_EQ(Phosphor::kName, "phosphor");
}
