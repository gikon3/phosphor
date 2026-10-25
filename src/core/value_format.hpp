#pragma once

#include <string>

#include "wasafe/types/value_view.hpp"

namespace Phosphor {

// Как показывать logic-значения строки.
struct RowFormat {
    WaSafe::Radix radix = WaSafe::Radix::HEX;
    bool isSigned = false;

    friend bool operator==(const RowFormat&, const RowFormat&) = default;
};

// Текст значения для подписи. Logic — в заданном основании, real — кратчайшая запись
// (`{:g}`), строка — как есть; невалидное значение даёт пустую строку.
std::string formatValue(WaSafe::ValueView v, const RowFormat& f);

}  // namespace Phosphor
