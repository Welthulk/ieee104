/*
 * SPDX-FileCopyrightText: 2026 SOPTIM AG
 * SPDX-License-Identifier: LicenseRef-Proprietary
 * Author: Udo Schmitz (SOPTIM AG)
 * All rights reserved by SOPTIM AG.
 */
/*
 * hello_server: smallest useful IEC 104 server (controlled station).
 *
 * One station (common address 1) with one measured value (IOA 100, short
 * float). The value rises every second and is sent spontaneously; the
 * library answers a general interrogation by itself. Commands are logged
 * and accepted.
 *
 *   hello_server [port]       default port 2404, 0 = any free port
 */
#include <iec104/iec104.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static volatile sig_atomic_t stop = 0;
static void on_signal(int sig) {
    (void)sig;
    stop = 1;
}

static int on_command(void* user, uint32_t connection, const iec104_asdu_info* info,
                      const iec104_object* command) {
    (void)user;
    printf("connection %u: command type %u, IOA %u, value %g\n", connection, info->type,
           command->ioa, command->value);
    return 1; /* accept */
}

static void on_log(void* user, iec104_log_level level, uint32_t connection, const char* message) {
    (void)user;
    printf("[%s] %u: %s\n", iec104_log_level_string(level), connection, message);
}

int main(int argc, char** argv) {
    const uint16_t port = argc > 1 ? (uint16_t)atoi(argv[1]) : IEC104_DEFAULT_PORT;

    iec104_callbacks callbacks = {0};
    callbacks.on_command = on_command;

    iec104_server* server = NULL;
    if (iec104_server_create(NULL, &callbacks, &server) != IEC104_OK) { /* NULL = standard values */
        return 1;
    }
    iec104_server_set_log(server, IEC104_LOG_INFO, on_log, NULL);

    iec104_object value = {0};
    value.type = 13; /* M_ME_NC_1: short floating point measured value */
    value.ioa = 100;
    value.value = 0.0;
    iec104_server_add_point(server, 1, &value, 0x0001); /* station 1, interrogation group 1 */

    iec104_status st = iec104_server_listen(server, NULL, port);
    if (st != IEC104_OK) {
        fprintf(stderr, "listen: %s\n", iec104_status_string(st));
        iec104_server_destroy(server);
        return 1;
    }
    printf("listening on port %u\n", iec104_server_port(server));
    fflush(stdout);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    time_t last = time(NULL);
    while (!stop) {
        iec104_server_poll(server, 100); /* receive, timers, answers, callbacks */
        if (time(NULL) != last) {        /* once per second: new value, sent spontaneously */
            last = time(NULL);
            value.value += 1.0;
            iec104_server_update(server, 1, &value);
        }
        fflush(stdout);
    }
    iec104_server_destroy(server);
    return 0;
}
