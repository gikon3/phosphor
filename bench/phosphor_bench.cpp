#include <QCoreApplication>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/digital_frame.hpp"
#include "core/loader.hpp"
#include "core/renderer.hpp"
#include "core/store_cache.hpp"
#include "core/time_axis.hpp"
#include "core/time_mapper.hpp"
#include "core/value_format.hpp"
#include "wasafe/model/database.hpp"

namespace {

using Clock = std::chrono::steady_clock;

constexpr double kReopenLimitMs = 1000.0;
constexpr double kFrameLimitMs = 100.0;
constexpr int kBitRows = 40;
constexpr int kRealRows = 10;
constexpr int kRequestPeriodMs = 16;
constexpr int kContinuousDurationMs = 2000;
constexpr std::uint64_t kScrollDivisor = 100;

struct Options {
    std::filesystem::path dump;
    std::filesystem::path cacheDir = std::filesystem::temp_directory_path() / "phosphor-bench-cache";
    int rows = 50;
    int width = 1920;
};

double elapsedMs(Clock::time_point from) {
    return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
}

void printHeader() {
    std::println("{:<44} {:>10} {:>10}  {}", "operation", "ms", "limit", "result");
}

void printRow(const std::string& name, double ms, std::optional<double> limit) {
    if (limit)
        std::println("{:<44} {:>10.1f} {:>10.0f}  {}", name, ms, *limit, ms <= *limit ? "OK" : "FAIL");
    else
        std::println("{:<44} {:>10.1f} {:>10}  {}", name, ms, "-", "-");
}

bool parseInt(const std::string& text, int& out) {
    const auto* const end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, out);
    return ec == std::errc{} && ptr == end;
}

// Применяет опцию со значением; false, если имя неизвестно или значение некорректно.
bool applyOption(Options& o, std::string_view name, std::string_view value) {
    if (name == "--cache") {
        o.cacheDir = std::string{value};
        return true;
    }
    if (name == "--rows")
        return parseInt(std::string{value}, o.rows);
    if (name == "--width")
        return parseInt(std::string{value}, o.width);
    return false;
}

std::optional<Options> parseOptions(std::span<const std::string_view> args) {
    Options o;
    bool haveDump = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i].starts_with("--")) {
            if (i + 1 >= args.size() || !applyOption(o, args[i], args[i + 1]))
                return std::nullopt;
            ++i;
        }
        else if (!haveDump) {
            o.dump = std::string{args[i]};
            haveDump = true;
        }
        else {
            return std::nullopt;
        }
    }
    if (!haveDump || o.rows < 1 || o.width < 1)
        return std::nullopt;
    return o;
}

// Первые rows - realRows строк берутся из однобитных сигналов и шин, остальные из real.
std::vector<WaSafe::NodeId> pickRows(const WaSafe::Database& db, int rows) {
    const int realRows = std::min(kRealRows, rows / 5);
    const int digitalRows = rows - realRows;
    std::vector<WaSafe::NodeId> digital;
    std::vector<WaSafe::NodeId> reals;
    for (const auto node : db.leafNodes(db.root().id())) {
        const bool isReal = db.signalHandle(node).kind() == WaSafe::TypeKind::REAL;
        if (isReal && std::ssize(reals) < realRows)
            reals.push_back(node);
        else if (!isReal && std::ssize(digital) < digitalRows)
            digital.push_back(node);
        if (std::ssize(reals) >= realRows && std::ssize(digital) >= digitalRows)
            break;
    }
    digital.insert(digital.end(), reals.begin(), reals.end());
    return digital;
}

std::vector<Phosphor::RowRequest> makeRows(std::span<const WaSafe::NodeId> nodes) {
    std::vector<Phosphor::RowRequest> rows;
    rows.reserve(nodes.size());
    for (const auto node : nodes)
        rows.push_back({1, node, {}});
    return rows;
}

struct Window {
    const char* name;
    Phosphor::TimeWindow window;
};

