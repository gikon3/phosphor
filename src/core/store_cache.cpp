#include "core/store_cache.hpp"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "core/error.hpp"

namespace Phosphor {

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 0xcbf2'9ce4'8422'2325;
constexpr std::uint64_t kFnvPrime = 0x100'0000'01b3;

std::uint64_t fnv1a64(std::string_view data) noexcept {
    auto hash = kFnvOffsetBasis;
    for (const char c : data) {
        hash ^= static_cast<unsigned char>(c);
        hash *= kFnvPrime;
    }
    return hash;
}

// UTF-8 байты пути: не зависят от кодовой страницы и платформы.
std::string utf8(const std::filesystem::path& p) {
    const auto u8 = p.generic_u8string();
    return std::string{u8.begin(), u8.end()};
}

}  // namespace

StoreCache::StoreCache(std::filesystem::path dir) : dir_{std::move(dir)} {
}

std::filesystem::path StoreCache::storePathFor(const std::filesystem::path& dump) const {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(dump, ec);
    if (ec)
        throw Error{std::format("cannot resolve dump path '{}': {}", utf8(dump), ec.message())};
    const auto canonical = std::filesystem::weakly_canonical(absolute, ec);
    if (ec)
        throw Error{std::format("cannot resolve dump path '{}': {}", utf8(dump), ec.message())};

    const auto size = std::filesystem::file_size(canonical, ec);
    if (ec)
        throw Error{std::format("cannot read size of dump '{}': {}", utf8(dump), ec.message())};
    const auto mtime = std::filesystem::last_write_time(canonical, ec);
    if (ec)
        throw Error{std::format("cannot read mtime of dump '{}': {}", utf8(dump), ec.message())};

    std::filesystem::create_directories(dir_, ec);
    if (ec)
        throw Error{std::format("cannot create cache directory '{}': {}", utf8(dir_), ec.message())};

    const auto key = std::format("{}\n{}\n{}", utf8(canonical), size, mtime.time_since_epoch().count());
    auto name = dump.stem();
    name += std::format("-{:016x}.wsfstore", fnv1a64(key));
    return dir_ / name;
}

const std::filesystem::path& StoreCache::directory() const noexcept {
    return dir_;
}

}  // namespace Phosphor
