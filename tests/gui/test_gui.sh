#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# test_gui.sh – prüft die Test-GUI (examples/julia/gui/iec104_gui.jl) über HTTP.
#
# Aufruf: tests/gui/test_gui.sh LIBRARY [LIB60870_C]
#
#   LIBRARY     geteilte Library (libiec104.so.1), wird der GUI als IEC104_LIB übergeben
#   LIB60870_C  optional, sonst Umgebungsvariable IEC104_LIB60870: Ordner lib60870-C
#               mit gebauten Beispielen; dann zusätzlich die Gegenstelle lib60870
#   JULIA, CURL Programme, Default aus dem Suchpfad
#   GUI_REPORT  Datei für das vollständige Protokoll des Selbsttests (optional)
#
# Alles läuft auf 127.0.0.1 mit Port 0: GUI und Station wählen freie Ports.
# Geprüft: Host- und Kopfzeilenschutz, nur GET; Station mit Datenpunkt (nur auf
# 127.0.0.1); Leitstelle der GUI mit STARTDT und Generalabfrage. Mit lib60870:
# simple_client gegen die Station (Verbindung, Generalabfrage, Ende von selbst),
# cs104_client_async starten und beenden. Die GUI wird am Ende immer beendet,
# danach darf nichts mehr auf ihren Ports lauschen.
# Rückgabe: 0 bestanden, 1 Fehler, 2 Voraussetzung fehlt.

set -u

LIB="${1:?Library fehlt (libiec104.so.1)}"
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
: > "$LOG"   # exists before the loop below reads it
IEC104_LIB="$LIB" IEC104_LIB60870="" IEC104_GUI_LISTE="$WORK/werteliste.tsv" "$JULIA" --startup-file=no "$GUI" 0 127.0.0.1 \
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
case "$R" in '{"boot":'*$'\n'200) ok "Host localhost:$PORT: 200 JSON" ;; *) fail "Host localhost: $R" ;; esac
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
# Werte werden auch ohne laufende Station geprüft (sonst scheitert später der Start).
act_rejected 'server/point?type=1&ioa=200&value=2' 'passt nicht zu Typ 1'
act 'server/listen?port=0&station=1'
SPORT="$(api state | grep -o '"server":{"running":true,"port":[0-9]*' | grep -o '[0-9]*$')"
[ -n "$SPORT" ] && [ "$SPORT" -gt 0 ] || fail "Station meldet keinen Port"
ss -ltn 2> /dev/null | grep -q -E "127\.0\.0\.1:$SPORT[[:space:]]" || fail "Station lauscht nicht auf 127.0.0.1"
ok "Station auf 127.0.0.1:$SPORT"
act 'server/point?type=13&ioa=100&value=42.5'
wait_state 5 "Datenpunkt angelegt" 0 '"points":\[\{"ioa":100,"type":13,"value":42.5\}\]'

# Prüfstand: Werteliste, automatischer Start, Prozessabbild mit Zählern je Ursache.
act 'list/add?station=1&ioa=100&type=13&text=Spannung%20L1'
act 'server/point?type=1&ioa=1001&value=0'
act 'list/add?station=1&ioa=6001&type=45&text=Schalter%20Q1&fb=1001'
wait_state 5 "Werteliste" 0 '"list":\[\{"st":1,"ioa":100,"ty":13,"fb":-1,"tx":"Spannung L1"\}' \
    '\{"st":1,"ioa":6001,"ty":45,"fb":1001,"tx":"Schalter Q1"\}'
grep -q $'^1\t6001\t45\t1001\tSchalter Q1$' "$WORK/werteliste.tsv" || fail "Werteliste nicht gespeichert"
ok "Werteliste gespeichert"
act_rejected 'list/add?station=0&ioa=1' 'Station 1'

act "client/connect?host=127.0.0.1&port=$SPORT&station=1&auto=1&term=1"
wait_state 10 "Verbinden mit STARTDT und Generalabfrage" 0 '"client":\{"state":"started"' \
    '\{"st":1,"ioa":100,"ty":13,"c":20,"neg":false,"v":"Wert 42.5","tt":"","u":"[0-9:]*","n":\[0,0,0,1,0,0,0,0,0\]\}' \
    '"kind":"frame","text":"Leitstelle → I [^"]*C_IC_NA_1[^"]*","explain":"Leitstelle fordert alle Werte an","dir":"out","f":"I","ty":100,"c":6,"io":\[0\]'
act_rejected 'client/path?nr=1&fault=close' 'nur mit zwei Wegen'   # one connection: no paths
act_rejected 'client/auto?on=1' 'nur mit zwei Wegen'
act_rejected 'series?do=start&ms=5' 'Takt 20'
act 'server/point?type=13&ioa=100&value=43'
act 'client/read?ioa=100'
wait_state 10 "spontan und gelesen gezählt" 0 \
    '\{"st":1,"ioa":100,"ty":13,"c":5,"neg":false,"v":"Wert 43","tt":"","u":"[0-9:]*","n":\[0,0,1,1,0,1,0,0,0\]\}'
