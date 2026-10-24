# Phosphor: этапы 0–1 (скелет и вертикальный срез) — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Чистый скелет проекта с Qt6 и рабочий срез «открыл VCD в фоне → дерево
иерархии → цифровые строки на холсте с масштабом, прокруткой и курсорами A/B»,
плюс стенд для замера скорости.

**Architecture:** Три цели CMake. `phosphor_core` — статическая библиотека без
QtGui/Widgets (QtCore допустим): время, кэш store, загрузка, сессия, вид,
прореживание и рабочий поток отрисовки. `phosphor_ui` — статическая библиотека
виджетов. `phosphor` — только `main.cpp`. GUI-поток трогает `WaSafe::Database`
только для навигации по иерархии (она в ОЗУ и неизменяема). Значения читает
исключительно рабочий поток `Renderer` через собственный `duplicate()`.

**Tech Stack:** C++23, CMake ≥ 3.24, Conan 2 (wasafe-vcd, gtest), системный
Qt 6 (Core, Widgets, Test), GoogleTest, Python 3 (генератор VCD).

**Spec:** [docs/superpowers/specs/2026-10-24-phosphor-design.md](../specs/2026-10-24-phosphor-design.md)

## Global Constraints

- C++23; GCC 14+, Clang 18+, MSVC 19.40+; Linux и Windows.
- Qt 6 — только через `find_package(Qt6 ... REQUIRED COMPONENTS ...)`, не через Conan.
- Остальные зависимости — Conan (`wasafe-vcd/0.1.0`, `gtest/1.18.0`).
- Пространство имён `Phosphor`. Файлы `snake_case.hpp/.cpp`, методы `camelCase`,
  закрытые поля с суффиксом `_` (как в wasafe).
- Модель ошибок — исключения. `Phosphor::Error : std::runtime_error` для своих ошибок.
  Ошибки wasafe (`WaSafe::Exception`) пробрасываются как есть.
- Строковые литералы в коде — на английском, без кириллицы; видимый пользователю текст идёт через `tr()`, русский появляется только в файлах переводов.
- Комментарии на русском. Они описывают контракт и причину. Не ссылаются на
  прошлую реализацию, на код в других файлах и на вызывающих.
- Структура классов по глобальным правилам: `public` → `protected` → `private`;
  внутри блока типы → константы → методы (фабрики, специальные, обычные,
  операторы) → поля. Спецификатор доступа повторяется перед каждым блоком,
  пустые блоки не пишутся.
- Форматирование — `.clang-format` проекта, проверки — `.clang-tidy` проекта.
- Ни одного вызова `valueAt`/`changes`/`nextChange`/`prevChange` у `Database`
  в GUI-потоке.
- Все цели производительности — таблица 5.1 спеки. Холст 1920 px, 50 строк.
- Работа идёт в ветке `feat/stage-0-1`, не в `main`.
- Тесты перечисляются в `tests/CMakeLists.txt` явно: каждая задача добавляет свои файлы. `tests/core/*` идут в `phosphor_core_tests`, `tests/app/*` — в `phosphor_ui_tests`.

Команды сборки и тестов (Linux, существующий пресет):

```bash
.venv/bin/conan install . --build=missing -s compiler.cppstd=23 -s build_type=Debug
cmake --preset conan-debug
cmake --build --preset conan-debug
ctest --preset conan-debug --output-on-failure
```

## Review Focus

1. **Отмена посреди индексации.** Следующее открытие того же дампа обязано
   переиндексировать, а не открыть недописанный store. Тест — в задаче 4.
2. **Дамп перезаписан пересимуляцией** (тот же путь, другой mtime или размер).
   Нужен новый store, старый вид не должен показываться. Тест — в задаче 3.
3. **Окно целиком до первого изменения, после последнего, дамп без меток
   времени.** Пустые или NONE-спаны вместо падения. Тест — в задаче 5.
4. **Предельный зум и нулевая ширина холста** (окно в несколько fs; скрытый
   виджет шириной 0). Никакого деления на ноль, пустой кадр. Тесты — в задачах 2 и 5.
5. **Битый VCD рядом с исправным.** Ошибка сообщается, второй источник
   продолжает рисоваться. Тест — в задаче 6.

---

## Карта файлов

```plaintext
CMakeLists.txt                 Qt6, опции PHOSPHOR_BUILD_TESTS / PHOSPHOR_BUILD_BENCH
conanfile.py                   рецепт приложения
src/CMakeLists.txt             цели phosphor_core, phosphor_ui, phosphor
src/version.hpp.in             Phosphor::Version
src/config.hpp.in              Phosphor::kDescription, kHomePageUrl
src/main.cpp                   QApplication, разбор CLI, MainWindow
src/core/error.hpp             Phosphor::Error, Phosphor::Cancelled
src/core/time_axis.hpp/.cpp    GlobalTime, TimeAxis, formatTime
src/core/store_cache.hpp/.cpp  StoreCache — путь store по ключу дампа
src/core/cancelling_builder.hpp/.cpp  декоратор Builder с std::stop_token
src/core/loader.hpp/.cpp       loadDump — кэш → openStore либо индексация
src/core/session.hpp/.cpp      Session — открытые источники, фоновая загрузка
src/core/view.hpp/.cpp         View — строки, окно времени, курсоры
src/core/time_mapper.hpp/.cpp  зум/прокрутка/пиксели ↔ время (чистые функции)
src/core/value_format.hpp/.cpp RowFormat, formatValue
src/core/digital_frame.hpp/.cpp buildDigitalFrame — прореживание строки
src/core/renderer.hpp/.cpp     Renderer — рабочий поток кадров
src/app/hierarchy_model.hpp/.cpp  QAbstractItemModel над Session
src/app/signal_mime.hpp/.cpp   MIME перетаскивания сигналов
src/app/main_window.hpp/.cpp   окно, меню, док, статус загрузки
src/app/wave_panel.hpp/.cpp    колонки имён/значений + холст + скроллбары
src/app/wave_canvas.hpp/.cpp   линейка, волны, курсоры, мышь
src/app/shortcuts.hpp          все сочетания клавиш в одном месте
tests/CMakeLists.txt
tests/core_main.cpp            QCoreApplication + gtest (phosphor_core_tests)
tests/ui_main.cpp              QApplication (offscreen) + gtest (phosphor_ui_tests)
tests/support/temp_files.hpp/.cpp  TempDir, writeFile, kSmallVcd (библиотека phosphor_test_support)
tests/core/*.cpp, tests/app/*.cpp
tools/gen_vcd.py               генератор синтетического VCD
bench/CMakeLists.txt, bench/phosphor_bench.cpp
```

