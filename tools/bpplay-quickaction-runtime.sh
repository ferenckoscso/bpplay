#!/bin/bash
#
# bpplay-quickaction-runtime.sh — the internal launcher used by the
# bundled "bpplay play.workflow" Finder Quick Action (right-click on
# one or more files, or a folder, in Finder -> "bpplay play").
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Same proven logic as tools/bpplay-drop-runtime.sh (temp launcher
# file + osascript-driven Terminal, to avoid AppleScript quoting
# collisions) -- the only difference: a Quick Action's "Run Shell
# Script" action has no built-in "my own bundle path" mechanism
# (unlike an AppleScript droplet's `path to me`), so this script
# checks the well-known Quick Action install locations by the
# workflow's documented name instead.
#
# This assumes the workflow was installed exactly as documented, at
# either ~/Library/Services/bpplay play.workflow or
# /Library/Services/bpplay play.workflow.

WORKFLOW_NAME="bpplay play.workflow"
for candidate in \
    "$HOME/Library/Services/$WORKFLOW_NAME/Contents/Resources/bpplay" \
    "/Library/Services/$WORKFLOW_NAME/Contents/Resources/bpplay"
do
    if [ -x "$candidate" ]; then
        BPPLAY="$candidate"
        break
    fi
done
DEVICE=""

if [ -z "${BPPLAY:-}" ] || [ ! -x "$BPPLAY" ]; then
    osascript -e 'display alert "bpplay play" message "Could not find the bundled bpplay binary. Was \"bpplay play.workflow\" installed at ~/Library/Services/ under its original name?"'
    exit 1
fi

if [ "$#" -eq 0 ]; then
    exit 0
fi

LAUNCHER="$(mktemp "${TMPDIR:-/tmp}/bpplay_launch.XXXXXXXX")" || exit 1
trap 'rm -f "$LAUNCHER"' EXIT

{
    echo '#!/bin/bash'
    if [ "$#" -eq 1 ] && [ -d "$1" ]; then
        printf '%q %s -dir %q\n' "$BPPLAY" "$DEVICE" "$1"
    else
        printf '%q %s' "$BPPLAY" "$DEVICE"
        printf ' %q' "$@"
        printf '\n'
    fi
    printf 'rm -f %q\n' "$LAUNCHER"
} > "$LAUNCHER"

chmod +x "$LAUNCHER"
trap - EXIT

osascript -e 'tell application "Terminal" to activate' \
          -e "tell application \"Terminal\" to do script \"$LAUNCHER\""
