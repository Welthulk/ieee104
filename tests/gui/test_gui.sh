#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# test_gui.sh – prüft die Test-GUI (examples/julia/gui/iec104_gui.jl) über HTTP.
#
# Aufruf: tests/gui/test_gui.sh LIBRARY [LIB60870_C]
#
#   LIBRARY     geteilte Library (libiec104.so.0), wird der GUI als IEC104_LIB übergeben
#   LIB60870_C  optional, sonst Umgebungsvariable IEC104_LIB60870: Ordner lib60870-C
#               mit gebauten Beispielen; dann zusätzlich die Gegenstelle lib60870
#   JULIA, CURL Programme, Default aus dem Suchpfad
#
# Alles läuft auf 127.0.0.1 mit Port 0: GUI und Station wählen freie Ports.
# Geprüft: Host- und Kopfzeilenschutz, nur GET; Station mit Datenpunkt (nur auf
# 127.0.0.1); Leitstelle der GUI mit STARTDT und Generalabfrage. Mit lib60870:
# simple_client gegen die Station (Verbindung, Generalabfrage, Ende von selbst),
# cs104_client_async starten und beenden. Die GUI wird am Ende immer beendet,
# danach darf nichts mehr auf ihren Ports lauschen.
# Rückgabe: 0 bestanden, 1 Fehler, 2 Voraussetzung fehlt.

set -u

LIB="${1:?Library fehlt (libiec104.so.0)}"
LIB60870="${2:-${IEC104_LIB60870:-}}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GUI="$ROOT/examples/julia/gui/iec104_gui.jl"
JULIA="${JULIA:-julia}"
CURL="${CURL:-curl}"
command -v "$JULIA" > /dev/null 2>&1 || { echo "julia fehlt" >&2; exit 2; }
command -v "$CURL" > /dev/null 2>&1 || { echo "curl fehlt" >&2; exit 2; }
[ -r "$LIB" ] || { echo "Library fehlt: $LIB" >&2; exit 2; }

WORK="$(mktemp -d)"
LOG="$WORK/gui.log"
GPID=""
PORT=""
SPORT=""
RC=0

listening() { ss -ltn 2>/dev/null | grep -q -E "[:.]$1[[:space:]]"; }

cleanup() {
    if [ -n "$GPID" ] && kill -0 "$GPID" 2> /dev/null; then
        local children
        children="$(ps -o pid= --ppid "$GPID" 2> /dev/null)"
        kill -INT "$GPID" 2> /dev/null   # atexit der GUI beendet eine laufende Gegenstelle
        for _ in $(seq 1 50); do
            kill -0 "$GPID" 2> /dev/null || break
            sleep 0.1
        done
        kill -0 "$GPID" 2> /dev/null && kill -KILL "$GPID" 2> /dev/null
        wait "$GPID" 2> /dev/null
        for c in $children; do
            kill -0 "$c" 2> /dev/null && { echo "Kindprozess $c lief noch" >&2; kill -KILL "$c"; RC=1; }
        done
    fi
    for p in $PORT $SPORT; do
        sleep 0.2
        listening "$p" && { echo "FEHLER: Port $p lauscht nach dem Ende noch" >&2; RC=1; }
    done
    if [ "$RC" -ne 0 ]; then
        echo "--- Ausgabe der GUI (Ende) ---" >&2
        tail -n 40 "$LOG" >&2
    fi
    rm -rf "$WORK"
    exit "$RC"
}
trap cleanup EXIT

fail() { echo "FEHLER: $*" >&2; RC=1; exit 1; }
ok() { echo "ok: $*"; }
url() { echo "http://127.0.0.1:$PORT$1"; }
api() { "$CURL" -sS --max-time 5 -H 'X-IEC104-GUI: 1' "$(url "/api/$1")"; }
status() { "$CURL" -s -o /dev/null --max-time 5 -w '%{http_code}' "$@"; }
last_seq() { api 'state?since=0' | grep -o '"seq":[0-9]*' | tail -n 1 | grep -o '[0-9]*$'; }

