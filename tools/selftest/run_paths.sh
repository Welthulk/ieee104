#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# run_paths.sh – Wegeredundanz: ein Client mit zwei Wegen zu zwei Selbsttest-Servern.
#
# Aufruf: run_paths.sh SERVER_PROGRAMM CLIENT_PROGRAMM [ZUSÄTZLICHE CLIENT-OPTIONEN]
#
# Beide Server senden dieselbe Messreihe (400 Werte, alle 50 ms) über ihren
# Ereignispuffer. Der Client stört alle 4 s den führenden Weg, abwechselnd
# getrennt und stumm; die Reihe muss ohne Lücke ankommen. Kurze Timer
# (t1 = 2 s, t3 = 1 s), damit ein stummer Weg schnell erkannt wird.
# Rückgabe: 0, wenn der Client bestanden hat, sonst 1.

set -u

SERVER="$1"
CLIENT="$2"
shift 2

WORK="$(mktemp -d)"
PIDS=()
trap 'rm -rf "$WORK"; for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done' EXIT

for n in 1 2; do
    "$SERVER" --host 127.0.0.1 --port 0 --port-file "$WORK/port$n" --broadcast --group 127.0.0.1 \
        --count 400 --interval 50 --t1 2 --t2 1 --t3 1 --duration 120 > "$WORK/server$n.log" 2>&1 &
    PIDS+=($!)
done
for n in 1 2; do
    for _ in $(seq 1 100); do
        [ -s "$WORK/port$n" ] && break
        sleep 0.05
    done
    if [ ! -s "$WORK/port$n" ]; then
        echo "Server $n hat keinen Port gemeldet:"; cat "$WORK/server$n.log"; exit 1
    fi
done

"$CLIENT" --path "127.0.0.1:$(cat "$WORK/port1")" --path "127.0.0.1:$(cat "$WORK/port2")" \
    --expect 400 --fault-every 4 --t1 2 --t2 1 --t3 1 --timeout 30 --log warning "$@"
exit $?
