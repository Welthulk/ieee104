#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# beispiel.sh – Julia-Station und Julia-Leitstelle einmal gegeneinander.
#
# Aufruf: bash beispiel.sh [port]     Default 2404
# Die Station sendet jede Sekunde einen Messwert, die Leitstelle macht eine
# Generalabfrage und endet nach drei spontanen Werten.

cd "$(dirname "$0")" || exit 1
PORT="${1:-2404}"
command -v julia > /dev/null 2>&1 || { echo "Julia fehlt (bash pruefen.sh)"; exit 1; }
LOG="$(mktemp)"
julia --startup-file=no julia/server.jl "$PORT" 120 < /dev/null > "$LOG" 2>&1 &
SERVER=$!
trap 'kill "$SERVER" 2>/dev/null; rm -f "$LOG"' EXIT
for _ in $(seq 1 120); do
    grep -q listening "$LOG" && break
    kill -0 "$SERVER" 2>/dev/null || { cat "$LOG"; exit 1; }
    sleep 0.5
done
echo "Station läuft auf Port $PORT, Leitstelle verbindet sich:"
julia --startup-file=no julia/client.jl 127.0.0.1 "$PORT"
RC=$?
[ "$RC" = 0 ] && echo "Ergebnis: ok" || echo "Ergebnis: FEHLER ($RC)"
exit "$RC"