std::vector<Window> makeWindows(Phosphor::TimeWindow all) {
    const Phosphor::GlobalTime span = all.end - all.begin;
    // Окна лежат в разных местах диапазона, и ни одно не перекрывает прочитанное предыдущим,
    // поэтому каждый кадр — прыжок в новую область. Весь диапазон идёт последним.
    const auto at = [&](Phosphor::GlobalTime divisor, int percent) {
        const Phosphor::GlobalTime width = std::max<Phosphor::GlobalTime>(span / divisor, Phosphor::kMinWindowSpan);
        const Phosphor::GlobalTime begin = all.begin + span / 100 * percent;
        return Phosphor::TimeWindow{begin, begin + width};
    };
    return {{"1/10000", at(10'000, 70)}, {"1/100", at(100, 20)}, {"whole range", all}};
}

double timeFrame(const WaSafe::Database& db, std::span<const WaSafe::NodeId> nodes, const Phosphor::TimeAxis& axis,
        Phosphor::TimeWindow window, int width) {
    const Phosphor::RowFormat fmt;
    std::size_t spans = 0;
    const auto start = Clock::now();
    for (const auto node : nodes)
        spans += Phosphor::buildDigitalFrame(db, node, axis, window, width, fmt).spans.size();
    const double ms = elapsedMs(start);
    // Запись в volatile не даёт оптимизатору выбросить построение кадра.
    volatile std::size_t sink = 0;
    sink = spans;
    return ms;
}

struct Continuous {
    int requests = 0;
    int delivered = 0;
    double firstMs = 0;      // от первого запроса до первой доставки; 0, если кадров не было
    double lastMs = 0;       // момент последней доставки от первого запроса
    double medianGapMs = 0;  // медиана промежутков между доставками
    double maxGapMs = 0;     // включает начало (до первой доставки) и конец (от последней до остановки)
};

double medianOf(std::vector<double> values) {
    if (values.empty())
        return 0;
    std::ranges::sort(values);
    return values[values.size() / 2];
}

// Новый запрос каждые 16 мс в течение 2 с. При windowDivisor > 1 окно шириной 1/windowDivisor
// диапазона сдвигается на 1 % своей ширины за запрос и отражается от границ данных, поэтому
// нагрузка кадра не меняется. При windowDivisor == 1 окно всегда равно всему диапазону, а новый
// кадр вынуждает сдвиг курсора A на 1 % диапазона (с оборотом). Промежутки считаются от первого
// запроса до первой доставки, между доставками и от последней доставки до остановки: рост
// промежутка при непрерывном вводе означает, что кадры не доходят.
Continuous measureContinuous(WaSafe::Database db, const Phosphor::TimeAxis& axis, Phosphor::TimeWindow all,
        std::span<const WaSafe::NodeId> nodes, int width, Phosphor::GlobalTime windowDivisor) {
    Phosphor::Renderer renderer;
    renderer.addSource(1, std::move(db), axis);

    Continuous result;
    std::optional<Clock::time_point> origin;
    std::vector<double> deliveries;
    QObject::connect(&renderer, &Phosphor::Renderer::frameReady, &renderer, [&](const Phosphor::FrameResult&) {
        if (origin)
            deliveries.push_back(std::chrono::duration<double, std::milli>(Clock::now() - *origin).count());
    });

    Phosphor::FrameRequest request;
    request.widthPx = width;
    request.rows = makeRows(nodes);
    const Phosphor::GlobalTime total = all.end - all.begin;
    const Phosphor::GlobalTime span = std::max<Phosphor::GlobalTime>(total / windowDivisor, 100);
    const Phosphor::GlobalTime step =
            std::max<Phosphor::GlobalTime>(span / static_cast<Phosphor::GlobalTime>(kScrollDivisor), 1);
    const Phosphor::GlobalTime travel = total - span;
    Phosphor::GlobalTime offset = 0;
    Phosphor::GlobalTime direction = 1;

    QTimer ticker;
    QTimer stopper;
    stopper.setSingleShot(true);
    QObject::connect(&ticker, &QTimer::timeout, &ticker, [&] {
        if (!origin)
            origin = Clock::now();
        request.generation = static_cast<std::uint64_t>(++result.requests);
        if (windowDivisor > 1) {
            request.window = {all.begin + offset, all.begin + offset + span};
            offset += direction * step;
            if (offset < 0 || offset > travel) {
                direction = -direction;
                offset = std::clamp<Phosphor::GlobalTime>(offset, 0, travel);
            }
        }
        else {
            request.window = all;
            request.cursorA = all.begin + offset;
            offset = (offset + step) % total;
        }
        renderer.request(request);
    });
    QObject::connect(&stopper, &QTimer::timeout, QCoreApplication::instance(), &QCoreApplication::quit);
    ticker.start(kRequestPeriodMs);
    stopper.start(kContinuousDurationMs);
    QCoreApplication::exec();

    const double endMs =
            std::chrono::duration<double, std::milli>(Clock::now() - origin.value_or(Clock::now())).count();
    result.delivered = static_cast<int>(deliveries.size());
    std::vector<double> gaps;
    double previous = 0;
    for (const double t : deliveries) {
        gaps.push_back(t - previous);
        previous = t;
    }
    gaps.push_back(endMs - previous);
    result.maxGapMs = *std::ranges::max_element(gaps);
    if (!deliveries.empty()) {
        result.firstMs = deliveries.front();
        result.lastMs = deliveries.back();
        gaps.pop_back();
        gaps.erase(gaps.begin());
        result.medianGapMs = medianOf(gaps);
    }
    return result;
}

int run(const Options& o) {
    const Phosphor::StoreCache cache{o.cacheDir};
    // Первая индексация измеряется всегда: store, оставшийся от прошлого запуска, удаляется.
    std::filesystem::remove(cache.storePathFor(o.dump));

    std::println("dump: {} ({:.1f} MiB)", o.dump.string(),
            static_cast<double>(std::filesystem::file_size(o.dump)) / (1024.0 * 1024.0));
    printHeader();

    auto start = Clock::now();
    {
        const auto indexed = Phosphor::loadDump(o.dump, cache, {});
        printRow("first indexing", elapsedMs(start), std::nullopt);
    }

    start = Clock::now();
    const auto db = Phosphor::loadDump(o.dump, cache, {});
    printRow("reopen (fresh store)", elapsedMs(start), kReopenLimitMs);

    const auto axis = Phosphor::TimeAxis::fromScale(db.timeScale());
    const auto range = db.timeRange();
    const Phosphor::TimeWindow all{axis.toGlobal(range.begin), axis.toGlobal(range.end)};
    const auto nodes = pickRows(db, o.rows);
    std::println("rows: {}, width: {} px, window: {} .. {}", nodes.size(), o.width, Phosphor::formatTime(all.begin),
            Phosphor::formatTime(all.end));

    const auto windows = makeWindows(all);
    for (const auto& w : windows)
        printRow(std::string{"frame, "} + w.name, timeFrame(db, nodes, axis, w.window, o.width), kFrameLimitMs);
    for (const auto& w : windows)
        printRow(std::string{"frame, "} + w.name + " (repeat)", timeFrame(db, nodes, axis, w.window, o.width),
                std::nullopt);

    for (const auto divisor : {100, 1}) {
        const auto c = measureContinuous(db.duplicate(), axis, all, nodes, o.width, divisor);
        std::println(
                "continuous, window 1/{} of range: requests {}, delivered {}, first {:.1f} ms, "
                "median gap {:.1f} ms, last {:.1f} ms, max gap {:.1f} ms (report only)",
                divisor, c.requests, c.delivered, c.firstMs, c.medianGapMs, c.lastMs, c.maxGapMs);
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) try {
    const QCoreApplication app{argc, argv};
    std::vector<std::string_view> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    const auto options = parseOptions(args);
    if (!options) {
        std::println(stderr, "usage: phosphor-bench DUMP [--cache DIR] [--rows 50] [--width 1920]");
        return 2;
    }
    return run(*options);
}
catch (const std::exception& e) {
    static_cast<void>(std::fputs(e.what(), stderr));
    return 1;
}
catch (...) {
    static_cast<void>(std::fputs("unknown exception", stderr));
    return 1;
}
