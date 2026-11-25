/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/**
 * @file iec104.cpp
 * @brief Implementation of the general C API functions (version, status texts).
 *
 * Layer: API. This file only contains functions that do not belong to a
 * protocol layer. It must not throw and must not use RTTI, like the rest of the
 * library (built with -fno-exceptions -fno-rtti).
 */
#include "iec104/iec104.h"

/*
 * The build system passes the project version (CMakeLists.txt, project()) as
 * IEC104_VERSION_STRING. The fallback only exists so that the file still
 * compiles when built outside CMake, e.g. by an IDE indexer.
 */
#ifndef IEC104_VERSION_STRING
#define IEC104_VERSION_STRING "0.0.0"
#endif

extern "C" {

const char* iec104_version_string(void) {
    return IEC104_VERSION_STRING;
}

const char* iec104_status_string(iec104_status status) {
    // A switch without 'default' lets the compiler warn when a new status code
    // is added to the enum but not to this table.
    switch (status) {
    case IEC104_OK:
        return "ok";
    case IEC104_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case IEC104_ERR_BUFFER_TOO_SMALL:
        return "buffer too small";
    case IEC104_ERR_NEED_MORE_DATA:
        return "need more data";
    case IEC104_ERR_MALFORMED:
        return "malformed frame";
    case IEC104_ERR_OUT_OF_RANGE:
        return "value out of range";
    case IEC104_ERR_UNSUPPORTED:
        return "unsupported";
    case IEC104_ERR_WOULD_BLOCK:
        return "would block";
    case IEC104_ERR_WRONG_STATE:
        return "not allowed in this state";
    case IEC104_ERR_IO:
        return "i/o error";
    case IEC104_ERR_CLOSED:
        return "connection closed";
    case IEC104_ERR_INTERNAL:
        return "internal error";
    }
    // Reached for integers that are not a declared enumerator (C callers can
    // pass any int).
    return "unknown status";
}

void iec104_link_config_init(iec104_link_config* config) {
    if (config == nullptr) {
        return;
    }
    // Values of the standard (docs/05, interoperability list).
    config->t0_ms = 30000;
    config->t1_ms = 15000;
    config->t2_ms = 10000;
    config->t3_ms = 20000;
    config->k = 12;
    config->w = 8;
    config->tolerant = 0;
    // ASDU profile of IEC 104 (docs/02_Technik_und_Quellen.md, "ASDU-Kopf und ASDU-Profil").
    config->cot_length = 2;
    config->ca_length = 2;
    config->ioa_length = 3;
    config->max_asdu_length = 249;
    config->originator = 0;
}

const char* iec104_link_state_string(iec104_link_state state) {
    switch (state) {
    case IEC104_LINK_CONNECTING:
        return "connecting";
    case IEC104_LINK_STOPPED:
        return "stopped";
    case IEC104_LINK_START_PENDING:
        return "start pending";
    case IEC104_LINK_STARTED:
        return "started";
    case IEC104_LINK_STOP_PENDING:
        return "stop pending";
    case IEC104_LINK_CLOSED:
        return "closed";
    }
    return "unknown state";
}

const char* iec104_log_level_string(iec104_log_level level) {
    switch (level) {
    case IEC104_LOG_ERROR:
        return "error";
    case IEC104_LOG_WARNING:
        return "warning";
    case IEC104_LOG_INFO:
        return "info";
    case IEC104_LOG_DEBUG:
        return "debug";
    case IEC104_LOG_TRACE:
        return "trace";
    }
    return "unknown level";
}

const char* iec104_command_result_string(iec104_command_result result) {
    switch (result) {
    case IEC104_COMMAND_CONFIRMED:
        return "confirmed";
    case IEC104_COMMAND_TERMINATED:
        return "executed (terminated)";
    case IEC104_COMMAND_SELECTED:
        return "selected";
    case IEC104_COMMAND_REJECTED:
        return "rejected";
    case IEC104_COMMAND_NO_CONFIRMATION:
        return "no confirmation";
    case IEC104_COMMAND_NO_TERMINATION:
        return "no termination";
    case IEC104_COMMAND_CONNECTION_LOST:
        return "connection lost";
    }
    return "unknown result";
}

const char* iec104_close_reason_string(iec104_close_reason reason) {
    switch (reason) {
    case IEC104_CLOSE_NONE:
        return "none";
    case IEC104_CLOSE_LOCAL:
        return "closed locally";
    case IEC104_CLOSE_PEER:
        return "closed by peer";
    case IEC104_CLOSE_CONNECT_FAILED:
        return "connect failed";
    case IEC104_CLOSE_T0_TIMEOUT:
        return "t0 timeout (connect)";
    case IEC104_CLOSE_T1_TIMEOUT:
        return "t1 timeout (no acknowledgement)";
    case IEC104_CLOSE_SEQUENCE_ERROR:
        return "sequence error";
    case IEC104_CLOSE_ACK_OUT_OF_WINDOW:
        return "acknowledge out of window";
    case IEC104_CLOSE_MALFORMED:
        return "malformed frame";
    case IEC104_CLOSE_UNEXPECTED_FRAME:
        return "unexpected frame";
    case IEC104_CLOSE_SOCKET_ERROR:
        return "socket error";
    }
    return "unknown reason";
}

} // extern "C"
