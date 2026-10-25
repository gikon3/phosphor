#pragma once

#include <filesystem>
#include <stop_token>

#include "core/store_cache.hpp"
#include "wasafe/model/database.hpp"

namespace Phosphor {

// Открывает дамп как индексированную БД. Свежий store из кэша переиспользуется; битый
// удаляется, и дамп индексируется заново. Индексация идёт во временный файл рядом со store
// с уникальным именем `<store>.<16 hex>.part`, который переименовывается в store только после
// успешного завершения: прерванная загрузка не оставляет недописанный store, а параллельные
// загрузки одного дампа не мешают друг другу. Открыв store, загрузка удаляет прочие store этого
// дампа (от прежних размера и mtime) и его `.part` старше часа; свежие чужие `.part` не трогаются.
// Бросает ровно три типа исключений:
//   Cancelled — запрошена остановка (свой `.part` удалён);
//   WaSafe::Exception — ошибка чтения дампа или записи/открытия store (свой `.part` удалён);
//   Error — сбой файловой операции кэша (удаление битого store, перенос результата).
[[nodiscard]] WaSafe::Database loadDump(const std::filesystem::path& dump, const StoreCache& cache,
        std::stop_token stop);

}  // namespace Phosphor
