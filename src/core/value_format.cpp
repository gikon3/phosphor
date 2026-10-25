#include "core/value_format.hpp"

#include <format>

namespace Phosphor {

std::string formatValue(WaSafe::ValueView v, const RowFormat& f) {
    switch (v.kind()) {
        case WaSafe::ValueKind::LOGIC:
            return v.logic().toString(f.radix, f.isSigned);
        case WaSafe::ValueKind::REAL:
            return std::format("{:g}", v.real());
        case WaSafe::ValueKind::STRING:
            return std::string{v.string()};
        default:
            return {};
    }
}

}  // namespace Phosphor
