# Save states and rewind (issue #40)

## Using them

- **Pause menu > STATES**: SAVE STATE, LOAD STATE, SLOT (1-4) and REWIND
  (on/off). Saving or loading closes the menu; a short message says how it
  went ("STATE 1 SAVED", "STATE 2 IS EMPTY", or why a state was refused).
- **Keys**: F8 saves, F9 loads, F10 picks the next slot, Backspace held
  rewinds. **Pad**: the left trigger rewinds; saving and loading have no pad
  button until one is chosen. **Touch**: TOUCH REWIND adds a rewind button
  beside the fast-forward one (off to begin with); saving and loading are on
  the pause menu.
- **CONTROLS > STATES AND REWIND** rebinds all of them. A pad button is never
  one the emulated pad, the MENU BUTTON or fast-forward uses, nor one of the
  others here.
- Settings (`Config/chaotix.ini`): `SaveStateKey`, `LoadStateKey`,
  `StateSlotKey`, `RewindKey`, `SaveStateButton`, `LoadStateButton`,
  `RewindButton` (button names as `FastForwardButton`, or `none`) and
  `TouchRewind` in `[Input]`; `Rewind` and `StateSlot` in `[Extras]`. A file
  from before has none of them and gets the defaults.

## Fairness

- Nothing here works during a **TIME ATTACK** run (the keys say "NOT IN TIME
  ATTACK", the touch button is not shown), nor while the menu is open, like
  fast-forward. A run starts from the title screen and ends there, the rewind
  history is cleared on both trips, and a state cannot be saved during a run,
  so no state or rewind step can reach into one: a record is always one
  unbroken run at the game's own speed.
- **Awards** stay earnable after a load or a rewind. They are local and
  personal (nothing leaves the machine), and retrying a hard part from a
  state is practice rather than a cheat, as in RetroAchievements' softcore
  mode; cheats, which change the game, still stop them. What would be wrong
  is a jump in time counting as a change: a condition such as
  `rings > prev(rings)` would hold the moment a state with more rings was
  loaded. So after a load and after each rewind step the tracker forgets the
  previous frame (`Tracker::forget_previous`) and evaluates nothing until it
  has seen a frame again; nothing is evaluated during the rewind itself.

## What a state is

`runtime/save_state.{h,cpp}`. One function, `transfer`, lists every field of
the state once; archives count, write or read them, so saving and loading
cannot disagree. Fields are fixed-size (no `long`, no pointers), written
little-endian, `bool` as a byte, so a state is the same bytes on every
platform the program builds for.

**In the state** - everything the emulated hardware's future depends on:
68K, both SH-2s and Z80 registers (not their bus maps or hooks), work RAM,
SDRAM, Z80 RAM, the SH-2 BIOS stub, VRAM/CRAM/VSRAM and the VDP's registers
and latches, the 32X system registers, its VDP, palette and both frame
buffers, PWM (not its sample log), the SH-2 on-chip peripherals and caches,
YM2612 and SN76489 (every internal register, through `state_io`), I/O ports,
the cartridge's SRAM mapping register, the clocks, the frame counter, the
machine's private time-slice records, the widescreen settings and every
latch the host patches keep (`runtime/patches.h`: plane shift, level scene,
special-stage and bonus-stage state, the radial table), a pending stage
request, the widescreen margins' shadow frame buffer, and in a file the
output picture.

**Not in the state**, on purpose:

| Left out | Why |
|---|---|
| The ROM (4-8 MB) | Named in the header instead: its SHA-1, and the SHA-1 of the image after ROM patches and character stats. A state is refused by any other ROM or patch set. |
| Cartridge SRAM (the DATA slots), `sram_dirty` | A load never rolls a save back, as BACK TO TITLE keeps it too. |
| Recompiled-code SDRAM epochs | The dispatcher caches "this SDRAM code still matches the ROM" by epoch (`game/recomp_dispatch.cpp`). Restoring an old value would let a check made at that value for other memory stand. A load moves the epochs on to values never checked at instead. (BACK TO TITLE restored them too; it now moves them on the same way.) |
| Tile/sprite replacers, character colours, the sprite recorder | Host resources and output only. The recorder is reset on a load: its lists described the frame buffers just replaced. |
| Bus maps, CPU hooks, executors, tooling hooks, `vdp.radial_lut` pointer | The machine's own pointers; the buses are re-pointed (`remap_m68k`, `remap_sh2`) after a load. |
| Input, volume gains, PWM gain, which music the host replaces, audio not yet played, `audio_enabled`, `profile`, stats other than the frame | Host settings and output. |
| The PWM sample log | Grows and is trimmed, but nothing reads it. |
| Pending music requests | Drained by the host after every frame; cleared on a load. |

The widescreen settings (`wide_extra`, ...) are in the state, so a test loads
exactly what it saved; the app writes its own over them before the next frame,
as it does every frame.

**Sizes**: the raw state is 1,508,065 bytes (955,105 without the 552,960-byte picture:
rewind leaves it out). A file is about 220-280 KB (the level, the special stage
and the bonus stage measured 276, 220 and 278 KB).

## The file

`<user data>/States/chaotix_<first 8 of the ROM SHA-1>_<slot>.state`, written
beside and renamed over, so a crash mid-write leaves the old one. A 164-byte
header, then the raw state compressed, then bytes the host keeps with it (the
app: where the replacement music was, which the game will not ask for again).

| Offset | Size | |
|---|---|---|
| 0 | 8 | `CHXSTATE` |
| 8 | 4 | format version (2; 1 before the ring tether options of issue #43) |
| 12 | 4 | header size (164) |
| 16 | 16 | program version (`0.7.1`) |
| 32 | 48 | ROM SHA-1 |
| 80 | 48 | SHA-1 of the image that ran (after patches) |
| 128 | 4 | flags (1 = picture included) |
| 132 | 4 | raw size |
| 136 | 4 | compressed size |
| 140 | 4 | CRC-32 of the raw state |
| 144 | 4 | host bytes |
| 148 | 8 | frame |
| 156 | 8 | saved at (Unix time) |

A state from another ROM, another patch set, another format or another
version of the program is refused with the reason, and a damaged one (CRC,
size) too; the machine is not touched. Within a version, a state saved on one
platform loads on another.

Compression is `runtime/lz.{h,cpp}`: LZ4's block format, written here (about
150 lines) so that Android and iOS need no new library. Machine state is
mostly runs, which it codes as offset-1 matches at memory speed; the decoder
checks every bound.

## Rewind

`runtime/rewind.{h,cpp}`. A snapshot (the raw state without the picture) is
taken every 2 frames of play. Only the newest is kept whole; each older one is
kept as its XOR with the next newer one, compressed. Two snapshots two frames
apart differ in a few kilobytes, so that is almost all zeros. Stepping back
undoes one delta (decompress, XOR) and loads the result; the oldest delta is
dropped when the budget is reached, and nothing depends on it.

Holding rewind steps back one snapshot per frame period and runs one frame
from it, muted and with the pad let go, for a picture: the game goes back at
twice its speed, and letting go plays on from the frame shown. The
replacement music's place is kept with each snapshot and put back with it.

- **Budget**: at most a minute (1800 snapshots) and at most 64 MB on
  computers, 32 MB on phones. Measured with `chaotix_headless --rewind 2:256`:
  50 s of the title and a widescreen level fit in 18.2 MB (about 12 KB a
  snapshot); 40 s of the special stage, whose polygons redraw the whole 32X
  frame buffer, in 47 MB (about 40 KB), so there the 64 MB hold about 50 s.
- **Cost when not rewinding**: taking a snapshot (serialise, XOR, compress)
  took 0.22 ms on average in a level and 0.36 ms in the special stage, 0.86 ms
  at worst, every other frame: 0.11-0.18 ms per frame against a frame period of
  16.7 ms and a frame's emulation of about 1.5-2 ms (the development PC,
  recompiled code).
