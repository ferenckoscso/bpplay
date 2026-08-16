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

# Developer ID signing + notarization -- both are skipped (falls back to the
# old ad-hoc/unsigned build) if the identity isn't in the keychain or the
# notarytool credential profile hasn't been set up, so this script still
# works on a machine without them.
SIGN_IDENTITY="Developer ID Application: Ferenc Koscso (M4D4ZUXPH6)"
NOTARY_PROFILE="bpplay-notarize"
CAN_SIGN=0
if security find-identity -v -p codesigning 2>/dev/null | grep -q "$SIGN_IDENTITY"; then
    CAN_SIGN=1
fi
CAN_NOTARIZE=0
if [ "$CAN_SIGN" = "1" ] && xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1; then
    CAN_NOTARIZE=1
fi

echo "=== Building bpplay $VERSION (universal binary) ==="
( cd "$ROOT_DIR/src" && make clean >/dev/null && make universal )

if [ "$CAN_SIGN" = "1" ]; then
    echo "=== Signing bpplay binary (Developer ID, hardened runtime) ==="
    codesign --force --options runtime --timestamp -s "$SIGN_IDENTITY" "$ROOT_DIR/src/bpplay"
else
    echo "=== Skipping Developer ID signing (identity not found) -- ad-hoc/unsigned build ==="
fi

echo "=== Building bpplay drop.app ==="
"$ROOT_DIR/tools/build-drop-app.sh"

echo "=== Building bpplay play.workflow (Quick Action) ==="
"$ROOT_DIR/tools/build-quickaction.sh"

echo "=== Assembling DMG staging area ==="
STAGE="$DIST_DIR/dmg-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$ROOT_DIR/src/bpplay" "$STAGE/bpplay"
"$ROOT_DIR/tools/set-file-icon.sh" "$ROOT_DIR/assets/icons/bpplay_dark.icns" "$STAGE/bpplay"
cp -R "$DIST_DIR/bpplay drop.app" "$STAGE/bpplay drop.app"
cp -R "$DIST_DIR/bpplay play.workflow" "$STAGE/bpplay play.workflow"
cp "$ROOT_DIR/LICENSE" "$STAGE/LICENSE"
cp "$ROOT_DIR/assets/dmg-readme.txt" "$STAGE/OLVASD_EL_ELOSZOR.txt"
cp "$ROOT_DIR/assets/dmg-readme-en.txt" "$STAGE/READ_ME_FIRST.txt"
cp "$ROOT_DIR/assets/remove-old-version-hu.command" "$STAGE/Régi verzió eltávolítása.command"
cp "$ROOT_DIR/assets/remove-old-version-en.command" "$STAGE/Remove old version.command"
chmod +x "$STAGE/Régi verzió eltávolítása.command" "$STAGE/Remove old version.command"

DMG_NAME="bpplay-${VERSION}-macos.dmg"
rm -f "$DIST_DIR/$DMG_NAME"

echo "=== Creating $DMG_NAME ==="
hdiutil create -volname "bpplay $VERSION" -srcfolder "$STAGE" -format UDZO "$DIST_DIR/$DMG_NAME"

rm -rf "$STAGE"

if [ "$CAN_SIGN" = "1" ]; then
    echo "=== Signing $DMG_NAME (Developer ID) ==="
    codesign --force --timestamp -s "$SIGN_IDENTITY" "$DIST_DIR/$DMG_NAME"
fi

if [ "$CAN_NOTARIZE" = "1" ]; then
    echo "=== Submitting $DMG_NAME to Apple notarization (this waits for the result) ==="
    xcrun notarytool submit "$DIST_DIR/$DMG_NAME" --keychain-profile "$NOTARY_PROFILE" --wait
    echo "=== Stapling notarization ticket ==="
    xcrun stapler staple "$DIST_DIR/$DMG_NAME"
    echo "=== Verifying ==="
    spctl -a -t open --context context:primary-signature -v "$DIST_DIR/$DMG_NAME"
elif [ "$CAN_SIGN" = "1" ]; then
    echo "=== Skipping notarization (no '$NOTARY_PROFILE' notarytool credential profile found) ==="
    echo "    Set up once with: xcrun notarytool store-credentials \"$NOTARY_PROFILE\" --apple-id <id> --team-id <team>"
fi

echo "=== Checksum ==="
( cd "$DIST_DIR" && shasum -a 256 "$DMG_NAME" > "$DMG_NAME.sha256" )
cat "$DIST_DIR/$DMG_NAME.sha256"

echo ""
echo "Done: $DIST_DIR/$DMG_NAME"
