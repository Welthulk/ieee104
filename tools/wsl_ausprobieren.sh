#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# wsl_ausprobieren.sh – richtet die Library in einem Linux-Ordner zum Ausprobieren ein
# (gedacht für WSL, läuft auf jedem Linux).
#
# Aufruf, z. B. aus dem Windows-Checkout heraus:
#   bash /mnt/c/<pfad>/<repo>/tools/wsl_ausprobieren.sh [ZIEL] [--ohne-bauen] [--mit-lib60870]
#
#   ZIEL            Arbeitsordner, Default ~/src/iee104
#   --ohne-bauen    nur die fertig gebaute Library prüfen, nicht aus den Quellen bauen
#   --mit-lib60870  lib60870-C v2.4.1 (fremde Gegenstelle für die Test-GUI) neben ZIEL
#                   holen (GitHub, Variable LIB60870_GIT) und ohne TLS bauen
#
# Schritte:
#   1. Repo nach ZIEL holen (git clone) bzw. aktualisieren (git pull);
#      dort liegen die Dateien auf dem Linux-Dateisystem, mit LF und schnell
#   2. Voraussetzungen prüfen; fehlende Pakete als apt-Befehl ausgeben
#   3. fertig gebaute Library aus dist/: pruefen.sh und beispiel.sh
#   4. aus den Quellen bauen und alle Tests (ctest) laufen lassen
#   5. optional lib60870 holen und bauen
#   6. ausgeben, wie Test-GUI und Selbsttest starten
#
# Das Skript ändert nichts am System; Pakete installiert es nicht selbst.

set -u

SRC="$(cd "$(dirname "$0")/.." && pwd)"
ZIEL="$HOME/src/iee104"
BAUEN=1
LIB60870=0
for arg in "$@"; do
    case "$arg" in
        --ohne-bauen) BAUEN=0 ;;
        --mit-lib60870) LIB60870=1 ;;
        -h|--help) sed -n '6,25p' "$0"; exit 0 ;;
        *) ZIEL="$arg" ;;
    esac
done

step() { printf '\n== %s\n' "$1"; }
FEHLER=0

# --- 1. Repo holen -----------------------------------------------------------------
step "1. Repo nach $ZIEL"
if [ "$SRC" = "$(cd "$ZIEL" 2>/dev/null && pwd)" ]; then
    echo "läuft bereits im Arbeitsordner"
elif [ -d "$ZIEL/.git" ]; then
    git -C "$ZIEL" -c safe.directory='*' pull --ff-only || { echo "git pull fehlgeschlagen"; exit 1; }
elif [ -d "$SRC/.git" ]; then
    mkdir -p "$(dirname "$ZIEL")"
    # Ein Checkout unter /mnt/c gehört aus Sicht von Git einem anderen Nutzer.
    git -c safe.directory='*' clone -q "$SRC" "$ZIEL" || { echo "git clone fehlgeschlagen"; exit 1; }
    echo "geholt aus $SRC"
else
    mkdir -p "$ZIEL"
    cp -r "$SRC"/. "$ZIEL"/
    echo "kopiert aus $SRC (kein Git-Repo)"
fi
cd "$ZIEL" || exit 1
echo "Stand: $(sed -n 's/^Stand: \(.*\)/\1/p' README.md | head -1)"

# --- 2. Voraussetzungen -----------------------------------------------------------------
step "2. Voraussetzungen"
APT=()
have() { command -v "$1" > /dev/null 2>&1; }
check() { # Name, vorhanden (0/1), apt-Paket
    if [ "$2" = 1 ]; then printf '  %-30s ok\n' "$1"; else printf '  %-30s fehlt\n' "$1"; APT+=("$3"); fi
}
check "Julia (Test-GUI, Beispiele)" "$(have julia && echo 1 || echo 0)" ""
check "cmake" "$(have cmake && echo 1 || echo 0)" cmake
check "ninja" "$(have ninja && echo 1 || echo 0)" ninja-build
check "C/C++-Compiler" "$( (have g++ && have gcc) && echo 1 || echo 0)" g++
check "OpenSSL-Header" "$([ -f /usr/include/openssl/ssl.h ] && echo 1 || echo 0)" libssl-dev
check "openssl (Testzertifikate)" "$(have openssl && echo 1 || echo 0)" openssl
check "unzip" "$(have unzip && echo 1 || echo 0)" unzip
PAKETE=()
for p in "${APT[@]}"; do [ -n "$p" ] && PAKETE+=("$p"); done
GTEST=0
if [ -f /usr/include/gtest/gtest.h ] || [ -f /usr/local/include/gtest/gtest.h ]; then
    GTEST=1
    printf '  %-30s ok\n' "GoogleTest"
else
    printf '  %-30s fehlt, wird beim Bauen von GitHub geladen\n' "GoogleTest"
fi
if [ "${#PAKETE[@]}" -gt 0 ]; then
    echo "  fehlende Pakete (Ubuntu/Debian): sudo apt install ${PAKETE[*]}"
fi
have julia || echo "  Julia: curl -fsSL https://install.julialang.org | sh"

# --- 3. Fertig gebaute Library ----------------------------------------------------------
step "3. Fertig gebaute Library"
PAKET="$(ls -d dist/iec104-*-linux-x86_64 2>/dev/null | sort -V | tail -1)"
if [ -z "$PAKET" ]; then
    ZIP="$(ls dist/iec104-*-linux-x86_64.zip 2>/dev/null | sort -V | tail -1)"
    if [ -n "$ZIP" ] && have unzip; then
        unzip -q -o "$ZIP" -d dist
        PAKET="${ZIP%.zip}"
    fi
