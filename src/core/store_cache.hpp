#pragma once

#include <chrono>
#include <filesystem>

namespace Phosphor {

// Место, где лежат индексированные .wsfstore дампов.
class StoreCache {
public:
    // `.part`, в который не писали дольше этого, считается брошенным: идущая индексация пишет в
    // файл и обновляет время его изменения.
    static constexpr std::chrono::hours kStalePartAge{1};

public:
    // Каталог создаётся при первом обращении к storePathFor, а не в конструкторе.
    explicit StoreCache(std::filesystem::path dir);

    // Путь store для дампа: `<stem>-<хеш канонического пути>-<хеш размера и mtime>.wsfstore`.
    // Перезаписанный дамп получает новый store, а старый не подхватывается; store одного дампа
    // узнаются по общему началу имени. Хеш — FNV-1a, а не std::hash: имя должно совпадать между
    // запусками и платформами. Бросает Error, если дампа нет или его атрибуты не читаются.
    [[nodiscard]] std::filesystem::path storePathFor(const std::filesystem::path& dump) const;

    // Удаляет прочие store того же дампа, что и store (тот же stem и хеш пути, другой хеш размера
    // и mtime), и `.part` этого дампа, не изменявшиеся дольше kStalePartAge. Свежий `.part` не
    // трогается: его может писать параллельная загрузка. Ошибки файловых операций игнорируются:
    // на Windows открытый файл не удаляется, и его уберёт следующая чистка.
    void removeOutdated(const std::filesystem::path& store) const;

    [[nodiscard]] const std::filesystem::path& directory() const noexcept;

private:
    std::filesystem::path dir_;
};

// Каталог кэша Phosphor: общий каталог кэша пользователя + имя проекта. Имена организации и
// приложения в путь не входят, поэтому их смена не теряет накопленные store. Без доступного для
// записи каталога кэша — каталог с именем проекта во временном каталоге системы (с предупреждением в журнал),
// иначе store легли бы в текущий каталог процесса.
[[nodiscard]] std::filesystem::path defaultCacheDirectory();

}  // namespace Phosphor