---

### Task 1: Скелет — чистка следов wasafe-vcd, Qt6, три цели

**Files:**

- Modify: `CMakeLists.txt`, `conanfile.py`, `src/CMakeLists.txt`, `src/version.hpp.in`, `src/config.hpp.in`, `src/main.cpp`, `tests/CMakeLists.txt`
- Create: `src/core/error.hpp`, `src/app/main_window.hpp/.cpp` (пустое окно), `tests/core_main.cpp`, `tests/ui_main.cpp`, `tests/core/test_version.cpp`
- Delete: `tests/test_empty.cpp`

**Interfaces:**

- Produces:
  - цели `phosphor_core` (STATIC, alias `phosphor::core`, файл `phosphor-core`, линкует `wasafe::wasafe_vcd`, `Qt6::Core`), `phosphor_ui` (STATIC, alias `phosphor::ui`, файл `phosphor-ui`, линкует `phosphor::core`, `Qt6::Widgets`), `phosphor` (исполняемый, `main.cpp`);
  - include-корень `src/` (`#include "core/time_axis.hpp"`), сгенерированные `version.hpp`/`config.hpp` в `${CMAKE_CURRENT_BINARY_DIR}/include/phosphor/`;
  - `namespace Phosphor::Version { kMajor, kMinor, kPatch, kString }`;
  - `class Error : public std::runtime_error { using runtime_error::runtime_error; }` и `class Cancelled : public Error` в `src/core/error.hpp`;
  - `class Phosphor::MainWindow : public QMainWindow` (пока пустое);
  - две цели тестов: `phosphor_core_tests` (файл `phosphor-core-tests`, `main` из `tests/core_main.cpp` с `QCoreApplication`, линкует только `phosphor::core` — граница слоёв проверяется сборкой) и `phosphor_ui_tests` (файл `phosphor-ui-tests`, `main` из `tests/ui_main.cpp` с `QApplication`; без `QT_QPA_PLATFORM` ставит `offscreen`; ctest тоже задаёт `QT_QPA_PLATFORM=offscreen`).

- [x] **Step 1: Привести `conanfile.py` к приложению.** Убрать `package_info` (библиотеки у приложения нет), `cmake/*` из `exports_sources`, `rmdir lib/cmake` из `package()`. `layout()` оставить с `cmake_layout`.
- [x] **Step 2: Корневой `CMakeLists.txt`.**
  - Опции: `PHOSPHOR_BUILD_TESTS` (как сейчас), `PHOSPHOR_BUILD_BENCH` (OFF).
  - Найти Qt: `find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Test)`, затем `qt_standard_project_setup()`.
  - `add_subdirectory(bench)` под опцией `PHOSPHOR_BUILD_BENCH`.
- [x] **Step 3: `src/CMakeLists.txt`.**
  - Три цели из Interfaces.
  - `configure_file` в `include/phosphor/`.
  - `install(TARGETS phosphor RUNTIME ...)` под `PHOSPHOR_INSTALL`. Всё про `WASAFE_VCD_*` и экспорт cmake-пакета удалить.
- [x] **Step 4: Переписать `version.hpp.in`/`config.hpp.in`** в пространство имён `Phosphor`.
- [x] **Step 5: Написать `main.cpp`.** Сделать `QApplication` и `QCommandLineParser` с `--version` (печатает `phosphor <kString>`) и позиционными `dump` (пока игнорируются), затем показать `MainWindow`.
- [x] **Step 6: Написать падающий тест** `tests/core/test_version.cpp`:

```cpp
TEST(Version, MatchesProject) {
    EXPECT_EQ(Phosphor::Version::kString, std::format("{}.{}.{}", Phosphor::Version::kMajor,
            Phosphor::Version::kMinor, Phosphor::Version::kPatch));
}
```

`tests/CMakeLists.txt`: файлы перечисляются явно, без `file(GLOB)`. `phosphor_core_tests` — `core_main.cpp` + `core/…`, линкует `phosphor::core`, `GTest::gtest`, `Qt6::Test`; `phosphor_ui_tests` — `ui_main.cpp` + `app/…`, линкует `phosphor::core`, `phosphor::ui`, `GTest::gtest`, `Qt6::Test`. Дальше `gtest_discover_tests(... PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen DISCOVERY_TIMEOUT 30)`.

- [x] **Step 7: Собрать и прогнать.**
  - Запуск: команды сборки из Global Constraints, затем `QT_QPA_PLATFORM=offscreen build/Debug/bin/phosphor --version`.
  - Ожидается: тест `Version.MatchesProject` PASS, вывод `phosphor 0.1.0`.
- [x] **Step 8: Проверить формат.**
  - Запуск: `clang-format --dry-run --Werror $(git ls-files 'src/*.cpp' 'src/*.hpp' 'tests/*.cpp' 'tests/*.hpp')`.
  - Ожидается: без вывода.
- [x] **Step 9: Стоп и коммит.** Показать итог и проверки, дождаться «продолжить», затем `git commit -m "build: скелет Phosphor — Qt6, цели core/ui/app, чистка следов wasafe-vcd"`

---

### Task 2: Общая ось времени и геометрия окна

**Files:**

- Create: `src/core/time_axis.hpp/.cpp`, `src/core/time_mapper.hpp/.cpp`
- Test: `tests/core/test_time_axis.cpp`, `tests/core/test_time_mapper.cpp`

**Interfaces:**

