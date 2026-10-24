#pragma once

#include <filesystem>

namespace Phosphor {

// Место, где лежат индексированные .wsfstore дампов.
class StoreCache {
public:
    // Каталог создаётся при первом обращении к storePathFor, а не в конструкторе.
    explicit StoreCache(std::filesystem::path dir);

    // Путь store для дампа. Имя зависит от канонического пути, размера и времени изменения
    // дампа, поэтому перезаписанный дамп получает новый store, а старый не подхватывается.
    // Хеш — FNV-1a, а не std::hash: ключ должен совпадать между запусками и платформами.
    // Бросает Error, если дампа нет или его атрибуты не читаются.
    [[nodiscard]] std::filesystem::path storePathFor(const std::filesystem::path& dump) const;

    [[nodiscard]] const std::filesystem::path& directory() const noexcept;

private:
    std::filesystem::path dir_;
};

}  // namespace Phosphor
