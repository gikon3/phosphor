configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/phosphor/version.hpp
    @ONLY)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/phosphor/config.hpp
    @ONLY)

# Ядро: без QtGui и Widgets.
add_library(phosphor_core STATIC)
add_library(phosphor::core ALIAS phosphor_core)

target_compile_features(phosphor_core PUBLIC cxx_std_23)

target_sources(phosphor_core
    PRIVATE
    core/cancelling_builder.cpp
    core/digital_frame.cpp
    core/error.cpp
    core/loader.cpp
    core/renderer.cpp
    core/session.cpp
    core/store_cache.cpp
    core/time_axis.cpp
    core/time_mapper.cpp
    core/value_format.cpp
    core/view.cpp)

target_include_directories(phosphor_core
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include)

target_link_libraries(phosphor_core
    PUBLIC
    wasafe::wasafe_vcd
    Qt6::Core)

# В API wasafe есть имя signals, а одноимённый макрос Qt ломает его заголовки.
# Вместо ключевых слов — Q_SIGNALS, Q_SLOTS, Q_EMIT.
target_compile_definitions(phosphor_core PUBLIC QT_NO_KEYWORDS)

set_target_properties(phosphor_core PROPERTIES OUTPUT_NAME phosphor-core)

# Виджеты.
add_library(phosphor_ui STATIC)
add_library(phosphor::ui ALIAS phosphor_ui)

target_sources(phosphor_ui
    PRIVATE
    app/hierarchy_model.cpp
    app/main_window.cpp
    app/signal_mime.cpp)

target_link_libraries(phosphor_ui
    PUBLIC
    phosphor::core
    Qt6::Widgets)

set_target_properties(phosphor_ui PROPERTIES OUTPUT_NAME phosphor-ui)

# Приложение.
qt_add_executable(phosphor main.cpp)

target_link_libraries(phosphor PRIVATE phosphor::ui)

if(PHOSPHOR_INSTALL)
    install(TARGETS phosphor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
