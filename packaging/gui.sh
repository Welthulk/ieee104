#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# gui.sh – startet die Test-GUI (Leitstelle und Station im Browser).
#
# Aufruf: bash gui.sh [port]          Default 8080, dann http://localhost:<port> öffnen
# Beenden mit Strg-C.

cd "$(dirname "$0")" || exit 1
PORT="${1:-8080}"
command -v julia > /dev/null 2>&1 || { echo "Julia fehlt (bash pruefen.sh)"; exit 1; }
echo "Test-GUI startet, im Browser öffnen: http://localhost:$PORT  (Strg-C beendet)"
exec julia --startup-file=no julia/gui/iec104_gui.jl "$PORT"
