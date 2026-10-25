#pragma once

#include <QByteArray>
#include <QLatin1StringView>
#include <span>
#include <vector>

#include "core/view.hpp"

namespace Phosphor {

// Тип данных перетаскивания сигналов; содержимое — JSON-массив {"source": …, "path": …}.
inline constexpr QLatin1StringView kSignalRefsMimeType{"application/x-phosphor-signals"};

[[nodiscard]] QByteArray encodeSignalRefs(std::span<const SignalRef> refs);

// Данные приходят извне, поэтому разбор терпим к мусору: не-массив даёт пустой результат, а
// элементы без пути, с пустым путём или с номером источника вне SourceId пропускаются.
[[nodiscard]] std::vector<SignalRef> decodeSignalRefs(const QByteArray& data);

}  // namespace Phosphor