- **Cost of a step back**: 0.5 ms (level) to 1.3 ms (special stage) at worst,
  plus the frame run for the picture.
- **Default**: on on computers; off on Android and iOS. A phone pays the
  snapshot cost every frame of play and keeps up to 32 MB whether or not
  rewind is ever used, and the touch layout has no rewind button unless
  TOUCH REWIND is turned on; turning REWIND on there is one menu row.

Turning REWIND off frees the buffer at once. Loading a state, BACK TO TITLE and
starting a stage clear it.

## Tests

- Unit (`tests/test_save_state.cpp`, no ROM): LZ round trips and damaged
  input; save -> load is byte-identical and a loaded machine runs on
  identically; SRAM kept; epochs moved on; another ROM, patch set, format or
  version, a damaged and a truncated file are refused with the machine
  untouched; the rewind buffer steps back through exact earlier states, keeps
  to its budget and snapshot limit; the music position round trip; a jump in
  time is not a change for an award. `tests/test_state_menu.cpp`: the menu
  pages, the bindings and the settings file.
- ROM (`tests/test_save_states.py`, ctest `save_states_carry_on_bit_identically`):
  in a level, the special stage, a widescreen level (HUD at the edge) and the
  widescreen bonus stage, a state saved mid-play and loaded again gives the
  same picture hash and the same whole-machine fingerprint
  (`--state-hash-every`) on every one of the next 300 frames as the run that
  never stopped, and the interpreter and the recompiled code then stay in
  lockstep from it.
- ROM, ctest `rewind_restores_every_snapshot_in_a_level` and
  `rewind_keeps_to_its_budget_in_the_special_stage`: every snapshot held is
  stepped back to and is exactly the state it was taken from; a 16 MB budget
  holds.

Headless options: `--save-state FRAME:FILE` (after that frame; repeatable),
`--load-state FILE` (before running, into the lockstep reference too; the run
then goes from the state's frame to `--frames`, so the other options' frame
numbers keep their meaning), `--state-hash-every N`, `--rewind
INTERVAL[:BUDGET_MB]`. The app has `--rewind-hold FRAME:PASSES` for testing.

## Limitations

- A state loads only in the version of the program that saved it.
- Input recordings (`--record-input`, `last_run.txt`) are a straight line from
  start-up; a session with a state loaded or a rewind does not replay.
- With 32X sprite replacements, the first picture after a load or a rewind
  step may show an original sprite (the recorder starts again from the state).
- No thumbnails in the menu; the slot shows when it was saved.
