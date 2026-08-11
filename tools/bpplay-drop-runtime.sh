#!/bin/bash
#
# bpplay-drop-runtime.sh — the internal launcher used by the bundled
# "bpplay drop.app" AppleScript droplet (see main.applescript and
# build-drop-app.sh). Not meant to be edited by hand after install —
# it lives inside the .app bundle's Contents/Resources/, next to the
# bpplay binary itself, and resolves its own location at runtime.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Same proven logic as the original tools/bpplay-drop.sh (temp
# launcher file + osascript-driven Terminal, to avoid the AppleScript
# quoting collisions that album/file names with spaces would
# otherwise cause) — the only differences: BPPLAY is resolved
# dynamically relative to this script's own location (works no matter
# where the user drags the .app), and DEVICE defaults to empty (system
# default output), so it works out of the box with zero configuration.
#
# If you want a specific output device instead of the system default,
# run `./bpplay -l` from Terminal to see the indices, then either pass
# `-d N` by hand from Terminal, or build your own custom Automator app
# per the manual's drag-and-drop chapter (tools/bpplay-drop.sh).

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BPPLAY="$SCRIPT_DIR/bpplay"
DEVICE=""

if [ "$#" -eq 0 ]; then
    echo "Usage: drop files or a folder onto this app, or:"
    echo "  $0 file1 [file2 ...]"
    echo "  $0 /path/to/Album"
    exit 1
fi

if [ ! -x "$BPPLAY" ]; then
    echo "bpplay not found or not executable: $BPPLAY"
    exit 1
fi

# A unique temporary file per launch. mktemp avoids the predictable-name
# problem of a fixed /tmp path, which on a shared machine another user could
# pre-create or symlink.
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
    # Clean up only after playback has finished. Deleting the script while
    # bash is still reading it would be asking for trouble.
    printf 'rm -f %q\n' "$LAUNCHER"
} > "$LAUNCHER"

chmod +x "$LAUNCHER"
trap - EXIT                              # the launcher must survive this script

# Terminal only ever runs the temporary script — no quoting collisions.
osascript -e 'tell application "Terminal" to activate' \
          -e "tell application \"Terminal\" to do script \"$LAUNCHER\""
