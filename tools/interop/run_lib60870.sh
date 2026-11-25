#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# run_lib60870.sh – Interop-Tests gegen lib60870-C als fremde Gegenstelle.
#
# Aufruf: tools/interop/run_lib60870.sh [--mit-stationen] LIB60870_C BUILD [AUSGABE]
#
#   LIB60870_C  Ordner lib60870-C einer gebauten lib60870 (v2.4.1 geprüft):
#               build/examples      ohne TLS (Pflicht)
#               build-tls/examples  mit mbedTLS 3.6 (optional, sonst ohne TLS-Fälle)
#   BUILD       eigener Build-Ordner (Selbsttest-Programme, examples/hello_server)
#   AUSGABE     Logs und Mitschnitte, Default BUILD/interop
#   --mit-stationen  auch die Fälle mit einer lib60870-Station (cs104_server,
#               cs104_redundancy_server, tls_server). Diese Beispiele lauschen
#               fest auf 0.0.0.0, also auf allen Netzwerkschnittstellen; ohne
#               Änderung an lib60870 geht das nicht anders. Ohne die Option
#               laufen nur die Fälle, in denen lib60870 die Leitstelle ist und
#               sich mit den eigenen Servern auf 127.0.0.1 verbindet; die
#               übrigen stehen in der Tabelle als "übersprungen".
#
# lib60870 ist nur Gegenstelle: Es laufen die unveränderten Beispielprogramme
# über TCP, nichts wird gelinkt. Feste Ports der Beispiele: 19998 (TLS-Fälle),
# mit --mit-stationen zusätzlich 2404; die müssen frei sein. Die TLS-Beispiele
# lesen ihre Zertifikate unter festen Namen aus dem Arbeitsordner; das Skript
# legt dort die RSA-Test-PKI ab (tools/make_test_certs.sh ... rsa).
#
# Ergebnis je Fall: bestanden | Abweichung Gegenstelle (bekannter Fehler des
# Beispielprogramms, eigenes Verhalten korrekt) | übersprungen | FEHLER.
# Rückgabe 1 bei FEHLER.

set -u

STATIONS=0
ARGS=()
for a in "$@"; do
    case "$a" in
        --mit-stationen) STATIONS=1 ;;
        -*) echo "Unbekannte Option: $a" >&2; exit 2 ;;
        *) ARGS+=("$a") ;;
    esac
done
set -- "${ARGS[@]}"
LIB="${1:?Ordner lib60870-C fehlt}"
BUILD="$(cd "${2:?Build-Ordner fehlt}" && pwd)"
OUT="${3:-$BUILD/interop}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PLAIN="$LIB/build/examples"
TLS="$LIB/build-tls/examples"
SERVER="$BUILD/iec104_selftest_server"
CLIENT="$BUILD/iec104_selftest_client"
HELLO="$BUILD/examples/hello_server"

[ -x "$PLAIN/cs104_client/simple_client" ] || { echo "lib60870 nicht gebaut: $PLAIN" >&2; exit 2; }
[ -x "$CLIENT" ] || { echo "Selbsttest nicht gebaut: $BUILD" >&2; exit 2; }
mkdir -p "$OUT"

RESULTS=()
FAILED=0
result() { # Fall, Ergebnis, Notiz
    RESULTS+=("$(printf '%-28s %-24s %s' "$1" "$2" "$3")")
    [ "$2" = "FEHLER" ] && FAILED=1
}
has() { grep -q -E -- "$2" "$1"; }
count() { grep -c -E -- "$2" "$1"; }

# Nur lesend prüfen, ob die festen Ports frei sind.
port_free() { ! ss -ltn 2>/dev/null | grep -q -E "[:.]$1[[:space:]]"; }
PORTS=()
[ "$STATIONS" = 1 ] && PORTS+=(2404)
[ -x "$TLS/tls_client/tls_client" ] && PORTS+=(19998)
for p in "${PORTS[@]}"; do
    port_free "$p" || { echo "Port $p ist belegt (läuft noch eine Gegenstelle?)" >&2; exit 2; }
