#!/bin/bash
# Build AntennaSwitchExtension.appex and package it as `AntennaSwitch.radioplugin` -- the
# installable bundle the Amateur Radio Suite browses + installs (zip of plugin.json + .appex).
#
#   App/scripts/make-radioplugin.sh                 # -> App/dist/AntennaSwitch.radioplugin (+ sha256)
#   VERSION=0.1.35 App/scripts/make-radioplugin.sh  # release: the plugin reports 0.1.35
#
# The version (scripts/version.sh: VERSION, else the latest git tag) is stamped into both
# places the plugin reports one -- the .appex Info.plist (MARKETING_VERSION /
# CURRENT_PROJECT_VERSION) and plugin.json's "version" (the __VERSION__ placeholder) --
# so it always matches the app. The script stops if either is off.
#
# Running the installed plugin needs Developer-ID signing + extension approval; this
# produces an ad-hoc-signed package suitable for the catalog/discovery flow and testing.
set -euo pipefail
cd "$(dirname "$0")/.."        # -> App/ (the SwiftPM package root)

XCODE_DIR="Xcode"
PROJECT="$XCODE_DIR/AntennaSwitchPlugin.xcodeproj"
SCHEME="AntennaSwitchExtension"
DIST="dist"
PKG="$DIST/AntennaSwitch.radioplugin"
source scripts/version.sh

# SwiftPM's bare-repo cache trips `safe.bareRepository=explicit` (a common global git
# setting) under xcodebuild; allow it for child git processes without touching config.
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.bareRepository GIT_CONFIG_VALUE_0=all

echo "==> Generating Xcode project from project.yml"
( cd "$XCODE_DIR" && xcodegen generate )

echo "==> Building $SCHEME (.appex) -- version $VERSION"
DERIVED="$(mktemp -d)"
xcodebuild -project "$PROJECT" -scheme "$SCHEME" -configuration Release \
  -destination 'platform=macOS' -derivedDataPath "$DERIVED" \
  MARKETING_VERSION="$VERSION" CURRENT_PROJECT_VERSION="$VERSION" \
  CODE_SIGNING_ALLOWED=NO build >/dev/null

APPEX="$(/usr/bin/find "$DERIVED/Build/Products" -name 'AntennaSwitchExtension.appex' -maxdepth 3 | head -1)"
[ -n "$APPEX" ] || { echo "ERROR: AntennaSwitchExtension.appex not found"; exit 1; }
for key in CFBundleShortVersionString CFBundleVersion; do
  BUILT="$(/usr/libexec/PlistBuddy -c "Print :$key" "$APPEX/Contents/Info.plist")"
  [ "$BUILT" = "$VERSION" ] || { echo "ERROR: .appex $key is '$BUILT', expected '$VERSION'"; exit 1; }
done

echo "==> Assembling $PKG"
STAGE="$(mktemp -d)"
sed "s/__VERSION__/${VERSION}/g" "Xcode/Extension/plugin.json" > "$STAGE/plugin.json"
grep -q "\"version\": \"${VERSION}\"" "$STAGE/plugin.json" || {
  echo "ERROR: plugin.json version was not stamped (is the __VERSION__ placeholder intact?)"; exit 1; }
cp -R "$APPEX" "$STAGE/"

# Only manage our own package -- do NOT wipe $DIST, which in a release pipeline also
# holds the standalone .app built by build-app.sh.
mkdir -p "$DIST"
rm -f "$PKG"
PKG_ABS="$PWD/$PKG"
( cd "$STAGE" && ditto -c -k --norsrc --noextattr . "$PKG_ABS" )
rm -rf "$STAGE" "$DERIVED"

SHA="$(shasum -a 256 "$PKG" | awk '{print $1}')"
echo "OK: built $PKG -- plugin version $VERSION (.appex + plugin.json)"
echo "    sha256: $SHA"
