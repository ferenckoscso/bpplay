#!/bin/bash
#
# build-dmg.sh — reproducibly builds the release DMG (bpplay binary +
# bpplay drop.app + LICENSE + first-run readme) and its SHA256
# checksum.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: ./tools/build-dmg.sh
# Output: dist/bpplay-<version>-macos.dmg + .sha256

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="$ROOT_DIR/dist"
VERSION="$(grep -m1 'define BPPLAY_VERSION' "$ROOT_DIR/src/bpplay.c" | sed -E 's/.*"([^"]+)".*/\1/')"

echo "=== Building bpplay $VERSION (universal binary) ==="
( cd "$ROOT_DIR/src" && make clean >/dev/null && make universal )

echo "=== Building bpplay drop.app ==="
"$ROOT_DIR/tools/build-drop-app.sh"

echo "=== Assembling DMG staging area ==="
STAGE="$DIST_DIR/dmg-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$ROOT_DIR/src/bpplay" "$STAGE/bpplay"
cp -R "$DIST_DIR/bpplay drop.app" "$STAGE/bpplay drop.app"
cp "$ROOT_DIR/LICENSE" "$STAGE/LICENSE"
cp "$ROOT_DIR/assets/dmg-readme.txt" "$STAGE/OLVASD_EL_ELOSZOR.txt"

DMG_NAME="bpplay-${VERSION}-macos.dmg"
rm -f "$DIST_DIR/$DMG_NAME"

echo "=== Creating $DMG_NAME ==="
hdiutil create -volname "bpplay $VERSION" -srcfolder "$STAGE" -format UDZO "$DIST_DIR/$DMG_NAME"

rm -rf "$STAGE"

echo "=== Checksum ==="
( cd "$DIST_DIR" && shasum -a 256 "$DMG_NAME" > "$DMG_NAME.sha256" )
cat "$DIST_DIR/$DMG_NAME.sha256"

echo ""
echo "Done: $DIST_DIR/$DMG_NAME"
