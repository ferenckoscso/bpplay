#!/bin/bash
#
# build-quickaction.sh — reproducibly builds "bpplay play.workflow",
# the self-contained Finder Quick Action (right-click a file, several
# files, or a folder -> "bpplay play"), bundled in the DMG since
# v0.9.3.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: run from anywhere; requires an already-built src/bpplay
# (run `make universal` in src/ first). Output: dist/bpplay play.workflow
#
# The Automator document.wflow skeleton (tools/bpplay-play.workflow-template/)
# was created once by hand in Automator.app (Quick Action, "files or
# folders" in Finder, Run Shell Script / Pass input: as arguments) and
# is tracked as-is -- this script only swaps its COMMAND_STRING and
# adds the bundled binary + runtime script, via PlistBuddy (safer than
# hand-editing the XML).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS_DIR="$ROOT_DIR/tools"
DIST_DIR="$ROOT_DIR/dist"
BPPLAY_BIN="$ROOT_DIR/src/bpplay"
WORKFLOW_NAME="bpplay play.workflow"
TEMPLATE_DIR="$TOOLS_DIR/bpplay-play.workflow-template"

if [ ! -x "$BPPLAY_BIN" ]; then
    echo "error: $BPPLAY_BIN not found or not executable -- build it first:"
    echo "  (cd src && make universal)"
    exit 1
fi

mkdir -p "$DIST_DIR"
rm -rf "$DIST_DIR/$WORKFLOW_NAME"
mkdir -p "$DIST_DIR/$WORKFLOW_NAME"
cp -R "$TEMPLATE_DIR/Contents" "$DIST_DIR/$WORKFLOW_NAME/Contents"

WFLOW="$DIST_DIR/$WORKFLOW_NAME/Contents/document.wflow"

COMMAND_FILE="$(mktemp)"
trap 'rm -f "$COMMAND_FILE"' EXIT
cat > "$COMMAND_FILE" <<'SCRIPT_EOF'
WORKFLOW_NAME="bpplay play.workflow"
for candidate in "$HOME/Library/Services/$WORKFLOW_NAME/Contents/Resources/bpplay-quickaction-runtime.sh" "/Library/Services/$WORKFLOW_NAME/Contents/Resources/bpplay-quickaction-runtime.sh"; do
    if [ -x "$candidate" ]; then
        exec "$candidate" "$@"
    fi
done
osascript -e 'display alert "bpplay play" message "Could not find bpplay-quickaction-runtime.sh -- was bpplay play.workflow installed at ~/Library/Services/ under its original name?"'
exit 1
SCRIPT_EOF

python3 "$TOOLS_DIR/set-quickaction-command.py" "$WFLOW" "$COMMAND_FILE"

mkdir -p "$DIST_DIR/$WORKFLOW_NAME/Contents/Resources"
cp "$BPPLAY_BIN" "$DIST_DIR/$WORKFLOW_NAME/Contents/Resources/bpplay"
cp "$TOOLS_DIR/bpplay-quickaction-runtime.sh" "$DIST_DIR/$WORKFLOW_NAME/Contents/Resources/bpplay-quickaction-runtime.sh"
chmod +x "$DIST_DIR/$WORKFLOW_NAME/Contents/Resources/bpplay-quickaction-runtime.sh"

# Sign the bundle with the real Developer ID identity when available (needed
# for notarization -- see build-dmg.sh), falling back to an ad-hoc signature
# otherwise. The bundled bpplay binary is signed separately by build-dmg.sh
# before this script runs. --deep is required here (unlike the drop.app) --
# without it codesign refuses with "code object is not signed at all" on
# Contents/document.wflow, since a .workflow has no CFBundleExecutable/
# CFBundlePackageType and codesign's bundle-format detection treats that
# loose top-level file as nested code needing its own signature otherwise.
SIGN_IDENTITY="Developer ID Application: Ferenc Koscso (M4D4ZUXPH6)"
if security find-identity -v -p codesigning 2>/dev/null | grep -q "$SIGN_IDENTITY"; then
    codesign --force --deep --options runtime --timestamp -s "$SIGN_IDENTITY" "$DIST_DIR/$WORKFLOW_NAME"
else
    codesign --force --deep -s - "$DIST_DIR/$WORKFLOW_NAME"
fi

echo "Built: $DIST_DIR/$WORKFLOW_NAME"