- Produces (`time_axis.hpp`):
  - `using GlobalTime = std::int64_t;` — общая ось в фемтосекундах. Спека требует «самую мелкую шкалу среди открытых»; здесь это реализовано как fs, самая мелкая единица wasafe. Тогда открытие нового дампа не пересчитывает уже сохранённые времена;
  - `class TimeAxis { static TimeAxis fromScale(WaSafe::TimeScale); GlobalTime toGlobal(WaSafe::TimeStamp) const noexcept; WaSafe::TimeStamp toLocalFloor(GlobalTime) const noexcept; WaSafe::TimeStamp toLocalCeil(GlobalTime) const noexcept; std::int64_t femtosecondsPerUnit() const noexcept; }`. `fromScale` бросает `Error`, если `exponent < -15` или множитель не помещается в int64. `toGlobal` насыщается к `INT64_MIN/MAX`, `kNoTime` переходит в `INT64_MIN`;
  - `std::string formatTime(GlobalTime t)` — автоединица fs/ps/ns/us/ms/s, до 3 знаков после точки, без хвостовых нулей.
- Produces (`time_mapper.hpp`):
  - `struct TimeWindow { GlobalTime begin; GlobalTime end; friend bool operator==(const TimeWindow&, const TimeWindow&) = default; };`
  - `inline constexpr GlobalTime kMinWindowSpan = 1;`
  - `double timeToPixel(TimeWindow w, int widthPx, GlobalTime t) noexcept;`
  - `GlobalTime pixelToTime(TimeWindow w, int widthPx, double x) noexcept;`
  - `TimeWindow zoomAround(TimeWindow w, GlobalTime pivot, double factor) noexcept;` — `factor < 1` приближает, пропорция pivot сохраняется, ширина не меньше `kMinWindowSpan`;
  - `TimeWindow scrollBy(TimeWindow w, GlobalTime delta) noexcept;` — с насыщением, ширина сохраняется.
  - При `widthPx <= 0`: `timeToPixel` возвращает 0, `pixelToTime` возвращает `w.begin`.

- [x] **Step 1: Написать падающие тесты.**

```cpp
TEST(TimeAxis, ConvertsNanoseconds) {
    const auto a = Phosphor::TimeAxis::fromScale({.exponent = -9, .scale = 10});
    EXPECT_EQ(a.femtosecondsPerUnit(), 10'000'000);
    EXPECT_EQ(a.toGlobal(3), 30'000'000);
    EXPECT_EQ(a.toLocalFloor(35'000'000), 3);
    EXPECT_EQ(a.toLocalCeil(35'000'000), 4);
}
TEST(TimeAxis, RejectsSubFemtosecond) { EXPECT_THROW(Phosphor::TimeAxis::fromScale({.exponent = -18, .scale = 1}), Phosphor::Error); }
TEST(TimeAxis, SaturatesOnOverflow) {
    const auto a = Phosphor::TimeAxis::fromScale({.exponent = 0, .scale = 1});
    EXPECT_EQ(a.toGlobal(std::numeric_limits<std::int64_t>::max() / 2), std::numeric_limits<std::int64_t>::max());
}
TEST(FormatTime, PicksUnit) {
    EXPECT_EQ(Phosphor::formatTime(340'000'000), "340 ns");
    EXPECT_EQ(Phosphor::formatTime(1'500'000), "1.5 ns");
    EXPECT_EQ(Phosphor::formatTime(0), "0 s");
}
TEST(TimeMapper, ZoomKeepsPivotFraction) {
    const auto w = Phosphor::zoomAround({0, 1000}, 250, 0.5);
    EXPECT_EQ(w, (Phosphor::TimeWindow{125, 625}));
}
TEST(TimeMapper, ZoomStopsAtMinSpan) {
    const auto w = Phosphor::zoomAround({0, 2}, 1, 1e-6);
    EXPECT_GE(w.end - w.begin, Phosphor::kMinWindowSpan);
}
TEST(TimeMapper, ZeroWidthIsSafe) {
    EXPECT_EQ(Phosphor::timeToPixel({0, 100}, 0, 50), 0.0);
    EXPECT_EQ(Phosphor::pixelToTime({0, 100}, 0, 10.0), 0);
}
TEST(TimeMapper, PixelRoundTrip) { EXPECT_EQ(Phosphor::pixelToTime({1000, 2000}, 1000, Phosphor::timeToPixel({1000, 2000}, 1000, 1500)), 1500); }
```

- [x] **Step 2: Прогнать и убедиться, что падают.**
  - Запуск: `cmake --build --preset conan-debug && ctest --preset conan-debug -R 'TimeAxis|FormatTime|TimeMapper'`.
  - Ожидается: ошибка компиляции, заголовков нет.
- [x] **Step 3: Реализовать** `time_axis` и `time_mapper` по интерфейсам выше. Внутри — арифметика `__int128` или `std::mul_overflow`-аналог с насыщением; на MSVC `__int128` нет, поэтому через проверку `a > max / b`.
- [x] **Step 4: Прогнать.** Те же тесты, ожидается PASS.
- [x] **Step 5: Стоп и коммит.** Показать итог и проверки, дождаться «продолжить», затем `git commit -m "add: общая ось времени в fs и геометрия окна"`

---

### Task 3: Кэш store

**Files:**

- Create: `src/core/store_cache.hpp/.cpp`, `tests/support/temp_files.hpp/.cpp` — статическая библиотека `phosphor_test_support` (линкует `phosphor::core`), её линкуют оба тестовых бинарника
- Test: `tests/core/test_store_cache.cpp`

**Interfaces:**

- Produces:
  - `class StoreCache { explicit StoreCache(std::filesystem::path dir); std::filesystem::path storePathFor(const std::filesystem::path& dump) const; const std::filesystem::path& directory() const noexcept; }`.
    - Имя файла — `<stem дампа>-<16 hex>.wsfstore`, где hex — FNV-1a 64 от строки `"<абсолютный канонический путь>\n<размер>\n<mtime в тиках>"`. FNV, а не `std::hash`: ключ должен совпадать между запусками и платформами.
    - Каталог создаётся по требованию.
    - Если дампа нет, бросает `Error`.
  - Поддержка тестов (`tests/support/temp_files.hpp`): `class TempDir` (RAII, `path()`), `std::filesystem::path writeFile(const std::filesystem::path& p, std::string_view content)`, `extern const std::string_view kSmallVcd`. Это VCD из спеки-примера ниже, на нём держатся задачи 4–10:

