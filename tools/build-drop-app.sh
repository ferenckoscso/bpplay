#!/bin/bash
#
# build-drop-app.sh — reproducibly builds "bpplay drop.app", the
# self-contained drag-and-drop launcher bundled in the DMG since
# v0.9.1.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: run from anywhere; requires an already-built src/bpplay
# (run `make universal` in src/ first). Output: dist/bpplay drop.app

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS_DIR="$ROOT_DIR/tools"
DIST_DIR="$ROOT_DIR/dist"
BPPLAY_BIN="$ROOT_DIR/src/bpplay"
ICON="$ROOT_DIR/assets/icons/bpplay_dark.icns"
APP_NAME="bpplay drop.app"

if [ ! -x "$BPPLAY_BIN" ]; then
    echo "error: $BPPLAY_BIN not found or not executable -- build it first:"
    echo "  (cd src && make universal)"
    exit 1
fi

mkdir -p "$DIST_DIR"
rm -rf "$DIST_DIR/$APP_NAME"

osacompile -o "$DIST_DIR/$APP_NAME" "$TOOLS_DIR/main.applescript"

mkdir -p "$DIST_DIR/$APP_NAME/Contents/Resources"
cp "$BPPLAY_BIN" "$DIST_DIR/$APP_NAME/Contents/Resources/bpplay"
cp "$TOOLS_DIR/bpplay-drop-runtime.sh" "$DIST_DIR/$APP_NAME/Contents/Resources/bpplay-drop-runtime.sh"
chmod +x "$DIST_DIR/$APP_NAME/Contents/Resources/bpplay-drop-runtime.sh"
cp "$ICON" "$DIST_DIR/$APP_NAME/Contents/Resources/droplet.icns"

# Ad-hoc re-sign after modifying the bundle contents (osacompile's own
# signature only covers what it wrote; we added files afterwards).
# This does NOT make it Gatekeeper-trusted -- same as the raw bpplay
# binary, the user still needs the one-time right-click-Open bypass
# documented in the DMG's own first-run readme.
codesign --force --deep -s - "$DIST_DIR/$APP_NAME"

echo "Built: $DIST_DIR/$APP_NAME"
