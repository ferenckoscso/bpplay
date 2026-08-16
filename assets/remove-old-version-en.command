#!/bin/bash
#
# Remove old version.command — finds and moves any pieces of a previous
# bpplay installation to the Trash, before you install a new version in
# the same place. Does NOT delete permanently (everything goes to the
# Trash, recoverable/emptyable from there), and ALWAYS asks for
# confirmation before moving anything.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -uo pipefail

echo "=== bpplay — looking for a previous version ==="
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
    echo "No previous bpplay installation found in the usual places."
    echo "(If you put it somewhere else, drag it to the Trash by hand.)"
    echo ""
    read -p "Press Enter to close... "
    exit 0
fi

echo "Found the following:"
for f in "${FOUND[@]}"; do
    echo "  - $f"
done
echo ""
read -p "Move these to the Trash? (y/n): " ANSWER
case "$ANSWER" in
    y|Y|yes|Yes)
        ;;
    *)
        echo "Cancelled, nothing was moved."
        read -p "Press Enter to close... "
        exit 0
        ;;
esac

echo ""
for f in "${FOUND[@]}"; do
    ESCAPED="${f//\"/\\\"}"
    if osascript -e "tell application \"Finder\" to delete POSIX file \"$ESCAPED\"" >/dev/null 2>&1; then
        echo "  Moved to Trash: $f"
    else
        echo "  ERROR, could not move: $f (delete it by hand)"
    fi
done

echo ""
echo "Done. You can now install the new version in the same place."
read -p "Press Enter to close... "