```text
$timescale 1ns $end
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
```

- [x] **Step 1: Написать падающие тесты.**
  - `StoreCache.SameDumpSamePath`: два вызова дают одинаковый путь, он лежит в `directory()` и заканчивается на `.wsfstore`.
  - `StoreCache.RewrittenDumpNewPath`: переписать файл с другим содержимым и `last_write_time` + 2 с, путь меняется. Это Review Focus 2.
  - `StoreCache.MissingDumpThrows`: `EXPECT_THROW(..., Phosphor::Error)`.
- [x] **Step 2: Прогнать.** Запуск `ctest --preset conan-debug -R StoreCache`, ожидается FAIL (не компилируется).
- [x] **Step 3: Реализовать** `StoreCache` и `temp_files`.
- [x] **Step 4: Прогнать.** Ожидается PASS.
- [x] **Step 5: Стоп и коммит.** Показать итог и проверки, дождаться «продолжить», затем `git commit -m "add: кэш store по ключу путь+размер+mtime"`

---

### Task 4: Загрузка дампа с отменой

**Files:**

- Create: `src/core/cancelling_builder.hpp/.cpp`, `src/core/loader.hpp/.cpp`
- Test: `tests/core/test_loader.cpp`

**Interfaces:**

- Consumes: `StoreCache` (задача 3), `Error`/`Cancelled` (задача 1).
- Produces:
  - `std::unique_ptr<WaSafe::Builder> makeCancellingBuilder(WaSafe::Builder& sink, std::stop_token stop);` — делегирует всё в `sink`. В `setTime` и на каждом 4096-м `valueChange` проверяет `stop.stop_requested()` и бросает `Cancelled`.
  - `WaSafe::Database loadDump(const std::filesystem::path& dump, const StoreCache& cache, std::stop_token stop);`
    - Если `storePathFor(dump)` существует, вызывается `openStore`. Если `openStore` бросил (битый store), store удаляется и дамп переиндексируется.
    - Иначе индексация через `VcdReader` + `makeIndexingBuilder(<store>.part)` + `makeCancellingBuilder`. Потом `takeDatabase()`, уничтожение этой БД, `rename(.part → store)`, `openStore(store)`. Переименование после закрытия нужно потому, что на Windows открытый файл не переименовать.
    - При любом исключении `.part` удаляется, исключение пробрасывается дальше: `Cancelled` либо `WaSafe::Exception`.

- [x] **Step 1: Написать падающие тесты** (дамп `kSmallVcd` во временном каталоге).
  - `Loader.IndexesThenReusesStore`: первый `loadDump` создаёт `storePathFor(dump)`. У него `db.find("top.clk")` есть, `db.timeRange().end >= 20`. Второй вызов не меняет `last_write_time` store, то есть файл переиспользован.
  - `Loader.CancelLeavesNoStore`: `std::stop_source s; s.request_stop();` → `EXPECT_THROW(loadDump(..., s.get_token()), Phosphor::Cancelled)`. После этого ни store, ни `.part` нет, а следующий `loadDump` без отмены успешен. Это Review Focus 1.
  - `Loader.BrokenVcdThrowsWaSafe`: файл `"$timescale 1ns $end\n$scope module top $end\n$var wire 1 ! a"` → `EXPECT_THROW(..., WaSafe::Exception)`, и файлов в каталоге кэша нет.
  - `Loader.CorruptStoreReindexes`: положить мусор по пути `storePathFor(dump)`, `loadDump` успешен.
- [x] **Step 2: Прогнать.** Запуск `ctest --preset conan-debug -R Loader`, ожидается FAIL.
- [x] **Step 3: Реализовать** `cancelling_builder` и `loader`.
- [x] **Step 4: Прогнать.** Ожидается PASS.
- [x] **Step 5: Стоп и коммит.** Показать итог и проверки, дождаться «продолжить», затем `git commit -m "add: загрузка дампа через кэш store с отменой"`

Прогресс в процентах в этом этапе не делается: у `VcdReader` нет позиции в файле, это доработка этапа 2. UI показывает неопределённый индикатор с отменой.

---

### Task 5: Форматирование значений и прореживание цифровой строки

**Files:**

- Create: `src/core/value_format.hpp/.cpp`, `src/core/digital_frame.hpp/.cpp`
- Test: `tests/core/test_value_format.cpp`, `tests/core/test_digital_frame.cpp`

**Interfaces:**

- Consumes: `TimeAxis`, `TimeWindow` (задача 2).
- Produces:
  - `struct RowFormat { WaSafe::Radix radix = WaSafe::Radix::HEX; bool isSigned = false; friend bool operator==(const RowFormat&, const RowFormat&) = default; };`
  - `std::string formatValue(WaSafe::ValueView v, const RowFormat& f);`
    - logic → `LogicVectorView::toString(f.radix, f.isSigned)`;
    - real → `std::format("{:g}", v)`;
    - string → как есть;
    - невалидный → пустая строка.
  - `enum class SpanKind : std::uint8_t { NONE, LOW, HIGH, X, Z, VALUE, DENSE };`
  - `struct DigitalSpan { int x0; int x1; SpanKind kind; std::string label; };` — полуинтервал пикселей `[x0, x1)`.
  - `struct DigitalFrame { bool isBit = false; std::vector<DigitalSpan> spans; };`
  - `DigitalFrame buildDigitalFrame(const WaSafe::Database& db, WaSafe::NodeId node, const TimeAxis& axis, TimeWindow window, int widthPx, const RowFormat& fmt);`. Правила:
    - пиксель `x` покрывает `[pixelToTime(x), pixelToTime(x+1))`;
    - начальное значение берётся через `valueAt(toLocalFloor(window.begin))`, дальше `changes([toLocalFloor(begin), toLocalCeil(end)))`;
    - до первого изменения — `NONE`;
    - изменение ровно в `window.begin` не считается: оно и есть начальное значение;
    - если в пиксель попало ≥ 2 изменений, это `DENSE`, соседние `DENSE` сливаются;
    - `isBit` — ширина logic равна 1. Для бита вид спана `LOW/HIGH/X/Z` по `Logic`;
    - для прочих logic — `VALUE` с `label`, а если есть хоть один бит `x`/`z`, то `X` с `label`;
    - real и string — `VALUE` с `label`;
    - соседние спаны с одинаковыми `kind` и `label` сливаются;
    - при `widthPx <= 0` или `window.end <= window.begin` — пустой `spans`.