act 'image/reset'
wait_state 5 "Zähler auf 0" 0 '"ioa":100,[^}]*"n":\[0,0,0,0,0,0,0,0,0\]'

# Befehl mit Laufzeiten; die Station nimmt alles an, die Rückmeldung setzt der Test.
act 'client/command?type=45&ioa=6001&value=1&mode=direct'
wait_state 10 "Befehl bestätigt und beendet" 0 \
    '"cmds":\[\{"id":1,"st":1,"ioa":6001,"ty":45,"tsel":-1,"tcon":[0-9]+,"tterm":[0-9]+,"tfin":[0-9]+,"fb":1001,"tfb":-1,[^}]*"res":"ausgeführt"'
# Eine Generalabfrage nach dem Befehl ist keine Rückmeldung (nur Ursache 3, 11, 12 zählt).
act 'client/gi?qoi=20'
wait_state 10 "Generalabfrage nach dem Befehl zählt nicht als Rückmeldung" 0 \
    '"ioa":1001,"ty":1,"c":20,' '"cmds":\[\{"id":1,[^}]*"fb":1001,"tfb":-1,'
act 'server/point?type=1&ioa=1001&value=1'
wait_state 10 "Rückmeldung mit Zeit" 0 '"cmds":\[\{"id":1,[^}]*"fb":1001,"tfb":[0-9]+,[^}]*"fbv":"ein"'
act 'client/command?type=45&ioa=6001&value=0&mode=sbo'
wait_state 10 "Select und Execute" 0 '"cmds":\[\{"id":2,[^}]*"tsel":[0-9]+,"tcon":[0-9]+,"tterm":[0-9]+,[^}]*"mode":"Select\+Execute"[^}]*"res":"ausgeführt"'
act_rejected 'client/command?type=13&ioa=1&value=1' 'kein Befehl'
act_rejected 'client/command?ioa=6001&value=1' 'Befehlstyp fehlt'   # nie stillschweigend Typ 45
act_rejected 'client/command?type=45&ioa=6001&value=1&fb=abc' 'keine ganze Zahl'   # geprüft vor dem Senden
act_rejected 'client/command?type=45&ioa=6001&value=1&fb=99999999' 'Rückmelde-IOA'
act 'server/accept?on=0'
act 'client/command?type=45&ioa=6001&value=1'
wait_state 10 "abgelehnter Befehl" 0 '"cmds":\[\{"id":3,[^}]*"res":"abgelehnt"'
act 'server/accept?on=1'
act 'list/take'
# Zwei Befehle an dieselbe IOA kurz nacheinander: erst nur Select, dann direkt. Jede
# Antwort gehört zum ältesten offenen Befehl, der auf sie wartet (wie in der Library): die
# Select-Bestätigung zu Befehl 4, Bestätigung und Beendigung der Ausführung zu Befehl 5.
# Mit falscher Zuordnung (neuester zuerst) bekäme Befehl 5 die Select-Bestätigung.
act 'client/command?type=45&ioa=6001&value=1&mode=select'
act 'client/command?type=45&ioa=6001&value=1&mode=direct'
wait_state 10 "zwei Befehle an dieselbe IOA richtig zugeordnet" 0 \
    '"cmds":\[\{"id":5,"st":1,"ioa":6001,"ty":45,"tsel":-1,"tcon":[0-9]+,"tterm":[0-9]+,"tfin":[0-9]+,[^}]*"res":"ausgeführt"[^}]*\},\{"id":4,"st":1,"ioa":6001,"ty":45,"tsel":[0-9]+,"tcon":-1,"tterm":-1,"tfin":[0-9]+,[^}]*"res":"ausgewählt"'
act 'client/cmds_clear'
wait_state 5 "Befehle geleert" 0 '"cmds":\[\]'
act_rejected 'server/listen?port=0&station=70000' 'Station (CA) 1…65534'
wait_state 5 "Station läuft nach falscher Eingabe weiter" 0 "\"server\":\\{\"running\":true,\"port\":$SPORT"
wait_state 5 "empfangene Punkte übernommen" 0 '\{"st":1,"ioa":1001,"ty":1,"fb":-1,"tx":""\}'
# --- Mithören: Mitschnitt aus tests/gui/test_capture.jl (pcap, Ethernet, 6 Rahmen) ------------
"$JULIA" --startup-file=no "$ROOT/tests/gui/test_capture.jl" --sample "$WORK/sample.pcap" ||
    fail "Beispiel-Mitschnitt nicht erzeugt"
