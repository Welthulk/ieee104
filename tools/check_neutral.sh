#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# check_neutral.sh – prüft das Repo auf verbotene Begriffe (Kunden, Firmen, Projekte).
#
# Aufruf:
#   tools/check_neutral.sh                    Repo prüfen
#   tools/check_neutral.sh --message DATEI    zusätzlich eine Commit-Nachricht prüfen
#   tools/check_neutral.sh --root ORDNER      einen anderen Ordner prüfen (z. B. ein entpacktes Zip)
#
# Begriffsdatei: ${IEC104_FORBIDDEN_TERMS:-$HOME/.config/iec104/forbidden_terms.txt}
#   ein Begriff pro Zeile, Leerzeilen und Zeilen mit # werden übersprungen.
#   "word:BEGRIFF" trifft nur das ganze Wort (für kurze Namen, die sonst in
#   normalen Wörtern vorkommen). Alle anderen Zeilen treffen als Teilzeichenkette.
#   "allow:ZEILE" erlaubt genau diese Zeile des Lizenzkopfs (Inhalt ohne
#   Kommentarzeichen und Leerzeichen am Rand), nur in den ersten 10 Zeilen
#   einer Datei. Sonst bleibt jeder Treffer ein Fehler (TASK-000, Abschnitt 4).
#   Die Datei liegt außerhalb des Repos und wird nicht committet.
#
# Geprüft werden Inhalt und Name aller Dateien außer intern/ (interne Unterlagen),
# build/, dist/ und .git/. docs/ wird geprüft, weil es weitergegeben wird.
# Von LICENSES/ (Lizenztexte) werden nur die Dateinamen geprüft.
# Vergleich ohne Beachtung der Groß-/Kleinschreibung.
#
# Rückgabe: 0 = keine Treffer, 1 = Treffer, 2 = Begriffsdatei fehlt oder ist leer.

set -u

TERMS_FILE="${IEC104_FORBIDDEN_TERMS:-$HOME/.config/iec104/forbidden_terms.txt}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MESSAGE_FILE=""

while [ $# -gt 0 ]; do
    case "$1" in
        --root)
            [ $# -ge 2 ] || { echo "--root braucht einen Ordner" >&2; exit 2; }
            ROOT="$(cd "$2" && pwd)" || exit 2; shift 2 ;;
        --message)
            [ $# -ge 2 ] || { echo "--message braucht eine Datei" >&2; exit 2; }
            MESSAGE_FILE="$2"; shift 2 ;;
        -h|--help) sed -n '6,27p' "$0"; exit 0 ;;
        *) echo "Unbekannte Option: $1" >&2; exit 2 ;;
    esac
done

if [ ! -r "$TERMS_FILE" ]; then
    echo "Begriffsdatei fehlt: $TERMS_FILE" >&2
    echo "Anlegen (ein Begriff pro Zeile) oder Pfad über IEC104_FORBIDDEN_TERMS setzen." >&2
    exit 2
fi
if [ -n "$MESSAGE_FILE" ] && [ ! -r "$MESSAGE_FILE" ]; then
    echo "Commit-Nachricht nicht lesbar: $MESSAGE_FILE" >&2
    exit 2
fi

TMPD="$(mktemp -d)"
trap 'rm -rf "$TMPD"' EXIT
SUBSTR="$TMPD/substr"   # Begriffe als Teilzeichenkette
WORDS="$TMPD/words"     # Begriffe als ganzes Wort
ALLOW="$TMPD/allow"     # erlaubte Zeilen des Lizenzkopfs
NAMES="$TMPD/names"     # Datei- und Ordnernamen

# Kommentare und Leerzeilen entfernen, Leerzeichen an den Rändern abschneiden
grep -v -e '^[[:space:]]*#' -e '^[[:space:]]*$' "$TERMS_FILE" \
    | sed -e 's/[[:space:]]*$//' -e 's/^[[:space:]]*//' > "$TMPD/all"
grep -i '^word:' "$TMPD/all" | sed 's/^[Ww][Oo][Rr][Dd]://' > "$WORDS"
grep -i '^allow:' "$TMPD/all" | sed 's/^[Aa][Ll][Ll][Oo][Ww]://' > "$ALLOW"
grep -v -i -e '^word:' -e '^allow:' "$TMPD/all" > "$SUBSTR"
if [ ! -s "$SUBSTR" ] && [ ! -s "$WORDS" ]; then
    echo "Begriffsdatei enthält keine Begriffe: $TERMS_FILE" >&2
    exit 2
fi

# Gibt Treffer aus. Rückgabe 0 bei mindestens einem Treffer, sonst 1.
grep_terms() {
    local hit=1
    if [ -s "$SUBSTR" ] && grep -i -F -f "$SUBSTR" "$@"; then hit=0; fi
    if [ -s "$WORDS" ] && grep -i -F -w -f "$WORDS" "$@"; then hit=0; fi
    return "$hit"
}

# Entfernt aus Treffern der Form "datei:zeile:inhalt" die erlaubten
# Lizenzkopfzeilen (siehe "allow:" oben). Gibt die übrigen Treffer aus.
drop_allowed_header_lines() {
    awk -v allowfile="$ALLOW" '
        BEGIN { while ((getline a < allowfile) > 0) allowed[a] = 1 }
        {
            rest = $0
            i = index(rest, ":"); rest = substr(rest, i + 1)
            j = index(rest, ":"); lineno = substr(rest, 1, j - 1) + 0
            text = substr(rest, j + 1)
            sub(/^[ \t]*(\/\*+|\*|\/\/|#)?[ \t]*/, "", text)
            sub(/[ \t]*$/, "", text)
            if (lineno <= 10 && (text in allowed)) next
            print
        }'
}

cd "$ROOT" || exit 2
FOUND=0

FILES=()
while IFS= read -r -d '' f; do
    FILES+=("$f")
done < <(find . \( -path ./intern -o -path ./build -o -path ./dist -o -path ./.git \) -prune \
             -o -type f -print0)

# Dateiinhalte (Binärdateien werden übersprungen; -H nennt immer den Dateinamen).
# Lizenztexte unter LICENSES/ nennen den Rechteinhaber und sind ausgenommen.
CONTENT_FILES=()
for f in "${FILES[@]}"; do
    case "$f" in ./LICENSES/*) ;; *) CONTENT_FILES+=("$f") ;; esac
done
if [ "${#CONTENT_FILES[@]}" -gt 0 ]; then
    OUT="$(grep_terms -n -H -I -- "${CONTENT_FILES[@]}" | drop_allowed_header_lines)"
    [ -n "$OUT" ] && { printf '%s\n' "$OUT"; FOUND=1; }
fi

# Datei- und Ordnernamen
printf '%s\n' "${FILES[@]}" > "$NAMES"
OUT="$(grep_terms -- "$NAMES")" && { printf '%s\n' "$OUT" | sed 's/^/Dateiname: /'; FOUND=1; }

# Commit-Nachricht
if [ -n "$MESSAGE_FILE" ]; then
    OUT="$(grep_terms -n -- "$MESSAGE_FILE")" \
        && { printf '%s\n' "$OUT" | sed 's/^/Commit-Nachricht: /'; FOUND=1; }
fi

if [ "$FOUND" -ne 0 ]; then
    echo "check_neutral: verbotene Begriffe gefunden (siehe oben)." >&2
    exit 1
fi
echo "check_neutral: keine Treffer."
exit 0