done
SKIP_NOTE="lib60870-Station lauscht auf 0.0.0.0 (--mit-stationen)"

PID=""
start_peer() { # Ordner, Programm, Log, Argumente...
    local dir="$1" prog="$2" log="$3"
    shift 3
    (cd "$dir" && exec "$prog" "$@" < /dev/null > "$log" 2>&1) &
    PID=$!
    sleep 1
    kill -0 "$PID" 2>/dev/null || echo "Gegenstelle $prog läuft nicht, siehe $log" >&2
}
stop_peer() {
    [ -n "$PID" ] && kill "$PID" 2>/dev/null && wait "$PID" 2>/dev/null
    PID=""
}
trap stop_peer EXIT

# --- 1. Eigener Client gegen lib60870-Server ---------------------------------
if [ "$STATIONS" = 1 ]; then
    start_peer "$OUT" "$PLAIN/cs104_server/cs104_server" "$OUT/1_peer.log"
    "$CLIENT" --port 2404 --foreign --idle 3 --t3 1 --timeout 5 --log trace > "$OUT/1_client.log" 2>&1
    stop_peer
    if has "$OUT/1_client.log" '^result: PASSED'; then
        result "Client -> Server" "bestanden" "GA, Uhrzeit, Zähler (Ursache 44), TESTFR, STOPDT"
    else
        result "Client -> Server" "FEHLER" "$OUT/1_client.log"
    fi
else
    result "Client -> Server" "übersprungen" "$SKIP_NOTE"
fi

# --- 2. lib60870-Client gegen eigenen Server -----------------------------------
# hello_server auf 127.0.0.1 mit freiem Port (0); der Port steht in seiner Ausgabe.
"$HELLO" 0 127.0.0.1 < /dev/null > "$OUT/2_server.log" 2>&1 &
PID=$!
HPORT=""
for _ in $(seq 1 100); do
    HPORT="$(sed -n 's/^listening on port \([0-9]*\)$/\1/p' "$OUT/2_server.log")"
    [ -n "$HPORT" ] && break
    sleep 0.05
done
timeout 30 "$PLAIN/cs104_client/simple_client" 127.0.0.1 "${HPORT:-0}" > "$OUT/2_peer.log" 2>&1
stop_peer
if has "$OUT/2_peer.log" 'Received STARTDT_CON' && has "$OUT/2_peer.log" 'M_EI_NA_1\(70\)' &&
   [ "$(count "$OUT/2_peer.log" 'C_IC_NA_1\(100\)')" -ge 2 ] && has "$OUT/2_peer.log" 'C_TS_TA_1\(107\)'; then
    result "Server <- Client" "bestanden" "STARTDT, Typ 70, GA mit ACTCON/ACTTERM, Testbefehl 107"
else
    result "Server <- Client" "FEHLER" "$OUT/2_peer.log"
fi

# --- 3. Umschaltung gegen lib60870-Redundanzserver ----------------------------
# Server puffert den zyklischen Wert IOA 110 je Gruppe; nach STOPDT/STARTDT
# muss die Reihe lückenlos weitergehen.
if [ "$STATIONS" = 1 ]; then
    start_peer "$OUT" "$PLAIN/cs104_redundancy_server/cs104_redundancy_server" "$OUT/3_peer.log"
    "$CLIENT" --port 2404 --foreign --switchover --series-ioa 110 --expect 5 --timeout 8 \
        > "$OUT/3_client.log" 2>&1
    stop_peer
    if has "$OUT/3_client.log" 'switch-over.*values before' && ! has "$OUT/3_client.log" 'switch-over.*FAILED' &&
       has "$OUT/3_client.log" 'series after switch-over.*in order' && ! has "$OUT/3_client.log" 'series after.*FAILED'; then
        if has "$OUT/3_client.log" 'no termination'; then
            result "Umschaltung (Redundanz)" "Abweichung Gegenstelle" "Reihe lückenlos; GA ohne gültiges ACTTERM (Typ 0)"
        else
            result "Umschaltung (Redundanz)" "bestanden" "Reihe lückenlos"
        fi
    else
        result "Umschaltung (Redundanz)" "FEHLER" "$OUT/3_client.log"
    fi
