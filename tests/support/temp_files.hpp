#pragma once

#include <filesystem>
#include <string_view>

namespace Phosphor::Test {

// Уникальный временный каталог, который удаляется вместе с содержимым при уничтожении.
class TempDir {
public:
    TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
    ~TempDir();

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
    std::filesystem::path path_;
};

// Записывает content в файл p (перезаписывая) и возвращает p. Бросает Error при сбое.
std::filesystem::path writeFile(const std::filesystem::path& p, std::string_view content);

// Небольшой VCD с одноразрядным, векторным и вещественным сигналами.
extern const std::string_view kSmallVcd;

}  // namespace Phosphor::Test
