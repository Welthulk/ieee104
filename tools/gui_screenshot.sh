#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# gui_screenshot.sh – Bild der Test-GUI für die Doku (docs/bilder/test_gui.png).
#
# Aufruf: tools/gui_screenshot.sh LIBRARY [AUSGABE.png]
#
# Startet die GUI auf 127.0.0.1 (freier Port) mit leerer Werteliste in einem eigenen Home-Ordner,
# legt eine Teststation mit Beispielwerten an, füllt die Werteliste, verbindet
# die Leitstelle (STARTDT, Generalabfrage), sendet einen Befehl mit
# Rückmeldung und rendert die Seite mit WebKitGTK (tools/gui_screenshot.js).
# Braucht julia, curl, gjs und WebKitGTK 4.0 sowie ein Display (DISPLAY).

set -u
LIB="${1:?Library fehlt (libiec104.so.0)}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${2:-$ROOT/docs/bilder/test_gui.png}"
WORK="$(mktemp -d)"
GPID=""
cleanup() {
    [ -n "$GPID" ] && kill "$GPID" 2> /dev/null && wait "$GPID" 2> /dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

# Own home folder: the value list goes to its ~/.config/iec104/ (shown as such), never into the real one.
JULIA_BIN="$(julia --startup-file=no -e 'print(first(Base.julia_cmd().exec))')"
mkdir -p "$WORK/home"
: > "$WORK/gui.log"   # exists before the loop below reads it
HOME="$WORK/home" JULIA_DEPOT_PATH="${JULIA_DEPOT_PATH:-$HOME/.julia}" IEC104_LIB="$(cd "$(dirname "$LIB")" && pwd)/$(basename "$LIB")" IEC104_LIB60870="" \
    "$JULIA_BIN" --startup-file=no \
    "$ROOT/examples/julia/gui/iec104_gui.jl" 0 127.0.0.1 < /dev/null > "$WORK/gui.log" 2>&1 &
GPID=$!
PORT=""
for _ in $(seq 1 1200); do
    PORT="$(sed -n 's|^IEC 104 Test-GUI .*http://localhost:\([0-9]*\)$|\1|p' "$WORK/gui.log")"
    [ -n "$PORT" ] && break
    kill -0 "$GPID" 2> /dev/null || { cat "$WORK/gui.log" >&2; exit 1; }
    sleep 0.25
done
[ -n "$PORT" ] || { echo "GUI meldet keinen Port" >&2; exit 1; }
api() { curl -sS --max-time 5 -H 'X-IEC104-GUI: 1' "http://127.0.0.1:$PORT/api/$1" > /dev/null; }
q() { printf '%s' "$1" | sed 's/ /%20/g'; }

api 'server/listen?port=0&station=1'
SPORT="$(curl -sS -H 'X-IEC104-GUI: 1' "http://127.0.0.1:$PORT/api/state" |
         grep -o '"server":{"running":true,"port":[0-9]*' | grep -o '[0-9]*$')"
for p in '1&ioa=1001&value=0' '3&ioa=1002&value=2' '5&ioa=1003&value=4' '13&ioa=2001&value=230.4' \
         '11&ioa=2002&value=412' '13&ioa=2003&value=0' '15&ioa=3001&value=123456'; do
    api "server/point?type=$p"
done
while IFS='|' read -r ioa type fb text; do
    api "list/add?station=1&ioa=$ioa&type=$type&fb=$fb&text=$(q "$text")"
done << 'EOF'
1001|1||Leistungsschalter Q1 Stellung
1002|3||Trenner Q2 Stellung
1003|5||Trafo T1 Stufe
2001|13||Spannung L1 (kV)
2002|11||Strom L1 (A)
2003|13||Wirkleistung Sollwert Rückmeldung (MW)
3001|15||Arbeit (kWh)
6001|45|1001|Leistungsschalter Q1 EIN/AUS
6004|50|2003|Sollwert Wirkleistung (MW)
4711|1||Erwartet, aber nicht geliefert
EOF
api "client/connect?host=127.0.0.1&port=$SPORT&station=1&auto=1&term=1"
sleep 2
api 'client/command?station=1&type=45&ioa=6001&value=1&mode=sbo'
sleep 0.5
api 'server/point?type=1&ioa=1001&value=1'
api 'client/command?station=1&type=50&ioa=6004&value=12.5'
sleep 0.5
api 'server/point?type=13&ioa=2003&value=12.5'
api 'server/point?type=13&ioa=2001&value=231.1'
api 'client/ci?rqt=5&frz=0'
sleep 1.5

mkdir -p "$(dirname "$OUT")"
gjs "$ROOT/tools/gui_screenshot.js" "http://localhost:$PORT/" "$OUT" 1500 4000
