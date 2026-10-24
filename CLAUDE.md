# Phosphor

Просмотрщик временных диаграмм на Qt6 (Widgets) поверх wasafe / wasafe-vcd.
Ориентир по удобству — SimVision (цифра) + Virtuoso/ViVA (аналог).

- Спека v1: [docs/superpowers/specs/2026-10-24-phosphor-design.md](docs/superpowers/specs/2026-10-24-phosphor-design.md)
- Текущий план: [docs/superpowers/plans/2026-10-24-phosphor-stage0-1.md](docs/superpowers/plans/2026-10-24-phosphor-stage0-1.md)

## Сборка и тесты

```bash
.venv/bin/conan install . --build=missing -s compiler.cppstd=23 -s build_type=Debug
cmake --preset conan-debug
cmake --build --preset conan-debug
ctest --preset conan-debug --output-on-failure
```

- Qt 6 — системный, через `find_package(Qt6)`; через Conan его не тянуть.
- wasafe и wasafe-vcd подключены как editable из `../wasafe` и `../wasafe-vcd`.
  Их можно дорабатывать, если не хватает функциональности, но это отдельные
  репозитории со своими правилами (`../wasafe/CLAUDE.local.md`).
- Два тестовых бинарника: `phosphor-core-tests` (только `phosphor::core`, на
  `QCoreApplication` — так сборка проверяет, что ядро не зависит от виджетов) и
  `phosphor-ui-tests` (`QApplication`, `QT_QPA_PLATFORM=offscreen` по умолчанию).
  Файлы тестов перечисляются в `tests/CMakeLists.txt` явно, без `file(GLOB)`.
- Для гонок есть Conan-профиль `clang-tsan`, для памяти — `clang-asan-ubsan`.

## Устройство

- `src/core/` → `phosphor_core` (`phosphor::core`): без QtGui/Widgets, QtCore
  допустим. Сессия, вид, команды, прореживание, рабочие потоки. Всё, что позже
  понадобится MCP и второму окну, живёт здесь.
- `src/app/` → `phosphor_ui` (`phosphor::ui`): виджеты. `src/main.cpp` → `phosphor`.
- Имена целей CMake — через подчёркивание (`phosphor_tests`), alias —
  `phosphor::<кратко>`, имя файла (`OUTPUT_NAME`) — через дефис (`phosphor-tests`).
- Время в ядре — `GlobalTime`, int64 в фемтосекундах; у каждого дампа свой
  `TimeAxis`.
- Сигналы в виде и сессии ссылаются на пару «источник + полный путь», не на
  `NodeId`: id меняются при переиндексации.

## Инварианты

- В GUI-потоке у `WaSafe::Database` можно только ходить по иерархии. Никаких
  `valueAt` / `changes` / `nextChange` / `prevChange`: значения читают рабочие
  потоки через собственный `duplicate()`.
- `WaSafe::Signal`, `Scope`, `ValueView` не пересекают границу потока; передаются
  `NodeId`, пути и значения-копии.
- Медленная работа — главный критерий провала (спека, раздел 5.1). Изменения,
  затрагивающие загрузку и отрисовку, проверяются стендом `bench/`.

## Код

- C++23, пространство имён `Phosphor`, файлы `snake_case`, методы `camelCase`,
  закрытые поля с суффиксом `_`. Ошибки — исключениями (`Phosphor::Error`).
- Форматирование — `.clang-format`, проверки — `.clang-tidy`.
- Комментарии на русском, по общим правилам из `~/.claude/CLAUDE.md`.

## Работа по плану и коммиты

- После каждой задачи плана остановиться: прогнать сборку и тесты, честно
  привести результат, кратко показать сделанное и предложить сообщение коммита.
  Коммитить только после того, как пользователь скажет продолжать.
- Агенты-исполнители задач не коммитят; коммит делает основной сеанс.
- Ветки: `feat/…`, `fix/…`. В `main` напрямую не коммитить.
- Сообщение коммита: префикс, двоеточие, коротко по-русски без точки в конце.
  Префиксы: `add:` (новое), `fix:` (исправление), `mod:` (изменение поведения),
  `refactor:` (без изменения поведения), `build:` (сборка, зависимости, CI),
  `doc:` (документация). Тело — только если нужно объяснить «почему».