act "listen/start?path=$WORK/sample.pcap&port=2404&image=1"
wait_state 10 "Mitschnitt ausgewertet" 0 \
    '"text":"Mithören · Station 10.0.0.5:2404 → Leitstelle 10.0.0.9:51000 · I N\(S\)=3 N\(R\)=1 · 11 M_ME_NB_1' \
    '"text":"Mithören · Leitstelle 10.0.0.9:51000 → Station 10.0.0.5:2404 · U STARTDT act"' \
    '"text":"Mithören beendet: 10 Pakete, 6 Rahmen"' \
    '\{"st":1,"ioa":101,"ty":11,"c":20,"neg":false,"v":"Wert 23"' \
    '"listen":\{"mode":"off"'
# Live: tcpdump schreibt noch; die Datei kommt in zwei Teilen, dazwischen neu begonnen.
head -c 200 "$WORK/sample.pcap" > "$WORK/live.pcap"
act "listen/start?path=$WORK/live.pcap&live=1&image=0"
wait_state 5 "Live: liest mit (erste 200 Byte gelesen)" 0 '"listen":\{"mode":"live","state":"liest mit",[^}]*"bytes":200\}'
S2="$(last_seq)"
: > "$WORK/live.pcap"
sleep 0.5
cat "$WORK/sample.pcap" > "$WORK/live.pcap"
wait_state 10 "Live: neu begonnene Datei vollständig gelesen" "$S2" \
    '"text":"Mithören: Datei neu begonnen, lese von vorn"' '"listen":\{"mode":"live","state":"liest mit","path":"[^"]*","packets":10,"frames":6,'
act 'listen/stop'
printf 'kein Mitschnitt' > "$WORK/bad.pcap"
act "listen/start?path=$WORK/bad.pcap"
wait_state 5 "keine pcap-Datei erkannt" 0 '"text":"Mithören: keine pcap- oder pcapng-Datei'
act_rejected 'listen/start?path=/gibt/es/nicht.pcap' 'gibt es nicht'

act 'client/close'

# --- Zwei Wege: Station A und B, Prüfstand mit Weg 2, Reihe, Störung, Schließen -----------
act_rejected "server/listen?port=$SPORT&station=1&port_b=$SPORT" 'anderen Port'
wait_state 5 "gleiche Ports vorher abgelehnt, Station läuft weiter" 0 "\"server\":\\{\"running\":true,\"port\":$SPORT"
act_rejected "server/listen?port=0&station=1&port_b=$PORT" 'Station A wieder beendet'   # Port der GUI belegt
wait_state 5 "Station B scheitert: keine halbe Station" 0 '"server":\{"running":false'
act 'server/listen?port=0&station=1&port_b=0'
PORTS="$(api state | grep -o '"server":{"running":true,"port":[0-9]*,"station":1,"accept":[a-z]*,"port_b":[0-9]*')"
PA="$(echo "$PORTS" | grep -o '"port":[0-9]*' | grep -o '[0-9]*')"
PB="$(echo "$PORTS" | grep -o '"port_b":[0-9]*' | grep -o '[0-9]*')"
[ -n "$PA" ] && [ -n "$PB" ] && [ "$PB" -gt 0 ] || fail "Station A und B melden keine Ports: $PORTS"
ok "Station A auf $PA, Station B auf $PB"
act "client/connect?host=127.0.0.1&port=$PA&port2=$PB&station=1&auto=1"
wait_state 10 "zwei Wege gestartet" 0 '"paths":\{"count":2,"leading":[12],' \
    '"nr":1,[^}]*"up":true' '"nr":2,[^}]*"up":true'
act 'series?do=start&ioa=9000&ms=50'
sleep 1
act 'series?do=reset'                        # während der Lauf weitergeht
L="$(api state | grep -o '"paths":{"count":2,"leading":[0-9]' | grep -o '[0-9]$')"
act "client/path?nr=$L&fault=close"
wait_state 10 "Umschaltung nach Trennen" 0 '"paths":\{"count":2,"leading":[12],"switches":1,'
sleep 1
act 'series?do=stop'
wait_state 5 "Reihe nach Zurücksetzen ohne Fehlende" 0 '"series":\{"on":false,[^}]*"missing":0,"dup":[0-9]+,"pending":0\}'
S3="$(last_seq)"
act 'client/stop'
wait_state 10 "STOPDT auf beiden Wegen ohne Umschaltung" "$S3" \
    '"text":"Leitstelle: Datenübertragung auf allen Wegen angehalten \(STOPDT\); kein Weg führt"' \
    '"paths":\{"count":2,"leading":0,"switches":1,'
api "state?since=$S3" | grep -q 'verbindet weiter' && fail "STOPDT als Ausfall gemeldet"
act 'client/close'
wait_state 10 "Client mit Wegen geschlossen, Zustand weiter lesbar" 0 \
    '"client":\{"state":"closed"' '"paths":\{"count":1,"leading":0,'

