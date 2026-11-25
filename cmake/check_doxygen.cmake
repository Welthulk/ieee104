# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
#
# Test Doc.NoDoxygenWarnings: builds the API documentation and fails if
# Doxygen reports anything (undocumented members, broken references).
#
#   cmake -DDOXYGEN=<doxygen> -DDOXYFILE=<build>/Doxyfile -DLOG=<build>/doc/warnings.log -P check_doxygen.cmake

get_filename_component(dir "${LOG}" DIRECTORY)
file(MAKE_DIRECTORY "${dir}")
file(REMOVE "${LOG}")
execute_process(COMMAND "${DOXYGEN}" "${DOXYFILE}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "doxygen failed (${rc})")
endif()
file(READ "${LOG}" warnings)
string(STRIP "${warnings}" warnings)
if(NOT warnings STREQUAL "")
    message(FATAL_ERROR "Doxygen warnings:\n${warnings}")
endif()
message(STATUS "API documentation without warnings")
