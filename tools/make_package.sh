#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# make_package.sh – baut das Weitergabe-Zip aus dem letzten Commit und prüft es.
#
# Aufruf: tools/make_package.sh            -> dist/iec104-<version>.zip
#                                             dist/iec104-<vorige>-to-<version>.patch
#
# Lieferungen sind mit Git-Tags lieferung-<version> markiert. Der Patch
# enthält die Änderungen der Paketdateien seit der vorigen Lieferung und
# lässt sich im Zielrepo mit "git apply --3way" einspielen. Das Tag der
# aktuellen Version setzt das Skript auf den aktuellen Commit, falls es noch
# nicht existiert.
#
# Inhalt: alle versionierten Dateien des letzten Commits außer den internen
# Unterlagen (intern/, tasks/, CLAUDE.md, .vscode/, dist/) und den internen
# Prüf- und Paketskripten. Ungespeicherte Änderungen landen nicht im Zip.
#
# Prüfung des fertigen Zips (bei einem Treffer wird es gelöscht, Rückgabe 1):
#   1. Begriffsliste mit tools/check_neutral.sh --root (Kunden, Projekte,
#      Hersteller; erlaubt sind nur die SPDX-Kopfzeilen und LICENSES/);
#   2. Verweise auf die internen Unterlagen und den Arbeitsablauf.

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

VERSION="$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1)"
[ -n "$VERSION" ] || { echo "Version nicht gefunden" >&2; exit 1; }
NAME="iec104-$VERSION"
OUT="dist/$NAME.zip"

if ! git diff --quiet HEAD -- . ':!dist'; then
    echo "Hinweis: ungespeicherte Änderungen werden nicht verpackt." >&2
fi

# Paketdateien: alles außer den internen Unterlagen und Skripten.
EXCLUDE='^(intern/|tasks/|CLAUDE\.md|\.vscode/|dist/|tools/check_neutral\.sh|tools/make_package\.sh)'
EXCLUDE_SPEC=(':!intern' ':!tasks' ':!CLAUDE.md' ':!.vscode' ':!dist'
              ':!tools/check_neutral.sh' ':!tools/make_package.sh')
mapfile -t FILES < <(git ls-files | grep -v -E "$EXCLUDE")

mkdir -p dist
rm -f "$OUT"
git archive --format=zip --prefix="$NAME/" -o "$OUT" HEAD -- "${FILES[@]}"

# --- Prüfung des Inhalts ------------------------------------------------------
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
unzip -q "$OUT" -d "$WORK"

fail() {
    echo "make_package: $1 – $OUT wurde gelöscht." >&2
    rm -f "$OUT"
    exit 1
}

"$ROOT/tools/check_neutral.sh" --root "$WORK/$NAME" > "$WORK/neutral.log" 2>&1 \
    || { cat "$WORK/neutral.log" >&2; fail "verbotene Begriffe im Zip"; }

# Verweise auf ausgelassene Unterlagen oder den Arbeitsablauf.
if grep -r -I -n -i -E 'intern/|tasks/|TASK-[0-9]|CLAUDE|claude|anthropic|check_neutral|make_package' \
        "$WORK/$NAME" > "$WORK/refs.log"; then
    cat "$WORK/refs.log" >&2
    fail "Verweise auf interne Unterlagen im Zip"
fi

echo "make_package: $OUT (${#FILES[@]} Dateien, geprüft)"

# --- Patch gegen die vorige Lieferung ----------------------------------------
TAG="lieferung-$VERSION"
PREV="$(git tag --list 'lieferung-*' --sort=-v:refname | grep -v -x "$TAG" | head -1 || true)"
if [ -n "$PREV" ]; then
    PATCH="dist/iec104-${PREV#lieferung-}-to-$VERSION.patch"
    git diff --binary "$PREV" HEAD -- . "${EXCLUDE_SPEC[@]}" > "$PATCH"
    # Prüfung: vorige Lieferung + Patch muss exakt das neue (geprüfte) Zip ergeben.
    mkdir -p "$WORK/old"
    git ls-tree -r --name-only "$PREV" | grep -v -E "$EXCLUDE" > "$WORK/old.files"
    git archive --format=tar "$PREV" -- $(cat "$WORK/old.files") | tar -x -C "$WORK/old"
    (cd "$WORK/old" && git apply --whitespace=nowarn "$ROOT/$PATCH") \
        || { rm -f "$PATCH"; fail "Patch lässt sich auf $PREV nicht anwenden"; }
    diff -r -q "$WORK/old" "$WORK/$NAME" > "$WORK/diff.log" \
        || { cat "$WORK/diff.log" >&2; rm -f "$PATCH"; fail "Patch ergibt nicht den Inhalt des Zips"; }
    echo "make_package: $PATCH (Änderungen seit $PREV)"
fi
if ! git rev-parse -q --verify "refs/tags/$TAG" > /dev/null; then
    git tag "$TAG"
    echo "make_package: Tag $TAG gesetzt (mit 'git push origin $TAG' übertragen)"
fi
