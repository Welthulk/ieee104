# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.

cmake_minimum_required(VERSION 3.21)

project(iec104
    VERSION 0.1.0
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

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
add_library(iec104
    src/api/iec104.cpp
    src/codec/apci.cpp
    src/codec/asdu.cpp
    src/codec/asdu_header.cpp
    src/codec/cp56time2a.cpp
    src/codec/elements.cpp
    src/codec/type_table.cpp)
add_library(iec104::iec104 ALIAS iec104)

target_include_directories(iec104
    PUBLIC  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_compile_definitions(iec104 PRIVATE IEC104_BUILDING_LIBRARY
    IEC104_VERSION_STRING="${PROJECT_VERSION}")

# Core rules: no exceptions, no RTTI (see docs/02_Konzept, Sprachwahl)
target_compile_options(iec104 PRIVATE -fno-exceptions -fno-rtti)
set_target_properties(iec104 PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
if(IEC104_ENABLE_CLANG_TIDY)
    set_target_properties(iec104 PROPERTIES CXX_CLANG_TIDY "${IEC104_CLANG_TIDY}")
endif()
iec104_set_warnings(iec104 TRUE)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(IEC104_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_executable(iec104_tests
        tests/test_api.cpp
        tests/test_apci.cpp
        tests/test_asdu.cpp
        tests/test_asdu_header.cpp
        tests/test_asdu_reference.cpp
        tests/test_capture.cpp
        tests/test_cp56time2a.cpp
        tests/test_elements.cpp
        tests/test_type_table.cpp)
    # Tests use the internal C++ headers directly (sans-I/O, no network)
    target_include_directories(iec104_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    # Reference data (captures of foreign implementations), read at run time
    target_compile_definitions(iec104_tests PRIVATE
        IEC104_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
    target_link_libraries(iec104_tests PRIVATE iec104::iec104 GTest::gtest GTest::gtest_main)
    iec104_set_warnings(iec104_tests FALSE)
    gtest_discover_tests(iec104_tests)
endif()

# ---------------------------------------------------------------------------
# Fuzzers
# ---------------------------------------------------------------------------
if(IEC104_BUILD_FUZZERS)
    add_executable(fuzz_codec fuzz/fuzz_codec.cpp)
    target_include_directories(fuzz_codec PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(fuzz_codec PRIVATE iec104::iec104)
    target_link_options(fuzz_codec PRIVATE -fsanitize=fuzzer)
    iec104_set_warnings(fuzz_codec FALSE)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS iec104 EXPORT iec104Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
