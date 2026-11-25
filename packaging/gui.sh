#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# gui.sh – startet die Test-GUI (Leitstelle und Station im Browser).
#
# Aufruf: bash gui.sh [port] [adresse]
#   port     Default 8080
#   adresse  Default 127.0.0.1 (nur dieser Rechner). Unter WSL kommt der
#            Windows-Browser manchmal nicht an 127.0.0.1 der WSL heran; dann
#            "bash gui.sh 8080 0.0.0.0" und die angezeigte Adresse öffnen.
#            Achtung: die GUI hat keine Anmeldung und kann Befehle senden.
# Beenden mit Strg-C. Der erste Start nach einer Julia-Installation dauert
# länger (Julia übersetzt einmalig).

cd "$(dirname "$0")" || exit 1
PORT="${1:-8080}"
ADDR="${2:-127.0.0.1}"
command -v julia > /dev/null 2>&1 || { echo "Julia fehlt (bash pruefen.sh)"; exit 1; }
if [ "$ADDR" = 127.0.0.1 ]; then
    echo "Test-GUI startet. Im Browser öffnen: http://localhost:$PORT"
else
    IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
    echo "Test-GUI startet auf $ADDR. Im Browser öffnen: http://${IP:-<adresse dieses Rechners>}:$PORT"
fi
echo "Bereit ist sie, sobald die Zeile \"IEC 104 Test-GUI ...\" erscheint (Strg-C beendet)."
exec julia --startup-file=no julia/gui/iec104_gui.jl "$PORT" "$ADDR"
