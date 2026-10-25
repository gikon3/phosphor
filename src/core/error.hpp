#pragma once

#include <QString>
#include <exception>
#include <stdexcept>
#include <string>
#include <system_error>

namespace Phosphor {

// Ошибка Phosphor. Ошибки сторонних библиотек в неё не оборачиваются. Текст — в UTF-8.
class Error : public std::runtime_error {
public:
    using runtime_error::runtime_error;
};

// Операция прервана по запросу, а не из-за сбоя.
class Cancelled : public Error {
public:
    using Error::Error;
};

// Текст исключения для показа. Текст Error читается как UTF-8, текст прочих исключений — как
// локальная 8-битная кодировка: системные сообщения и пути std::filesystem::path::string на
// Windows приходят в кодовой странице ANSI, а на остальных платформах обе кодировки — UTF-8.
[[nodiscard]] QString errorText(const std::exception& e);

// Системное сообщение ошибки в UTF-8, чтобы его можно было вставить в текст Error.
[[nodiscard]] std::string systemMessage(const std::error_code& ec);

}  // namespace Phosphor
