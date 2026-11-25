#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# pruefen.sh – prüft, ob die fertig gebaute Library auf diesem Rechner läuft.
#
# Aufruf (im entpackten Paket, auch unter WSL in /mnt/c/...): bash pruefen.sh

cd "$(dirname "$0")" || exit 1
OK=1
ok()   { printf '  %-34s ok   %s\n' "$1" "$2"; }
nein() { printf '  %-34s FEHLT %s\n' "$1" "$2"; OK=0; }

echo "Prüfung: $(pwd)"
[ "$(uname -m)" = x86_64 ] && ok "Linux x86-64" "$(uname -sr)" || nein "Linux x86-64" "$(uname -sm)"

GLIBC="$(ldd --version 2>/dev/null | head -1 | grep -o '[0-9]\+\.[0-9]\+$')"
if [ -n "$GLIBC" ] && [ "$(printf '%s\n2.32\n' "$GLIBC" | sort -V | head -1)" = 2.32 ]; then
    ok "glibc ab 2.32" "$GLIBC"
else
    nein "glibc ab 2.32" "${GLIBC:-unbekannt}"
fi

MISSING="$(ldd lib/libiec104.so.0 2>&1 | grep 'not found')"
if [ -z "$MISSING" ]; then
    ok "Abhängigkeiten der Library" "$(ldd lib/libiec104.so.0 | grep -o 'libssl.so.[0-9]*' | head -1)"
else
    nein "Abhängigkeiten der Library" "$(echo "$MISSING" | awk '{print $1}' | tr '\n' ' ')"
    echo "      OpenSSL 3 nachinstallieren: sudo apt install libssl3  (bzw. dnf install openssl-libs)"
fi

if command -v julia > /dev/null 2>&1; then
    V="$(julia --version 2>/dev/null | grep -o '[0-9]\+\.[0-9]\+\.[0-9]\+')"
    if [ "$(printf '%s\n1.10.0\n' "$V" | sort -V | head -1)" = 1.10.0 ]; then
        ok "Julia ab 1.10 (für julia/)" "$V"
        R="$(julia --startup-file=no -e 'include("julia/IEC104.jl"); print(Main.IEC104.version())' 2>&1 | tail -1)"
        case "$R" in
            [0-9]*) ok "Library aus Julia ladbar" "$R" ;;
            *) nein "Library aus Julia ladbar" "$R" ;;
        esac
    else
        nein "Julia ab 1.10 (für julia/)" "$V"
    fi
else
    printf '  %-34s --   %s\n' "Julia (nur für julia/)" "nicht installiert: curl -fsSL https://install.julialang.org | sh"
fi

[ "$OK" = 1 ] && echo "Ergebnis: alles bereit" || echo "Ergebnis: siehe FEHLT"
[ "$OK" = 1 ]
