#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# patches_entfernen_und_pushen.sh – entfernt alle *.patch-Dateien aus der ganzen Historie
# eines Klons und überschreibt danach den Server (nur im privaten Repo, kommt nicht ins Zip).
#
# Aufruf (MSYS2/Git Bash oder Linux), nach tools/bereinige_historie.sh, ohne zwischendurch
# zu fetchen oder zu pullen:
#   bash patches_entfernen_und_pushen.sh KLON
#
# Grundsatz: alle Prüfungen vor dem Umschreiben; bricht das Skript dort ab, ist nichts geändert.
#   1. Ziel: origin muss ein Server sein (keine Sicherung, kein lokaler Pfad); Zweig main,
#      sauberer Arbeitsbaum, kein Stash; bereinige_historie.sh liegt daneben.
#   2. Server unverändert seit der Bereinigung (ls-remote = origin/main); auf dem Server keine
#      weiteren Zweige und keine Tags, die lokal fehlen.
#   3. Vorab: keine Texte von bereinige_historie.sh; keine verräterischen Wörter in
#      Commit-Nachrichten (nach den festen Ersetzungen unten), Tag-Namen und Tag-Texten;
#      kein Tag auf einem Commit, der nur aus Patch-Dateien besteht.
#   4. Sicherung (Mirror-Klon neben KLON), dann Umschreiben: *.patch (auch .PATCH, in jedem
#      Ordner, auch mit Umlauten im Namen) aus allen Commits; leer gewordene Commits fallen weg.
#   5. Kontrolle wie in 3, zusätzlich keine *.patch mehr.
#   6. Zeigt Ziel und neue Historie; nur bei Eingabe von JA: main mit --force-with-lease gegen
#      den Stand aus 2, Tags abgleichen; danach Kontrolle mit ls-remote.

set -u
KLON="${1:?Klon des Ziel-Repos angeben}"
HIER="$(cd "$(dirname "$0")" && pwd)"
BEREINIGE="$HIER/bereinige_historie.sh"
cd "$KLON" || exit 1
KLON="$(pwd)"
stop() { echo "ABBRUCH: $*"; echo "Am Klon wurde nichts geändert."; exit 1; }

# Feste Ersetzungen in Commit-Nachrichten (sed-Ausdrücke); Treffer der Prüfung unten
# lassen sich hier ergänzen.
MSG_SED=(-e 's/^alten Patch entfernt$/Aufräumen/')
# Wörter, die auf den Lieferweg schließen lassen (mit Wortgrenzen, Groß-/Kleinschreibung egal).
WORTE='(^|[^[:alpha:]äöüÄÖÜß])(patch(es)?|liefer(ung|ungen|patch|patches)?|lieferpatch(es)?|privat(e[snm]?)?|sync_lieferung|upstream)([^[:alpha:]äöüÄÖÜß]|$)'

# --- 1. Ziel und Klon -----------------------------------------------------------------
git rev-parse --git-dir > /dev/null 2>&1 || stop "$KLON ist kein Git-Klon"
URL="$(git remote get-url origin 2> /dev/null)" || stop "kein origin"
case "$URL" in
    *-sicherung-*|*-vor-push-*) stop "origin zeigt auf eine Sicherung ($URL), nicht auf den Server" ;;
