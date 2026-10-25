#include <gtest/gtest.h>

#include <stop_token>

#include "core/loader.hpp"
#include "core/store_cache.hpp"
#include "core/value_format.hpp"
#include "support/temp_files.hpp"

TEST(ValueFormat, HexAndSigned) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    const Phosphor::StoreCache cache{tmp.path() / "cache"};
    const auto db = Phosphor::loadDump(dump, cache, {});
    const auto data = db.find("top.data")->node();
    const auto vout = db.find("top.vout")->node();

    EXPECT_EQ(Phosphor::formatValue(db.valueAt(data, 10), {WaSafe::Radix::HEX, false}), "a5");
    EXPECT_EQ(Phosphor::formatValue(db.valueAt(data, 10), {WaSafe::Radix::DEC, true}), "-91");
    EXPECT_EQ(Phosphor::formatValue(db.valueAt(vout, 10), {}), "0.5");
    EXPECT_EQ(Phosphor::formatValue(WaSafe::ValueView{}, {}), "");
    EXPECT_EQ(Phosphor::formatValue(WaSafe::ValueView{std::string_view{"abc"}}, {}), "abc");
}
