#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# wsl_ausprobieren.sh – richtet die Library in einem Linux-Ordner zum Ausprobieren ein
# (gedacht für WSL, läuft auf jedem Linux).
#
# Aufruf, z. B. aus dem Windows-Checkout heraus:
#   bash /mnt/c/<pfad>/<repo>/tools/wsl_ausprobieren.sh [ZIEL] [--ohne-bauen]
#
#   ZIEL          Arbeitsordner, Default ~/src/iee104
#   --ohne-bauen  nur die fertig gebaute Library prüfen, nicht aus den Quellen bauen
#
# Schritte:
#   1. Repo nach ZIEL holen (git clone) bzw. aktualisieren (git pull);
#      dort liegen die Dateien auf dem Linux-Dateisystem, mit LF und schnell
#   2. Voraussetzungen prüfen; fehlende Pakete als apt-Befehl ausgeben
#   3. fertig gebaute Library aus dist/: pruefen.sh und beispiel.sh
#   4. aus den Quellen bauen und alle Tests (ctest) laufen lassen
#   5. ausgeben, wie Test-GUI und Selbsttest starten
#
# Das Skript ändert nichts am System; Pakete installiert es nicht selbst.

set -u

SRC="$(cd "$(dirname "$0")/.." && pwd)"
ZIEL="$HOME/src/iee104"
BAUEN=1
for arg in "$@"; do
    case "$arg" in
        --ohne-bauen) BAUEN=0 ;;
        -h|--help) sed -n '6,22p' "$0"; exit 0 ;;
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
check "GoogleTest" "$( ([ -f /usr/include/gtest/gtest.h ] || [ -f /usr/local/include/gtest/gtest.h ]) && echo 1 || echo 0)" libgtest-dev
check "unzip" "$(have unzip && echo 1 || echo 0)" unzip
PAKETE=()
for p in "${APT[@]}"; do [ -n "$p" ] && PAKETE+=("$p"); done
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
    if cmake --preset debug > build-debug.log 2>&1 && cmake --build --preset debug >> build-debug.log 2>&1; then
        echo "  gebaut: build/debug"
        if ctest --preset debug --output-on-failure > ctest.log 2>&1; then
            echo "  Tests: $(grep -E 'tests passed' ctest.log)"
        else
            FEHLER=1
            grep -E 'tests passed|\(Failed\)|\*\*\*' ctest.log | head -20
            echo "  Einzelheiten: $ZIEL/ctest.log"
        fi
    else
        FEHLER=1
        tail -20 build-debug.log
        echo "  Bau fehlgeschlagen, Einzelheiten: $ZIEL/build-debug.log"
    fi
fi

# --- 5. Wie weiter ------------------------------------------------------------------------
step "5. Ausprobieren"
[ -n "$PAKET" ] && echo "  Test-GUI:     bash $ZIEL/$PAKET/gui.sh     dann http://localhost:8080 im Browser"
if [ -x build/debug/iec104_selftest_server ]; then
    echo "  Selbsttest:   cd $ZIEL && tools/selftest/run_selftest.sh build/debug/iec104_selftest_server build/debug/iec104_selftest_client"
    echo "  Liste:        $ZIEL/build/debug/iec104_interop_list --role station | less"
fi
echo "  Anleitungen:  $ZIEL/docs/ (06 Anleitung, 07 Julia, 04 Selbsttest)"
echo
[ "$FEHLER" = 0 ] && echo "Ergebnis: alles ok" || echo "Ergebnis: es gab Fehler (siehe oben)"
exit "$FEHLER"
