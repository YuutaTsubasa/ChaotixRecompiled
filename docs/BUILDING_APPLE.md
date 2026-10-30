# macOS and iOS

The Apple builds are the same SDL3 frontend as everywhere else, packaged on a
Mac by one script:

```bash
scripts/package_apple.sh macos VERSION           # ChaotixRecompiled-VERSION-macos-arm64.zip
scripts/package_apple.sh ios VERSION             # ChaotixRecompiled-VERSION-ios-arm64.ipa
scripts/package_apple.sh ios-simulator VERSION   # ...-ios-simulator-arm64.zip (testing only)
```

The output goes to `build/release-VERSION/` (or a third argument). It has
been built and run on an M1 Mac mini with macOS 27.0.1 and Xcode 27.1 (Apple
clang 21, iOS 27.1 SDK), CMake 4.4 and Ninja.

## What the Mac needs

| | |
|---|---|
| Xcode | with its licence accepted (`sudo xcodebuild -license accept`); the iOS SDK for `ios`, the Simulator runtime for testing |
| CMake ≥ 3.20, Ninja | e.g. from Homebrew or `pip install cmake ninja` |
| This repository | `git archive HEAD` from any checkout is enough |
| SDL 3 source | `release-3.2.24` was used; set `SDL3_SOURCE_DIR` |
| SDL_ttf source | `release-3.2.2`, cloned with `--recurse-submodules` (FreeType, HarfBuzz, plutosvg are vendored); set `SDL3_TTF_SOURCE_DIR` |
| The game code | `generated/` from a build with the ROM, see below |

Both SDL libraries are linked in statically, so the `.app` needs nothing from
Homebrew and has no frameworks inside to sign.

### The generated code

The recompiler turns the user's ROM into C++ in `generated/`. Cross builds
(iOS) cannot run it, so `generated/` must be filled before packaging:

- on any desktop host, configure with `-DCHAOTIX_ROM=...` and build once; then
  copy that `generated/` into the checkout on the Mac (it is `.gitignore`d,
  so `git archive` leaves it out). The output is the same on every host:
  Windows' and the Mac's differ only in line endings.
- or, for `macos` only, set `CHAOTIX_ROM=/path/to/chaotix.32x` when running
  the script and the build generates it on the Mac.

The ROM itself never goes into the package: the app asks for it on first start.

## Example: from a Windows checkout to the Mac

```bash
# On Windows (Git Bash): the sources, SDL, and the generated code
git archive --prefix=src/ HEAD | ssh mac 'mkdir -p ~/chaotix && tar -C ~/chaotix -xf -'
tar czf - --exclude=.git -C /path/to/_thirdparty SDL3 SDL_ttf | ssh mac 'mkdir -p ~/chaotix/thirdparty && tar -C ~/chaotix/thirdparty -xzf -'
tar czf - generated | ssh mac 'tar -C ~/chaotix/src -xzf -'

# On the Mac
cd ~/chaotix/src
export SDL3_SOURCE_DIR=~/chaotix/thirdparty/SDL3 SDL3_TTF_SOURCE_DIR=~/chaotix/thirdparty/SDL_ttf
scripts/package_apple.sh macos 0.5.2
scripts/package_apple.sh ios 0.5.2
```

`scp` from Git Bash needs `MSYS_NO_PATHCONV=1` for remote paths. Each
platform builds in its own directory (`build/apple-macos`, `build/apple-ios`,
`build/apple-ios-simulator`; `BUILD_ROOT` moves them); a rebuild only compiles
what changed.

## What the script makes

**macOS** — `ChaotixRecompiled-VERSION-macos-arm64.zip` (about 4.5 MB):

```
ChaotixRecompiled-VERSION-macos-arm64/
  ChaotixRecompiled.app        signed ad hoc; Contents/Resources/AppIcon.icns
  READ-ME-FIRST.txt            first start (Gatekeeper), where the files are
  README.md LEGAL.md LICENSE THIRD_PARTY.md
  licenses/                    SDL3, SDL_ttf, FreeType, HarfBuzz, plutosvg, plutovg, the fonts' OFL
```