# Aktion muss {"ok":true} liefern.
act() {
    local r
    r="$(api "$1")"
    case "$r" in *'"ok":true'*) ;; *) fail "$1: $r" ;; esac
}
# Aktion muss mit diesem Text abgelehnt werden.
act_rejected() {
    local r
    r="$(api "$1")"
    case "$r" in *'"ok":false'*"$2"*) ok "$1 abgelehnt: $2" ;; *) fail "$1: erwartet '$2', erhalten $r" ;; esac
}
# Wartet bis zu N Sekunden, bis der Zustand (ab Ereignis SINCE) alle Muster enthält.
wait_state() { # Sekunden, Beschreibung, since, Muster...
    local t="$1" what="$2" since="$3" end s p missing
    shift 3
    end=$((SECONDS + t))
    while :; do
        s="$(api "state?since=$since")"
        missing=""
        for p in "$@"; do
            printf '%s' "$s" | grep -q -E -- "$p" || { missing="$p"; break; }
        done
        [ -z "$missing" ] && { ok "$what"; return 0; }
        [ "$SECONDS" -lt "$end" ] || fail "$what: fehlt nach $t s: $missing"
        sleep 0.25
    done
}

# --- GUI starten ---------------------------------------------------------------
IEC104_LIB="$LIB" IEC104_LIB60870="" "$JULIA" --startup-file=no "$GUI" 0 127.0.0.1 \
    ${LIB60870:+"$LIB60870"} < /dev/null > "$LOG" 2>&1 &
GPID=$!
for _ in $(seq 1 1200); do   # der erste Julia-Start übersetzt einmalig
    PORT="$(sed -n 's|^IEC 104 Test-GUI .*http://localhost:\([0-9]*\)$|\1|p' "$LOG")"
    [ -n "$PORT" ] && break
    kill -0 "$GPID" 2> /dev/null || fail "GUI läuft nicht"
    sleep 0.25
done
[ -n "$PORT" ] || fail "GUI meldet keinen Port"
ok "GUI auf Port $PORT"
ss -ltn 2> /dev/null | grep -q -E "127\.0\.0\.1:$PORT[[:space:]]" || fail "GUI lauscht nicht nur auf 127.0.0.1"

# --- Schutz: Host, Kopfzeile, Methode --------------------------------------------
[ "$(status "$(url /api/state)")" = 403 ] || fail "ohne X-IEC104-GUI nicht 403"
ok "ohne X-IEC104-GUI: 403"
[ "$(status -H 'X-IEC104-GUI: 1' -H "Host: evil.example:$PORT" "$(url /api/state)")" = 403 ] ||
    fail "fremder Host nicht 403"
[ "$(status -H "Host: evil.example:$PORT" "$(url /)")" = 403 ] || fail "Seite mit fremdem Host nicht 403"
[ "$(status -H 'X-IEC104-GUI: 1' -H "Host: localhost:1" "$(url /api/state)")" = 403 ] ||
    fail "falscher Port im Host nicht 403"
ok "fremder Host, falscher Port: 403"
R="$("$CURL" -sS --max-time 5 -H 'X-IEC104-GUI: 1' -H "Host: localhost:$PORT" -w '\n%{http_code}' \
    "$(url /api/state)")"
case "$R" in '{"client":'*$'\n'200) ok "Host localhost:$PORT: 200 JSON" ;; *) fail "Host localhost: $R" ;; esac
[ "$(status -X POST -H 'X-IEC104-GUI: 1' "$(url /api/state)")" = 405 ] || fail "POST nicht 405"
H="$("$CURL" -s -i --max-time 5 -X OPTIONS -H 'Origin: http://evil.example' \
    -H 'Access-Control-Request-Method: GET' -H 'Access-Control-Request-Headers: x-iec104-gui' \
    "$(url /api/state)")"
case "$H" in HTTP/1.1\ 405*) ;; *) fail "OPTIONS nicht 405" ;; esac
printf '%s' "$H" | grep -q -i '^access-control' && fail "Access-Control-Kopfzeile gesendet"
ok "POST, OPTIONS: 405 ohne Access-Control"
"$CURL" -sS --max-time 5 -H "Host: localhost:$PORT" "$(url /)" | grep -q "X-IEC104-GUI" ||
    fail "Seite sendet X-IEC104-GUI nicht"
