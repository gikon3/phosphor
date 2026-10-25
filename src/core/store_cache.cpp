#include "core/store_cache.hpp"

#include <QChar>
#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QtLogging>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "core/error.hpp"
#include "phosphor/config.hpp"

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

constexpr std::size_t kHashDigits = 16;
constexpr std::u8string_view kStoreSuffix = u8".wsfstore";
constexpr std::u8string_view kPartSuffix = u8".part";

bool isHash(std::u8string_view s) noexcept {
    return s.size() == kHashDigits &&
            std::ranges::all_of(s, [](char8_t c) { return (c >= u8'0' && c <= u8'9') || (c >= u8'a' && c <= u8'f'); });
}

// Хвост имени store после `<stem>-<хеш пути>-`: `<хеш>.wsfstore`.
bool isStoreTail(std::u8string_view tail) noexcept {
    return tail.size() == kHashDigits + kStoreSuffix.size() && tail.ends_with(kStoreSuffix) &&
            isHash(tail.substr(0, kHashDigits));
}

// Хвост имени `.part`: `<хеш>.wsfstore.<16 hex>.part`.
bool isPartTail(std::u8string_view tail) noexcept {
    const auto storeTail = kHashDigits + kStoreSuffix.size();
    return tail.size() == storeTail + 1 + kHashDigits + kPartSuffix.size() && isStoreTail(tail.substr(0, storeTail)) &&
            tail[storeTail] == u8'.' && isHash(tail.substr(storeTail + 1, kHashDigits)) && tail.ends_with(kPartSuffix);
}

}  // namespace

StoreCache::StoreCache(std::filesystem::path dir) : dir_{std::move(dir)} {
}

std::filesystem::path StoreCache::storePathFor(const std::filesystem::path& dump) const {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(dump, ec);
    if (ec)
        throw Error{std::format("cannot resolve dump path '{}': {}", utf8(dump), systemMessage(ec))};
    const auto canonical = std::filesystem::weakly_canonical(absolute, ec);
    if (ec)
        throw Error{std::format("cannot resolve dump path '{}': {}", utf8(dump), systemMessage(ec))};

    const auto size = std::filesystem::file_size(canonical, ec);
    if (ec)
        throw Error{std::format("cannot read size of dump '{}': {}", utf8(dump), systemMessage(ec))};
    const auto mtime = std::filesystem::last_write_time(canonical, ec);
    if (ec)
        throw Error{std::format("cannot read mtime of dump '{}': {}", utf8(dump), systemMessage(ec))};

    std::filesystem::create_directories(dir_, ec);
    if (ec)
        throw Error{std::format("cannot create cache directory '{}': {}", utf8(dir_), systemMessage(ec))};

    const auto version = std::format("{}\n{}", size, mtime.time_since_epoch().count());
    auto name = dump.stem();
    name += std::format("-{:016x}-{:016x}.wsfstore", fnv1a64(utf8(canonical)), fnv1a64(version));
    return dir_ / name;
}

void StoreCache::removeOutdated(const std::filesystem::path& store) const {
    const auto storeName = store.filename().u8string();
    const std::u8string_view storeView{storeName};
    const auto tailSize = kHashDigits + kStoreSuffix.size();
    if (storeView.size() <= tailSize || !isStoreTail(storeView.substr(storeView.size() - tailSize)))
        return;
    const auto prefix = storeView.substr(0, storeView.size() - tailSize);
    const auto staleBefore = std::filesystem::file_time_type::clock::now() - kStalePartAge;

    std::error_code ec;
    for (std::filesystem::directory_iterator it{dir_, ec}, end; !ec && it != end; it.increment(ec)) {
        const auto name = it->path().filename().u8string();
        const std::u8string_view view{name};
        if (!view.starts_with(prefix) || view == storeView)
            continue;
        const auto tail = view.substr(prefix.size());
        std::error_code fileEc;
        if (isStoreTail(tail)) {
            std::filesystem::remove(it->path(), fileEc);
        }
        else if (isPartTail(tail)) {
            const auto written = std::filesystem::last_write_time(it->path(), fileEc);
            if (!fileEc && written < staleBefore)
                std::filesystem::remove(it->path(), fileEc);
        }
    }
}

const std::filesystem::path& StoreCache::directory() const noexcept {
    return dir_;
}

std::filesystem::path defaultCacheDirectory() {
    const auto location = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (!location.isEmpty())
        return std::filesystem::path{location.toStdU16String()} / kName;
    const auto fallback = QDir::tempPath() + QLatin1Char{'/'} + QString::fromUtf8(kName.data(), kName.size());
    qWarning("No writable cache location, using %s", qPrintable(fallback));
    return std::filesystem::path{fallback.toStdU16String()};
}

}  // namespace Phosphor
