#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# make_package.sh – baut das Weitergabe-Zip aus dem letzten Commit und prüft es.
#
# Aufruf: tools/make_package.sh            -> dist/iec104-<version>.zip
#                                             dist/iec104-<vorige>-to-<version>.patch
#                                             dist/iec104-<version>-linux-x86_64.zip
#
# Lieferungen sind mit Git-Tags lieferung-<version> markiert. Der Patch
# enthält die Änderungen der Paketdateien seit der vorigen Lieferung und
# lässt sich im Zielrepo mit "git apply --3way" einspielen. Das Tag der
# aktuellen Version setzt das Skript auf den aktuellen Commit, falls es noch
# nicht existiert.
#
# Inhalt: alle versionierten Dateien des letzten Commits außer den internen
# Unterlagen (intern/, tasks/, CLAUDE.md, .vscode/, dist/) und den internen
# Prüf-, Paket- und Sync-Skripten. Ungespeicherte Änderungen landen nicht im Zip.
#
# Prüfung des fertigen Zips (bei einem Treffer wird es gelöscht, Rückgabe 1):
#   1. Begriffsliste mit tools/check_neutral.sh --root (Kunden, Projekte,
#      Hersteller; erlaubt sind nur die SPDX-Kopfzeilen und LICENSES/);
#   2. Verweise auf die internen Unterlagen und den Arbeitsablauf.
#
# Fertig gebaute Library: aus dem geprüften Quell-Zip gebaut (Release,
# geteilt, mit TLS). Ins Zip kommt nur das Nötigste: lib/libiec104.so.0
# (eine Datei, keine Symlinks), include/, LICENSES/, julia/ (Anbindung für
# Julia) und eine Kurzanleitung (packaging/README-binary.md). Geprüft werden die Zeichenketten der Library
# (Begriffsliste, interne Verweise, Pfade des Bau-Rechners) und die
# benötigten Symbolversionen, damit die Library auch auf älteren Systemen
# läuft (glibc 2.34, OpenSSL 3.0, z. B. AlmaLinux 9, Ubuntu 22.04, WSL2).

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
EXCLUDE='^(intern/|tasks/|CLAUDE\.md|\.vscode/|dist/|tools/check_neutral\.sh|tools/make_package\.sh|tools/sync_lieferung\.sh)'
EXCLUDE_SPEC=(':!intern' ':!tasks' ':!CLAUDE.md' ':!.vscode' ':!dist'
              ':!tools/check_neutral.sh' ':!tools/make_package.sh' ':!tools/sync_lieferung.sh')
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
# --- Fertig gebaute Library -------------------------------------------------
BIN="$NAME-linux-x86_64"
BOUT="dist/$BIN.zip"
rm -f "$BOUT"
mkdir -p "$WORK/bin"
{
    cmake -S "$WORK/$NAME" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON -DIEC104_TLS=ON -DIEC104_BUILD_TESTS=OFF \
        -DIEC104_BUILD_EXAMPLES=OFF -DCMAKE_INSTALL_LIBDIR=lib &&
    cmake --build "$WORK/build" &&
    cmake --install "$WORK/build" --prefix "$WORK/full" --strip
} > "$WORK/bin.log" 2>&1 || { tail -30 "$WORK/bin.log" >&2; fail "Bau der Library fehlgeschlagen"; }

# Zusammenstellen: die Library unter ihrem SONAME als echte Datei.
P="$WORK/bin/$BIN"
mkdir -p "$P/lib" "$P/include/iec104" "$P/LICENSES"
SONAME="$(objdump -p "$WORK/full/lib/libiec104.so" | sed -n 's/^ *SONAME *//p')"
[ -n "$SONAME" ] || fail "SONAME der Library nicht gefunden"
cp -L "$WORK/full/lib/libiec104.so" "$P/lib/$SONAME"
cp "$WORK/full/include/iec104/iec104.h" "$P/include/iec104/"
cp "$WORK/$NAME"/LICENSES/* "$P/LICENSES/"
sed "s/@VERSION@/$VERSION/g" "$WORK/$NAME/packaging/README-binary.md" > "$P/README.md"
cp -r "$WORK/$NAME/examples/julia" "$P/julia"   # Julia binding, finds ../lib by itself
cp "$WORK/$NAME"/packaging/pruefen.sh "$WORK/$NAME"/packaging/gui.sh \
   "$WORK/$NAME"/packaging/beispiel.sh "$P/"     # Prüfen, GUI, Beispiel (Linux/WSL)
ELF=("$P/lib/$SONAME")

# Zeichenketten der Library und die Kurzanleitung als Text prüfen.
mkdir -p "$WORK/bincheck"
for f in "${ELF[@]}"; do strings -a "$f" > "$WORK/bincheck/$(basename "$f").txt"; done
cp "$P/README.md" "$WORK/bincheck/"
cp -r "$P/julia" "$P"/*.sh "$WORK/bincheck/"
"$ROOT/tools/check_neutral.sh" --root "$WORK/bincheck" > "$WORK/neutral_bin.log" 2>&1 \
    || { cat "$WORK/neutral_bin.log" >&2; fail "verbotene Begriffe in den Binärdateien"; }
if grep -r -n -i -E "intern/|tasks/|TASK-[0-9]|claude|anthropic|$HOME|$ROOT" \
        "$WORK/bincheck" > "$WORK/binrefs.log"; then
    head -20 "$WORK/binrefs.log" >&2
    fail "interne Verweise oder Pfade des Bau-Rechners in den Binärdateien"
fi

# Benötigte Symbolversionen: höchstens glibc 2.34, OpenSSL 3.0.0.
newest() { objdump -T "${ELF[@]}" | grep -o "$1[0-9.]*" | sed "s/^$1//" | sort -V -u | tail -1; }
not_above() { [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" = "$2" ]; }
GLIBC="$(newest GLIBC_)"
OPENSSL="$(newest OPENSSL_)"
GLIBCXX="$(newest GLIBCXX_)"
not_above "$GLIBC" 2.34 || fail "braucht glibc $GLIBC (höchstens 2.34)"
not_above "${OPENSSL:-3.0.0}" 3.0.0 || fail "braucht OpenSSL $OPENSSL (höchstens 3.0.0)"

(cd "$WORK/bin" && zip -q -r "$ROOT/$BOUT" "$BIN")
echo "make_package: $BOUT (glibc $GLIBC, libstdc++ $GLIBCXX, OpenSSL $OPENSSL)"

if ! git rev-parse -q --verify "refs/tags/$TAG" > /dev/null; then
    git tag "$TAG"
    echo "make_package: Tag $TAG gesetzt (mit 'git push origin $TAG' übertragen)"
fi