- [x] **Step 1: Написать падающие тесты.** БД получается через `loadDump(kSmallVcd)`. Окно `{0, axis.toGlobal(20)}`, ширина 20 px, то есть 1 ns на пиксель.
  - `ValueFormat.HexAndSigned`: `data` в 10 ns даёт `"a5"` в HEX и `"-91"` в DEC signed. `vout` в 10 ns даёт `"0.5"`.
  - `DigitalFrame.ClockSpans`: `clk` даёт `isBit == true` и спаны `{0,5,LOW}`, `{5,10,HIGH}`, `{10,15,LOW}`, `{15,20,HIGH}`.
  - `DigitalFrame.BusLabelsAndX`: `data` даёт `{0,10,VALUE,"00"}`, `{10,20,VALUE,"a5"}`. В окне `{0, toGlobal(30)}` (30 px) последний спан `{20,30,X,"xx"}`.
  - `DigitalFrame.DensePixel`: ширина 1 px на окне `{0, toGlobal(20)}`. У `clk` изменения в 5, 10, 15 ns дают один спан `{0,1,DENSE}`. На ширине 4 px (5 ns на пиксель) у каждого пикселя ≤ 1 изменения, поэтому `DENSE` нет.
  - `DigitalFrame.WindowOutsideData`: окно `{toGlobal(1000), toGlobal(2000)}` даёт один спан последнего значения на всю ширину. Окно с отрицательным началом `{-toGlobal(10), toGlobal(0)}` даёт `NONE` до 0. Это Review Focus 3.
  - `DigitalFrame.ZeroWidthEmpty`: `widthPx = 0` дают пустые `spans`, окно `{5, 5}` — тоже. Это Review Focus 4.
- [x] **Step 2: Прогнать.** Запуск `ctest --preset conan-debug -R 'ValueFormat|DigitalFrame'`, ожидается FAIL.
- [x] **Step 3: Реализовать.** Один проход курсором, текущий пиксель и счётчик изменений в нём. Строку `label` формировать только при закрытии спана.
- [x] **Step 4: Прогнать.** Ожидается PASS.
- [x] **Step 5: Стоп и коммит.** Показать итог и проверки, дождаться «продолжить», затем `git commit -m "add: форматирование значений и прореживание цифровой строки"`

---

### Task 6: Сессия — источники и фоновая загрузка

**Files:**

- Create: `src/core/session.hpp/.cpp`
- Test: `tests/core/test_session.cpp`

**Interfaces:**

- Consumes: `loadDump`, `StoreCache`, `TimeAxis`, `TimeWindow`.
- Produces:
  - `using SourceId = std::uint32_t;`
  - `enum class SourceState : std::uint8_t { LOADING, READY, FAILED };`
  - `struct SourceInfo { SourceId id; QString label; std::filesystem::path path; SourceState state; QString error; };` — `label` по умолчанию равен имени файла.
  - `class Session : public QObject`:
    - `explicit Session(StoreCache cache, QObject* parent = nullptr);` деструктор останавливает и дожидается загрузок;
    - `SourceId openDump(const std::filesystem::path& path);` — асинхронно, сразу шлёт `sourceAdded`;
    - `void cancelLoad(SourceId id);` — после отмены источник удаляется (`sourceRemoved`);
    - `void removeSource(SourceId id);`
    - `std::vector<SourceId> sourceIds() const;` — в порядке открытия;
    - `const SourceInfo* info(SourceId) const;`
    - `const WaSafe::Database* hierarchyDb(SourceId) const;` — только для навигации по иерархии в GUI-потоке, `nullptr` до `READY`;
    - `std::optional<TimeAxis> axis(SourceId) const;`
    - `WaSafe::Database takeRenderDatabase(SourceId id);` — одноразово. Это `duplicate()`, сделанный в потоке загрузки, для `Renderer`;
    - `std::optional<TimeWindow> totalRange() const;` — объединение `timeRange()` готовых источников в `GlobalTime`;
    - сигналы `sourceAdded(SourceId)`, `sourceReady(SourceId)`, `sourceFailed(SourceId, QString)`, `sourceRemoved(SourceId)`.
  - Загрузка идёт в `std::jthread` на источник. Результат (две БД и ось) возвращается в поток `Session` через `QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)`. `Database` только перемещаемая, поэтому её передают в `std::shared_ptr`.
  - `SourceId` регистрируется `qRegisterMetaType`.

- [x] **Step 1: Написать падающие тесты** (`QSignalSpy::wait(5000)`).
  - `Session.LoadsDump`: `openDump(kSmallVcd)` → `sourceReady`. `info()->state == READY`, `hierarchyDb(id)->find("top.data")` есть, `totalRange()->begin == 0` и `totalRange()->end >= axis->toGlobal(20)`.
  - `Session.FailedSourceDoesNotAffectOthers`: исправный и битый дампы. Приходит `sourceFailed` с непустым текстом, исправный `READY`, `takeRenderDatabase(good)` валиден. Это Review Focus 5.
  - `Session.CancelRemovesSource`: открыть, сразу `cancelLoad`, дождаться `sourceRemoved`, в `sourceIds()` источника нет. Если загрузка успела завершиться, это тоже допустимо: тогда тест вызывает `removeSource` и проверяет то же.
  - `Session.DestructorJoinsLoads`: `openDump` и немедленное уничтожение `Session` — без падения и без утечек под ASan.
- [x] **Step 2: Прогнать.** Запуск `ctest --preset conan-debug -R Session`, ожидается FAIL.
- [x] **Step 3: Реализовать** `Session`.
- [x] **Step 4: Прогнать.** Ожидается PASS. Дополнительно один прогон с профилем `clang-tsan`:
  - предусловие: TSan-варианты wasafe и wasafe-vcd собраны в их репозиториях (`build/debug-tsan`);
  - `.venv/bin/conan install . -pr clang-tsan -s compiler.cppstd=23 --build=missing`, затем `cmake --preset conan-debug-tsan`, `cmake --build --preset conan-debug-tsan`, `ctest --preset conan-debug-tsan -R Session`;
  - ожидается PASS без отчётов TSan.
