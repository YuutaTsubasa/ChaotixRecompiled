#!/bin/bash
# Builds and packages the Apple releases, on a Mac with Xcode, CMake and Ninja.
#
#   scripts/package_apple.sh macos|ios|ios-simulator VERSION [OUTPUT_DIR]
#
#   macos          ChaotixRecompiled-VERSION-macos-arm64.zip: the .app, signed
#                  ad hoc, with the read-me, licences and credits beside it
#   ios            ChaotixRecompiled-VERSION-ios-arm64.ipa: for sideloading,
#                  signed ad hoc (sideloading tools sign it again)
#   ios-simulator  ChaotixRecompiled-VERSION-ios-simulator-arm64.zip: the same
#                  app for the iOS Simulator, for testing only
#
# Environment:
#   SDL3_SOURCE_DIR, SDL3_TTF_SOURCE_DIR  SDL 3 and SDL_ttf source checkouts
#       (SDL_ttf with its vendored submodules); both are linked in statically
#   CHAOTIX_ROM   macos only, optional: generate the game code from this ROM
#       during the build. Otherwise generated/ must hold code generated
#       beforehand (on any host: the output is the same everywhere).
#   BUILD_ROOT    where the build directories go (default: build/ here)
#
# docs/BUILDING_APPLE.md describes the whole procedure.
set -euo pipefail

usage() { echo "usage: $0 macos|ios|ios-simulator VERSION [OUTPUT_DIR]" >&2; exit 2; }
[ $# -ge 2 ] || usage
PLATFORM=$1
VERSION=$2
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUTPUT=${3:-$ROOT/build/release-$VERSION}
BUILD_ROOT=${BUILD_ROOT:-$ROOT/build}
: "${SDL3_SOURCE_DIR:?set SDL3_SOURCE_DIR to an SDL 3 source checkout}"
: "${SDL3_TTF_SOURCE_DIR:?set SDL3_TTF_SOURCE_DIR to an SDL_ttf source checkout}"
[ "$(uname)" = Darwin ] || { echo "Apple packages are made on a Mac" >&2; exit 1; }

case $PLATFORM in
macos)
    BUILD=$BUILD_ROOT/apple-macos
    NAME=ChaotixRecompiled-$VERSION-macos-arm64
    ARGS=(-DCMAKE_OSX_ARCHITECTURES=arm64 -DCHAOTIX_BUILD_TESTS=OFF)
    if [ -n "${CHAOTIX_ROM:-}" ]; then
        ARGS+=(-DCHAOTIX_ROM="$CHAOTIX_ROM" -DCHAOTIX_BUILD_TOOLS=ON)
    else
        ARGS+=(-DCHAOTIX_ROM= -DCHAOTIX_BUILD_TOOLS=OFF)
    fi
    ;;
ios|ios-simulator)
    if [ "$PLATFORM" = ios ]; then
        SDK=iphoneos
        NAME=ChaotixRecompiled-$VERSION-ios-arm64
    else
        SDK=iphonesimulator
        NAME=ChaotixRecompiled-$VERSION-ios-simulator-arm64
    fi
    BUILD=$BUILD_ROOT/apple-$PLATFORM
    ARGS=(-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=$SDK -DCMAKE_OSX_ARCHITECTURES=arm64)
    ;;
*) usage ;;
esac
if [ -z "${CHAOTIX_ROM:-}" ] && [ ! -f "$ROOT/generated/recomp_tables.cpp" ]; then
    echo "generated/ holds no game code: generate it first (docs/BUILDING_APPLE.md)" >&2
    exit 1
fi

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCHAOTIX_APP_VERSION="$VERSION" \
    -DCHAOTIX_SDL3_SOURCE_DIR="$SDL3_SOURCE_DIR" -DCHAOTIX_SDL3_TTF_SOURCE_DIR="$SDL3_TTF_SOURCE_DIR" \
    "${ARGS[@]}"
cmake --build "$BUILD" --target chaotix
MIN_OS=$(sed -n 's/^CMAKE_OSX_DEPLOYMENT_TARGET:[A-Z]*=//p' "$BUILD/CMakeCache.txt")
mkdir -p "$OUTPUT"
OUTPUT=$(cd "$OUTPUT" && pwd)

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
ICON=$ROOT/assets/icon/icon.png  # 256 px; the other platforms' icons come from it

# The licences of what is linked into the program, and the fonts embedded in it.
copy_licences() {
    local dir=$1
    mkdir -p "$dir"
    cp "$ROOT/assets/fonts/OFL-Inter.txt" "$ROOT/assets/fonts/OFL-PressStart2P.txt" "$dir/"
    cp "$SDL3_SOURCE_DIR/LICENSE.txt" "$dir/SDL3-LICENSE.txt"
    cp "$SDL3_TTF_SOURCE_DIR/LICENSE.txt" "$dir/SDL_ttf-LICENSE.txt"
    cp "$SDL3_TTF_SOURCE_DIR/external/freetype/docs/FTL.TXT" "$dir/FreeType-FTL.txt"
    cp "$SDL3_TTF_SOURCE_DIR/external/harfbuzz/COPYING" "$dir/HarfBuzz-COPYING.txt"
    cp "$SDL3_TTF_SOURCE_DIR/external/plutosvg/LICENSE" "$dir/plutosvg-LICENSE.txt"
    cp "$SDL3_TTF_SOURCE_DIR/external/plutovg/LICENSE" "$dir/plutovg-LICENSE.txt"
}