# --- Gegenstelle lib60870 ---------------------------------------------------------------
if [ -n "$LIB60870" ]; then
    S="$(last_seq)"
    act 'peer/start?prog=simple_client'
    wait_state 20 "simple_client: Verbindung, STARTDT, Generalabfrage" "$S" \
        '"text":"lib60870: Connected!"' '"text":"lib60870: Received STARTDT_CON"' \
        '"text":"Station( A)?: Verbindung [0-9]+ Datenübertragung läuft"' \
        '"text":"Station( A)?: Generalabfrage \(Typ 100, Ursache 6\) von Verbindung [0-9]+"' \
        '"text":"lib60870: RECVD ASDU type: M_ME_NC_1\(13\)' \
        '"text":"Station( A)?: Testbefehl mit Zeitmarke \(Typ 107'
    wait_state 15 "simple_client endet von selbst" "$S" '"peer":\{[^}]*"running":false' \
        '"text":"lib60870: simple_client beendet, Rückgabe 0"'

    S="$(last_seq)"
    act 'peer/start?prog=cs104_client_async'
    act_rejected 'peer/start?prog=simple_client' 'Gegenstelle läuft bereits'
    wait_state 15 "cs104_client_async: STARTDT, Generalabfrage" "$S" \
        '"peer":\{[^}]*"running":true,"program":"cs104_client_async"' \
        '"text":"lib60870: Received STARTDT_CON"' \
        '"text":"Station( A)?: Generalabfrage \(Typ 100, Ursache 6\) von Verbindung [0-9]+"'
    act 'peer/stop'
    wait_state 8 "cs104_client_async beendet" "$S" '"peer":\{[^}]*"running":false' \
        '"text":"lib60870: cs104_client_async beendet, '
    [ -z "$(ps -o pid= --ppid "$GPID" 2> /dev/null)" ] || fail "Gegenstelle läuft noch als Prozess"
    act_rejected 'peer/stop' 'keine Gegenstelle aktiv'
fi

# --- Selbsttest der GUI -------------------------------------------------------------------
act 'selftest/start'
act_rejected 'selftest/start' 'Selbsttest läuft bereits'
# Während des Laufs kopiert: Zwischenstand, nie „Alle Schritte bestanden“.
RUNNING="$(api 'selftest/report?full=0')"
echo "$RUNNING" | grep -q '^Zwischenstand: .*(noch nicht fertig)' || fail "Protokoll im Lauf ohne Zwischenstand"
echo "$RUNNING" | grep -q 'Alle Schritte bestanden' && fail "Protokoll im Lauf meldet „Alle Schritte bestanden“"
ok "Protokoll während des Laufs als Zwischenstand"
T=""
for _ in $(seq 1 600); do   # höchstens 120 s
    T="$(api 'state?since=999999999' | grep -o '"selftest":{"running":[a-z]*,"total":[0-9]*,"passed":[0-9]*,"failed":[0-9]*')"
    echo "$T" | grep -q -E '"running":false,"total":[0-9]+,"passed":[0-9]+,"failed":[0-9]+' &&
        ! echo "$T" | grep -q '"passed":0,"failed":0' && break
    sleep 0.2
done
echo "$T" | grep -q '"running":false' || fail "Selbsttest nach 120 s nicht beendet: $T"
if ! echo "$T" | grep -q '"failed":0'; then
    api 'selftest/report?full=0' >&2
    fail "Selbsttest mit Fehlern: $T"
fi
REPORT="$(api 'selftest/report?full=1')"
[ -n "${GUI_REPORT:-}" ] && printf '%s\n' "$REPORT" > "$GUI_REPORT"   # zum Ansehen
echo "$REPORT" | grep -q '^Ergebnis: [0-9]* von [0-9]* bestanden (' || fail "Protokoll ohne Ergebnis"
echo "$REPORT" | grep -q 'Leitstelle → U STARTDT act' || fail "Protokoll ohne Rahmen"
echo "$REPORT" | grep -q '↳ Leitstelle fordert alle Werte an' || fail "Protokoll ohne Erklärungen"
if [ -n "$LIB60870" ]; then
    echo "$REPORT" | grep -q 'lib60870 als fremde Leitstelle – OK' || fail "Schritt lib60870 fehlt oder Fehler"
else
    echo "$REPORT" | grep -q 'lib60870 als fremde Leitstelle – ÜBERSPRUNGEN' ||
        echo "$REPORT" | grep -q '^23  ÜBERSPRUNGEN' || fail "Schritt lib60870 nicht übersprungen"
fi
ok "Selbsttest: $(echo "$REPORT" | grep '^Ergebnis')"

echo "test_gui: bestanden"
