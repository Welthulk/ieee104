# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
#
# Examples. Builds inside the library project, or on its own against an
# installed library:
#   cmake -S examples -B build-examples -DCMAKE_PREFIX_PATH=<install prefix>
cmake_minimum_required(VERSION 3.21)
project(iec104_examples LANGUAGES C)

if(NOT TARGET iec104::iec104)
    find_package(iec104 REQUIRED)
endif()

foreach(name hello_server hello_client)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE iec104::iec104)
endforeach()
