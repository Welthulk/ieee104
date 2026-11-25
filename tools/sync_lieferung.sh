#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# sync_lieferung.sh – spielt neue Lieferungen aus dem privaten Repo ins Ziel-Repo (internes Repo) ein.
#
# Aufruf (MSYS2 oder Linux), aus dem Klon des privaten Repos:
#   tools/sync_lieferung.sh ZIELREPO [--push]
#   z. B. /c/rd/IEEE104/privat/tools/sync_lieferung.sh /c/rd/IEEE104/iec104-0.1.0 --push
#
# Ablauf:
#   1. privates Repo aktualisieren (git pull)
#   2. Zielrepo: muss sauber sein, git pull
#   3. Stand aus README.md lesen ("Version x.y.z")
#   4. alle passenden Patches aus dist/ der Reihe nach einspielen; wegen
#      core.autocrlf unter Windows mit LF-Zeilenenden auf den Index
#      (sed + git apply --cached), danach Arbeitskopie aus dem Index
#   5. fertig gebaute Library der neuen Version nach dist/ entpacken,
#      ältere entpackte Fassungen entfernen
#   6. ein Commit "IEC-104-Library <version>"; mit --push auch pushen
#
# Bei einem Fehler bricht das Skript ab, bevor etwas committet ist; zurück
# auf den alten Stand mit: git -C ZIELREPO reset --hard HEAD
#
# Nicht Teil der Lieferung (tools/make_package.sh lässt es aus).

set -eu

PRIV="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:?Zielrepo fehlt, z. B. /c/rd/IEEE104/iec104-0.1.0}"
PUSH="${2:-}"
[ -d "$TARGET/.git" ] || { echo "kein Git-Repo: $TARGET" >&2; exit 2; }
PATCH="$(mktemp)"
trap 'rm -f "$PATCH"' EXIT

step() { printf '\n== %s\n' "$1"; }
version_of() { sed -n 's/^Stand: .*Version \([0-9.]*\),.*/\1/p' "$1/README.md"; }

step "privates Repo aktualisieren"
git -C "$PRIV" pull --ff-only

step "Zielrepo prüfen"
if [ -n "$(git -C "$TARGET" status --porcelain)" ]; then
    git -C "$TARGET" status --short
    echo "Zielrepo hat ungespeicherte Änderungen; erst committen oder verwerfen." >&2
    exit 1
fi
git -C "$TARGET" pull --ff-only
V="$(version_of "$TARGET")"
[ -n "$V" ] || { echo "Version in $TARGET/README.md nicht gefunden" >&2; exit 1; }
echo "Stand Zielrepo: $V"

step "Patches einspielen"
START="$V"
while :; do
    NEXT="$(ls "$PRIV"/dist/iec104-"$V"-to-*.patch 2>/dev/null | head -1)"
    [ -n "$NEXT" ] || break
    TO="$(basename "$NEXT" .patch)"
    TO="${TO##*-to-}"
    echo "$V -> $TO"
    sed 's/\r$//' "$NEXT" > "$PATCH"
    git -C "$TARGET" apply --cached --check "$PATCH"
    git -C "$TARGET" apply --cached "$PATCH"
    git -C "$TARGET" checkout -- .
    V="$TO"
done
if [ "$V" = "$START" ]; then
    echo "Zielrepo ist aktuell ($V)."
    exit 0
fi
[ "$(version_of "$TARGET")" = "$V" ] || { echo "README nach den Patches nicht auf $V" >&2; exit 1; }

step "fertig gebaute Library $V"
BIN="$PRIV/dist/iec104-$V-linux-x86_64.zip"
if [ -f "$BIN" ]; then
    mkdir -p "$TARGET/dist"
    for OLD in "$TARGET"/dist/iec104-*-linux-x86_64; do
        [ -d "$OLD" ] || continue
        [ "$(basename "$OLD")" = "iec104-$V-linux-x86_64" ] && continue
        echo "entferne $(basename "$OLD")"
        git -C "$TARGET" rm -r -q --ignore-unmatch "dist/$(basename "$OLD")"
        rm -rf "$OLD"
    done
    unzip -q -o "$BIN" -d "$TARGET/dist"
    echo "entpackt: dist/iec104-$V-linux-x86_64"
else
    echo "keine fertig gebaute Library für $V gefunden ($BIN)"
fi

step "Commit"
git -C "$TARGET" add -A
git -C "$TARGET" commit -q -m "IEC-104-Library $V"
git -C "$TARGET" log --oneline -1
if [ "$PUSH" = "--push" ]; then
    step "Push"
    git -C "$TARGET" push
else
    echo "Noch nicht gepusht: git -C $TARGET push"
fi
