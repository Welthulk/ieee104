# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.

cmake_minimum_required(VERSION 3.21)

project(iec104
    VERSION 0.9.3
    DESCRIPTION "IEC 60870-5-104 library (C++17 core, C API)"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(IEC104_BUILD_TESTS          "Build unit tests (GoogleTest)"            ON)
option(IEC104_BUILD_FUZZERS        "Build libFuzzer targets (Clang only)"     OFF)
option(IEC104_ENABLE_SANITIZERS    "Build with ASan and UBSan"                OFF)
option(IEC104_WARNINGS_AS_ERRORS   "Treat warnings as errors (library code)"  ON)
option(IEC104_ENABLE_CLANG_TIDY    "Run clang-tidy while compiling"           OFF)
option(IEC104_BUILD_SELFTEST       "Build the self-test programs"             ON)
option(IEC104_TLS                  "TLS support with OpenSSL 3.x"             ON)
option(IEC104_FETCH_GTEST          "Fetch GoogleTest from GitHub if not installed" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Global instrumentation (applies to library, tests and fuzzers)
# ---------------------------------------------------------------------------
if(IEC104_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

if(IEC104_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "IEC104_BUILD_FUZZERS requires Clang (libFuzzer). Use the 'fuzz' preset.")
    endif()
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(IEC104_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    set(IEC104_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

# Warnings for our own targets
function(iec104_set_warnings target strict)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wcast-align -Wnull-dereference -Wdouble-promotion -Wformat=2)
    if(strict AND IEC104_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
add_library(iec104_objects OBJECT
    src/api/client.cpp
    src/api/config.cpp
    src/api/iec104.cpp
    src/api/link_common.cpp
    src/api/logger.cpp
    src/api/server.cpp
    src/api/values.cpp
    src/apci/apci_config.cpp
    src/apci/connection.cpp
    src/codec/apci.cpp
    src/codec/asdu.cpp
    src/codec/asdu_header.cpp
    src/codec/cp56time2a.cpp
    src/codec/elements.cpp
    src/codec/type_table.cpp
    src/session/command_table.cpp
    src/session/command_tracker.cpp
    src/session/process_image.cpp
    src/session/redundancy_group.cpp
    src/session/server_session.cpp
    src/transport/link.cpp
    src/transport/tcp.cpp
    src/transport/tls.cpp)
# TLS backend (src/transport/tls.hpp): OpenSSL 3.x, or a stub without TLS.
if(IEC104_TLS)
    find_package(OpenSSL 3.0 REQUIRED)
    target_sources(iec104_objects PRIVATE src/transport/tls_openssl.cpp)
    # Only functions of OpenSSL 3.0, so that the library runs with every 3.x.
    target_compile_definitions(iec104_objects PRIVATE OPENSSL_API_COMPAT=30000
        OPENSSL_NO_DEPRECATED)
    target_link_libraries(iec104_objects PUBLIC OpenSSL::SSL OpenSSL::Crypto)
else()
    target_sources(iec104_objects PRIVATE src/transport/tls_none.cpp)
endif()
# The sources are compiled once; the library (static or shared, see
# BUILD_SHARED_LIBS) is made from these objects. Tests and fuzzers link the
# objects directly because they check internal functions.
target_include_directories(iec104_objects
    PUBLIC  ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(iec104_objects PRIVATE IEC104_BUILDING_LIBRARY
    IEC104_VERSION_STRING="${PROJECT_VERSION}")
# Core rules: no exceptions, no RTTI (see docs/02_Technik_und_Quellen.md, "Sprache und Architektur")
target_compile_options(iec104_objects PRIVATE -fno-exceptions -fno-rtti)
set_target_properties(iec104_objects PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
if(IEC104_ENABLE_CLANG_TIDY)
    set_target_properties(iec104_objects PROPERTIES CXX_CLANG_TIDY "${IEC104_CLANG_TIDY}")
endif()
iec104_set_warnings(iec104_objects TRUE)

add_library(iec104 $<TARGET_OBJECTS:iec104_objects>)
add_library(iec104::iec104 ALIAS iec104)
target_include_directories(iec104 PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(iec104 PROPERTIES
    LINKER_LANGUAGE CXX
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
if(IEC104_TLS)
    # Exported as link-only dependency of a static library (find_dependency in iec104Config).
    target_link_libraries(iec104 PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
if(NOT BUILD_SHARED_LIBS)
    # A static library carries no dependencies: users that link with the C
    # compiler need the C++ runtime and libm explicitly.
    target_link_libraries(iec104 INTERFACE stdc++ m)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(IEC104_BUILD_TESTS)
    enable_testing()
    # GoogleTest from the system (e.g. EPEL, libgtest-dev). Without it, the
    # pinned release is fetched once from GitHub (only for the tests, never
    # part of the library); IEC104_FETCH_GTEST=OFF forbids the download.
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        if(NOT IEC104_FETCH_GTEST)
            message(FATAL_ERROR "GoogleTest not found (install it, or -DIEC104_FETCH_GTEST=ON, "
                                "or -DIEC104_BUILD_TESTS=OFF)")
        endif()
        message(STATUS "GoogleTest not installed: fetching googletest 1.14.0 from GitHub")
        include(FetchContent)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
            URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    include(GoogleTest)

    add_executable(iec104_tests
        tests/test_api.cpp
        tests/test_apci.cpp
        tests/test_apci_connection.cpp
        tests/test_apci_selftest.cpp
        tests/test_asdu.cpp
        tests/test_asdu_header.cpp
        tests/test_asdu_reference.cpp
        tests/test_capture.cpp
        tests/test_command_tracker.cpp
        tests/test_cp56time2a.cpp
        tests/test_elements.cpp
        tests/test_operation.cpp
        tests/test_interop_list.cpp
        tests/test_redundancy.cpp
        tests/test_session.cpp
        tests/test_tcp_loopback.cpp
        tests/test_tls.cpp
        tests/test_type_table.cpp
        tests/test_values.cpp)
    # Tests use the internal C++ headers directly (sans-I/O, no network)
    target_include_directories(iec104_tests PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tools)
    # Reference data (captures of foreign implementations), read at run time
    target_compile_definitions(iec104_tests PRIVATE
        IEC104_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
    target_link_libraries(iec104_tests PRIVATE iec104_objects GTest::gtest GTest::gtest_main)
    iec104_set_warnings(iec104_tests FALSE)
    gtest_discover_tests(iec104_tests)

    # Test PKI for the TLS tests and the self-test over TLS, made at build time.
    set(IEC104_TEST_CERT_DIR ${CMAKE_BINARY_DIR}/tls)
    if(IEC104_TLS)
        add_custom_command(OUTPUT ${IEC104_TEST_CERT_DIR}/foreign.pem
            COMMAND ${PROJECT_SOURCE_DIR}/tools/make_test_certs.sh ${IEC104_TEST_CERT_DIR}
            DEPENDS ${PROJECT_SOURCE_DIR}/tools/make_test_certs.sh
            COMMENT "Test certificates for TLS")
        add_custom_target(iec104_test_certs DEPENDS ${IEC104_TEST_CERT_DIR}/foreign.pem)
        add_dependencies(iec104_tests iec104_test_certs)
    endif()
    target_compile_definitions(iec104_tests PRIVATE
        IEC104_TEST_CERT_DIR="${IEC104_TEST_CERT_DIR}")
endif()

# ---------------------------------------------------------------------------
# Self-test programs (server and client over TCP)
# ---------------------------------------------------------------------------
if(IEC104_BUILD_SELFTEST AND NOT IEC104_BUILD_FUZZERS)
    foreach(role server client)
        add_executable(iec104_selftest_${role} tools/selftest/selftest_${role}.cpp)
        # Only the public C API is used, as by any application.
        target_link_libraries(iec104_selftest_${role} PRIVATE iec104::iec104)
        iec104_set_warnings(iec104_selftest_${role} TRUE)
    endforeach()
    # Interoperability list of the library (clause 9), see docs/09_Interoperabilitaetsliste.md.
    add_executable(iec104_interop_list tools/interop_list/interop_list.cpp)
    target_link_libraries(iec104_interop_list PRIVATE iec104::iec104)
    iec104_set_warnings(iec104_interop_list TRUE)
    set_target_properties(iec104_interop_list PROPERTIES
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    install(TARGETS iec104_interop_list RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(IEC104_BUILD_TESTS)
        add_test(NAME Doc.InteropListUpToDate
            COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:iec104_interop_list>
                    -DDOC=${PROJECT_SOURCE_DIR}/docs/09_Interoperabilitaetsliste.md
                    -P ${PROJECT_SOURCE_DIR}/cmake/check_interop_list.cmake)
    endif()

    # Installed programs find the library next to them (bin/../lib), wherever the tree is unpacked.
    set_target_properties(iec104_selftest_server iec104_selftest_client PROPERTIES
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    install(TARGETS iec104_selftest_server iec104_selftest_client
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(IEC104_BUILD_TESTS)
        add_test(NAME SelfTest.ServerAndClientOverTcp
            COMMAND ${PROJECT_SOURCE_DIR}/tools/selftest/run_selftest.sh
                    $<TARGET_FILE:iec104_selftest_server> $<TARGET_FILE:iec104_selftest_client>)
        set_tests_properties(SelfTest.ServerAndClientOverTcp PROPERTIES TIMEOUT 90)
        # Interop with lib60870-C as foreign peer (only if given; see docs/08_Interop.md).
        set(IEC104_LIB60870_DIR "" CACHE PATH "lib60870-C folder with build/ (and build-tls/) for interop tests")
        if(IEC104_LIB60870_DIR AND IEC104_BUILD_EXAMPLES)
            get_filename_component(_lib60870 "${IEC104_LIB60870_DIR}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
            add_test(NAME Interop.Lib60870
                COMMAND ${PROJECT_SOURCE_DIR}/tools/interop/run_lib60870.sh ${_lib60870} ${CMAKE_BINARY_DIR})
            set_tests_properties(Interop.Lib60870 PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        endif()
        add_test(NAME SelfTest.SwitchOver
            COMMAND ${PROJECT_SOURCE_DIR}/tools/selftest/run_selftest.sh
                    $<TARGET_FILE:iec104_selftest_server> $<TARGET_FILE:iec104_selftest_client>
                    --switchover)
        set_tests_properties(SelfTest.SwitchOver PROPERTIES TIMEOUT 90)
        if(IEC104_TLS)
            add_test(NAME SelfTest.ServerAndClientOverTls
                COMMAND ${PROJECT_SOURCE_DIR}/tools/selftest/run_selftest.sh
                        $<TARGET_FILE:iec104_selftest_server> $<TARGET_FILE:iec104_selftest_client>
                        --tls ${IEC104_TEST_CERT_DIR})
            set_tests_properties(SelfTest.ServerAndClientOverTls PROPERTIES TIMEOUT 90)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Fuzzers
# ---------------------------------------------------------------------------
if(IEC104_BUILD_FUZZERS)
    add_executable(fuzz_codec fuzz/fuzz_codec.cpp)
    target_include_directories(fuzz_codec PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(fuzz_codec PRIVATE iec104_objects)
    target_link_options(fuzz_codec PRIVATE -fsanitize=fuzzer)
    iec104_set_warnings(fuzz_codec FALSE)

    add_executable(fuzz_session fuzz/fuzz_session.cpp)
    target_include_directories(fuzz_session PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(fuzz_session PRIVATE iec104_objects)
    target_link_options(fuzz_session PRIVATE -fsanitize=fuzzer)
    iec104_set_warnings(fuzz_session FALSE)

    add_executable(fuzz_apci fuzz/fuzz_apci.cpp)
    target_include_directories(fuzz_apci PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(fuzz_apci PRIVATE iec104_objects)
    target_link_options(fuzz_apci PRIVATE -fsanitize=fuzzer)
    iec104_set_warnings(fuzz_apci FALSE)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
option(IEC104_BUILD_EXAMPLES "Build the examples (examples/)" ON)
if(IEC104_BUILD_EXAMPLES AND NOT IEC104_BUILD_FUZZERS)
    add_subdirectory(examples)
    if(IEC104_BUILD_TESTS AND NOT IEC104_ENABLE_SANITIZERS)
        # The examples, built against the installed library (find_package)
        # and run against each other. Not with sanitizers: the installed
        # library would need the sanitizer runtime in the foreign project.
        add_test(NAME Examples.HelloWorldInstalled
            COMMAND ${PROJECT_SOURCE_DIR}/tools/check_install.sh ${CMAKE_BINARY_DIR}
                    ${PROJECT_SOURCE_DIR})
        set_tests_properties(Examples.HelloWorldInstalled PROPERTIES TIMEOUT 120)
    endif()
endif()

# ---------------------------------------------------------------------------
# Test GUI in Julia (examples/julia/gui) over HTTP, only on 127.0.0.1. Needs
# the shared library, Julia and curl; with IEC104_LIB60870_DIR also with
# lib60870 as controlling station against the station of the GUI. The frame
# description of the commented protocol (frames.jl) only needs Julia.
# ---------------------------------------------------------------------------
if(IEC104_BUILD_TESTS)
    find_program(IEC104_JULIA julia)
    if(IEC104_JULIA)
        add_test(NAME Julia.Frames
            COMMAND ${IEC104_JULIA} --startup-file=no ${PROJECT_SOURCE_DIR}/tests/gui/test_frames.jl)
        set_tests_properties(Julia.Frames PROPERTIES TIMEOUT 300)
        # Listening in: pcap/pcapng, link layers, TCP reassembly (pure Julia, no library).
        add_test(NAME Julia.Capture
            COMMAND ${IEC104_JULIA} --startup-file=no ${PROJECT_SOURCE_DIR}/tests/gui/test_capture.jl)
        set_tests_properties(Julia.Capture PROPERTIES TIMEOUT 300)
    endif()
endif()
if(IEC104_BUILD_TESTS AND BUILD_SHARED_LIBS)
    find_program(IEC104_CURL curl)
    if(IEC104_JULIA AND IEC104_CURL)
        set(_gui_env "JULIA=${IEC104_JULIA}" "CURL=${IEC104_CURL}")
        add_test(NAME Julia.Gui
            COMMAND ${PROJECT_SOURCE_DIR}/tests/gui/test_gui.sh $<TARGET_FILE:iec104>)
        # Empty IEC104_LIB60870: without lib60870, even if set in the environment.
        set_tests_properties(Julia.Gui PROPERTIES TIMEOUT 300
            ENVIRONMENT "${_gui_env};IEC104_LIB60870=")
        # The page script itself, run by gjs (GNOME JavaScript) against a running GUI.
        find_program(IEC104_GJS gjs)
        if(IEC104_GJS)
            add_test(NAME Julia.GuiPage
                COMMAND ${PROJECT_SOURCE_DIR}/tests/gui/test_page.sh $<TARGET_FILE:iec104>)
            set_tests_properties(Julia.GuiPage PROPERTIES TIMEOUT 300
                ENVIRONMENT "${_gui_env};GJS=${IEC104_GJS}")
        endif()
        if(IEC104_LIB60870_DIR)
            get_filename_component(_lib60870 "${IEC104_LIB60870_DIR}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
            add_test(NAME Julia.GuiLib60870
                COMMAND ${PROJECT_SOURCE_DIR}/tests/gui/test_gui.sh $<TARGET_FILE:iec104> ${_lib60870})
            set_tests_properties(Julia.GuiLib60870 PROPERTIES TIMEOUT 300
                ENVIRONMENT "${_gui_env}")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# API documentation (Doxygen): target "doc", output <build>/doc/html
# ---------------------------------------------------------------------------
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    configure_file(cmake/Doxyfile.in ${CMAKE_BINARY_DIR}/Doxyfile @ONLY)
    add_custom_target(doc
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
        COMMENT "API documentation: ${CMAKE_BINARY_DIR}/doc/html/index.html"
        VERBATIM)
    if(IEC104_BUILD_TESTS)
        add_test(NAME Doc.NoDoxygenWarnings
            COMMAND ${CMAKE_COMMAND} -DDOXYGEN=${DOXYGEN_EXECUTABLE}
                    -DDOXYFILE=${CMAKE_BINARY_DIR}/Doxyfile
                    -DLOG=${CMAKE_BINARY_DIR}/doc/warnings.log
                    -P ${PROJECT_SOURCE_DIR}/cmake/check_doxygen.cmake)
    endif()
endif()

# ---------------------------------------------------------------------------
# Install and package export (find_package(iec104), target iec104::iec104)
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)
set(IEC104_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/iec104)
install(TARGETS iec104 EXPORT iec104Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES README.md CHANGELOG.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs LICENSES examples DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(PROGRAMS tools/make_test_certs.sh DESTINATION ${CMAKE_INSTALL_DOCDIR}/tools)
install(EXPORT iec104Targets NAMESPACE iec104:: DESTINATION ${IEC104_CMAKE_DIR})
configure_package_config_file(cmake/iec104Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/iec104Config.cmake
    INSTALL_DESTINATION ${IEC104_CMAKE_DIR})
# Version 0.x: only the same minor version is compatible.
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/iec104ConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/iec104Config.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/iec104ConfigVersion.cmake
        DESTINATION ${IEC104_CMAKE_DIR})
