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

# osacompile bakes in its own compiled asset catalog (Assets.car) holding
# Apple's generic stock "droplet" template icon -- on current macOS this
# compiled catalog wins icon resolution over the legacy
# CFBundleIconFile+.icns pair below it, so without removing it the app
# shows Apple's placeholder droplet instead of the bpplay logo, even
# though droplet.icns above is present and correctly referenced.
rm -f "$DIST_DIR/$APP_NAME/Contents/Resources/Assets.car"

# Re-sign after modifying the bundle contents (osacompile's own signature
# only covers what it wrote; we added files afterwards). Uses the real
# Developer ID identity + hardened runtime when available (needed for
# notarization -- see build-dmg.sh), falling back to an ad-hoc signature
# otherwise. The bundled bpplay binary itself is signed separately by
# build-dmg.sh before this script runs; --deep is not needed to re-cover
# it, only to seal this bundle's own top-level executable + Resources.
SIGN_IDENTITY="Developer ID Application: Ferenc Koscso (M4D4ZUXPH6)"
if security find-identity -v -p codesigning 2>/dev/null | grep -q "$SIGN_IDENTITY"; then
    codesign --force --options runtime --timestamp -s "$SIGN_IDENTITY" "$DIST_DIR/$APP_NAME"
else
    # Ad-hoc: NOT Gatekeeper-trusted -- the user still needs the one-time
    # bypass documented in the DMG's own first-run readme.
    codesign --force --deep -s - "$DIST_DIR/$APP_NAME"
fi

echo "Built: $DIST_DIR/$APP_NAME"