- [x] **Step 5: Стоп и коммит.** Показать итог и проверки, дождаться «продолжить», затем `git commit -m "add: сессия с фоновой загрузкой источников"`

---

### Task 7: Вид — строки, окно, курсоры

**Files:**

- Create: `src/core/view.hpp/.cpp`
- Test: `tests/core/test_view.cpp`

**Interfaces:**

- Consumes: `SourceId`, `RowFormat`, `TimeWindow`, `kMinWindowSpan`.
- Produces:
  - `struct SignalRef { SourceId source; std::string path; friend bool operator==(const SignalRef&, const SignalRef&) = default; };` — путь берётся из `WaSafe::Signal::fullPath()`. Ссылка по пути, а не по `NodeId`, переживает переиндексацию.
  - `struct Row { SignalRef signal; RowFormat format; };`
  - `class View : public QObject`:
    - `std::span<const Row> rows() const noexcept;` — вид действителен до следующего изменения строк
    - `void insertRows(std::size_t at, std::vector<Row> rows);` (`at` обрезается до `size()`);
    - `void removeRows(std::span<const std::size_t> indices);`
    - `void moveRow(std::size_t from, std::size_t to);`
    - `void setFormat(std::size_t row, RowFormat f);`
    - `TimeWindow window() const noexcept;` и `void setWindow(TimeWindow w);` — ширина не меньше `kMinWindowSpan`;
    - `std::optional<GlobalTime> cursorA() const noexcept;` и аналогично `cursorB`, `setCursorA(std::optional<GlobalTime>)`, `setCursorB(...)`;
    - сигналы `rowsChanged()`, `windowChanged()`, `cursorsChanged()` — испускаются только при фактическом изменении.
  - Эти мутаторы — единственный способ менять вид. На этапе 3 их обернёт undo-стек.

- [x] **Step 1: Написать падающие тесты.**
  - `View.InsertMoveRemove`: вставка трёх строк, `moveRow(0, 2)`, проверка порядка, `removeRows({0, 2})` оставляет одну нужную строку. Испущено ровно 3 `rowsChanged`.
  - `View.WindowClampsMinSpan`: `setWindow({10, 10})` даёт `window().end - window().begin == kMinWindowSpan`.
  - `View.NoSignalOnSameValue`: повторный `setCursorA(5)` не испускает `cursorsChanged`.
- [x] **Step 2: Прогнать.** Запуск `ctest --preset conan-debug -R View`, ожидается FAIL.
- [x] **Step 3: Реализовать** `View`.
- [x] **Step 4: Прогнать.** Ожидается PASS.
- [x] **Step 5: Стоп и коммит.** Показать итог и проверки, дождаться «продолжить», затем `git commit -m "add: модель вида — строки, окно времени, курсоры"`

---

### Task 8: Рабочий поток кадров

**Files:**

- Create: `src/core/renderer.hpp/.cpp`
- Test: `tests/core/test_renderer.cpp`

**Interfaces:**

- Consumes: `buildDigitalFrame`, `formatValue`, `TimeAxis`, `SourceId`, `RowFormat`, `TimeWindow`.
- Produces:
  - `struct RowRequest { SourceId source; WaSafe::NodeId node; RowFormat format; };` — `NodeId` разрешает GUI через `hierarchyDb(source)->find(path)`; невалидный `NodeId` означает строку без данных;
  - `struct FrameRequest { std::uint64_t generation; TimeWindow window; int widthPx; std::vector<RowRequest> rows; std::optional<GlobalTime> cursorA; std::optional<GlobalTime> cursorB; };`
  - `struct RowResult { DigitalFrame frame; std::string valueA; std::string valueB; };`
  - `struct FrameResult { std::uint64_t generation; TimeWindow window; int widthPx; std::vector<RowResult> rows; };` (`Q_DECLARE_METATYPE`);
  - `class Renderer : public QObject`:
    - `explicit Renderer(QObject* parent = nullptr);` деструктор останавливает поток;
    - `void addSource(SourceId id, WaSafe::Database db, TimeAxis axis);`
    - `void removeSource(SourceId id);`
    - `void request(FrameRequest r);` — побеждает последний запрос: необработанный заменяется новым, а текущий прерывается между строками, если пришёл запрос с бо́льшим `generation`;
    - сигнал `frameReady(Phosphor::FrameResult)` в потоке `Renderer`'а.
  - Один `std::jthread`, очередь команд под мьютексом и `std::condition_variable_any` со `stop_token`. БД источников живут и используются только в рабочем потоке.

- [x] **Step 1: Написать падающие тесты.**
  - `Renderer.ProducesFrameAndCursorValues`: источник из `kSmallVcd`, строки `clk`, `data`, `cursorA = toGlobal(12)`, `cursorB = toGlobal(16)`. В `frameReady` 2 строки, у `data` `valueA == "a5"`, у `clk` `valueA == "0"` и `valueB == "1"`, `frame.spans` непусты.
  - `Renderer.LatestWins`: 20 запросов подряд с `generation` 1..20. Последний полученный кадр имеет `generation == 20`, и ни один полученный не приходит после него с меньшим номером.
  - `Renderer.UnknownSourceEmptyRow`: `RowRequest` с несуществующим `SourceId` даёт строку с пустыми `spans`/`valueA`.
- [x] **Step 2: Прогнать.** Запуск `ctest --preset conan-debug -R Renderer`, ожидается FAIL.
- [x] **Step 3: Реализовать** `Renderer`.
- [x] **Step 4: Прогнать.** Ожидается PASS, плюс прогон `-R Renderer` под `clang-tsan`, как в задаче 6.
- [x] **Step 5: Стоп и коммит.** Показать итог и проверки, дождаться «продолжить», затем `git commit -m "add: рабочий поток подготовки кадров"`

---

### Task 9: Главное окно и дерево иерархии

**Files:**