Bundle id `org.chaotixrecompiled.app`, macOS 11 or later, Apple Silicon only.
The `Info.plist` comes from `platforms/apple/macos/Info.plist.in` (CMake fills
in the version, `CHAOTIX_APP_VERSION`), with `NSHighResolutionCapable` and the
texts macOS shows when the setup page first looks in Downloads, Documents and
the Desktop. The icon is `assets/icon/icon.png` scaled by `sips` and packed by
`iconutil`. User data is SDL's preference folder,
`~/Library/Application Support/ChaotixRecompiled/ChaotixRecompiled/`
(`Config/`, `SaveData/`, `Game/`, `Mods/`), as on the other desktops. The
setup page lists ROMs in Downloads, Documents and on the Desktop; Browse opens
the system's Open panel, and a ROM dropped on the window installs too.

The app is not notarised, so Gatekeeper refuses a double-click the first time.
Right-click (Control-click) the app > Open > Open; on macOS 15 and later the
dialog may only offer Done, and System Settings > Privacy & Security >
"Open Anyway" is the way. `xattr -dr com.apple.quarantine ChaotixRecompiled.app`
does the same from Terminal. The READ-ME-FIRST says so too.

**iOS** — `ChaotixRecompiled-VERSION-ios-arm64.ipa` (about 4.5 MB): a zip with
`Payload/ChaotixRecompiled.app`, signed ad hoc. Sideloading tools (AltStore,
SideStore, Sideloadly, or re-signing with your own Apple ID in Xcode) sign it
again with an identity the device accepts; an ad hoc signature alone does not
install on a stock iPhone. iOS / iPadOS 15 or later, arm64, landscape only.

