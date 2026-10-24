add_executable(phosphor)

target_compile_features(phosphor PUBLIC cxx_std_23)

target_link_libraries(phosphor PUBLIC wasafe::wasafe_vcd)

target_sources(phosphor
    PRIVATE
    main.cpp)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/wasafe_vcd/version.hpp
    @ONLY)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/config.hpp
    @ONLY)

target_include_directories(phosphor
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

if(WASAFE_VCD_INSTALL)
    install(TARGETS phosphor
        EXPORT phosphor-targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

    # ---------------------------------------------------------------------
    # CMake-пакет для потребителей: find_package(phosphor CONFIG)
    # ---------------------------------------------------------------------
    set(PHOSPHOR_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/phosphor)

    install(EXPORT phosphor-targets
        FILE phosphor-targets.cmake
        DESTINATION ${PHOSPHOR_CMAKE_DIR})

    include(CMakePackageConfigHelpers)

    configure_package_config_file(
        ${PROJECT_SOURCE_DIR}/cmake/phosphor-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/phosphor-config.cmake
        INSTALL_DESTINATION ${PHOSPHOR_CMAKE_DIR})

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/phosphor-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/phosphor-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/phosphor-config-version.cmake
        DESTINATION ${WASAFE_VCD_CMAKE_DIR})
endif()
