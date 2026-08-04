#!/bin/bash
#
# bpplay-drop.sh — drag-and-drop launcher body for bpplay (macOS)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This is the "Run Shell Script" body of a working Automator application.
# It is also runnable on its own:  ./bpplay-drop.sh file1 [file2 ...]
#                                  ./bpplay-drop.sh /path/to/Album
#
# CREATING THE AUTOMATOR APP
#   1. Automator -> New Document -> Application
#   2. Search for "Run Shell Script" and drag it onto the workflow
#   3. Set "Pass input:" to "as arguments"
#   4. Delete the sample text and paste the body below (between the ==== lines)
#   5. Save it as "bpplay drop.app", e.g. on the Desktop
#   6. Drop a folder or a set of files onto it -> a Terminal window opens,
#      the music plays, transport keys work (n/b/space/q)
#
# HOW IT WORKS
#   The bpplay command is written into a temporary script, and AppleScript is
#   handed only that one simple path. This avoids the quoting collisions that
#   album and file names with spaces would otherwise cause (the source of the
#   AppleScript -2741 error).
#
# CONFIGURE THESE TWO LINES BEFORE FIRST USE:
#   BPPLAY  — the full path to your bpplay binary
#   DEVICE  — the output device, e.g. "-d 1". Run `bpplay -l` to see the
#             indices. Leave it empty to use the system default output.
#
# ==================== AUTOMATOR SCRIPT BODY ====================

BPPLAY="$HOME/Desktop/bpplay"
DEVICE="-d 1"

if [ "$#" -eq 0 ]; then
    echo "Usage: drop files or a folder onto this app, or:"
    echo "  $0 file1 [file2 ...]"
    echo "  $0 /path/to/Album"
    exit 1
fi

if [ ! -x "$BPPLAY" ]; then
    echo "bpplay not found or not executable: $BPPLAY"
    echo "Edit the BPPLAY variable at the top of this script."
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

# ==================== END OF BODY ====================
#
# Notes:
#   - One folder dropped -> -dir mode (recursive, sorted by name);
#     several files -> played in the order given
#   - .dsf files switch to DSD (DoP) automatically, no extra flag needed