- `platforms/apple/ios/Info.plist.in`: landscape orientations (iPhone and
  iPad), `UILaunchScreen` (without a launch screen iOS runs the app at an old
  phone's size), full screen, no status bar, `UIFileSharingEnabled` and
  `LSSupportsOpeningDocumentsInPlace` (the app's Documents appear in the Files
  app and in Finder's file sharing), the game-controller keys, and
  `UIApplicationSupportsIndirectInputEvents` for an iPad's trackpad.
- The icon: an asset catalog of `icon.png` at every iPhone and iPad size,
  compiled by `xcrun actool` into `Assets.car`; the `Info.plist` keys it names
  are merged in with PlistBuddy.
- The documents and `licenses/` are inside the app.

## Getting the ROM and mods onto an iPhone or iPad

Everything the app keeps is in its Documents, which the Files app shows as
**On My iPhone > Chaotix** (On My iPad on an iPad), and Finder shows under the
device's Files tab:

- Copy the ROM there (any name, e.g. `chaotix.32x`; a `rom` subfolder works
  too). The setup page lists it (tap Rescan if the app was already open);
  tap it, then Install. The ROM is verified and copied to `Game/`.
- Or tap **Browse**: the system's document picker opens, and a ROM chosen from
  anywhere Files can reach (iCloud Drive, a USB drive, another app) is copied
  in. SDL 3 has no file dialog on iOS, so this picker is the app's own
  (`src/platform/ios_document_picker.mm`).
- `Mods/` (music, textures, characters, ROM patches, touch images; see the
  README) is created there on the first start; `Config/chaotix.ini` and
  `SaveData/` are there as well.

Touch controls appear as soon as the screen is touched, as on Android, and the
unlock counter in the top-right corner opens the menu by touch. A controller
(MFi, Xbox, PlayStation) works through SDL.

## Tests on the Mac

The Mac is also a test host. A native build with the ROM runs the whole suite,
including the lockstep runs that compare the recompiled code with the
interpreters instruction by instruction:

```bash
cmake -S . -B build-test -G Ninja -DCMAKE_BUILD_TYPE=Release -DCHAOTIX_ROM=$HOME/chaotix/rom/chaotix.32x \
    -DCHAOTIX_SDL3_SOURCE_DIR=$SDL3_SOURCE_DIR -DCHAOTIX_SDL3_TTF_SOURCE_DIR=$SDL3_TTF_SOURCE_DIR
ninja -C build-test
ctest --test-dir build-test -j4 --output-on-failure
build-test/chaotix_headless --rom ~/chaotix/rom/chaotix.32x --frames 1500 --lockstep \
    --stage 300:0:1:0:2:7:0 --fuzz 3:700:1500        # must print "bit-identical"
```

On the M1 all 40 tests pass (Apple clang 21, arm64), including the golden frame
hashes recorded on Windows, and lockstep is bit-identical. The recompiler's
output from the same ROM matches the Windows one byte for byte (line endings
aside). Running them there found one test bug, a reference into a vector that
a later push could move; libc++ overwrites freed memory where MinGW did not.

The packaged app can be run from a terminal, also over ssh while someone is
logged in at the Mac (the Metal renderer then works); `--autotest N
--autotest-shot FILE` runs N frames and saves the picture:

```bash
APP=ChaotixRecompiled.app/Contents/MacOS/ChaotixRecompiled
$APP --user-dir /tmp/chx --autotest 1100 --stage 300:0:1:0:2:7:0 --autotest-shot /tmp/level.png ~/chaotix/rom/chaotix.32x
CHAOTIX_SETUP_SHOT=/tmp/setup.png $APP --user-dir /tmp/chx-empty   # the setup page
```

`SDL_VIDEO_DRIVER=dummy SDL_RENDER_DRIVER=software` works where no window can
be opened. `screencapture` does not work over ssh (no permission).

### The iOS Simulator

```bash
scripts/package_apple.sh ios-simulator 0.5.2
UDID=$(xcrun simctl create "Chaotix iPhone" "iPhone 17" com.apple.CoreSimulator.SimRuntime.iOS-27-0)
xcrun simctl boot $UDID
ditto -x -k build/release-0.5.2/ChaotixRecompiled-0.5.2-ios-simulator-arm64.zip /tmp/sim
xcrun simctl install $UDID /tmp/sim/ChaotixRecompiled.app
DOCS=$(xcrun simctl get_app_container $UDID org.chaotixrecompiled.app data)/Documents
cp ~/chaotix/rom/chaotix.32x "$DOCS/"
xcrun simctl launch --console-pty $UDID org.chaotixrecompiled.app     # the setup page lists the ROM
xcrun simctl io $UDID screenshot /tmp/sim.png
# Arguments pass through: install the ROM and go straight into a level with touch controls
xcrun simctl launch $UDID org.chaotixrecompiled.app "$DOCS/chaotix.32x" --touch --stage 120:0:1:0:2:7:0
```

Verified there: the setup page in landscape, finding a ROM put in Documents,
installing it, the front end, a level with the touch controls and the unlock
counter, `Mods/` created in Documents, and Browse presenting the document
picker. Nothing can be tapped over ssh, so picking a file in the picker was
not tried.

## Pitfalls

- **iOS 27 and UIScene.** iOS 27 stops, at launch, an app linked against its
  SDK unless the app adopts the UIScene life cycle, which SDL 3.2 does not.
  Which SDK counts is what the program says it was linked against, so the
  link names iOS 18 (`-Wl,-platform_version,ios,<min>,18.0`, and
  `ios-simulator` for the Simulator; `otool -l` shows `sdk 18.0`).
- **Minimum versions.** Xcode 27's libc++ warns below macOS 11 and iOS 15, so
  those are the deployment targets (`CMAKE_OSX_DEPLOYMENT_TARGET`). A build
  directory keeps the value it was first configured with.
- **Signing.** Ad hoc only (`codesign --sign -`), which Apple Silicon needs to
  run anything. SDL is linked in, so only the app itself is signed; anything
  added inside the bundle later must be signed before the app (inside out).
  The packaged copy has no build-tree `LC_RPATH`.
- **stderr.** The log goes to stderr; it can be fully buffered when it is
  not a terminal, so the end of it may be lost if the app crashes. Crash
  reports are in `~/Library/Logs/DiagnosticReports/*.ips` (JSON after the
  first line), the Simulator's included.
- **Generated code from Windows** has CRLF line endings; that does not matter.

## Not done

- Not notarised (that needs a paid Apple developer account); Gatekeeper's
  first-start step above remains.
- Untested on a real iPhone or iPad: no device was available. The Simulator
  runs the same arm64 code with a Simulator SDK.
- arm64 only. An Intel or universal Mac build should only need
  `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`, but has not been tried.
- The picture and the touch controls use the whole screen, including the
  corners and the side where an iPhone's camera housing is in landscape; the
  safe area is not respected yet.
