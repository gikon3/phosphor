#pragma once

#include <cstdint>

namespace Phosphor {

// Номер источника в сессии. Уникален в пределах сессии и не переиспользуется.
using SourceId = std::uint32_t;

}  // namespace Phosphor
