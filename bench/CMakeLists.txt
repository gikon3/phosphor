add_executable(phosphor_bench phosphor_bench.cpp)

target_link_libraries(phosphor_bench PRIVATE phosphor::core)

set_target_properties(phosphor_bench PROPERTIES OUTPUT_NAME phosphor-bench)
