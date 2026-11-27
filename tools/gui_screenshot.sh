#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# gui_screenshot.sh – Bilder der Test-GUI für die Doku (docs/bilder/).
#
# Aufruf: tools/gui_screenshot.sh LIBRARY [ORDNER]
#
# Erzeugt im ORDNER (Standard docs/bilder):
#   test_gui.png             Reiter „Prüfstand“: Teststation mit Beispielwerten, Werteliste,
#                            Leitstelle mit STARTDT und Generalabfrage, Befehle mit Rückmeldung
#   test_gui_station.png     Reiter „Station und lib60870“ derselben Sitzung
#   test_gui_mithoeren.png   Reiter „Mithören“ nach Auswertung eines Beispiel-Mitschnitts
#                            (eigene Sitzung, damit nur mitgehörte Rahmen zu sehen sind)
#   test_gui_wege.png        Reiter „Prüfstand“ mit zwei Wegen zu Station A und B: Reihe,
#                            Weg 1 getrennt, Weg 2 stumm, Umschaltungen mit Nachlauf
# Die GUI läuft auf 127.0.0.1 (freier Port) mit eigenem Home-Ordner (leere Werteliste,
# die echte bleibt unberührt). Gerendert wird mit WebKitGTK (tools/gui_screenshot.js).
# Braucht julia, curl, gjs, WebKitGTK 4.0 und ein Display (DISPLAY).

set -u
LIB="${1:?Library fehlt (libiec104.so.1)}"
LIB="$(cd "$(dirname "$LIB")" && pwd)/$(basename "$LIB")"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${2:-$ROOT/docs/bilder}"
WORK="$(mktemp -d)"
GPID=""
PORT=""
DEPOT="${JULIA_DEPOT_PATH:-$HOME/.julia}"   # the real one: compiled code is reused
JULIA_BIN="$(julia --startup-file=no -e 'print(first(Base.julia_cmd().exec))')"

stop_gui() {
    [ -n "$GPID" ] && kill "$GPID" 2> /dev/null && wait "$GPID" 2> /dev/null
    GPID=""
}
cleanup() { stop_gui; rm -rf "$WORK"; }
trap cleanup EXIT

start_gui() {
    rm -rf "$WORK/home"
    mkdir -p "$WORK/home"
    : > "$WORK/gui.log"   # exists before the loop below reads it
    HOME="$WORK/home" JULIA_DEPOT_PATH="$DEPOT" IEC104_LIB="$LIB" IEC104_LIB60870="" \
        "$JULIA_BIN" --startup-file=no "$ROOT/examples/julia/gui/iec104_gui.jl" 0 127.0.0.1 \
        < /dev/null > "$WORK/gui.log" 2>&1 &
    GPID=$!
    PORT=""
    for _ in $(seq 1 1200); do
        PORT="$(sed -n 's|^IEC 104 Test-GUI .*http://localhost:\([0-9]*\)$|\1|p' "$WORK/gui.log")"
        [ -n "$PORT" ] && return 0
        kill -0 "$GPID" 2> /dev/null || { cat "$WORK/gui.log" >&2; exit 1; }
        sleep 0.25
    done
    echo "GUI meldet keinen Port" >&2
    exit 1
}
api() { curl -sS --max-time 5 -H 'X-IEC104-GUI: 1' "http://127.0.0.1:$PORT/api/$1" > /dev/null; }
q() { printf '%s' "$1" | sed 's/ /%20/g'; }
shot() { # Reiter, Datei, [JavaScript vor dem Bild]
    gjs "$ROOT/tools/gui_screenshot.js" "http://localhost:$PORT/#$1" "$OUT/$2" 1500 4000 "${3:-}" 2>&1 |
        grep -v '^libEGL'
}
mkdir -p "$OUT"

# --- Sitzung 1: Prüfstand gegen die Teststation ------------------------------------------
start_gui
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
shot pruef test_gui.png
shot station test_gui_station.png
stop_gui

# --- Sitzung 2: Mithören eines Beispiel-Mitschnitts -----------------------------------------
start_gui
"$JULIA_BIN" --startup-file=no "$ROOT/tests/gui/test_capture.jl" --sample "$WORK/home/iec104.pcap"
api "listen/start?path=$WORK/home/iec104.pcap&port=2404&image=1"
sleep 1.5
shot mithoeren test_gui_mithoeren.png
stop_gui

# --- Sitzung 3: zwei Wege (Station A und B), Reihe, ein Weg getrennt, einer stumm ----------
start_gui
api 'server/listen?port=0&station=1&port_b=0'
PORTS="$(curl -sS -H 'X-IEC104-GUI: 1' "http://127.0.0.1:$PORT/api/state" |
         grep -o '"port":[0-9]*,"station":1,"accept":[a-z]*,"port_b":[0-9]*')"
PA="$(echo "$PORTS" | sed 's/^"port":\([0-9]*\).*/\1/')"
PB="$(echo "$PORTS" | sed 's/.*"port_b":\([0-9]*\)$/\1/')"
api 'server/point?type=13&ioa=2001&value=230.4'
api 'server/point?type=1&ioa=1001&value=1'
# Kurze Timer: ein stummer Weg fällt nach t3 + t1 = 3 s auf.
api "client/connect?host=127.0.0.1&port=$PA&port2=$PB&station=1&auto=1&t1=2&t2=1&t3=1"
sleep 2
api 'series?do=start&ioa=9000&ms=200'
sleep 3
api 'client/path?nr=1&fault=close'      # sofort bemerkt: Weg 2 übernimmt
sleep 4
api 'client/path?nr=2&fault=silent'     # erst nach t3 + t1: Weg 1 übernimmt mit Nachlauf
sleep 6
api 'series?do=stop'
sleep 1
# Unter Telegramme nur die Ereignisse (Umschaltungen, Störungen), keine Rahmen.
shot pruef test_gui_wege.png "document.getElementById('showframes').checked = false; renderLog();"