- Create: `src/app/hierarchy_model.hpp/.cpp`, `src/app/signal_mime.hpp/.cpp`, `src/app/shortcuts.hpp`
- Modify: `src/app/main_window.hpp/.cpp`, `src/main.cpp`
- Test: `tests/app/test_hierarchy_model.cpp`

**Interfaces:**

- Consumes: `Session`, `SignalRef`, `View`.
- Produces:
  - `class HierarchyModel : public QAbstractItemModel` с конструктором `HierarchyModel(Session&, QObject* parent = nullptr)`:
    - корни — источники в порядке `sourceIds()`, текст `label`, во время загрузки с суффиксом `tr(" (loading…)")`, при ошибке `tr(" (error)")` и `Qt::ToolTipRole` с текстом ошибки;
    - дети источника — scope'ы корня, затем их scope'ы и сигналы рекурсивно;
    - композиты без собственного потока раскрываются в `children()`;
    - узлы строятся лениво, через `canFetchMore`/`fetchMore` либо по первому `rowCount`;
    - модель реагирует на `sourceAdded/Ready/Failed/Removed`.
  - `std::optional<SignalRef> HierarchyModel::signalRef(const QModelIndex&) const;` — значение только у сигналов, у которых `hasOwnStream()`.
  - `QMimeData* HierarchyModel::mimeData(const QModelIndexList&) const`, MIME `application/x-phosphor-signals`. Кодирование и разбор — `QByteArray encodeSignalRefs(const std::vector<SignalRef>&)` и `std::vector<SignalRef> decodeSignalRefs(const QByteArray&)` в `signal_mime.hpp` (JSON-массив `{"source":…, "path":…}`).
  - `MainWindow(Session&, View&, Renderer&)`:
    - меню `tr("&File")` → `tr("&Open…")` (`Ctrl+O`, несколько файлов, фильтр `*.vcd`) и `tr("E&xit")`;
    - слева док `tr("Hierarchy")` с `QTreeView`. Enter или двойной клик добавляет выбранные сигналы в конец `View`, перетаскивание тоже поддерживается;
    - статус-бар: на каждый загружаемый источник неопределённый `QProgressBar` и кнопка `tr("Cancel")` (`cancelLoad`);
    - ошибка загрузки показывается `QMessageBox::warning` с текстом;
    - на `sourceReady` — `renderer.addSource(id, session.takeRenderDatabase(id), *session.axis(id))`. Если окно вида ещё не задано, ставится `totalRange()`;
    - геометрия и `saveState()` хранятся в `QSettings("phosphor", "phosphor")`.
  - `main.cpp`:
    - кэш — `StoreCache{QStandardPaths::writableLocation(QStandardPaths::CacheLocation) / "stores"}`;
    - позиционные `dump` открываются через `openDump`.
  - `shortcuts.hpp` — `inline` константы `QKeySequence` всех действий этапа (открыть, приблизить, отдалить, вписать всё, курсор A/B к началу окна). Раскладка предварительная: в спеке ориентир SimVision/Virtuoso, сверка с ними будет отдельно.

- [ ] **Step 1: Написать падающие тесты** (Session с `kSmallVcd`, ждать `sourceReady`).
  - `HierarchyModel.ShowsSourceScopesSignals`: один корень с текстом имени файла, под ним `top`, под `top` три строки `clk`, `data`, `vout`.
  - `HierarchyModel.SignalRefForLeaf`: у индекса `data` `signalRef` равен `{id, "top.data"}`, у индекса `top` — `nullopt`.
  - `HierarchyModel.MimeRoundTrip`: `decodeSignalRefs(encodeSignalRefs(v)) == v`, а `mimeData({clkIdx, dataIdx})` содержит обе ссылки.
- [ ] **Step 2: Прогнать.** Запуск `ctest --preset conan-debug -R HierarchyModel`, ожидается FAIL.
- [ ] **Step 3: Реализовать** модель, MIME, окно и `main.cpp`.
- [ ] **Step 4: Прогнать тесты.** Ожидается PASS.
- [ ] **Step 5: Проверить вручную.**
  - Запуск: `build/Debug/bin/phosphor /path/to/small.vcd`.
  - Ожидается: в доке дерево `small.vcd → top → clk, data, vout`; Ctrl+O открывает второй файл вторым корнем.
- [ ] **Step 6: Стоп и коммит.** Показать итог и проверки, дождаться «продолжить», затем `git commit -m "add: главное окно, дерево иерархии, открытие дампов"`

---

### Task 10: Панель волн — колонки, холст, зум, курсоры

**Files:**

- Create: `src/app/wave_panel.hpp/.cpp`, `src/app/wave_canvas.hpp/.cpp`
- Modify: `src/app/main_window.cpp` (центральный виджет)
- Test: `tests/app/test_wave_panel.cpp`

**Interfaces:**

- Consumes: `View`, `Session::hierarchyDb`, `Renderer`, `FrameRequest/FrameResult`, `time_mapper`, `formatTime`, `decodeSignalRefs`, `shortcuts.hpp`.
- Produces:
  - `class WavePanel : public QWidget` с конструктором `WavePanel(Session&, View&, Renderer&, QWidget* parent = nullptr)`:
    - `QSplitter`: колонка имён | колонка `@A / @B` | `WaveCanvas`; общий вертикальный `QScrollBar`, горизонтальный `QScrollBar` по `Session::totalRange()`;
    - высота строки 22 px, линейка сверху 20 px;
    - принимает drop MIME `application/x-phosphor-signals` (вставка в позицию под курсором мыши);
    - Delete удаляет выделенные строки, перетаскивание строки в колонке имён вызывает `moveRow`;
    - контекстное меню строки: Bin / Oct / Hex / Dec / Signed Dec → `setFormat`.
  - `void WavePanel::requestFrame();` — на любое `rowsChanged/windowChanged/cursorsChanged`, `resizeEvent` и `sourceReady` собирает `FrameRequest` с растущим `generation`. Строки разрешаются `hierarchyDb(source)->find(path)`; ненайденные идут с невалидным `NodeId` и подписываются в колонке имён `tr("(not in dump)")`.
  - `class WaveCanvas : public QWidget` рисует последний `FrameResult`:
    - бит — ступенька;
    - шина — шестиугольники с `label`, текст обрезан с `…` и не рисуется уже 2 символов;
    - `X` красным, `Z` — линией посередине, `DENSE` — заливкой, `NONE` — ничего;
    - если `result.window`/`widthPx` не совпадают с текущими, кадр рисуется с аффинным пересчётом x (старый кадр растягивается и сдвигается) до прихода свежего. Это «сначала грубая картинка» из таблицы 5.1.
  - Мышь и клавиатура холста:
    - `Ctrl+колесо` — `zoomAround` курсора мыши, множитель 0.8 на шаг;
    - `Shift+колесо` — `scrollBy` на 10 % окна;
    - колесо без модификаторов — вертикальная прокрутка строк;
    - левый клик ставит A, средний клик или `Shift+левый` ставят B;
    - перетаскивание в пределах 4 px от курсора двигает его;
    - на линейке подписи `formatTime`, метки A и B и `Δ <formatTime(B−A)>` между ними;
    - клавиши из `shortcuts.hpp`: приблизить/отдалить вокруг центра, вписать всё (`totalRange`).

