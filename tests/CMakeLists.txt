include_guard()

if(NOT BUILD_TESTING)
    return()
endif()

find_package(GTest REQUIRED)

include(GoogleTest)

# Тесты ядра: собираются без Gui и Widgets, поэтому граница слоёв проверяется линковкой.
add_executable(phosphor_core_tests
    core_main.cpp
    core/test_time_axis.cpp
    core/test_time_mapper.cpp
    core/test_version.cpp)

set_target_properties(phosphor_core_tests PROPERTIES
    OUTPUT_NAME phosphor-core-tests)

target_link_libraries(phosphor_core_tests
    PRIVATE
    phosphor::core
    GTest::gtest
    Qt6::Test)

gtest_discover_tests(phosphor_core_tests
    PROPERTIES
    ENVIRONMENT QT_QPA_PLATFORM=offscreen
    DISCOVERY_TIMEOUT 30)

# Тесты виджетов.
add_executable(phosphor_ui_tests
    ui_main.cpp)

set_target_properties(phosphor_ui_tests PROPERTIES
    OUTPUT_NAME phosphor-ui-tests)

target_link_libraries(phosphor_ui_tests
    PRIVATE
    phosphor::core
    phosphor::ui
    GTest::gtest
    Qt6::Test)

gtest_discover_tests(phosphor_ui_tests
    PROPERTIES
    ENVIRONMENT QT_QPA_PLATFORM=offscreen
    DISCOVERY_TIMEOUT 30)
