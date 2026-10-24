#pragma once

#include <stdexcept>

namespace Phosphor {

// Ошибка Phosphor. Ошибки библиотек wasafe в неё не оборачиваются.
class Error : public std::runtime_error {
public:
    using runtime_error::runtime_error;
};

// Операция прервана по запросу, а не из-за сбоя.
class Cancelled : public Error {
public:
    using Error::Error;
};

}  // namespace Phosphor