else
    result "Umschaltung (Redundanz)" "übersprungen" "$SKIP_NOTE"
fi

# --- TLS ----------------------------------------------------------------------
if [ ! -x "$TLS/tls_server/tls_server" ]; then
    result "TLS" "übersprungen" "lib60870 ohne TLS gebaut ($TLS fehlt)"
else
    PKI="$OUT/pki_rsa"
    "$ROOT/tools/make_test_certs.sh" "$PKI" rsa > /dev/null
    SRVDIR="$OUT/tls_server_dir"
    CLIDIR="$OUT/tls_client_dir"
    mkdir -p "$SRVDIR" "$CLIDIR"
    cp "$PKI/server.key" "$SRVDIR/server_CA1_1.key"
    cp "$PKI/server.pem" "$SRVDIR/server_CA1_1.pem"
    cp "$PKI/ca.pem" "$SRVDIR/root_CA1.pem"
    cp "$PKI/client.pem" "$SRVDIR/client_CA1_1.pem"
    cp "$PKI/client.key" "$CLIDIR/client_CA1_1.key"
    cp "$PKI/client.pem" "$CLIDIR/client_CA1_1.pem"
    cp "$PKI/ca.pem" "$CLIDIR/root_CA1.pem"
    cp "$PKI/server.pem" "$CLIDIR/server_CA1_1.pem"
    OWN=(--tls-ca "$PKI/ca.pem" --tls-cert "$PKI/client.pem" --tls-key "$PKI/client.key"
         --tls-peer-name localhost)

    # 4./5. Eigener TLS-Client gegen lib60870-TLS-Server (er verhandelt alle 2 s neu).
    for V in 1.3 1.2; do
        if [ "$STATIONS" != 1 ]; then
            result "TLS $V Client -> Server" "übersprungen" "$SKIP_NOTE"
            continue
        fi
        N="tls${V/./}"
        start_peer "$SRVDIR" "$TLS/tls_server/tls_server" "$OUT/${N}_peer.log"
        "$CLIENT" --port 19998 --foreign "${OWN[@]}" --tls-version "$V" --idle 7 --t3 2 \
            --timeout 4 --log trace > "$OUT/${N}_client.log" 2>&1
        stop_peer
        L="$OUT/${N}_client.log"
        if has "$L" "TLS \.+ TLSv$V" && has "$L" 'STARTDT\) \.+ ok' &&
           has "$L" 'clock synchronization \.+ confirmed' && has "$L" 'idle period.*ok' &&
           { [ "$V" = 1.3 ] || has "$L" 'renegotiation complete'; }; then
            NOTE="Handshake, Uhrzeit, TESTFR"
            [ "$V" = 1.2 ] && NOTE="$NOTE, $(count "$L" 'renegotiation complete')x Renegotiation der Gegenstelle"
            if has "$L" 'no termination'; then
                result "TLS $V Client -> Server" "Abweichung Gegenstelle" "$NOTE; GA ohne gültiges ACTTERM"
            else
                result "TLS $V Client -> Server" "bestanden" "$NOTE"
            fi
        else
            result "TLS $V Client -> Server" "FEHLER" "$L"
        fi
    done

    # 6. lib60870-TLS-Client gegen eigenen TLS-Server (verbindet sich zweimal).
    "$SERVER" --host 127.0.0.1 --port 19998 --tls-ca "$PKI/ca.pem" --tls-cert "$PKI/server.pem" \
        --tls-key "$PKI/server.key" --log info --duration 16 < /dev/null > "$OUT/6_server.log" 2>&1 &
    PID=$!
    sleep 0.5
    (cd "$CLIDIR" && timeout 15 "$TLS/tls_client/tls_client" 127.0.0.1 > "$OUT/6_peer.log" 2>&1)
    wait "$PID" 2>/dev/null
    PID=""
    if [ "$(count "$OUT/6_server.log" '\[info\].*TLS established')" -eq 2 ] &&
       [ "$(count "$OUT/6_server.log" 'command type 103 ioa 0: accepted')" -eq 2 ] &&
       [ "$(count "$OUT/6_peer.log" 'C_IC_NA_1\(100\)')" -ge 4 ]; then
        result "TLS Server <- Client" "bestanden" "2 Verbindungen: GA, Befehl 45 (negativ: IOA 5000 ist Sollwert), Uhrzeit"
    else
        result "TLS Server <- Client" "FEHLER" "$OUT/6_server.log"
    fi

    # 7. Befund: lib60870 lehnt EC-Schlüssel ab (misst sie an einer RSA-Mindestlänge).
    ECDIR="$OUT/tls_client_ec"
    "$ROOT/tools/make_test_certs.sh" "$OUT/pki_ec" > /dev/null
    mkdir -p "$ECDIR"
    cp "$OUT/pki_ec/client.key" "$ECDIR/client_CA1_1.key"
    cp "$OUT/pki_ec/client.pem" "$ECDIR/client_CA1_1.pem"
    cp "$OUT/pki_ec/ca.pem" "$ECDIR/root_CA1.pem"
    cp "$OUT/pki_ec/server.pem" "$ECDIR/server_CA1_1.pem"
    P="$OUT/pki_ec"
    "$SERVER" --host 127.0.0.1 --port 19998 --tls-ca "$P/ca.pem" --tls-cert "$P/server.pem" \
        --tls-key "$P/server.key" --log info --duration 8 < /dev/null > "$OUT/7_server.log" 2>&1 &
    PID=$!
    sleep 0.5
    (cd "$ECDIR" && timeout 7 "$TLS/tls_client/tls_client" 127.0.0.1 > "$OUT/7_peer.log" 2>&1)
    wait "$PID" 2>/dev/null
    PID=""
    if has "$OUT/7_peer.log" 'insufficient key length'; then
        result "TLS mit EC-Schlüsseln" "Abweichung Gegenstelle" "lib60870: insufficient key length -> RSA-Zertifikate verwenden"
    else
        result "TLS mit EC-Schlüsseln" "bestanden" "EC-Schlüssel akzeptiert"
    fi

    # 8. Abgelaufenes Client-Zertifikat der lib60870-Beispiele wird abgewiesen.
    # Der eigene Server nutzt dafür Zertifikat und Schlüssel des lib60870-
    # Beispielservers, denn der lib60870-Client akzeptiert nur dieses.
    E="$TLS/tls_server"
    "$SERVER" --host 127.0.0.1 --port 19998 --tls-ca "$E/root_CA1.pem" \
        --tls-cert "$E/server_CA1_1.pem" --tls-key "$E/server_CA1_1.key" --log info --duration 8 \
        < /dev/null > "$OUT/8_server.log" 2>&1 &
    PID=$!
    sleep 0.5
    (cd "$TLS/tls_client" && timeout 7 ./tls_client 127.0.0.1 > "$OUT/8_peer.log" 2>&1)
    wait "$PID" 2>/dev/null
    PID=""
    if has "$OUT/8_server.log" 'certificate has expired'; then
        result "Abgelaufenes Zertifikat" "bestanden" "abgewiesen: certificate has expired"
    else
        result "Abgelaufenes Zertifikat" "FEHLER" "$OUT/8_server.log"
    fi
fi

echo
printf '%-28s %-24s %s\n' "Fall" "Ergebnis" "Notiz"
printf '%s\n' "${RESULTS[@]}"
echo
echo "Logs: $OUT"
exit "$FAILED"
