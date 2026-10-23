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
| Linux | build configuration present, not yet run by the author |
| macOS | [builds and runs](docs/BUILDING_APPLE.md) on Apple Silicon (an M1; every test passes, lockstep bit-identical) |
| Android | [builds and runs](platforms/android/README.md) (verified on an API 36 emulator) |
| iOS / iPadOS | [builds and runs in the iOS Simulator](docs/BUILDING_APPLE.md); an `.ipa` for sideloading, not yet tried on a device |

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
which picks it up. See [platforms/android/README.md](platforms/android/README.md),
and [docs/BUILDING_APPLE.md](docs/BUILDING_APPLE.md) for the macOS app and the
iOS `.ipa` (`scripts/package_apple.sh macos|ios VERSION` on a Mac).

## Run

```bash
build/ChaotixRecompiled
```

On the first launch a setup page asks for your ROM. It lists the ROMs it can
find (Downloads, Documents, an `__ROM__` folder next to the executable, the
app's own folder on Android, the app's Documents on iOS, where the Files app
can put it); you can also drag a file onto the window, press
`O` to browse, or tap the on-screen buttons. The file is verified by SHA-1 and
copied into the app's own folder, so later launches go straight into the game.
A verified ROM given on the command line (including dragging it onto the
Windows executable) replaces the installed copy, so later launches do not need
the original file. Unrecognised images are rejected without replacing an existing
installation; the error shows the selected path and both SHA-1 values. If an
older version installed an unrecognised image, startup returns to setup so you
can choose a verified ROM with Browse.

Useful flags: `--user-dir DIR` keeps settings, saves and the installed ROM in
`DIR` (a portable install), `--install FILE` installs a ROM without showing
the setup page (for packaging scripts), and `--menu main|options|awards`
opens a menu page at startup (so it can be captured without a keyboard;
`timeattack` is one of them).

Settings live in the user data directory (`Config/chaotix.ini`), saves in
`SaveData/`, the installed ROM in `Game/`. That directory is
`%APPDATA%\ChaotixRecompiled\ChaotixRecompiled` on Windows,
`~/.local/share/ChaotixRecompiled/ChaotixRecompiled` on Linux,
`~/Library/Application Support/ChaotixRecompiled/ChaotixRecompiled` on macOS
and the app's Documents on iOS (the Files app's On My iPhone > Chaotix).

