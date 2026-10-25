#include "core/session.hpp"

#include <QMetaObject>
#include <QMetaType>
#include <algorithm>
#include <exception>
#include <format>
#include <memory>
#include <ranges>
#include <stop_token>
#include <utility>
#include <vector>

#include "core/error.hpp"
#include "core/loader.hpp"

namespace Phosphor {

namespace { constexpr auto kUnknownError = "unknown error while loading dump"; }  // namespace

// Итог одной загрузки: либо cancelled, либо непустой error, либо успех. При успехе заполнены axis,
// hierarchyDb и renderDb, а range — только если диапазон времени дампа непуст (иначе nullopt).
struct Session::LoadResult {
    bool cancelled = false;
    QString error;
    std::optional<TimeAxis> axis;
    std::optional<TimeWindow> range;
    std::optional<WaSafe::Database> hierarchyDb;
    std::optional<WaSafe::Database> renderDb;
};

Session::Session(StoreCache cache, QObject* parent) : QObject{parent}, cache_{std::move(cache)} {
    qRegisterMetaType<SourceId>("Phosphor::SourceId");
}

Session::~Session() {
    // Сначала стоп всем, затем ожидание: загрузки останавливаются параллельно. Результаты,
    // которые потоки успели поставить в очередь, Qt отбрасывает вместе с объектом.
    for (auto& source : sources_ | std::views::values)
        source.loader.request_stop();
    for (auto& source : sources_ | std::views::values) {
        if (source.loader.joinable())
            source.loader.join();
    }
}

SourceId Session::openDump(const std::filesystem::path& path) {
    const auto id = nextId_++;
    SourceInfo info;
    info.id = id;
    info.label = QString::fromStdU16String(path.filename().u16string());
    info.path = path;

    // Поток стартует до вставки источника: если вставка бросит, jthread остановится и будет
    // дождан, а его результат без источника отбросится.
    std::jthread loader{[this, id, path, cache = cache_](std::stop_token stop) {
        const auto result = std::make_shared<LoadResult>(loadSource(path, cache, std::move(stop)));
        QMetaObject::invokeMethod(this, [this, id, result] { finishLoad(id, *result); }, Qt::QueuedConnection);
    }};

    auto& source = sources_[id];
    source.info = std::move(info);
    source.loader = std::move(loader);
    Q_EMIT sourceAdded(id);
    return id;
}

void Session::cancelLoad(SourceId id) {
    const auto it = sources_.find(id);
    if (it == sources_.end() || it->second.info.state != SourceState::LOADING)
        return;
    it->second.cancelRequested = true;
    it->second.loader.request_stop();
}

void Session::removeSource(SourceId id) {
    const auto it = sources_.find(id);
    if (it == sources_.end())
        return;
    if (it->second.info.state == SourceState::LOADING)
        cancelLoad(id);
    else
        erase(id);
}

std::vector<SourceId> Session::sourceIds() const {
    return sources_ | std::views::keys | std::ranges::to<std::vector>();
}

const SourceInfo* Session::info(SourceId id) const {
    const auto it = sources_.find(id);
    return it == sources_.end() ? nullptr : &it->second.info;
}

const WaSafe::Database* Session::hierarchyDb(SourceId id) const {
    const auto it = sources_.find(id);
    if (it == sources_.end())
        return nullptr;
    const auto& db = it->second.hierarchyDb;
    return db ? &*db : nullptr;
}

std::optional<TimeAxis> Session::axis(SourceId id) const {
    const auto it = sources_.find(id);
    return it == sources_.end() ? std::nullopt : it->second.axis;
}

WaSafe::Database Session::takeRenderDatabase(SourceId id) {
    const auto it = sources_.find(id);
    if (it == sources_.end())
        throw Error{std::format("unknown source {}", id)};
    auto& renderDb = it->second.renderDb;
    if (!renderDb)
        throw Error{std::format("render database of source {} is not ready or already taken", id)};
    auto db = std::move(*renderDb);
    renderDb.reset();
    return db;
}

std::optional<TimeWindow> Session::totalRange() const {
    std::optional<TimeWindow> total;
    for (const auto& source : sources_ | std::views::values) {
        if (!source.range)
            continue;
        if (total)
            total = TimeWindow{std::min(total->begin, source.range->begin), std::max(total->end, source.range->end)};
        else
            total = source.range;
    }
    return total;
}

Session::LoadResult Session::loadSource(const std::filesystem::path& path, const StoreCache& cache,
        std::stop_token stop) noexcept {
    LoadResult result;
    try {
        auto db = loadDump(path, cache, std::move(stop));
        const auto axis = TimeAxis::fromScale(db.timeScale());
        const auto local = db.timeRange();
        auto renderDb = db.duplicate();
        result.axis = axis;
        if (!local.empty())
            result.range = TimeWindow{axis.toGlobal(local.begin), axis.toGlobal(local.end)};
        result.renderDb.emplace(std::move(renderDb));
        result.hierarchyDb.emplace(std::move(db));
    }
    catch (const Cancelled&) {
        result.cancelled = true;
    }
    catch (const std::exception& e) {
        result.error = errorText(e);
        if (result.error.isEmpty())
            result.error = QString::fromLatin1(kUnknownError);
    }
    catch (...) {
        result.error = QString::fromLatin1(kUnknownError);
    }
    return result;
}

void Session::finishLoad(SourceId id, LoadResult& result) {
    const auto it = sources_.find(id);
    if (it == sources_.end())
        return;
    auto& source = it->second;
    // Поток уже отдал результат и только завершается, ожидание короткое.
    source.loader.join();

    if (source.cancelRequested || result.cancelled) {
        erase(id);
        return;
    }
    if (!result.error.isEmpty()) {
        source.info.state = SourceState::FAILED;
        source.info.error = result.error;
        // Копия, а не ссылка на поле: обработчик может удалить источник.
        Q_EMIT sourceFailed(id, result.error);
        return;
    }
    source.axis = result.axis;
    source.range = result.range;
    source.hierarchyDb = std::move(result.hierarchyDb);
    source.renderDb = std::move(result.renderDb);
    source.info.state = SourceState::READY;
    Q_EMIT sourceReady(id);
}

void Session::erase(SourceId id) {
    Q_EMIT sourceAboutToBeRemoved(id);
    sources_.erase(id);
    Q_EMIT sourceRemoved(id);
}

}  // namespace Phosphor
