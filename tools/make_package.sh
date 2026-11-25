#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# make_package.sh – baut das Weitergabe-Zip aus dem letzten Commit.
#
# Aufruf: tools/make_package.sh            -> dist/iec104-<version>.zip
#
# Inhalt: alle versionierten Dateien des letzten Commits außer den internen
# Arbeitsunterlagen (CLAUDE.md, tasks/, docs/04_Analyse.md, .vscode/, dist/)
# und den internen Prüf- und Paketskripten. Ungespeicherte Änderungen
# landen nicht im Zip; vorher committen.

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

VERSION="$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1)"
[ -n "$VERSION" ] || { echo "Version nicht gefunden" >&2; exit 1; }
OUT="dist/iec104-$VERSION.zip"

if ! git diff --quiet HEAD -- . ':!dist'; then
    echo "Hinweis: ungespeicherte Änderungen werden nicht verpackt." >&2
fi

mapfile -t FILES < <(git ls-files | grep -v -E \
    '^(CLAUDE\.md|tasks/|docs/04_Analyse\.md|\.vscode/|dist/|tools/check_neutral\.sh|tools/make_package\.sh)')

mkdir -p dist
rm -f "$OUT"
git archive --format=zip --prefix="iec104-$VERSION/" -o "$OUT" HEAD -- "${FILES[@]}"
echo "make_package: $OUT (${#FILES[@]} Dateien)"
