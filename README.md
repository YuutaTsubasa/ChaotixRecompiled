# Knuckles' Chaotix Recompiled

[![CI](../../actions/workflows/ci.yml/badge.svg)](../../actions/workflows/ci.yml)

A static recompilation of the Sega 32X game *Knuckles' Chaotix* into native
C++. This is not an emulator: the 68000 and both SH-2 CPUs' game code is
translated ahead of time into C++ that is compiled for the host. A small
hardware compatibility layer (32X and Mega Drive registers, both VDPs,
interrupts, timers, sound chips) plus an SDL3 frontend make it an ordinary
native program on each platform.

**No game data is included.** You supply your own legally obtained ROM of
*Knuckles' Chaotix (Japan, USA)* — SHA-1
`0c2fff7bc79ed26507c08ac47464c3af19f7ced7`. No 32X BIOS is needed. Read
[LEGAL.md](LEGAL.md) before building or sharing anything.

## What works

Boots to the title screen; menus, character select and the stage-select hall
work; the levels are playable, with sound (Z80 driver, YM2612, PSG and PWM).

**True widescreen in levels**: the game's own level renderer is extended to
draw real tiles, objects and sprites in the extra area, rather than stretching
or cropping a 4:3 image. Anything from 1.24:1 (320×240) to 2:1 (480×224) is
filled — 426×224 at 16:9 — and the centre 320 pixels stay bit-identical to the
unmodified game, which is checked by a test. Scenes outside levels stay 4:3.

| Platform | State |
|---|---|
| Windows | built and played (MSYS2 GCC, and MSVC) |
| Linux / macOS | build configuration present, not yet run by the author |
| Android | [builds and runs](platforms/android/README.md) (verified on an API 36 emulator) |
| iOS | build configuration present, not yet built |

[docs/MILESTONES.md](docs/MILESTONES.md) tracks the detail;
[ARCHITECTURE.md](ARCHITECTURE.md) explains how it is put together. (Those two documents are written in
Traditional Chinese; the rest of the documentation is in English.)

## Build

