#!/bin/bash
# Build "Antenna Switch Controller.app" — a double-clickable macOS bundle.
#
#   ./build-app.sh            # release build into ./dist
#   open "dist/Antenna Switch Controller.app"
set -euo pipefail

cd "$(dirname "$0")"

APP_NAME="Antenna Switch Controller"
BINARY="AntennaSwitchController"
DIST="dist"
BUNDLE="$DIST/$APP_NAME.app"

echo "▶ Building release binary…"
swift build -c release

BIN_PATH="$(swift build -c release --show-bin-path)/$BINARY"

echo "▶ Assembling app bundle…"
rm -rf "$BUNDLE"
mkdir -p "$BUNDLE/Contents/MacOS" "$BUNDLE/Contents/Resources"
cp "$BIN_PATH" "$BUNDLE/Contents/MacOS/$BINARY"
cp Resources/Info.plist "$BUNDLE/Contents/Info.plist"
if [ -f Resources/AppIcon.icns ]; then
  cp Resources/AppIcon.icns "$BUNDLE/Contents/Resources/AppIcon.icns"
fi

# Ad-hoc code signature so Gatekeeper lets it run locally.
codesign --force --deep --sign - "$BUNDLE" >/dev/null 2>&1 || \
  echo "  (codesign skipped — app still runs locally)"

echo "✓ Built: $BUNDLE"
echo "  Launch with:  open \"$BUNDLE\""
