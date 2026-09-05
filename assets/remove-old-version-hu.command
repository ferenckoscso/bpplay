#!/bin/bash
#
# Régi verzió eltávolítása.command — segít megkeresni és a Kukába
# helyezni a bpplay korábbi telepítésének darabjait, mielőtt egy új
# verziót telepítenél ugyanoda. NEM töröl véglegesen (Kukába kerül
# minden, onnan visszaállítható/kiüríthető), és MINDIG megerősítést
# kér, mielőtt bármit is elmozdítana.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -uo pipefail

echo "=== bpplay — régi verzió keresése ==="
echo ""

# Sima futtathato-jeloltek: csak REGULAR FILE-kent szamitanak talalatnak
# (nem konyvtarkent) -- kulonben egy veletlenul "bpplay" nevu SAJAT mappa
# (pl. egy klonozott repo) is tevesen talalatnak minosulne.
PLAIN_CANDIDATES=(
    "$HOME/bpplay"
    "$HOME/Applications/bpplay"
    "$HOME/Desktop/bpplay"
    "/usr/local/bin/bpplay"
    "/Applications/bpplay"
)
# Bundle-jeloltek (.app / .workflow): ezek MINDIG konyvtarak.
BUNDLE_CANDIDATES=(
    "$HOME/Applications/bpplay drop.app"
    "$HOME/Desktop/bpplay drop.app"
    "/Applications/bpplay drop.app"
    "$HOME/Library/Services/bpplay play.workflow"
)

FOUND=()
for c in "${PLAIN_CANDIDATES[@]}"; do
    if [ -f "$c" ]; then
        FOUND+=("$c")
    fi
done
for c in "${BUNDLE_CANDIDATES[@]}"; do
    if [ -d "$c" ]; then
        FOUND+=("$c")
    fi
done

if [ ${#FOUND[@]} -eq 0 ]; then
    echo "Nem találtam korábbi bpplay-telepítést a szokásos helyeken."
    echo "(Ha máshova tetted, azt kézzel kell a Kukába húznod.)"
    echo ""
    read -p "Nyomj Entert a bezáráshoz... "
    exit 0
fi

echo "A következőket találtam:"
for f in "${FOUND[@]}"; do
    echo "  - $f"
done
echo ""
read -p "Ezeket a Kukába helyezzem? (i/n): " ANSWER
case "$ANSWER" in
    i|I|igen|Igen)
        ;;
    *)
        echo "Megszakítva, semmi nem lett elmozdítva."
        read -p "Nyomj Entert a bezáráshoz... "
        exit 0
        ;;
esac

echo ""
for f in "${FOUND[@]}"; do
    ESCAPED="${f//\"/\\\"}"
    if osascript -e "tell application \"Finder\" to delete POSIX file \"$ESCAPED\"" >/dev/null 2>&1; then
        echo "  Kukába helyezve: $f"
    else
        echo "  HIBA, nem sikerült elmozdítani: $f (töröld kézzel)"
    fi
done

echo ""
echo "Kész. Most már telepítheted az új verziót ugyanoda, ahonnan ezeket eltávolítottuk."
read -p "Nyomj Entert a bezáráshoz... "
