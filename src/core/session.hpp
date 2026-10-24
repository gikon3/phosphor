#pragma once

#include <QObject>
#include <QString>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

#include "core/store_cache.hpp"
#include "core/time_axis.hpp"
#include "core/time_mapper.hpp"
#include "wasafe/model/database.hpp"

namespace Phosphor {

// Номер источника в сессии. Уникален в пределах сессии и не переиспользуется.
using SourceId = std::uint32_t;

enum class SourceState : std::uint8_t { LOADING, READY, FAILED };

struct SourceInfo {
    SourceId id = 0;
    QString label;
    std::filesystem::path path;
    SourceState state = SourceState::LOADING;
    QString error;
};

// Открытые дампы. Каждый источник грузится в своём потоке; результат применяется в потоке
// сессии через очередь событий, поэтому все методы и сигналы относятся к этому потоку.
// Один и тот же дамп можно открыть несколько раз — получатся независимые источники.
class Session : public QObject {
    Q_OBJECT

public:
    explicit Session(StoreCache cache, QObject* parent = nullptr);
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;
    // Останавливает все загрузки и дожидается их потоков: поток не должен пережить сессию,
    // которой он отдаёт результат.
    ~Session() override;

    // Начинает фоновую загрузку и сразу шлёт sourceAdded. Метка источника — имя файла.
    // Итог загрузки — sourceReady или sourceFailed.
    SourceId openDump(const std::filesystem::path& path);

    // Останавливает загрузку; когда поток завершится, источник удаляется (sourceRemoved),
    // даже если загрузка успела закончиться. Для незагружающегося источника ничего не делает.
    void cancelLoad(SourceId id);

    // Удаляет готовый или сломанный источник сразу. Загружающийся — как cancelLoad.
    // К моменту sourceRemoved источника в сессии уже нет, и полученные для него указатели
    // недействительны.
    void removeSource(SourceId id);

    // В порядке открытия.
    [[nodiscard]] std::vector<SourceId> sourceIds() const;

    // nullptr, если источника нет.
    [[nodiscard]] const SourceInfo* info(SourceId id) const;

    // Только для навигации по иерархии в потоке сессии: значения из неё не читаются.
    // nullptr, пока источник не READY.
    [[nodiscard]] const WaSafe::Database* hierarchyDb(SourceId id) const;

    // nullopt, пока источник не READY.
    [[nodiscard]] std::optional<TimeAxis> axis(SourceId id) const;

    // Отдаёт независимую копию БД для чтения значений в другом потоке. Копия открывается в
    // потоке загрузки, чтобы дорогое открытие не шло в потоке сессии. Отдаётся один раз;
    // бросает Error, если источника нет, он не READY или копия уже забрана.
    [[nodiscard]] WaSafe::Database takeRenderDatabase(SourceId id);

    // Объединение непустых диапазонов времени готовых источников; nullopt, если таких нет.
    [[nodiscard]] std::optional<TimeWindow> totalRange() const;

Q_SIGNALS:
    void sourceAdded(Phosphor::SourceId id);
    void sourceReady(Phosphor::SourceId id);
    void sourceFailed(Phosphor::SourceId id, const QString& error);
    void sourceRemoved(Phosphor::SourceId id);

private:
    struct LoadResult;

    struct Source {
        SourceInfo info;
        std::optional<TimeAxis> axis;
        std::optional<TimeWindow> range;
        std::optional<WaSafe::Database> hierarchyDb;
        std::optional<WaSafe::Database> renderDb;
        bool cancelRequested = false;
        std::jthread loader;
    };

private:
    // Ловит всё: исключение, вышедшее из функции std::jthread, вызывает std::terminate.
    static LoadResult loadSource(const std::filesystem::path& path, const StoreCache& cache,
            std::stop_token stop) noexcept;

    void finishLoad(SourceId id, LoadResult& result);
    void erase(SourceId id);

private:
    StoreCache cache_;
    // Номера растут с каждым открытием, поэтому порядок ключей — порядок открытия.
    std::map<SourceId, Source> sources_;
    SourceId nextId_ = 1;
};

}  // namespace Phosphor
