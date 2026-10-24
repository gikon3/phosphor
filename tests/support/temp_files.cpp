#include "support/temp_files.hpp"

#include <format>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "core/error.hpp"

namespace Phosphor::Test {

TempDir::TempDir() {
    std::random_device rd;
    const auto base = std::filesystem::temp_directory_path();
    for (;;) {
        auto candidate = base / std::format("phosphor-test-{:08x}{:08x}", rd(), rd());
        std::error_code ec;
        if (std::filesystem::create_directory(candidate, ec)) {
            path_ = std::move(candidate);
            return;
        }
        if (ec)
            throw Error{std::format("cannot create temp directory '{}': {}", candidate.string(), ec.message())};
    }
}

TempDir::~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
}

const std::filesystem::path& TempDir::path() const noexcept {
    return path_;
}

std::filesystem::path writeFile(const std::filesystem::path& p, std::string_view content) {
    std::ofstream out{p, std::ios::binary | std::ios::trunc};
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    if (!out)
        throw Error{std::format("cannot write file '{}'", p.string())};
    return p;
}

const std::string_view kSmallVcd = R"($timescale 1ns $end
$scope module top $end
$var wire 1 ! clk $end
$var wire 8 " data [7:0] $end
$var real 64 # vout $end
$upscope $end
$enddefinitions $end
#0
0!
b00000000 "
r0 #
#5
1!
#10
0!
b10100101 "
r0.5 #
#15
1!
#20
0!
bxxxxxxxx "
)";

const std::string_view kBrokenVcd =
        "$timescale 1ns $end\n$scope module top $end\n$var wire 1 ! a $end\n$upscope $end\n"
        "$enddefinitions $end\n#abc\n1!\n";

}  // namespace Phosphor::Test