Requirements: CMake ≥ 3.20, a C++17 compiler, and SDL3 plus SDL_ttf for the
playable frontend. GCC, Clang and MSVC all build the tree warning-free.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCHAOTIX_ROM=/path/to/chaotix.32x
cmake --build build
```

The build runs `chaotix_recomp` over your ROM (guided by the execution traces
in `coverage/`) and compiles the resulting C++ from `generated/`. Without
`CHAOTIX_ROM` you get an interpreter-only runtime and the unit tests — which
is what CI builds, since CI has no ROM.

Windows (MSYS2 MinGW64): `pacman -S mingw-w64-x86_64-{gcc,cmake,ninja,sdl3,sdl3-ttf}`,
and pass a Windows-style ROM path (`cygpath -m`).

Where SDL is not packaged (Android, iOS, CI), point the build at source
checkouts instead: `-DCHAOTIX_SDL3_SOURCE_DIR=...` and
`-DCHAOTIX_SDL3_TTF_SOURCE_DIR=...` (the latter needs its vendored
submodules, so clone it with `--recurse-submodules`).

Presets: `cmake --preset windows-msvc | windows-ninja | linux |
macos-universal | macos-xcode | ios | android-arm64`, with the `CHAOTIX_ROM`
environment variable set.

Cross builds (Android, iOS) do not run the recompiler: build once on a host
with `CHAOTIX_ROM` so `generated/` exists, then configure the target build,
which picks it up. See [platforms/android/README.md](platforms/android/README.md).

## Run

```bash
build/ChaotixRecompiled
```

On the first launch a setup page asks for your ROM. It lists the ROMs it can
find (Downloads, Documents, an `__ROM__` folder next to the executable, the
app's own folder on Android); you can also drag a file onto the window, press
`O` to browse, or tap the on-screen buttons. The file is verified by SHA-1 and
copied into the app's own folder, so later launches go straight into the game.
A ROM given on the command line still wins over the installed copy.

Useful flags: `--user-dir DIR` keeps settings, saves and the installed ROM in
`DIR` (a portable install), `--install FILE` installs a ROM without showing
the setup page (for packaging scripts), and `--menu main|options|awards`
opens a menu page at startup (so it can be captured without a keyboard).

Settings live in the user data directory (`Config/chaotix.ini`), saves in
`SaveData/`, the installed ROM in `Game/`.

| Key | Action |
|---|---|
| Arrows | D-pad |
| Z / X / C | A / B / C |
| A / S / D | X / Y / Z |
| Enter | Start |
| Esc | Menu (left stick click on a pad) |
| F1 / F5 | Debug overlay / overlay page |
| F2 | Aspect ratio (Auto, 4:3, 16:9, 16:10, 21:9) |
| F3 / F4 | Filter / scaling mode |
| F6 | True widescreen on/off (extra columns apply from the next level load) |
| F7 | Achievements (the menu's Awards page) |
| F11, Alt+Enter | Borderless fullscreen |
| Tab (hold) | Fast-forward |
| F12 | Screenshot |

Pressing Start on the title screen opens the front end: START GAME, OPTIONS,
CONTROLS, AWARDS and QUIT, drawn over the title screen in the style of the
era. START GAME hands over to the game's own menus, so nothing the original
offers is lost. Esc opens it again outside a level, and opens the same menu
as a pause menu inside one.

Escape leaves the game's own menu and returns to the title screen. The game
has no way back there itself, so the title screen is kept when handing over
and put back; anything chosen inside the game's menus is discarded, as it
would be by turning the console off. Inside a level Escape is the pause menu
instead.

CONTROLS gives each player a device of its own: AUTO (the keyboard and any
gamepad, which is the default for player 1), the keyboard alone, one gamepad
by name, or nothing. Each player also has its own keyboard set, so two people
can share one keyboard — player 2 starts on the keypad. Select a button and
press Enter to rebind it; Escape leaves it alone. OPTIONS has RESET AWARDS,
which asks twice before locking everything again.

On-screen touch controls appear once the screen is touched (`TouchControls =
Auto`), always (`On`), or never (`Off`). Gamepads work through SDL
(Xbox, PlayStation and Nintendo layouts): West/South/East = A/B/C,
LB/North/RB = X/Y/Z, Back = Mode. Every other button belongs to the emulated
6-button pad, so the menu is on the left stick click; B backs out of it. On
touch devices virtual controls appear over the game, and the unlock counter
in the top corner opens the menu.

## Achievements

A small local achievement system in the spirit of RetroAchievements: the
conditions in [`assets/achievements.ini`](assets/achievements.ini) are checked
against the game's memory every frame, unlocks pop up in game and are stored
in `SaveData/achievements.ini`. Nothing is downloaded or uploaded and no
account is involved. `F7` shows the list; see
[docs/ACHIEVEMENTS.md](docs/ACHIEVEMENTS.md) to write your own.

## Tools

- `rom_analyzer --rom <rom> [--coverage f.cov] --out dir` — verification and
  reports (code spaces, functions, xrefs, hardware register usage).
- `chaotix_recomp --rom <rom> --coverage f.cov --out generated` — the recompiler.
- `chaotix_headless --rom <rom> --frames N [--wide E] [--wide-bottom R]
  [--lockstep] [--compare-native] [--shot F] [--press F:btn:dur] [--coverage f]
  [--trace ...]` — deterministic headless execution and validation.

## Tests

```bash
cd build && ctest --output-on-failure
```

Unit tests always run. With `CHAOTIX_ROM` set, the integration tests compare
the recompiled build against the reference interpreter frame by frame, check
golden frame hashes for 4:3, widescreen and 16:9, and verify that widescreen
leaves the centre 320 pixels untouched. `-DCHAOTIX_LONG_TESTS=ON` adds
`ctest -L long`: a 36,000-frame lockstep run and 720 golden render hashes, a
few minutes.

Coverage traces for the recompiler are recorded with
`python tools/coverage/record_sessions.py --rom <rom>` (writes `coverage/*.cov`).

The screens the program draws itself (first-run setup, achievements) use
Inter, which is compiled into the executable so they work before anything has
been installed. See [THIRD_PARTY.md](THIRD_PARTY.md).

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) — in short: never post game data, and a
change to emulated behaviour is reviewable only with a lockstep run behind it.

## Legal

This repository contains only original code. It does not contain, and must
not be used to distribute, Sega's copyrighted ROM, BIOS or assets.

The releases ship a built binary containing the recompiled code, as other
static recompilation projects do. That binary holds a translation of the
game's instructions and none of its data — no graphics, music or level data —
and it stops at the setup screen until you supply your own ROM. That position
is not settled law; [LEGAL.md](LEGAL.md) sets out what is shipped, what is
not, and the risk.

Not affiliated with or endorsed by Sega.

## Licence

[MIT](LICENSE), for the original code in this repository.

That covers this project's own work only. The bundled Inter font is under the
SIL Open Font License, and SDL3 and SDL_ttf under Zlib; see
[THIRD_PARTY.md](THIRD_PARTY.md). It does **not** grant you any rights to
*Knuckles' Chaotix* itself, which remains Sega's ([LEGAL.md](LEGAL.md)).