copy_documents() {
    local dir=$1
    for f in README.md LEGAL.md LICENSE THIRD_PARTY.md; do cp "$ROOT/$f" "$dir/"; done
    copy_licences "$dir/licenses"
}

# The build directory's own library paths have no place in a release, and
# would only carry the builder's folder names.
drop_build_rpaths() {
    local binary=$1 path
    otool -l "$binary" | awk '$1 == "path" && substr($2, 1, 1) == "/" { print $2 }' | while read -r path; do
        install_name_tool -delete_rpath "$path" "$binary"
    done
}

if [ "$PLATFORM" = macos ]; then
    APP=$STAGE/$NAME/ChaotixRecompiled.app
    mkdir -p "$STAGE/$NAME"
    ditto "$BUILD/ChaotixRecompiled.app" "$APP"
    drop_build_rpaths "$APP/Contents/MacOS/ChaotixRecompiled"
    # The icon, at the sizes iconutil wants, from the 256 px PNG.
    ICONSET=$STAGE/AppIcon.iconset
    mkdir -p "$ICONSET" "$APP/Contents/Resources"
    for size in 16 32 64 128 256; do
        sips -z $size $size "$ICON" --out "$ICONSET/icon_${size}x${size}.png" > /dev/null
        if [ $((size * 2)) -le 256 ]; then
            sips -z $((size * 2)) $((size * 2)) "$ICON" --out "$ICONSET/icon_${size}x${size}@2x.png" > /dev/null
        fi
    done
    iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
    copy_documents "$STAGE/$NAME"
    sed "s/@VERSION@/$VERSION/g" "$ROOT/platforms/apple/macos/READ-ME-FIRST.txt" > "$STAGE/$NAME/READ-ME-FIRST.txt"
    # Not notarised: signed ad hoc, which Apple Silicon needs to run it at all.
    # Nothing is nested inside (SDL is linked in), so one signature covers it.
    codesign --force --sign - --timestamp=none "$APP"
    codesign --verify --strict --verbose=1 "$APP"
    mkdir -p "$OUTPUT"
    OUT=$OUTPUT/$NAME.zip
    rm -f "$OUT"
    # ditto keeps the bundle's signature, symlinks and modes intact.
    ditto -c -k --keepParent "$STAGE/$NAME" "$OUT"
else
    APP=$STAGE/Payload/ChaotixRecompiled.app
    mkdir -p "$STAGE/Payload"
    ditto "$BUILD/ChaotixRecompiled.app" "$APP"
    drop_build_rpaths "$APP/ChaotixRecompiled"
    # The icon: an asset catalog of the PNG at every size Xcode lists for
    # iPhone and iPad, compiled by actool into Assets.car (and the PNGs older
    # readers look for); actool reports the Info.plist keys that name it.
    CATALOG=$STAGE/Assets.xcassets
    ICONS=$CATALOG/AppIcon.appiconset
    mkdir -p "$ICONS"
    echo '{"info":{"author":"xcode","version":1}}' > "$CATALOG/Contents.json"
    images=""
    for entry in iphone:20:2,3 iphone:29:2,3 iphone:40:2,3 iphone:60:2,3 \
                 ipad:20:1,2 ipad:29:1,2 ipad:40:1,2 ipad:76:1,2 ipad:83.5:2 ios-marketing:1024:1; do
        IFS=: read -r idiom points scales <<< "$entry"
        for scale in ${scales//,/ }; do
            pixels=$(awk "BEGIN { printf \"%d\", $points * $scale + 0.5 }")
            [ -f "$ICONS/icon-$pixels.png" ] || sips -z $pixels $pixels "$ICON" --out "$ICONS/icon-$pixels.png" > /dev/null
            images+="${images:+,}{\"filename\":\"icon-$pixels.png\",\"idiom\":\"$idiom\",\"scale\":\"${scale}x\",\"size\":\"${points}x${points}\"}"
        done
    done
    echo "{\"images\":[$images],\"info\":{\"author\":\"xcode\",\"version\":1}}" > "$ICONS/Contents.json"
    xcrun actool "$CATALOG" --compile "$APP" --platform $SDK --minimum-deployment-target "$MIN_OS" \
        --app-icon AppIcon --target-device iphone --target-device ipad \
        --output-partial-info-plist "$STAGE/icon.plist" --errors --warnings > /dev/null
    /usr/libexec/PlistBuddy -c "Merge $STAGE/icon.plist" "$APP/Info.plist"
    copy_documents "$APP"
    # Signed ad hoc and inside out (only the program here: SDL is linked in).
    codesign --force --sign - --timestamp=none "$APP"
    codesign --verify --strict --verbose=1 "$APP"
    mkdir -p "$OUTPUT"
    if [ "$PLATFORM" = ios ]; then
        # An .ipa is a zip with Payload/ at its root.
        OUT=$OUTPUT/$NAME.ipa
        rm -f "$OUT"
        ditto -c -k --keepParent "$STAGE/Payload" "$OUT"
    else
        OUT=$OUTPUT/$NAME.zip
        rm -f "$OUT"
        ditto -c -k --keepParent "$APP" "$OUT"
    fi
fi

echo "$(shasum -a 256 "$OUT" | cut -d' ' -f1)  $OUT  ($(du -h "$OUT" | cut -f1))"
