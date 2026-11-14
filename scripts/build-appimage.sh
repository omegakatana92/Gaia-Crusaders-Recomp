#!/usr/bin/env bash
# Build the Steam Deck / Linux AppImage from a release build.
#
#   KIND          generic (default) or recompiled
#   BUILD_DIR     build directory holding bin/GaiaCrusaders (default build/linux-$KIND)
#   APPIMAGETOOL  path to appimagetool (default: on PATH)
#
# The release build should link SDL2 statically against an old glibc baseline;
# see docs/BUILD_LINUX.md ("Release build").  No ROM data is packaged.
# A recompiled build contains code translated from YOUR ROM: keep it private.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

KIND=${KIND:-generic}
BUILD_DIR=${BUILD_DIR:-build/linux-$KIND}
APPDIR=${APPDIR:-build/AppDir-$KIND}
DIST=${DIST:-dist}
TOOL=${APPIMAGETOOL:-appimagetool}
if [[ $KIND == recompiled ]]; then OUT=GaiaCrusaders-Recompiled-SteamDeck-x86_64-PRIVATE.AppImage; else OUT=GaiaCrusaders-SteamDeck-x86_64.AppImage; fi

if [[ ! -x "$BUILD_DIR/bin/GaiaCrusaders" ]]; then
  echo "error: $BUILD_DIR/bin/GaiaCrusaders not found; build first" >&2
  exit 1
fi

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/gaia/profiles" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps" "$APPDIR/usr/share/doc/gaia" "$DIST"
cp "$BUILD_DIR/bin/GaiaCrusaders" "$APPDIR/usr/bin/gaia"
strip "$APPDIR/usr/bin/gaia" 2>/dev/null || true
cp profiles/gaia.json "$APPDIR/usr/share/gaia/profiles/"
cp packaging/appimage/gaia.desktop "$APPDIR/gaia.desktop"
cp packaging/appimage/gaia.png "$APPDIR/gaia.png"
cp packaging/appimage/AppRun "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun" "$APPDIR/usr/bin/gaia"
cp packaging/appimage/gaia.png "$APPDIR/usr/share/icons/hicolor/256x256/apps/gaia.png"
cp packaging/appimage/gaia.desktop "$APPDIR/usr/share/applications/gaia.desktop"
cp -r LICENSES LICENSE README.md docs/CONTROLS.md "$APPDIR/usr/share/doc/gaia/"

if ldd "$APPDIR/usr/bin/gaia" | grep -q SDL2; then
  echo "warning: the binary links SDL2 dynamically; the host must provide libSDL2-2.0.so.0" >&2
fi

ARCH=x86_64 $TOOL --no-appstream "$APPDIR" "$DIST/$OUT"
echo "wrote $DIST/$OUT (no ROM data included)"
