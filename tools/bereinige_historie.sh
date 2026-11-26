#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# bereinige_historie.sh – entfernt Spuren der Arbeitsumgebung aus der ganzen Historie
# eines Klons (nur im privaten Repo, kommt nicht ins Zip).
#
# Aufruf (MSYS2/Git Bash oder Linux), im Klon des Ziel-Repos:
#   bash bereinige_historie.sh [--pruefen] KLON
#     --pruefen   nur zeigen, in welchen Commits welche Texte stehen; nichts ändern
#
# Ablauf ohne --pruefen:
#   1. Prüft: sauberer Arbeitsbaum, Zweig main, gleich mit origin/main.
#   2. Sicherung: vollständiger Mirror-Klon neben KLON (KLON-sicherung-DATUM.git).
#   3. Ersetzt die Texte unten in allen Dateien aller Commits und in den Commit-Nachrichten
#      (git filter-branch, alle Zweige und Tags).
#   4. Prüft, dass keiner der Texte mehr vorkommt, räumt auf (refs/original, reflog, gc).
#   5. Zeigt den Befehl zum Überschreiben des Servers – ausführen nur bewusst von Hand.
# Rückweg vor dem Push: KLON löschen und aus der Sicherung neu klonen.

set -u
PRUEFEN=0
[ "${1:-}" = "--pruefen" ] && { PRUEFEN=1; shift; }
KLON="${1:?Klon des Ziel-Repos angeben}"
cd "$KLON" || exit 1
git rev-parse --git-dir > /dev/null 2>&1 || { echo "$KLON ist kein Git-Klon"; exit 1; }

# Gesuchte Texte (feste Zeichenketten) und Ersatz, getrennt durch @@.
PAARE=(
    '/mnt/c/rd/@@/mnt/c/<pfad>/'
    '~/src/iee104@@~/src/iec104'
    '$HOME/src/iee104@@$HOME/src/iec104'
    ', und Lieferpatches passen nicht.@@.'
    'ScopeOfTask001@@ScopeOfVersion1'
    '(in der Lieferung als gleichnamiges Zip)@@(auch als gleichnamiges Zip)'
    '| Lieferung |@@| Paket |'
    'Erste Lieferung:@@Erste Version:'
    'im Quellpaket `iec104-@@in den Quellen der Library `iec104-'
    'im Quellpaket.@@in den Quellen der Library.'
)

# sed-Ausdruck für ein Paar (feste Zeichenketten, Sonderzeichen maskiert).
# Suchtext für sed (BRE): nur . * [ ] ^ $ \ / maskieren – nicht |, denn \| ist in GNU sed „oder“.
quote_von() { printf '%s' "$1" | sed -e 's/[]\/$*.^[]/\\&/g'; }
# Ersatztext: nur \ / & maskieren.
quote_nach() { printf '%s' "$1" | sed -e 's/[\/&]/\\&/g'; }
SED=()
MUSTER=()
for p in "${PAARE[@]}"; do
    von="${p%%@@*}"
    nach="${p#*@@}"
    SED+=(-e "s/$(quote_von "$von")/$(quote_nach "$nach")/g")
    MUSTER+=(-e "$von")
done

fundstellen() {
    local n=0 rev
    for rev in $(git rev-list --branches --tags); do
        if git grep -I -F -l "${MUSTER[@]}" "$rev" -- . > /dev/null 2>&1; then
            n=$((n + 1))
            [ "$PRUEFEN" = 1 ] && git grep -I -F -n "${MUSTER[@]}" "$rev" -- . | head -5
        fi
    done
    local m
    m="$(git log --branches --tags --format=%B | grep -c -F "${MUSTER[@]}")"
    echo "Commits mit Fundstellen im Inhalt: $n; Zeilen in Commit-Nachrichten: $m"
    [ "$n" = 0 ] && [ "$m" = 0 ]
}

if [ "$PRUEFEN" = 1 ]; then
    fundstellen
    exit 0
fi

# 1. Voraussetzungen
[ -z "$(git status --porcelain)" ] || { echo "Arbeitsbaum nicht sauber"; exit 1; }
[ "$(git rev-parse --abbrev-ref HEAD)" = main ] || { echo "nicht auf main"; exit 1; }
git fetch -q origin || { echo "git fetch fehlgeschlagen"; exit 1; }
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] ||
    { echo "main ist nicht gleich origin/main – erst angleichen"; exit 1; }

# 2. Sicherung
SICHERUNG="$(cd .. && pwd)/$(basename "$(pwd)")-sicherung-$(date +%Y%m%d-%H%M%S).git"
git clone -q --mirror . "$SICHERUNG" || { echo "Sicherung fehlgeschlagen"; exit 1; }
echo "Sicherung: $SICHERUNG"

# 3. Ersetzen in Inhalt und Nachrichten
export FILTER_BRANCH_SQUELCH_WARNING=1
SED_ARGS="$(printf '%q ' "${SED[@]}")"
MUSTER_ARGS="$(printf '%q ' "${MUSTER[@]}")"
git filter-branch -f \
    --tree-filter "git grep -I -F -l $MUSTER_ARGS -- . 2>/dev/null | while read -r f; do sed -i $SED_ARGS \"\$f\"; done; true" \
    --msg-filter "sed $SED_ARGS" \
    --tag-name-filter cat -- --branches --tags ||
    { echo "filter-branch fehlgeschlagen – Sicherung: $SICHERUNG"; exit 1; }
# origin/main bleibt der Stand des Servers: force-with-lease prüft gegen ihn.

# 4. Aufräumen und Kontrolle
git for-each-ref --format='%(refname)' refs/original/ | xargs -r -n 1 git update-ref -d
if ! fundstellen; then
    echo "Es sind noch Fundstellen da – nicht pushen. Sicherung: $SICHERUNG"
    exit 1
fi
git reflog expire --expire=now --all
git gc -q --prune=now
git status --short | head -5

# 5. Nur anzeigen
echo
echo "Bereinigt. Zum Überschreiben des Servers (alle Klone müssen danach neu klonen):"
echo "  git -C \"$(pwd)\" push --force-with-lease origin main"
echo "  git -C \"$(pwd)\" push --force --tags origin"
echo "Rückweg vor dem Push: Ordner löschen, aus $SICHERUNG neu klonen."
