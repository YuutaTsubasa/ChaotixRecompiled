# Android build

Status: **configuration written, not yet built or run on a device** (the
development machine used so far has no Android SDK/NDK).

1. On a desktop host, build once with your ROM so `generated/` exists:
   `cmake --preset windows-ninja` (or `linux` / `macos-universal`) with `CHAOTIX_ROM` set.
2. Get the SDL3 source (same version as used on desktop, ≥ 3.2).
3. Build:
   ```bash
   cd platforms/android
   gradle assembleRelease -PSDL3_SOURCE_DIR=/path/to/SDL
   ```
4. Copy your ROM to the device into the app's documents/external files folder
   (`Android/data/org.chaotixrecompiled/files/`), or into a `rom/` folder there.

Touch controls appear automatically; Bluetooth gamepads work through SDL.
