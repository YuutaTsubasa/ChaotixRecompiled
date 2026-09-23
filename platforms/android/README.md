# Android build

Status: **builds and runs.** Verified on an Android 16 (API 36) emulator with
the `x86_64` ABI: the first-run setup page appears in landscape, finds a ROM
pushed to the app's external files folder, installs it, and the game boots.
`arm64-v8a` builds from the same tree (not yet run on physical hardware).

The APK contains the recompiled game code, which is generated from *your* ROM.
Build it for yourself; **do not distribute the APK** — it carries Sega's
copyrighted code. See [../../LEGAL.md](../../LEGAL.md).

## Prerequisites

| | Version used |
|---|---|
| JDK | 17 (Temurin) |
| Gradle | 8.7 |
| Android Gradle Plugin | 8.5.0 |
| SDK platform | 34 (`compileSdk`/`targetSdk`), `minSdk` 26 |
| NDK | 29.0.13599879 (override with `-PNDK_VERSION=...`) |
| CMake | 3.22.1+, from the SDK |
| SDL3 source | ≥ 3.2, the same checkout the desktop build uses |

`platforms/android/local.properties` must point at your SDK
(`sdk.dir=C\:\Users\you\AppData\Local\Android\Sdk`); it is gitignored.

## Build

Generate the sources once on a desktop host so `generated/` exists (the
Android build compiles the same tree, it does not run the recompiler):

```bash
cmake --preset windows-ninja   # or linux / macos-universal, with CHAOTIX_ROM set
```

Then:

```bash
cd platforms/android
gradle assembleDebug -PSDL3_SOURCE_DIR=/path/to/SDL3 -PABIS=arm64-v8a
```

`-PABIS` is a comma-separated ABI list and defaults to `arm64-v8a`; use
`x86_64` for the emulator, or `arm64-v8a,x86_64` for both (each ABI is a full
compile of the generated code, so building one at a time is much faster).
`gradle assembleRelease` produces an unsigned release APK.

Gradle drives the root `CMakeLists.txt` through `externalNativeBuild`, so the
Android build stays in step with the desktop one; SDL3 itself is compiled from
`-PSDL3_SOURCE_DIR`, whose Java sources also provide `SDLActivity`.

## Getting the ROM onto the device

The setup page searches the app's own storage, so copy your ROM to either of:

```
/sdcard/Android/data/org.chaotixrecompiled/files/
/sdcard/Android/data/org.chaotixrecompiled/files/rom/
```

for example:

```bash
adb push chaotix.32x /sdcard/Android/data/org.chaotixrecompiled/files/
```

Tap the entry, then **INSTALL**: the ROM is copied into the app's private
`Game/` folder and the setup page is skipped on every later launch. **BROWSE**
opens the system file picker instead, and a verified ROM is labelled
`verified Knuckles' Chaotix`.

## Controls

Touch controls (D-pad plus A/B/C and X/Y/Z) are drawn over the game and appear
automatically on a touch screen; Bluetooth and USB gamepads work through SDL.
The app requests landscape.

## Emulator notes

A stock AVD's userdata partition is too small for a debug APK with the
generated code in it — create or restart the emulator with more room:

```bash
emulator -avd Medium_Phone_API_36.0 -wipe-data -partition-size 2047
```

(2047 MB is the maximum `-partition-size` accepts.) Use a `x86_64` system
image and `-PABIS=x86_64`; an `arm64-v8a` APK will not install on it.