esac
if [ "${NUR_TEST_LOKALER_SERVER:-0}" != 1 ]; then   # nur für Tests mit einem lokalen Test-Server
    case "$URL" in
        /*|[A-Za-z]:*|file://*|.*) stop "origin ist ein lokaler Pfad ($URL); Server-URL setzen: git remote set-url origin <url>" ;;
    esac
fi
echo "Ziel: $URL"
[ "$(git symbolic-ref -q --short HEAD)" = main ] || stop "nicht auf Zweig main"
[ -z "$(git status --porcelain)" ] || { git status --short | head; stop "Arbeitsbaum nicht sauber"; }
[ -z "$(git stash list)" ] || stop "Stash vorhanden – erst anwenden oder verwerfen (git stash list)"
[ -f "$BEREINIGE" ] || stop "bereinige_historie.sh fehlt neben diesem Skript ($HIER)"

# --- 2. Server --------------------------------------------------------------------------
SERVER="$(git rev-parse -q --verify origin/main)" || stop "origin/main unbekannt"
REMOTE="$(git ls-remote origin 2> /dev/null)" || stop "Server nicht erreichbar"
LIVE="$(printf '%s\n' "$REMOTE" | awk '$2 == "refs/heads/main" {print $1}')"
[ "$LIVE" = "$SERVER" ] ||
    stop "Server hat sich seit der Bereinigung geändert (main dort $LIVE, erwartet $SERVER) – Klon aus der Sicherung herstellen, pullen und neu bereinigen"
ANDERE="$(printf '%s\n' "$REMOTE" | awk '{print $2}' | grep -E '^refs/heads/' | grep -v -x 'refs/heads/main')"
[ -z "$ANDERE" ] || stop "auf dem Server liegen weitere Zweige: $(echo $ANDERE) – erst klären"
for t in $(printf '%s\n' "$REMOTE" | awk '{print $2}' | grep -E '^refs/tags/' | grep -v '\^{}$'); do
    git rev-parse -q --verify "$t" > /dev/null || stop "Tag ${t#refs/tags/} liegt nur auf dem Server – erst holen und bereinigen"
done
SONST="$(printf '%s\n' "$REMOTE" | awk '{print $2}' | grep -v -E '^(HEAD|refs/heads/main|refs/tags/.*)$')"
[ -n "$SONST" ] && echo "Hinweis: weitere Refs auf dem Server (z. B. Merge-Requests), die per Push nicht änderbar sind: $(echo $SONST | cut -c1-200)"

# --- 3. Prüfungen vor dem Umschreiben -------------------------------------------------------
pruefen() { # Phase (vorher/nachher)
    local fehler=0 rev
    bash "$BEREINIGE" --pruefen . | tail -1 | grep -q 'Inhalt: 0; Zeilen in Commit-Nachrichten: 0' ||
        { echo "  bereinige_historie.sh findet noch Texte"; fehler=1; }
    local msg   # Betreffzeilen so, wie sie nach den festen Ersetzungen aussehen
    msg="$(git log --branches --tags --format='%s' | sed "${MSG_SED[@]}" | grep -i -E "$WORTE")"
    [ -n "$msg" ] && { echo "  Commit-Nachrichten mit verräterischen Wörtern (Ersetzung in MSG_SED ergänzen):"; echo "$msg" | sort -u | head | sed 's/^/    /'; fehler=1; }
    local tags
    tags="$(git for-each-ref refs/tags --format='%(refname:short) %(contents)' | grep -i -E "$WORTE")"
    [ -n "$tags" ] && { echo "  Tag-Namen oder Tag-Texte mit verräterischen Wörtern:"; echo "$tags" | head | sed 's/^/    /'; fehler=1; }
    return $fehler
}
patch_namen() { # Commit -> Patch-Dateien darin (auch Umlaute, Großschreibung)
    git -c core.quotePath=false ls-tree -r -z --name-only "$1" | tr '\0' '\n' | grep -i -E '\.patch$'
}
nur_patches() { # Commit besteht nur aus Patch-Dateien
    [ -z "$(git -c core.quotePath=false ls-tree -r -z --name-only "$1" | tr '\0' '\n' | grep -v -i -E '\.patch$' | grep .)" ]
}
echo "Prüfe vor dem Umschreiben ..."
pruefen vorher || stop "Prüfung vor dem Umschreiben"
for t in $(git tag); do
    nur_patches "$t^{commit}" && stop "Tag $t zeigt auf einen Commit, der nur aus Patch-Dateien besteht – Tag erst löschen: git tag -d $t && git push origin --delete $t"
done
ANZAHL=0
for rev in $(git rev-list --branches --tags); do
    [ -n "$(patch_namen "$rev")" ] && ANZAHL=$((ANZAHL + 1))
done
echo "Commits mit Patch-Dateien: $ANZAHL"

# --- 4. Sicherung und Umschreiben -----------------------------------------------------------
SICHERUNG="$(cd .. && pwd)/$(basename "$KLON")-vor-push-$(date +%Y%m%d-%H%M%S)-$$.git"
git clone -q --mirror . "$SICHERUNG" || stop "Sicherung fehlgeschlagen"
git -C "$SICHERUNG" remote set-url origin "$URL"
RUECKWEG="rm -rf \"$KLON\" && git clone \"$SICHERUNG\" \"$KLON\" && git -C \"$KLON\" remote set-url origin \"$URL\" && git -C \"$KLON\" fetch -q origin"
echo "Sicherung: $SICHERUNG"
NACHRICHTEN_AENDERN=0
git log --branches --tags --format=%s | sed "${MSG_SED[@]}" | cmp -s - <(git log --branches --tags --format=%s) ||
    NACHRICHTEN_AENDERN=1
if [ "$ANZAHL" -gt 0 ] || [ "$NACHRICHTEN_AENDERN" = 1 ]; then
    export FILTER_BRANCH_SQUELCH_WARNING=1
    MSG_ARGS="$(printf '%q ' "${MSG_SED[@]}")"
    if ! git filter-branch -f \
        --index-filter "git ls-files -z -- ':(icase)*.patch' | xargs -0 -r git rm -q --cached --" \
        --msg-filter "sed $MSG_ARGS" \
        --prune-empty --tag-name-filter cat -- --branches --tags; then
        git reset -q --hard
        echo "filter-branch fehlgeschlagen. Rückweg:"
        echo "  $RUECKWEG"
        exit 1
    fi
    git for-each-ref --format='%(refname)' refs/original/ | xargs -r -n 1 git update-ref -d
    git reset -q --hard HEAD
fi

# --- 5. Kontrolle ---------------------------------------------------------------------------
echo "Kontrolle ..."
REST=""
for rev in $(git rev-list --branches --tags); do
    p="$(patch_namen "$rev")"
    [ -n "$p" ] && REST="$REST$rev: $p"$'\n'
done
if [ -n "$REST" ] || ! pruefen nachher; then
    [ -n "$REST" ] && { echo "  noch Patch-Dateien:"; printf '%s' "$REST" | head; }
    echo "NICHT pushen. Rückweg:"
    echo "  $RUECKWEG"
    exit 1
fi
echo "sauber: keine *.patch, keine Texte, keine verräterischen Wörter"

# --- 6. Zeigen, fragen, pushen, kontrollieren ------------------------------------------------
echo
echo "Ziel: $URL"
echo "Neue Historie (main):"
git log --oneline main | head -30
echo "Tags: $(git tag | tr '\n' ' ')"
echo
read -r -p "Server jetzt überschreiben? Alle Klone müssen danach neu klonen. Mit JA bestätigen: " ANTWORT
if [ "$ANTWORT" != "JA" ]; then
    echo "nicht gepusht. Klon ist umgeschrieben; Rückweg:"
    echo "  $RUECKWEG"
    exit 0
fi
git push --force-with-lease=main:"$SERVER" origin main || { echo "Push von main abgelehnt (Server geändert oder main geschützt)"; exit 1; }
git push --force --prune origin 'refs/tags/*:refs/tags/*' || { echo "Abgleich der Tags fehlgeschlagen"; exit 1; }
NEU="$(git ls-remote origin)"
OK=1
[ "$(printf '%s\n' "$NEU" | awk '$2 == "refs/heads/main" {print $1}')" = "$(git rev-parse main)" ] || OK=0
for t in $(git tag); do
    [ "$(printf '%s\n' "$NEU" | awk -v r="refs/tags/$t" '$2 == r {print $1}')" = "$(git rev-parse "refs/tags/$t")" ] || OK=0
done
git fetch -q origin
if [ "$OK" = 1 ]; then
    echo "Server überschrieben und geprüft: main = $(git rev-parse --short main)"
else
    echo "WARNUNG: Server weicht nach dem Push ab – git ls-remote origin prüfen"
fi
cat << EOF

Danach:
  - alle anderen Klone neu klonen und nicht mehr aus alten Klonen pushen,
    in WSL z. B.: rm -rf ~/src/ieee104 && bash /mnt/c/rd/IEEE104/iec104-0.1.0/tools/wsl_ausprobieren.sh ~/src/ieee104 --mit-lib60870
  - GitLab hält alte Commits noch eine Zeit lang per Hash erreichbar (Merge-Requests,
    Pipelines). Endgültig: Einstellungen → Allgemein → Erweitert → Housekeeping bzw.
    „Prune unreachable objects“.
  - Sicherung $SICHERUNG nach ein paar Tagen löschen.
EOF
