#!/usr/bin/env bash
# Creates a distributable DMG with "Antenna Switch Controller.app" inside and an
# /Applications symlink. Build the app first with ./build-app.sh.
# Usage: App/scripts/make-dmg.sh   ->   App/dist/Antenna-Switch-Controller-macOS.dmg

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"      # -> App/
DIST="$ROOT/dist"
APP="$DIST/Antenna Switch Controller.app"
VERSION="${VERSION:-}"
DMG="$DIST/Antenna-Switch-Controller-macOS.dmg"

test -d "$APP" || { echo "Build the app first: ./build-app.sh" >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

echo "==> Staging DMG contents at $STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"

rm -f "$DMG"

echo "==> Creating DMG at $DMG"
hdiutil create \
    -volname "Antenna Switch Controller${VERSION:+ $VERSION}" \
    -srcfolder "$STAGE" \
    -ov -format UDZO \
    "$DMG" >/dev/null

ls -lh "$DMG" | awk '{ printf "    %s\n", $0 }'
echo "==> Wrote $DMG"
