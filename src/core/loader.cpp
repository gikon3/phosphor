#include "core/loader.hpp"

#include <cstdint>
#include <format>
#include <random>
#include <string>
#include <system_error>
#include <utility>

#include "core/cancelling_builder.hpp"
#include "core/error.hpp"
#include "wasafe/core/exception.hpp"
#include "wasafe/io/builder.hpp"
#include "wasafe/io/ingest.hpp"
#include "wasafe/io/store.hpp"
#include "wasafe_vcd/reader.hpp"

namespace Phosphor {

namespace {

// UTF-8 байты пути: не зависят от кодовой страницы и платформы.
std::string utf8(const std::filesystem::path& p) {
    const auto u8 = p.generic_u8string();
    return std::string{u8.begin(), u8.end()};
}

// Уникальное имя временного файла рядом со store: параллельные загрузки одного дампа не
// должны писать в один файл и удалять чужие незавершённые.
std::filesystem::path uniquePartPath(const std::filesystem::path& store) {
    std::random_device device;
    std::mt19937_64 engine{(static_cast<std::uint64_t>(device()) << 32U) | device()};
    auto part = store;
    part += std::format(".{:016x}.part", engine());
    return part;
}

// Удаляет файл, не бросая: вызывается при разборе другой ошибки.
void removeQuietly(const std::filesystem::path& p) noexcept {
    std::error_code ec;
    std::filesystem::remove(p, ec);
}

// Индексирует дамп в part и оставляет закрытый файл на диске.
void indexInto(const std::filesystem::path& dump, const std::filesystem::path& part, std::stop_token stop) {
    WaSafe::Vcd::VcdReader reader{dump};
    auto indexing = WaSafe::makeIndexingBuilder(part);
    auto cancelling = makeCancellingBuilder(*indexing, std::move(stop));
    // БД, держащая part открытым, уничтожается на выходе: открытый файл нельзя переименовать
    // на Windows.
    const auto db = WaSafe::ingest(reader, *cancelling);
}

// Публикует part как store. Если переименовать не вышло, а store уже есть, его создала
// параллельная загрузка (на Windows цель может быть открыта) — тогда берётся он.
void publish(const std::filesystem::path& part, const std::filesystem::path& store) {
    std::error_code ec;
    std::filesystem::rename(part, store, ec);
    if (!ec)
        return;
    const auto renameMessage = ec.message();
    removeQuietly(part);
    std::error_code existsEc;
    if (std::filesystem::exists(store, existsEc))
        return;
    throw Error{std::format("cannot move index to '{}': {}", utf8(store), renameMessage)};
}

}  // namespace

WaSafe::Database loadDump(const std::filesystem::path& dump, const StoreCache& cache, std::stop_token stop) {
    const auto store = cache.storePathFor(dump);
    std::error_code ec;

    if (std::filesystem::exists(store, ec)) {
        try {
            return WaSafe::openStore(store);
        }
        catch (const WaSafe::Exception&) {
            std::filesystem::remove(store, ec);
            if (ec)
                throw Error{std::format("cannot remove broken store '{}': {}", utf8(store), ec.message())};
        }
    }

    const auto part = uniquePartPath(store);
    try {
        indexInto(dump, part, std::move(stop));
    }
    catch (...) {
        removeQuietly(part);
        throw;
    }
    publish(part, store);
    return WaSafe::openStore(store);
}

}  // namespace Phosphor
