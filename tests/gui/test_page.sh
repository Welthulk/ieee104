#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# test_page.sh – führt das Skript der GUI-Seite in gjs gegen eine laufende GUI aus
# (tests/gui/test_page.js): Laufzeitfehler der Seite, falsche Element-IDs und
# Abweichungen zwischen JSON der GUI und Seite fallen so ohne Browser auf.
#
# Mit Display zusätzlich: Namen der Inline-Handler in WebKitGTK (tests/gui/test_handlers_webkit.js).
#
# Aufruf: tests/gui/test_page.sh LIBRARY [LIB60870_C]
#   JULIA, GJS, CURL  Programme, Default aus dem Suchpfad
# Alles auf 127.0.0.1 mit freien Ports. Rückgabe: 0 bestanden, 1 Fehler, 2 Voraussetzung fehlt.

set -u
LIB="${1:?Library fehlt (libiec104.so.0)}"
LIB60870="${2:-}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
JULIA="${JULIA:-julia}"
GJS="${GJS:-gjs}"
for p in "$JULIA" "$GJS" "${CURL:-curl}"; do
    command -v "$p" > /dev/null 2>&1 || { echo "$p fehlt" >&2; exit 2; }
done
WORK="$(mktemp -d)"
GPID=""
cleanup() {
    [ -n "$GPID" ] && kill "$GPID" 2> /dev/null && wait "$GPID" 2> /dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

: > "$WORK/gui.log"   # exists before the loop below reads it
IEC104_LIB="$LIB" IEC104_LIB60870="" IEC104_GUI_LISTE="$WORK/werteliste.tsv" "$JULIA" --startup-file=no \
    "$ROOT/examples/julia/gui/iec104_gui.jl" 0 127.0.0.1 ${LIB60870:+"$LIB60870"} < /dev/null > "$WORK/gui.log" 2>&1 &
GPID=$!
PORT=""
for _ in $(seq 1 1200); do
    PORT="$(sed -n 's|^IEC 104 Test-GUI .*http://localhost:\([0-9]*\)$|\1|p' "$WORK/gui.log")"
    [ -n "$PORT" ] && break
    kill -0 "$GPID" 2> /dev/null || { cat "$WORK/gui.log" >&2; exit 1; }
    sleep 0.25
done
[ -n "$PORT" ] || { echo "GUI meldet keinen Port" >&2; exit 1; }
"$GJS" "$ROOT/tests/gui/test_page.js" "$ROOT/examples/julia/gui/index.html" "$PORT"
RC=$?
# Inline-Handler zusätzlich in echtem WebKit, wenn es ein Display gibt (sonst übersprungen).
"$GJS" "$ROOT/tests/gui/test_handlers_webkit.js" "$ROOT/examples/julia/gui/index.html" 2>&1 | grep -v '^libEGL'
case "${PIPESTATUS[0]}" in
    0) ;;
    2) echo "WebKit-Prüfung der Handler übersprungen (kein Display oder kein WebKitGTK)" ;;
    *) RC=1 ;;
esac
# Das GUI-Programm darf im Terminal nichts melden (z. B. gescheiterte Anfragen).
if grep -q -v '^IEC 104 Test-GUI ' "$WORK/gui.log"; then
    echo "FEHLER: Ausgabe der GUI im Terminal:" >&2
    cat "$WORK/gui.log" >&2
    RC=1
fi
exit "$RC"
