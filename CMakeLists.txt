cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED PHOSPHOR_VERSION)
    set(PHOSPHOR_VERSION 0.0.0)
endif()

project(phosphor
    VERSION ${PHOSPHOR_VERSION}
    DESCRIPTION "${PHOSPHOR_DESCRIPTION}"
    HOMEPAGE_URL "${PHOSPHOR_HOMEPAGE_URL}"
    LANGUAGES CXX)

if(NOT PROJECT_VERSION_MINOR)
    set(PROJECT_VERSION_MINOR 0)
endif()
if(NOT PROJECT_VERSION_PATCH)
    set(PROJECT_VERSION_PATCH 0)
endif()

# ---------------------------------------------------------------------------
# Опции сборки
# ---------------------------------------------------------------------------
option(PHOSPHOR_INSTALL "Генерировать правила установки" ${PROJECT_IS_TOP_LEVEL})
option(PHOSPHOR_BUILD_TESTS "Собирать тесты" ${PROJECT_IS_TOP_LEVEL})
option(PHOSPHOR_BUILD_BENCH "Собирать стенд производительности" OFF)

# ---------------------------------------------------------------------------
# Общие настройки компилятора
# ---------------------------------------------------------------------------
# Решения уровня всего дерева сборки: при подключении через add_subdirectory
# раскладку и тип сборки задаёт родительский проект.
if(PROJECT_IS_TOP_LEVEL)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Тип сборки" FORCE)
    endif()

    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

    include(CTest)
endif()

include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Зависимости
# ---------------------------------------------------------------------------
find_package(wasafe_vcd CONFIG REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Test)
qt_standard_project_setup()

# ---------------------------------------------------------------------------
# Цели
# ---------------------------------------------------------------------------
add_subdirectory(src)

if(PHOSPHOR_BUILD_BENCH)
    add_subdirectory(bench)
endif()

if(PHOSPHOR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
