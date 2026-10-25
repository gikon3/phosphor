#pragma once

#include <memory>
#include <stop_token>

#include "wasafe/io/builder.hpp"

namespace Phosphor {

// Декоратор, делегирующий всё в sink и прерывающий запись по запросу остановки:
// бросает Cancelled в setTime и на каждом 4096-м valueChange. Проверка не на каждое
// изменение, потому что чтение атомика стоит заметно на горячем пути, а на метках времени
// и на редких блоках изменений отмена всё равно срабатывает быстро.
// Ссылка на sink не владеющая: он обязан пережить декоратор.
[[nodiscard]] std::unique_ptr<WaSafe::Builder> makeCancellingBuilder(WaSafe::Builder& sink, std::stop_token stop);

}  // namespace Phosphor