ok "Seite mit Kopfzeile X-IEC104-GUI"

# --- Gegenstelle ohne Station / ohne Ordner ---------------------------------------
if [ -n "$LIB60870" ]; then
    wait_state 5 "lib60870-Programme gefunden" 0 '"peer":\{"available":true' \
        '"key":"simple_client","label":"[^"]*","available":true' \
        '"key":"cs104_client_async","label":"[^"]*","available":true'
    act_rejected 'peer/start?prog=simple_client' 'erst die Station starten'
    act_rejected 'peer/start?prog=/bin/true' 'unbekanntes Programm'
else
    wait_state 5 "ohne lib60870-Ordner" 0 '"peer":\{"available":false,"folder":"","running":false'
    act_rejected 'peer/start?prog=simple_client' 'kein lib60870-Ordner'
fi

# --- Station und Leitstelle der GUI ------------------------------------------------
act 'server/listen?port=0&station=1'
SPORT="$(api state | grep -o '"server":{"running":true,"port":[0-9]*' | grep -o '[0-9]*$')"
[ -n "$SPORT" ] && [ "$SPORT" -gt 0 ] || fail "Station meldet keinen Port"
ss -ltn 2> /dev/null | grep -q -E "127\.0\.0\.1:$SPORT[[:space:]]" || fail "Station lauscht nicht auf 127.0.0.1"
ok "Station auf 127.0.0.1:$SPORT"
act 'server/point?type=13&ioa=100&value=42.5'
wait_state 5 "Datenpunkt angelegt" 0 '"points":\[\{"ioa":100,"type":13,"value":42.5\}\]'

act "client/connect?host=127.0.0.1&port=$SPORT&station=1"
wait_state 10 "Leitstelle verbunden" 0 '"client":\{"state":"stopped"'
act 'client/start'
wait_state 10 "STARTDT" 0 '"client":\{"state":"started"'
act 'client/gi?qoi=20'
wait_state 10 "Generalabfrage: Punkt in der Tabelle" 0 \
    '\{"station":1,"ioa":100,"type":13,"cause":20,"negative":false,"value":42.5,' \
    'Station: Generalabfrage \(Typ 100, Ursache 6\)'
act 'client/close'

# --- Gegenstelle lib60870 ---------------------------------------------------------------
if [ -n "$LIB60870" ]; then
    S="$(last_seq)"
    act 'peer/start?prog=simple_client'
    wait_state 20 "simple_client: Verbindung, STARTDT, Generalabfrage" "$S" \
        '"text":"lib60870: Connected!"' '"text":"lib60870: Received STARTDT_CON"' \
        '"text":"Station: Verbindung [0-9]+ Datenübertragung läuft"' \
        '"text":"Station: Generalabfrage \(Typ 100, Ursache 6\) von Verbindung [0-9]+"' \
        '"text":"lib60870: RECVD ASDU type: M_ME_NC_1\(13\)' \
        '"text":"Station: Testbefehl mit Zeitmarke \(Typ 107'
    wait_state 15 "simple_client endet von selbst" "$S" '"peer":\{[^}]*"running":false' \
        '"text":"lib60870: simple_client beendet, Rückgabe 0"'

    S="$(last_seq)"
    act 'peer/start?prog=cs104_client_async'
    act_rejected 'peer/start?prog=simple_client' 'Gegenstelle läuft bereits'
    wait_state 15 "cs104_client_async: STARTDT, Generalabfrage" "$S" \
        '"peer":\{[^}]*"running":true,"program":"cs104_client_async"' \
        '"text":"lib60870: Received STARTDT_CON"' \
        '"text":"Station: Generalabfrage \(Typ 100, Ursache 6\) von Verbindung [0-9]+"'
    act 'peer/stop'
    wait_state 8 "cs104_client_async beendet" "$S" '"peer":\{[^}]*"running":false' \
        '"text":"lib60870: cs104_client_async beendet, '
    [ -z "$(ps -o pid= --ppid "$GPID" 2> /dev/null)" ] || fail "Gegenstelle läuft noch als Prozess"
    act_rejected 'peer/stop' 'keine Gegenstelle aktiv'
fi

echo "test_gui: bestanden"
