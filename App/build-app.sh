#!/bin/bash
# Build "Antenna Switch Controller.app" — a double-clickable macOS bundle.
#
#   ./build-app.sh              # release build for the host arch into ./dist
#   UNIVERSAL=1 ./build-app.sh  # universal (arm64 + x86_64) — used by CI/release
#   open "dist/Antenna Switch Controller.app"
set -euo pipefail

cd "$(dirname "$0")"

APP_NAME="Antenna Switch Controller"
BINARY="AntennaSwitchControllerApp"   # executable product name (see Package.swift)
DIST="dist"
BUNDLE="$DIST/$APP_NAME.app"

# Version stamped into Info.plist. The release pipeline passes the tag it is
# about to publish (VERSION=0.1.3); a local build falls back to `git describe`
# so the About box still shows something meaningful.
if [ -z "${VERSION:-}" ]; then
  VERSION="$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//')"
  VERSION="${VERSION:-0.0.0-dev}"
fi

# Universal builds (both Apple Silicon and Intel) for distributable releases.
# NB: empty-array expansion must be `${a[@]+"${a[@]}"}` to stay safe under
# `set -u` on macOS's bash 3.2 (a bare "${a[@]}" errors when the array is empty).
ARCH_FLAGS=()
if [ "${UNIVERSAL:-0}" = "1" ]; then
  ARCH_FLAGS=(--arch arm64 --arch x86_64)
  echo "▶ Building universal release binary (arm64 + x86_64)…"
else
  echo "▶ Building release binary (host arch)…"
fi

# Build ONLY the executable product. Building all products multi-arch makes the
# shared library target compile under two products at once, which trips
# XCBuild's "Multiple commands produce …" duplicate-output bug.
swift build -c release --product "$BINARY" ${ARCH_FLAGS[@]+"${ARCH_FLAGS[@]}"}

BIN_PATH="$(swift build -c release --product "$BINARY" ${ARCH_FLAGS[@]+"${ARCH_FLAGS[@]}"} --show-bin-path)/$BINARY"

echo "▶ Assembling app bundle…"
rm -rf "$BUNDLE"
mkdir -p "$BUNDLE/Contents/MacOS" "$BUNDLE/Contents/Resources"
cp "$BIN_PATH" "$BUNDLE/Contents/MacOS/$BINARY"
echo "  stamping version ${VERSION}"
sed "s/__VERSION__/${VERSION}/g" Resources/Info.plist > "$BUNDLE/Contents/Info.plist"
if [ -f Resources/AppIcon.icns ]; then
  cp Resources/AppIcon.icns "$BUNDLE/Contents/Resources/AppIcon.icns"
fi

# Ad-hoc code signature so Gatekeeper lets it run locally.
codesign --force --deep --sign - "$BUNDLE" >/dev/null 2>&1 || \
  echo "  (codesign skipped — app still runs locally)"

echo "✓ Built: $BUNDLE"
echo "  Launch with:  open \"$BUNDLE\""
