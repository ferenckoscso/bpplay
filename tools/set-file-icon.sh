#!/bin/bash
#
# set-file-icon.sh — sets a plain file's custom Finder icon from a .icns,
# for the raw `bpplay` CLI binary in the DMG (it has no bundle/Info.plist
# to carry an icon reference, unlike bpplay drop.app / bpplay play.workflow).
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: tools/set-file-icon.sh <icon.icns> <target-file>
#
# Uses NSWorkspace via JXA (osascript -l JavaScript) — built into every
# macOS install (AppKit/Foundation frameworks), no extra dependency. This
# is the same underlying mechanism as Finder's manual "Get Info, paste
# icon" — it writes a custom icon resource onto the target file itself,
# it does not need or create an app bundle.

set -euo pipefail

ICON="$1"
TARGET="$2"

if [ ! -f "$ICON" ]; then
    echo "error: icon not found: $ICON" >&2
    exit 1
fi
if [ ! -f "$TARGET" ]; then
    echo "error: target not found: $TARGET" >&2
    exit 1
fi

# osascript prints run()'s return value to stdout regardless of exit code
# (a JS "return 1" does NOT make osascript itself exit non-zero) -- capture
# and check it explicitly rather than trusting $?.
RESULT="$(osascript -l JavaScript -e '
ObjC.import("AppKit");
function run(argv) {
    var iconPath = argv[0];
    var targetPath = argv[1];
    var icon = $.NSImage.alloc.initWithContentsOfFile(iconPath);
    if (!icon) {
        return "error: could not load icon image: " + iconPath;
    }
    var ok = $.NSWorkspace.sharedWorkspace.setIconForFileOptions(icon, targetPath, 0);
    if (!ok) {
        return "error: NSWorkspace setIcon:forFile: failed for " + targetPath;
    }
    return "ok";
}
' "$ICON" "$TARGET")"

if [ "$RESULT" != "ok" ]; then
    echo "$RESULT" >&2
    exit 1
fi
