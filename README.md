# Knuckles' Chaotix Recompiled

A static recompilation of the Sega 32X game *Knuckles' Chaotix* into native C++.
The 68000 and both SH-2 CPUs' game code is translated ahead of time; a small
hardware compatibility layer (32X/Mega Drive registers, VDPs, interrupts,
timers) plus an SDL3 frontend make it a native program on each platform.

Status: boots to the title screen, menus and save-select work, levels are
playable, with sound (Z80 driver + YM2612 + PSG + PWM; first version). See [ARCHITECTURE.md](ARCHITECTURE.md) and
[docs/MILESTONES.md](docs/MILESTONES.md).

**No game data is included.** You need your own legally obtained ROM of
*Knuckles' Chaotix (Japan, USA)* — SHA-1 `0c2fff7bc79ed26507c08ac47464c3af19f7ced7`.
No 32X BIOS files are needed.

## Build

Requirements: CMake ≥ 3.20, a C++17 compiler, SDL3 (for the playable frontend).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCHAOTIX_ROM=/path/to/chaotix.32x
cmake --build build
```

The build runs `chaotix_recomp` on your ROM (plus the execution traces in
`coverage/`) and compiles the generated C++ from `generated/`. Without
`CHAOTIX_ROM` you get an interpreter-only runtime.

Windows (MSYS2 MinGW64): `pacman -S mingw-w64-x86_64-{gcc,cmake,ninja,sdl3}` and
pass a Windows-style ROM path (`cygpath -m`).

Presets: `cmake --preset windows-msvc | windows-ninja | linux | macos-universal | macos-xcode | ios | android-arm64`
(set the `CHAOTIX_ROM` environment variable). Only `windows-ninja` has been
built and tested so far.

Cross builds (Android/iOS): generate code on a host first (build once with
`CHAOTIX_ROM`), then configure the target build; it picks up `generated/`.
See [platforms/android/README.md](platforms/android/README.md).

## Run

```bash
build/ChaotixRecompiled /path/to/chaotix.32x
```

Or set `RomPath` in the config file, or put the ROM in a `__ROM__` folder
next to the executable. Settings live in the user data directory
(`Config/chaotix.ini`); saves in `SaveData/`.

| Key | Action |
|---|---|
| Arrows | D-pad |
| Z / X / C | A / B / C |
| A / S / D | X / Y / Z |
| Enter | Start |
| F1 / F5 | Debug overlay / overlay page |
| F2 | Aspect ratio (Auto, 4:3, 16:9, 16:10, 21:9) |
| F3 / F4 | Filter / scaling mode |
| F11, Alt+Enter | Borderless fullscreen |
| Tab (hold) | Fast-forward |
| F12 | Screenshot |

Gamepads (Xbox, PlayStation, Nintendo layouts via SDL): West/South/East =
A/B/C, LB/North/RB = X/Y/Z. On touch devices virtual controls appear.

## Tools

- `rom_analyzer --rom <rom> [--coverage f.cov] --out dir` — verification and
  reports (code spaces, functions, xrefs, hardware register usage).
- `chaotix_recomp --rom <rom> --coverage f.cov --out generated` — the recompiler.
- `chaotix_headless --rom <rom> --frames N [--lockstep] [--shot F] [--press F:btn:dur] [--coverage f] [--trace ...]`
  — deterministic headless execution and validation.

## Tests

```bash
cd build && ctest --output-on-failure
```

Unit tests always run; with `CHAOTIX_ROM` set, integration tests compare the
recompiled build against the reference interpreter frame by frame and check
golden frame hashes. `-DCHAOTIX_LONG_TESTS=ON` adds `ctest -L long`
(36,000-frame lockstep and 720 golden render hashes, a few minutes).

Coverage traces for the recompiler are recorded with
`python tools/coverage/record_sessions.py --rom <rom>` (writes `coverage/*.cov`).

## Legal

This repository contains only original code. It does not contain, and must
not be used to distribute, Sega's copyrighted ROM, BIOS or assets. Files in
`generated/` are derived from your ROM and must not be redistributed.
