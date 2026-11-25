# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
#
# Test Doc.InteropListUpToDate: docs/09_Interoperabilitaetsliste.md must be
# exactly what iec104_interop_list prints with the defaults.
#
#   cmake -DTOOL=<iec104_interop_list> -DDOC=<docs/09_...md> -P check_interop_list.cmake

execute_process(COMMAND "${TOOL}" OUTPUT_VARIABLE generated RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "iec104_interop_list failed (${rc})")
endif()
file(READ "${DOC}" documented)
if(NOT generated STREQUAL documented)
    message(FATAL_ERROR "docs/09_Interoperabilitaetsliste.md is out of date; regenerate:\n"
                        "  ${TOOL} > ${DOC}")
endif()
message(STATUS "interoperability list up to date")