| Key | Action |
|---|---|
| Arrows | D-pad |
| Z / X / C | A / B / C |
| A / S / D | X / Y / Z |
| Enter | Start |
| Esc | Menu (on a pad, the button set under CONTROLS - left stick click to start with) |
| F1 / F5 | Debug overlay / overlay page |
| F2 | Aspect ratio (Auto, 4:3, 16:9, 16:10, 21:9) |
| F3 / F4 | Filter / scaling mode |
| F6 | True widescreen on/off (applies from the next level load; resizing the window applies at once) |
| F7 | Achievements (the menu's Awards page) |
| F11, Alt+Enter | Borderless fullscreen |
| Tab (hold) | Fast-forward (on a pad, the right trigger; see Fast-forward below) |
| F8 / F9 | Save state / load state, in the current slot (see Save states and rewind below) |
| F10 | Next save state slot (1-4) |
| Backspace (hold) | Rewind (on a pad, the left trigger) |
| F12 | Screenshot |

The menu is the front end: it is up as soon as the program starts, a screen of
its own with the game's title art framed on the left and the list beside it —
START GAME, TIME ATTACK, OPTIONS, CONTROLS, AWARDS, QUIT — over the same picture enlarged
and dimmed. The art is the game drawing its own title screen, from your ROM,
so nothing is extracted or shipped.

OPTIONS is split into VIDEO (widescreen, aspect ratio, scaling, screen),
SOUND (the volumes), INPUT (touch controls, fast-forward, 6-button pad) and EXTRAS (cheats,
the menu's look, awards, and MODS, the mod packs).

The menu is written in the game's own font -- the white digits and shaded
yellow capitals of its TRAINING and DATA LOAD screens -- and shows the game's
zones behind itself, a different one each time it opens, under a translucent
coat of that zone's colour. Both are made from your ROM: the font is read
from it at start-up, and each zone's picture is rendered once by the game
itself, on a second machine in the background while a menu is open -- it is
paused whenever you are playing -- (a couple of seconds each on a PC, longer
on a phone), then kept in the user data folder under
`Cache/Backdrops`. Until a picture exists the menu is plain. OPTIONS > EXTRAS
> MENU BACKGROUND picks CYCLE (every zone in turn, the default), one zone
for good (WORLD ENTRANCE, ISOLATED ISLAND, BOTANIC BASE, MARINA MADNESS,
SPEED SLIDER, AMAZING ARENA or TECHNO TOWER; only that picture is made), or
PLAIN. MENU FONT (GAME / PIXEL) and MENU BACKGROUND on PLAIN, `MenuFont` and
`MenuBackground` in `[Video]`, bring back the earlier look. How it works:
[docs/MENU_ART.md](docs/MENU_ART.md).

START GAME hands the game control, and it proceeds into its own menus exactly
as it would have on the console, so nothing the original offers is out of
reach. Escape (or the pad's menu button) then brings up a pause menu, which
adds BACK TO TITLE. The game has no way back to its title screen, so the
title is copied when control is handed over and put back on the way out;
anything chosen inside the game's menus is discarded, as it would be by
turning the console off. The save is kept, as the cartridge's battery keeps
it: progress the game has saved to a DATA slot is there when you come back.
Progress made with NO SAVE (DATA LOAD's first choice, and the one selected at
first) only ever lives in memory, so going back to the title loses it, as
resetting the console would.

TIME ATTACK drops you straight into any level. Knuckles' Chaotix carries a
complete stage-select screen that the shipped game never reaches (game mode
0x30), and the page offers exactly what that screen does, in its order:

| Field | What it chooses |
| --- | --- |
| PLACE | Botanic Base, Speed Slider, Amazing Arena, Techno Tower, Marina Madness, Training, Introduction |
| LEVEL | which of that place's levels — the attractions have 1-5, Training 0-4 |
| AT-TIME | Morning, Day, Sunset, Night |
| PLAYER | the character you play: Mighty, Knuckles, Charmy Bee, Vector, Bomb, Heavy or Espio |
| COMBI | the partner on the other end of the tether, from the same seven |
| PLAYERS | 1 PLAYER, or 2 PLAYERS to give the partner to the second controller |

The names, the levels each place has, and the two-player switch are all read
out of the game's own tables rather than invented here, and START uses the
game's own route into a level, so a run behaves exactly as the console would.
START goes straight there, in about a second: none of the game's own screens
are passed through, only its level entry (the ball wipe and the BOTANIC BASE
ticket). A run is played with NO SAVE, as the stage-select screen sets it, so
it never touches your DATA slots.

Every run is recorded, frame by frame, to `SaveData/last_run.txt`, and
`chaotix_headless --replay-input` plays one back exactly. It is there because
what the game does when a level is finished at the goal is not something a
script can reach, so the only way to study it is to record somebody reaching
it. A whole run is about 150 KB and holds nothing but pad readings.

BEST TIME shows the record for whichever stage is selected. When a run ends
the game would send you to its lobby, so the front end takes the time and
brings you back to this page instead, with what you managed. A run that used
up the level's own ten minutes is not a time. The clock stops where the game
stops it -- at the goal, not at the end of the twenty-odd seconds of tally
afterwards -- so a record reads the same as the time you watched when you
finished. Two players needs a device for player 2 under
CONTROLS, which starts as NONE.

CONTROLS gives each player a device of its own: AUTO (the keyboard and any
gamepad, which is the default for player 1), the keyboard alone, one gamepad
by name, or nothing. Each player has its own keyboard set *and* its own
gamepad set, so two people can share one keyboard (player 2 starts on the
keypad) or two controllers can be laid out differently. Choose a button and
press Enter, then press the key or the gamepad button to bind it; Escape
leaves it alone. Each row also says what that button does in this game: C
jumps, B holds your partner still (the game answers with its own HOLD!
bubble), A pulls the partner in for ten rings, Start pauses. X, Y, Z and Mode
say NOT USED BY THIS GAME, because they are: pressing them changes neither
the picture nor the game's state, on the title screen or in a level.

MENU BUTTON is Escape's counterpart on a controller: the emulated 6-button
pad takes every face and shoulder button, so the choice is among the ones it
leaves alone - left stick click (the default), right stick click,
Guide/Home/PS, the touchpad click, or Share/Capture. The same button backs
out of the menu again.

### Fast-forward

FAST FORWARD KEY (Tab to begin with; press Enter, then the key) and FAST
FORWARD PAD (the right trigger to begin with; also the left trigger, either
stick click, Guide/Home/PS, the touchpad click, Share/Capture or NONE -- never
one the emulated pad uses, nor the MENU BUTTON) are on the CONTROLS page.
OPTIONS > INPUT has the rest:

- TOUCH FAST FORWARD (OFF by default) adds a small fast-forward button to the
  touch controls, in the top-left corner, which both touch layouts leave
  empty. Its image is `ff.bmp` in `Mods/Touch`.
- FAST FORWARD: HOLD (on while held, the default) or TOGGLE (each press turns
  it on or off; the touch button stays lit while it is on).
- FAST FORWARD SPEED: 2X and 4X run exactly two or four frames of the game
  for every frame at the normal pace, and keep the sound -- all of it, played
  that many times faster, so it is higher but unbroken. MAX (the default, and
  what Tab always did) runs as fast as the machine allows, up to eight frames
  for every picture shown, and is silent because it has no steady speed to
  play the sound at.

Fast-forward only runs more frames of the same game, so the game behaves
exactly as it would have. It does nothing during a TIME ATTACK run (the touch
button is not shown either), so every record is set at the game's own speed,
nor while the menu is open. In the settings file: `FastForwardKey`,
`FastForwardButton`, `TouchFastForward`, `FastForwardMode` (Hold | Toggle)
and `FastForwardSpeed` (2x | 4x | Max) in `[Input]`.

### Save states and rewind

The pause menu's STATES page saves and loads the game in one of four slots
(SLOT), and turns rewind on or off. F8 saves, F9 loads and F10 picks the next
slot; holding Backspace (or the left trigger) steps back through the last
minute of play, at twice the game's speed and without sound, and letting go
plays on from there. CONTROLS > STATES AND REWIND rebinds the keys, gives
saving and loading a pad button if wanted (none to begin with) and turns on
an on-screen rewind button (TOUCH REWIND, off to begin with; `rew.bmp` in
`Mods/Touch`). Rewind is on to begin with on computers and off on phones and
tablets.

A state is the whole machine, so loading one carries on exactly as the game
would have from that moment; the cartridge save (the DATA slots) is never put
back by a load or by rewinding. States are files in `States/` in the user data
folder and survive restarting the program, but only load with the same ROM,
ROM patches and version of this program. None of it works during a TIME
ATTACK run, so a record is always one unbroken run. See
[docs/SAVE_STATES.md](docs/SAVE_STATES.md).

OPTIONS > EXTRAS has RESET AWARDS, which asks twice before locking everything again.

On-screen touch controls appear once the screen is touched (`TouchControls =
Auto`), always (`On`), or never (`Off`). Gamepads work through SDL
(Xbox, PlayStation and Nintendo layouts): West/South/East = A/B/C,
LB/North/RB = X/Y/Z, Back = Mode (the game itself only uses three of those --
see CONTROLS). Every other button belongs to the emulated
6-button pad, so the menu is on one of the spare buttons (see MENU BUTTON
above); East/B backs out of it too. On touch devices virtual controls appear
over the game, and the unlock counter in the top corner opens the menu.

## Achievements

A small local achievement system in the spirit of RetroAchievements: the
conditions in [`assets/achievements.ini`](assets/achievements.ini) are checked
against the game's memory every frame, unlocks pop up in game and are stored
in `SaveData/achievements.ini`. Nothing is downloaded or uploaded and no
account is involved. `F7` shows the list; see
[docs/ACHIEVEMENTS.md](docs/ACHIEVEMENTS.md) to write your own.

Twenty-two of them, 270 points: rings, each of the seven attractions, how far
in you get, one for each of the seven characters, and one for playing with a
second player on the partner. The character ones want TIME ATTACK, which is
how you reach a character the game would otherwise have chosen for you. Each
was checked by driving the game to it rather than by reading the conditions.

## Cheats

OPTIONS > EXTRAS > CHEATS has 99 RINGS and SPEED SHOES, and `[Cheats] Codes` in the
config takes Action Replay codes (`FFE009:63`). Game Genie codes are not
supported: they patch the ROM's instructions, which the recompiled code does
not read. Achievements and time attack records pause while any cheat is on.
See [docs/CHEATS.md](docs/CHEATS.md).

## Volume and touch controls

OPTIONS > SOUND has VOLUME (everything), MUSIC and EFFECTS. The sound driver's sound
effects take over some of the music's channels while they play, and those
channels follow EFFECTS for that time; see `src/audio/volumes.h`.

Touch controls can be reskinned: BMP images named after the buttons
(`dpad.bmp`, `a.bmp` ... `start.bmp`, `ff.bmp`, `button.bmp` for all face buttons,
`<name>_pressed.bmp` while held) in `Mods/Touch`. The folder holds a
README.txt that lists them.

OPTIONS > INPUT > TOUCH STICK turns the touch d-pad into a joystick
(`TouchStick = true` in `[Input]`): the thumb that lands on it keeps it until
lifted, and steers even after sliding off the base. Its images are
`stick.bmp` (the base) and `knob.bmp`, each with an optional `_pressed`
version.

TOUCH LAYOUT (OPTIONS > INPUT, `TouchLayout` in `[Input]`) has CLASSIC,
every button of the pad, and SONIC CD, the layout of Sonic CD's 2013 mobile
version: the directions on the left and one large jump button on the right,
with HOLD (B) and CALL (A) small above it -- the only buttons this game
uses -- and anywhere else on the lower right of the screen jumping too.

TOUCH OPACITY and PRESSED OPACITY (OPTIONS > INPUT; `TouchOpacity` and
`TouchPressedOpacity` in `[Input]`) set how opaque the touch controls are at
rest and while held, custom images included: 65% and 100% by default.

The `Mods` folder is in the user data folder. On Android it is
`ChaotixRecompiled/Mods` in the phone's storage once you allow it (OPTIONS >
EXTRAS > MODS > MODS FOLDER opens Android's "All files access" page), and
`Android/data/org.chaotixrecompiled/files/Mods` until then. On iOS it is
`Mods` in the app's Documents, which the Files app shows.

In widescreen levels the HUD keeps to the left edge of the picture; OPTIONS >
VIDEO > WIDE HUD puts it back at its 4:3 place.

## Mod packs

Every kind of mod below can come as a *pack*: a folder of its own in `Mods`,
with a `mod.ini` (name, author, version, description, link, preview image)
and inside it the same `Textures`, `Characters`, `Music`, `Patches` and
`Touch` folders the `Mods` folder has:

```
Mods/
  ChineseText/
    mod.ini          name = Traditional Chinese / author = ... / version = 1.0.1
    preview.png
    Textures/        mdtext_*.txt/.png, 32x_*.png, ...
  FasterKnuckles/
    mod.ini
    Characters/knuckles.ini
  Textures/          your own loose files, as before
```

OPTIONS > EXTRAS > MODS lists the packs with their versions: left/right turns
one on or off, Enter shows its description and preview with MOVE UP and MOVE
DOWN. Where two enabled packs replace the same thing (a tile, a sprite frame,
a text pattern, a character's file, a piece of music, a touch button) the
higher one wins; patches of every enabled pack are applied, the lowest
first. Loose files in `Mods/Textures`, `Mods/Music`, ... still work, above
every pack. Graphics, music and touch images change as soon as you leave the
page (or press RELOAD MODS); patches and character stats the next time the
game starts. The choices are kept in `[Mods]` in `Config/chaotix.ini`.
Details, and how to publish a pack on GameBanana:
[docs/MOD_PACKS.md](docs/MOD_PACKS.md).

## ROM patches

`.ips` and `.bps` files in `Mods/Patches` are applied, in name order, to the
ROM in memory as the game starts: graphics, palettes and other ROM hacks
without rebuilding, and without changing your ROM file. A patch that changes
the game's code works too; that code then runs in the interpreter. Details:
[docs/ROM_PATCHES.md](docs/ROM_PATCHES.md).

## Graphics replacement

Both layers' graphics can be replaced with PNG files: the Mega Drive layer's
tiles (backgrounds and level art) and the 32X layer's sprites (the
characters, the HUD, objects, the title). OPTIONS > EXTRAS > DUMP GRAPHICS
saves each graphic the game draws to `Mods/Textures/Dump`, as `md_<hash>.png`
or `32x_<bank>_<frame>.png`; an edited copy in `Mods/Textures` is drawn
instead, in the game's palette, wherever the game uses it. Only the picture
changes: the game runs exactly as without the mod. Details:
[docs/TEXTURE_MODS.md](docs/TEXTURE_MODS.md).

To redraw a whole character, OPTIONS > EXTRAS > EXPORT SPRITES writes every
frame of each character's sprite banks, without playing, as one sheet per
bank to `Mods/Textures/Sheets`: `32x_<bank>.png`, the frames side by side in
a grid, and `32x_<bank>.txt`, where each frame is. A sheet and its `.txt`
copied into `Mods/Textures` replace every frame of that bank at once; a
frame's own `32x_<bank>_<frame>.png` still wins over its cell
(`chaotix_headless --export-sheets DIR` does the same from the command line).

Mega Drive text built from letter tiles (the title menu, OPTIONS, PLAYER
SELECT, the DATA LOAD banners) can be replaced by *sequence* rather than tile
by tile, for translations: `mdtext_<name>.txt` names a run of tiles, such as
the seven of "OPTIONS", and `mdtext_<name>.png` is drawn over that run
wherever a plane shows it, and nowhere else. With DUMP GRAPHICS on, the runs
of tiles on each screen are listed in `Mods/Textures/Dump/Text`, with a
numbered screenshot, ready to copy. Details in
[docs/TEXTURE_MODS.md](docs/TEXTURE_MODS.md#mega-drive-text-by-tile-sequence-mdtext_nametxt-and-mdtext_namepng).
Text drawn with Mega Drive sprites (the menu banners' letters, PLAY STYLE,
SOUND TEST, the 8x16 font's names) is replaced the same way by
`mdsprite_<name>.txt` + `.png`, naming the group of sprites that shows it;
the dump lists the sprites on screen in groups as `Dump/Text/sprites_<n>.txt`
([details](docs/TEXTURE_MODS.md#mega-drive-text-by-sprite-mdsprite_nametxt-and-mdsprite_namepng)).

## Character mods

`Mods/Characters/<name>.ini` (`mighty`, `knuckles`, `charmy`, `vector`,
`bomb`, `heavy`, `espio`) gives a character new colours (`[Palette]`, its 18
colours as `#RRGGBB`; Espio's shimmer in `[Cycle]`) and new handling
(`[Stats]`: acceleration, top speed, gravity, jump and more, by name).
Colours change only the picture; stats change the game, and take effect the
next time it starts. With new sprites in `Mods/Textures`, Mighty can become
someone else entirely. Details: [docs/CHARACTER_MODS.md](docs/CHARACTER_MODS.md).

## Music replacement

Put WAV files in `Mods/Music` (see above for where `Mods` is),
named after the game's music id in hex: `05.wav` replaces BOTANIC BASE's
music. The game's sound effects still play. Loops come from the WAV's sampler
chunk or a `05.loop` text file, and `ModMusicVolume` in `[Audio]` sets their
level. Every music request is logged with its id, so playing the game shows
which file to make; see [docs/MUSIC_MODS.md](docs/MUSIC_MODS.md). Only music
you have the right to use, please.

## Tools

- `rom_analyzer --rom <rom> [--coverage f.cov] --out dir [--refs-to ADDR]
  [--disasm ADDR[,COUNT]]` —
  verification and reports (code spaces, functions, xrefs, hardware register
  usage). `--refs-to` prints every instruction that touches one address, with
  the code around it, which is how a variable is followed back into the game.
  `--disasm` lists a routine from one address, decoding straight from the ROM,
  so code the traces never reached can still be read.
- `chaotix_recomp --rom <rom> --coverage f.cov --out generated` — the recompiler.
- `chaotix_headless --rom <rom> --frames N [--wide E] [--wide-bottom R]
  [--lockstep] [--compare-native] [--shot F] [--press F:btn:dur] [--coverage f]
  [--stage F:PLACE:LEVEL:TIME:PLAYER:COMBI:PLAYERS] [--replay-input f]
  [--trace ...]` — deterministic headless execution and validation. `--stage`
  asks the game to start a level the way the front end's TIME ATTACK does, and
  `--replay-input` plays back a session the program recorded with
  `--record-input`, so that something only a person can do — finishing a level,
  say — can be studied here as often as needed.
- `chaotix_headless --save-state FRAME:FILE` writes a save state after that
  frame and `--load-state FILE` starts from one (the run then goes on to
  `--frames`); `--state-hash-every N` prints a fingerprint of the whole
  machine and `--rewind INTERVAL[:MB]` times the rewind buffer and checks
  every snapshot it holds (docs/SAVE_STATES.md).

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

## Credits

Code, bug reports and ideas from the community made much of this; the people
behind them are listed in [CREDITS.md](CREDITS.md).

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
