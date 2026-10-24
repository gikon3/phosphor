include_guard()

if(NOT BUILD_TESTING)
    return()
endif()

find_package(GTest REQUIRED)

add_executable(phosphor_tests
    test_empty.cpp
)

set_target_properties(phosphor_tests PROPERTIES
    OUTPUT_NAME phosphor-tests)

target_link_libraries(phosphor_tests
    PRIVATE
    GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(phosphor_tests)
