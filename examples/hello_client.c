/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/*
 * hello_client: smallest useful IEC 104 client (controlling station).
 *
 * Connects, starts data transfer, sends a general interrogation to station 1,
 * prints every received object, and ends when the interrogation is
 * terminated and three spontaneous values have arrived.
 *
 *   hello_client [host] [port]     default 127.0.0.1 2404
 *
 * Exit code 0 = done, 1 = connection failed or closed.
 */
#include <iec104/iec104.h>
#include <stdio.h>
#include <stdlib.h>

static int interrogation_done = 0;
static int spontaneous = 0;
static iec104_link_state state = IEC104_LINK_CONNECTING;

static void on_state(void* user, uint32_t connection, iec104_link_state s,
                     iec104_close_reason reason) {
    (void)user;
    (void)connection;
    state = s;
    printf("state: %s", iec104_link_state_string(s));
    if (s == IEC104_LINK_CLOSED) {
        printf(" (%s)", iec104_close_reason_string(reason));
    }
    printf("\n");
}

static void on_object(void* user, uint32_t connection, const iec104_asdu_info* info,
                      const iec104_object* object) {
    (void)user;
    (void)connection;
    printf("type %3u  cause %2u  station %u  IOA %u  value %g\n", info->type, info->cause,
           info->common_address, object->ioa, object->value);
    if (info->type == 100 && info->cause == IEC104_COT_ACTIVATION_TERMINATION) {
        interrogation_done = 1;
    }
    if (info->cause == IEC104_COT_SPONTANEOUS) {
        ++spontaneous;
    }
}

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : "127.0.0.1";
    const uint16_t port = argc > 2 ? (uint16_t)atoi(argv[2]) : IEC104_DEFAULT_PORT;

    iec104_callbacks callbacks = {0};
    callbacks.on_state = on_state;
    callbacks.on_object = on_object;

    iec104_client* client = NULL;
    if (iec104_client_create(NULL, &callbacks, &client) != IEC104_OK ||
        iec104_client_connect(client, host, port) != IEC104_OK) {
        return 1;
    }

    int started = 0;
    int interrogated = 0;
    while (state != IEC104_LINK_CLOSED && !(interrogation_done && spontaneous >= 3)) {
        iec104_client_poll(client, 100);
        if (state == IEC104_LINK_STOPPED && !started) {
            iec104_client_start(client); /* STARTDT */
            started = 1;
        }
        if (state == IEC104_LINK_STARTED && !interrogated) {
            iec104_client_interrogation(client, 1, 20); /* station 1, QOI 20 = all points */
            interrogated = 1;
        }
    }
    const int ok = state != IEC104_LINK_CLOSED;
    iec104_client_destroy(client);
    return ok ? 0 : 1;
}
