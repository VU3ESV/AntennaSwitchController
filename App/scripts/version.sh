# Single source of the version stamped into every macOS deliverable. It is sourced
# (not executed), from the App/ directory, by:
#   build-app.sh              -> the standalone .app's Info.plist
#   scripts/package-signed.sh -> the .appex embedded in that .app
#   scripts/make-radioplugin.sh -> the .radioplugin's .appex + plugin.json
# so the suite plugin always reports the same version as the app.
#
# Release CI passes VERSION (the tag being published, e.g. 0.1.35). A local build
# falls back to the latest git tag, then 0.0.0-dev. A leading "v" is dropped.
if [ -z "${VERSION:-}" ]; then
  # `|| true` keeps a tagless checkout (e.g. CI's shallow clone) from tripping
  # `set -e`/`pipefail` when git describe exits non-zero.
  VERSION="$(git describe --tags --abbrev=0 2>/dev/null || true)"
fi
VERSION="${VERSION#v}"
VERSION="${VERSION:-0.0.0-dev}"
export VERSION
