#!/usr/bin/env bash
# Build "Antenna Switch Controller.app" with the AntennaSwitch ExtensionKit extension EMBEDDED
# and Developer-ID signed, then build the DMG and (when notary creds are present) notarize +
# staple it.
#
# This is what makes an installed app register its extension for the Amateur Radio Suite to
# host (see the suite's docs/EXTENSIONKIT.md). Used by the release workflow; also runnable
# locally. Run from the repo's App/ directory (this script lives in App/scripts/).
#
# Signing is GATED on secrets — with none set it falls back to ad-hoc so the build still
# succeeds (but an ad-hoc app's extension will NOT register on another Mac).
#
#   VERSION=1.0.1 ./scripts/package-signed.sh
#
# Env — build:
#   UNIVERSAL=1             build a universal (arm64 + x86_64) binary (set by CI)
#   VERSION                 version stamped into the app's Info.plist by build-app.sh
# Env — signing (from CI secrets):
#   MACOS_CERT_P12_BASE64   base64 of the AntennaSwitch Developer ID Application .p12 (cert + key)
#   MACOS_CERT_PASSWORD     the .p12 export password
#   KEYCHAIN_PASSWORD       password for the temp keychain (any value)
# Env — notarization (optional; needs the signing cert too):
#   NOTARY_APPLE_ID         Apple ID email
#   NOTARY_TEAM_ID          team id (Y6FT52BKDA)
#   NOTARY_PASSWORD         app-specific password for that Apple ID
set -euo pipefail
cd "$(dirname "$0")/.."          # -> the repo's App/ directory

APP="dist/Antenna Switch Controller.app"
APPEX_NAME="AntennaSwitchExtension.appex"
ENTITLEMENTS="Xcode/Extension/AntennaSwitch.entitlements"
PROJECT="Xcode/AntennaSwitchPlugin.xcodeproj"
SCHEME="AntennaSwitchExtension"
TMP="${RUNNER_TEMP:-$(mktemp -d)}"
# SwiftPM's bare-repo cache trips a common global git setting under xcodebuild.
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.bareRepository GIT_CONFIG_VALUE_0=all

echo "==> Building standalone app"
UNIVERSAL="${UNIVERSAL:-1}" VERSION="${VERSION:-0.0.0-dev}" ./build-app.sh

echo "==> Building the extension (.appex)"
( cd Xcode && xcodegen generate >/dev/null )
DERIVED="$(mktemp -d)"
xcodebuild -project "$PROJECT" -scheme "$SCHEME" -configuration Release \
  -destination 'platform=macOS' -derivedDataPath "$DERIVED" CODE_SIGNING_ALLOWED=NO build >/dev/null
APPEX="$(find "$DERIVED/Build/Products" -name "$APPEX_NAME" | head -1)"

echo "==> Embedding $APPEX_NAME under Contents/Extensions/"
mkdir -p "$APP/Contents/Extensions"
rm -rf "$APP/Contents/Extensions/$APPEX_NAME"
cp -R "$APPEX" "$APP/Contents/Extensions/"
rm -rf "$DERIVED"

# --- import the signing cert into a throwaway keychain (CI) -----------------------------
IDENTITY=""
if [ -n "${MACOS_CERT_P12_BASE64:-}" ]; then
  echo "==> Importing Developer ID certificate into a temporary keychain"
  KC="$TMP/ars-signing.keychain-db"
  KCPW="${KEYCHAIN_PASSWORD:-ars-ci-temp}"
  security create-keychain -p "$KCPW" "$KC"
  security set-keychain-settings -lut 21600 "$KC"
  security unlock-keychain -p "$KCPW" "$KC"
  echo "$MACOS_CERT_P12_BASE64" | base64 --decode > "$TMP/cert.p12"
  security import "$TMP/cert.p12" -k "$KC" -P "${MACOS_CERT_PASSWORD:-}" -T /usr/bin/codesign
  security set-key-partition-list -S apple-tool:,apple: -s -k "$KCPW" "$KC" >/dev/null
  # Make the temp keychain searchable (keep the existing ones too).
  security list-keychains -d user -s "$KC" $(security list-keychains -d user | tr -d '"')
  rm -f "$TMP/cert.p12"
  # In the fresh keychain there is exactly one Developer ID identity, so name-selection is unambiguous.
  IDENTITY="$(security find-identity -v -p codesigning "$KC" | sed -n 's/.*"\(Developer ID Application: .*\)"/\1/p' | head -1)"
fi

# --- sign inside-out (extension first, then the app) ------------------------------------
if [ -n "$IDENTITY" ]; then
  echo "==> Signing with: $IDENTITY"
  codesign --force -s "$IDENTITY" -o runtime --timestamp \
    --entitlements "$ENTITLEMENTS" "$APP/Contents/Extensions/$APPEX_NAME"
  codesign --force -s "$IDENTITY" -o runtime --timestamp "$APP"
else
  echo "==> WARNING: no MACOS_CERT_P12_BASE64 — ad-hoc signing (extension will NOT register on other Macs)"
  codesign --force -s - --deep "$APP"
fi
codesign --verify --strict --verbose=2 "$APP"

echo "==> Building DMG"
./scripts/make-dmg.sh
DMG="dist/Antenna-Switch-Controller-macOS.dmg"

# --- notarize + staple the DMG ----------------------------------------------------------
if [ -n "$IDENTITY" ] && [ -n "${NOTARY_APPLE_ID:-}" ] && [ -n "${NOTARY_PASSWORD:-}" ]; then
  echo "==> Notarizing $DMG (a few minutes)…"
  xcrun notarytool submit "$DMG" \
    --apple-id "$NOTARY_APPLE_ID" --team-id "${NOTARY_TEAM_ID:-}" --password "$NOTARY_PASSWORD" --wait
  xcrun stapler staple "$DMG"
  echo "==> Notarized + stapled."
else
  echo "==> Skipping notarization (no NOTARY_* secrets) — DMG signed but not notarized."
fi
echo "==> Done: $DMG"
