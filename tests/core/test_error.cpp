#include <gtest/gtest.h>

#include <QString>
#include <stdexcept>
#include <string>
#include <system_error>

#include "core/error.hpp"

// Тексты ошибок Phosphor формируются в UTF-8.
TEST(ErrorText, OwnErrorsAreUtf8) {
    const Phosphor::Error e{"caf\xc3\xa9"};

    EXPECT_EQ(Phosphor::errorText(e), QString::fromUtf8("caf\xc3\xa9"));
    EXPECT_EQ(Phosphor::errorText(e).size(), 4);
}

TEST(ErrorText, CancelledIsOwnError) {
    const Phosphor::Cancelled e{"caf\xc3\xa9"};

    EXPECT_EQ(Phosphor::errorText(e), QString::fromUtf8("caf\xc3\xa9"));
}

// Тексты прочих исключений — в локальной 8-битной кодировке: на Windows это кодовая страница ANSI.
TEST(ErrorText, ForeignErrorsUseLocalEncoding) {
    const std::runtime_error e{"caf\xc3\xa9"};

    EXPECT_EQ(Phosphor::errorText(e), QString::fromLocal8Bit("caf\xc3\xa9"));
}

TEST(SystemMessage, IsUtf8OfSystemText) {
    const auto ec = std::make_error_code(std::errc::no_such_file_or_directory);

    const auto message = Phosphor::systemMessage(ec);

    EXPECT_FALSE(message.empty());
    EXPECT_EQ(QString::fromUtf8(message), QString::fromLocal8Bit(ec.message()));
}
