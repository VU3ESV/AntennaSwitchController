#!/bin/bash
# Build "Antenna Switch Controller.app" — a double-clickable macOS bundle.
#
#   ./build-app.sh              # release build for the host arch into ./dist
#   UNIVERSAL=1 ./build-app.sh  # universal (arm64 + x86_64) — used by CI/release
#   open "dist/Antenna Switch Controller.app"
set -euo pipefail

cd "$(dirname "$0")"

APP_NAME="Antenna Switch Controller"
BINARY="AntennaSwitchController"
DIST="dist"
BUNDLE="$DIST/$APP_NAME.app"

# Universal builds (both Apple Silicon and Intel) for distributable releases.
ARCH_FLAGS=()
if [ "${UNIVERSAL:-0}" = "1" ]; then
  ARCH_FLAGS=(--arch arm64 --arch x86_64)
fi

echo "▶ Building release binary… ${ARCH_FLAGS[*]:-(host arch)}"
swift build -c release "${ARCH_FLAGS[@]}"

BIN_PATH="$(swift build -c release "${ARCH_FLAGS[@]}" --show-bin-path)/$BINARY"

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
