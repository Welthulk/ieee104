#!/usr/bin/env bash
# check_neutral.sh – prüft das Repo auf verbotene Begriffe (Kunden, Firmen, Projekte).
#
# Aufruf:
#   tools/check_neutral.sh                    Repo prüfen
#   tools/check_neutral.sh --message DATEI    zusätzlich eine Commit-Nachricht prüfen
#
# Begriffsdatei: ${IEC104_FORBIDDEN_TERMS:-$HOME/.config/iec104/forbidden_terms.txt}
#   ein Begriff pro Zeile, Leerzeilen und Zeilen mit # werden übersprungen.
#   Die Datei liegt außerhalb des Repos und wird nicht committet.
#
# Geprüft werden Inhalt und Name aller Dateien außer docs/, folien/, build/ und .git/.
# Vergleich ohne Beachtung der Groß-/Kleinschreibung, als Teilzeichenkette.
#
# Rückgabe: 0 = keine Treffer, 1 = Treffer, 2 = Begriffsdatei fehlt oder ist leer.

set -u

TERMS_FILE="${IEC104_FORBIDDEN_TERMS:-$HOME/.config/iec104/forbidden_terms.txt}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MESSAGE_FILE=""

while [ $# -gt 0 ]; do
    case "$1" in
        --message) MESSAGE_FILE="${2:-}"; shift 2 ;;
        -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
        *) echo "Unbekannte Option: $1" >&2; exit 2 ;;
    esac
done

if [ ! -r "$TERMS_FILE" ]; then
    echo "Begriffsdatei fehlt: $TERMS_FILE" >&2
    echo "Anlegen (ein Begriff pro Zeile) oder Pfad über IEC104_FORBIDDEN_TERMS setzen." >&2
    exit 2
fi

PATTERNS="$(mktemp)"
trap 'rm -f "$PATTERNS"' EXIT
# Kommentare, Leerzeilen und Leerzeichen am Zeilenende entfernen
grep -v -e '^[[:space:]]*#' -e '^[[:space:]]*$' "$TERMS_FILE" \
    | sed -e 's/[[:space:]]*$//' -e 's/^[[:space:]]*//' > "$PATTERNS"
if [ ! -s "$PATTERNS" ]; then
    echo "Begriffsdatei enthält keine Begriffe: $TERMS_FILE" >&2
    exit 2
fi

cd "$ROOT" || exit 2
FOUND=0

list_files() {
    find . \( -path ./docs -o -path ./folien -o -path ./build -o -path ./.git \) -prune \
        -o -type f -print0
}

# Dateiinhalte (Binärdateien werden übersprungen)
if list_files | xargs -0 -r grep -n -i -F -I -f "$PATTERNS" --; then
    FOUND=1
fi

# Datei- und Ordnernamen
if list_files | tr '\0' '\n' | grep -i -F -f "$PATTERNS" | sed 's/^/Dateiname: /'; then
    FOUND=1
fi

# Commit-Nachricht
if [ -n "$MESSAGE_FILE" ]; then
    if grep -n -i -F -f "$PATTERNS" -- "$MESSAGE_FILE" | sed 's/^/Commit-Nachricht: /' | grep .; then
        FOUND=1
    fi
fi

if [ "$FOUND" -ne 0 ]; then
    echo "check_neutral: verbotene Begriffe gefunden (siehe oben)." >&2
    exit 1
fi
echo "check_neutral: keine Treffer."
exit 0