- [ ] **Step 1: Написать падающие тесты** (offscreen; Session + Renderer с `kSmallVcd`; `QSignalSpy` на `frameReady`).
  - `WavePanel.AddsRowsAndShowsValues`: вставка `top.clk`, `top.data` в `View`, `setCursorA(toGlobal(12))`. После кадра текст колонки значений у `data` содержит `"a5"`. Видимые тексты колонок доступны через `QString WavePanel::valueText(std::size_t row) const`, она объявляется в этой задаче.
  - `WavePanel.MissingSignalMarked`: строка `{id, "top.nope"}` даёт `QString WavePanel::nameText(std::size_t row) const`, содержащий `"not in dump"`.
  - `WavePanel.CtrlWheelZooms`: синтетический `QWheelEvent` с `Ctrl` над серединой холста уменьшает `view.window()`. Середина времени окна остаётся в пределах 1 % ширины.
  - `WavePanel.HiddenCanvasNoCrash`: `resize(0, 0)` + `requestFrame()` + обработка событий — без падения. Это Review Focus 4.
- [ ] **Step 2: Прогнать.** Запуск `ctest --preset conan-debug -R WavePanel`, ожидается FAIL.
- [ ] **Step 3: Реализовать** `WavePanel` и `WaveCanvas`.
- [ ] **Step 4: Прогнать тесты.** Ожидается PASS.
- [ ] **Step 5: Проверить вручную** на `kSmallVcd` и на дампе из генератора задачи 11 размером ~50 МиБ.
  - Ожидается: волны видны, `Ctrl+колесо` плавно приближает, курсоры ставятся и тянутся, Δt верна, GUI не замирает при открытии.
- [ ] **Step 6: Стоп и коммит.** Показать итог и проверки, дождаться «продолжить», затем `git commit -m "add: панель волн — колонки, холст, зум, курсоры"`

---

### Task 11: Генератор большого VCD и стенд

**Files:**

- Create: `tools/gen_vcd.py`, `bench/CMakeLists.txt`, `bench/phosphor_bench.cpp`
- Modify: `README.md` (раздел «Стенд»)

**Interfaces:**

- Consumes: `StoreCache`, `loadDump`, `TimeAxis`, `buildDigitalFrame`, `time_mapper`.
- Produces:
  - `tools/gen_vcd.py --out PATH [--signals 10000] [--size-mib 600] [--seed 1]` — детерминированный VCD.
    - Состав: 70 % однобитных (половина — клоки разных периодов, половина — случайные переключения), 20 % шин шириной 8–64, 10 % `real` (затухающие синусы с шумом).
    - Иерархия на 3 уровня по ~30 модулей.
    - Изменения пишутся, пока размер файла меньше `--size-mib`.
  - `phosphor-bench DUMP [--cache DIR] [--rows 50] [--width 1920]` (цель `phosphor_bench`, файл `phosphor-bench`, под `PHOSPHOR_BUILD_BENCH`). Печатает таблицу «операция | мс | порог | OK/FAIL»:
    - первая индексация, без порога, только печать;
    - повторное открытие, порог 1000 мс;
    - кадр из 50 строк (первые 40 однобитных/шин + 10 `real` из `leafNodes`) на окнах «весь диапазон», «1/100», «1/10000», порог 100 мс каждый.
  - Отдаление на весь диапазон большого дампа может не уложиться в 100 мс до сводок этапа 2. Стенд должен честно показать FAIL, а не скрывать его.

- [ ] **Step 1: Написать `gen_vcd.py`, проверить на малом размере.**
  - Запуск: `python3 tools/gen_vcd.py --out /tmp/g.vcd --signals 200 --size-mib 2 && build/Debug/bin/phosphor /tmp/g.vcd`.
  - Ожидается: файл ~2 МиБ, Phosphor открывает его без ошибок.
- [ ] **Step 2: Написать `phosphor_bench.cpp` и `bench/CMakeLists.txt`.**
- [ ] **Step 3: Прогнать стенд в Release.**
  - Запуск: `.venv/bin/conan install . --build=missing -s compiler.cppstd=23 -s build_type=Release`, затем `cmake --preset conan-release -DPHOSPHOR_BUILD_BENCH=ON`, `cmake --build --preset conan-release`, `python3 tools/gen_vcd.py --out /tmp/big.vcd`, `build/Release/bin/phosphor-bench /tmp/big.vcd --cache /tmp/phosphor-bench-cache`.
  - Ожидается: таблица напечатана, повторное открытие OK. Результаты кадров записать в README как исходную точку для этапа 2.
- [ ] **Step 4: Стоп и коммит.** Показать итог и проверки, дождаться «продолжить», затем `git commit -m "add: генератор большого VCD и стенд замеров"`

---

## Что сознательно не входит в этот план

Этапы 2–6 спеки получат свои планы:

- сводки min/max и прогресс разбора в wasafe;
- форматы и составные сигналы, папки, undo/redo;
- аналоговые панели;
- поиск и фильтр дерева;
- сессия, «Перечитать», CLI `-s`.

Тайловый кэш кадров добавляется, только если стенд задачи 11 покажет, что аффинного пересчёта старого кадра не хватает для «кадр ≤ 16 мс».
