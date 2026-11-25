#!/usr/bin/env bash
# check_neutral.sh – prüft das Repo auf verbotene Begriffe (Kunden, Firmen, Projekte).
#
# Aufruf:
#   tools/check_neutral.sh                    Repo prüfen
#   tools/check_neutral.sh --message DATEI    zusätzlich eine Commit-Nachricht prüfen
#
# Begriffsdatei: ${IEC104_FORBIDDEN_TERMS:-$HOME/.config/iec104/forbidden_terms.txt}
#   ein Begriff pro Zeile, Leerzeilen und Zeilen mit # werden übersprungen.
#   "word:BEGRIFF" trifft nur das ganze Wort (für kurze Namen, die sonst in
#   normalen Wörtern vorkommen). Alle anderen Zeilen treffen als Teilzeichenkette.
#   Die Datei liegt außerhalb des Repos und wird nicht committet.
#
# Geprüft werden Inhalt und Name aller Dateien außer docs/, folien/, build/ und .git/.
# Vergleich ohne Beachtung der Groß-/Kleinschreibung.
#
# Rückgabe: 0 = keine Treffer, 1 = Treffer, 2 = Begriffsdatei fehlt oder ist leer.

set -u

TERMS_FILE="${IEC104_FORBIDDEN_TERMS:-$HOME/.config/iec104/forbidden_terms.txt}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MESSAGE_FILE=""

while [ $# -gt 0 ]; do
    case "$1" in
        --message)
            [ $# -ge 2 ] || { echo "--message braucht eine Datei" >&2; exit 2; }
            MESSAGE_FILE="$2"; shift 2 ;;
        -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
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
NAMES="$TMPD/names"     # Datei- und Ordnernamen

# Kommentare und Leerzeilen entfernen, Leerzeichen an den Rändern abschneiden
grep -v -e '^[[:space:]]*#' -e '^[[:space:]]*$' "$TERMS_FILE" \
    | sed -e 's/[[:space:]]*$//' -e 's/^[[:space:]]*//' > "$TMPD/all"
grep -i '^word:' "$TMPD/all" | sed 's/^[Ww][Oo][Rr][Dd]://' > "$WORDS"
grep -v -i '^word:' "$TMPD/all" > "$SUBSTR"
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

cd "$ROOT" || exit 2
FOUND=0

FILES=()
while IFS= read -r -d '' f; do
    FILES+=("$f")
done < <(find . \( -path ./docs -o -path ./folien -o -path ./build -o -path ./.git \) -prune \
             -o -type f -print0)

# Dateiinhalte (Binärdateien werden übersprungen; -H nennt immer den Dateinamen)
if [ "${#FILES[@]}" -gt 0 ] && grep_terms -n -H -I -- "${FILES[@]}"; then
    FOUND=1
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
