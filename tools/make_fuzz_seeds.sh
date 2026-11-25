#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# make_fuzz_seeds.sh – erzeugt Startdateien für den Fuzzer aus den Referenzdaten.
#
# Aufruf: tools/make_fuzz_seeds.sh [ZIELORDNER]     (Default: fuzz/corpus)
#
# Zusätzlich erhält fuzz/corpus_session/ die Referenz-ASDUs im IEC-104-Profil
# ohne Profil-Oktett (Eingabeformat von fuzz/fuzz_session.cpp).
#
# Quellen (tests/data/):
#   lib60870_capture.txt         jede APDU als eigene Datei (APCI-Pfad des Fuzzers)
#   lib60870_asdu_reference.txt  jede ASDU mit vorangestelltem Profil-Oktett
#                                (1 = IEC 104 tolerant, 2 = COT 1/CA 1/IOA 2),
#                                passend zu select_profile() in fuzz/fuzz_codec.cpp
#
# Die Dateien heißen seed_*; vorhandene Korpusdateien bleiben unberührt.
# Der Korpus ist nicht versioniert (.gitignore), die Seeds sind jederzeit
# reproduzierbar.

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:-$ROOT/fuzz/corpus}"
DATA="$ROOT/tests/data"
mkdir -p "$DEST"

# Schreibt Hex-Oktette (durch Leerzeichen getrennt) binär in eine Datei.
write_hex() { # $1 = Datei, $2.. = Oktette
    local file="$1"; shift
    local escaped=""
    for octet in "$@"; do escaped="$escaped\\x$octet"; done
    printf "%b" "$escaped" > "$file"
}

count=0
n=0
while IFS= read -r line; do
    case "$line" in
        SEND:*|RCVD:*)
            n=$((n + 1))
            # shellcheck disable=SC2086 # Wortaufteilung der Oktette ist gewollt
            write_hex "$DEST/seed_capture_$n" ${line#*:}
            count=$((count + 1)) ;;
    esac
done < "$DATA/lib60870_capture.txt"

while IFS= read -r line; do
    case "$line" in
        "ASDU "*)
            set -- ${line%%:*}            # ASDU <name> <profil> SQ<n>
            name="$2" profile="$3" sq="$4"
            selector=01
            [ "$profile" = "cot1_ca1_ioa2" ] && selector=02
            # shellcheck disable=SC2086
            write_hex "$DEST/seed_asdu_${name}_${profile}_${sq}" $selector ${line#*:}
            count=$((count + 1))
            if [ "$profile" = "iec104" ]; then
                mkdir -p "$ROOT/fuzz/corpus_session"
                # shellcheck disable=SC2086
                write_hex "$ROOT/fuzz/corpus_session/seed_${name}_${sq}" ${line#*:}
                count=$((count + 1))
            fi ;;
    esac
done < "$DATA/lib60870_asdu_reference.txt"

echo "make_fuzz_seeds: $count Dateien in $DEST"