fi
if [ -z "$PAKET" ] || [ ! -f "$PAKET/pruefen.sh" ]; then
    echo "  keine fertig gebaute Library mit Prüfskripten in dist/ gefunden"
else
    echo "  $PAKET"
    bash "$PAKET/pruefen.sh" || FEHLER=1
    if have julia; then
        bash "$PAKET/beispiel.sh" 2470 || FEHLER=1
    fi
fi

# --- 4. Aus den Quellen bauen -----------------------------------------------------------
step "4. Aus den Quellen bauen und testen"
if [ "$BAUEN" = 0 ]; then
    echo "  übersprungen (--ohne-bauen)"
elif [ "${#PAKETE[@]}" -gt 0 ]; then
    echo "  übersprungen: erst die fehlenden Pakete installieren (siehe 2.)"
else
    # Mit Unit-Tests; fehlt GoogleTest, lädt CMake es von GitHub. Geht das
    # nicht (Proxy), ohne Unit-Tests bauen und die Selbsttests laufen lassen.
    UNIT=1
    if ! cmake --preset debug > build-debug.log 2>&1; then
        if [ "$GTEST" = 0 ]; then
            echo "  GoogleTest nicht ladbar (Netz/Proxy?): baue ohne Unit-Tests"
            UNIT=0
            cmake --preset debug -DIEC104_BUILD_TESTS=OFF >> build-debug.log 2>&1
        fi
    fi
    if cmake --build --preset debug >> build-debug.log 2>&1; then
        echo "  gebaut: build/debug"
        if [ "$UNIT" = 1 ]; then
            if ctest --preset debug --output-on-failure > ctest.log 2>&1; then
                echo "  Tests: $(grep -E 'tests passed' ctest.log)"
            else
                FEHLER=1
                grep -E 'tests passed|\(Failed\)|\*\*\*' ctest.log | head -20
                echo "  Einzelheiten: $ZIEL/ctest.log"
            fi
        else
            tools/make_test_certs.sh build/debug/tls > /dev/null 2>&1
            for MODUS in "" "--tls build/debug/tls" "--switchover"; do
                # shellcheck disable=SC2086
                if tools/selftest/run_selftest.sh build/debug/iec104_selftest_server \
                        build/debug/iec104_selftest_client $MODUS > selftest.log 2>&1; then
                    echo "  Selbsttest ${MODUS:-TCP}: bestanden"
                else
                    FEHLER=1
                    echo "  Selbsttest ${MODUS:-TCP}: FEHLER (siehe $ZIEL/selftest.log)"
                fi
            done
        fi
    else
        FEHLER=1
        tail -20 build-debug.log
        echo "  Bau fehlgeschlagen, Einzelheiten: $ZIEL/build-debug.log"
    fi
fi

# --- 5. lib60870 als fremde Gegenstelle (optional) --------------------------------------
L60870="$(dirname "$ZIEL")/lib60870/lib60870-C"
if [ "$LIB60870" = 1 ]; then
    step "5. lib60870 nach $(dirname "$L60870")"
    if [ -x "$L60870/build/examples/cs104_client/simple_client" ]; then
        echo "  schon gebaut"
    else
        # Nur Gegenstelle: Die Beispielprogramme laufen als eigene Prozesse, nichts wird gelinkt.
        [ -d "$(dirname "$L60870")/.git" ] ||
            git clone -q --depth 1 --branch v2.4.1 "${LIB60870_GIT:-https://github.com/mz-automation/lib60870.git}" \
                "$(dirname "$L60870")" || { FEHLER=1; echo "  git clone fehlgeschlagen (Netz/Proxy?)"; }
        if [ -d "$L60870" ] && cmake -S "$L60870" -B "$L60870/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
                > lib60870-build.log 2>&1 && cmake --build "$L60870/build" >> lib60870-build.log 2>&1; then
            echo "  gebaut: $L60870/build/examples"
        else
            FEHLER=1
            echo "  Bau fehlgeschlagen, Einzelheiten: $ZIEL/lib60870-build.log"
        fi
    fi
fi

# --- 6. Wie weiter ------------------------------------------------------------------------
step "6. Ausprobieren"
if [ -n "$PAKET" ]; then
    echo "  Test-GUI:     bash $ZIEL/$PAKET/gui.sh     dann http://localhost:8080 im Browser"
    echo "                (kommt der Windows-Browser nicht durch: bash $ZIEL/$PAKET/gui.sh 8080 0.0.0.0)"
    if [ -x "$L60870/build/examples/cs104_client/simple_client" ]; then
        echo "  mit lib60870: bash $ZIEL/$PAKET/gui.sh 8080 127.0.0.1 $L60870"
        echo "                (Windows-Browser: bash $ZIEL/$PAKET/gui.sh 8080 0.0.0.0 $L60870)"
    fi
fi
if [ -x build/debug/iec104_selftest_server ]; then
    echo "  Selbsttest:   cd $ZIEL && tools/selftest/run_selftest.sh build/debug/iec104_selftest_server build/debug/iec104_selftest_client"
    echo "  Liste:        $ZIEL/build/debug/iec104_interop_list --role station | less"
fi
echo "  Anleitungen:  $ZIEL/docs/ (06 Anleitung, 07 Julia, 04 Selbsttest)"
echo
[ "$FEHLER" = 0 ] && echo "Ergebnis: alles ok" || echo "Ergebnis: es gab Fehler (siehe oben)"
exit "$FEHLER"
