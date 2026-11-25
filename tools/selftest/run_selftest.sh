#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# run_selftest.sh – Selbsttest-Server und -Client über Loopback gegeneinander.
#
# Aufruf: run_selftest.sh SERVER_PROGRAMM CLIENT_PROGRAMM [--tls ZERTIFIKATSVERZEICHNIS]
#                         [ZUSÄTZLICHE CLIENT-OPTIONEN]
#
# Mit --tls laufen beide über TLS mit den Testzertifikaten aus
# tools/make_test_certs.sh; der Server erneuert die Schlüssel alle 2 s.
#
# Der Server lauscht auf einem freien Port (--port 0) und schreibt ihn in eine
# Datei; der Client liest ihn von dort. Kurze Timer (t3 = 1 s) und eine Ruhe
# von 3 s sorgen dafür, dass auch der Verbindungstest (TESTFR) durchlaufen wird.
# Rückgabe: 0, wenn Server und Client bestanden haben, sonst 1.

set -u

SERVER="$1"
CLIENT="$2"
shift 2
SERVER_TLS=()
CLIENT_TLS=()
if [ "${1:-}" = "--tls" ]; then
    D="$2"
    shift 2
    SERVER_TLS=(--tls-ca "$D/ca.pem" --tls-cert "$D/server.pem" --tls-key "$D/server.key"
                --tls-crl "$D/crl.pem" --tls-renewal 2)
    CLIENT_TLS=(--tls-ca "$D/ca.pem" --tls-cert "$D/client.pem" --tls-key "$D/client.key"
                --tls-peer-name localhost)
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"; [ -n "${SPID:-}" ] && kill "$SPID" 2>/dev/null' EXIT

"$SERVER" --host 127.0.0.1 --port 0 --port-file "$WORK/port" --once --count 100 \
    --interval 1 --t3 1 --duration 60 ${SERVER_TLS[@]+"${SERVER_TLS[@]}"} > "$WORK/server.log" 2>&1 &
SPID=$!

for _ in $(seq 1 100); do
    [ -s "$WORK/port" ] && break
    sleep 0.05
done
if [ ! -s "$WORK/port" ]; then
    echo "Server hat keinen Port gemeldet:"; cat "$WORK/server.log"; exit 1
fi

"$CLIENT" --host 127.0.0.1 --port "$(cat "$WORK/port")" --expect 100 --count 10 \
    --t3 1 --idle 3 --timeout 20 ${CLIENT_TLS[@]+"${CLIENT_TLS[@]}"} "$@"
CRC=$?

wait "$SPID"
SRC=$?
SPID=""
echo "--- Server ---"
cat "$WORK/server.log"

[ "$CRC" -eq 0 ] && [ "$SRC" -eq 0 ] && exit 0
exit 1
