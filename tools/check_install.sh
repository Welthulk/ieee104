#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# check_install.sh – installiert einen Build, baut examples/ dagegen
# (find_package) und lässt hello_server und hello_client gegeneinander laufen.
#
# Aufruf: tools/check_install.sh BUILD_ORDNER QUELL_ORDNER
# Rückgabe: 0 = Installation, Build und Lauf erfolgreich.

set -u

BUILD="$1"
SRC="$2"
WORK="$(mktemp -d)"
SPID=""
trap '[ -n "$SPID" ] && kill "$SPID" 2>/dev/null; rm -rf "$WORK"' EXIT

fail() { echo "check_install: $1" >&2; [ -f "$WORK/log" ] && cat "$WORK/log" >&2; exit 1; }

cmake --install "$BUILD" --prefix "$WORK/prefix" > "$WORK/log" 2>&1 || fail "install"
cmake -S "$SRC/examples" -B "$WORK/build" -DCMAKE_PREFIX_PATH="$WORK/prefix" \
    > "$WORK/log" 2>&1 || fail "configure examples"
cmake --build "$WORK/build" > "$WORK/log" 2>&1 || fail "build examples"

"$WORK/build/hello_server" 0 > "$WORK/server.log" 2>&1 &
SPID=$!
PORT=""
for _ in $(seq 1 100); do
    PORT="$(sed -n 's/^listening on port \([0-9]*\)$/\1/p' "$WORK/server.log")"
    [ -n "$PORT" ] && break
    sleep 0.05
done
[ -n "$PORT" ] || fail "hello_server did not start"

timeout 30 "$WORK/build/hello_client" 127.0.0.1 "$PORT"
RC=$?
[ "$RC" -eq 0 ] || fail "hello_client exit code $RC"
echo "check_install: ok (installed, built with find_package, examples ran)"
