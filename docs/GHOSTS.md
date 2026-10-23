# TIME ATTACK ghosts (issue #41)

Race a translucent copy of a run you made before.

## Using them

- **TIME ATTACK > GHOST**: OFF, BEST or LAST. BEST races the fastest run of
  the selected stage that is kept, whoever ran it; LAST races the run of the
  stage finished last. It starts as BEST on computers and OFF on phones and
  tablets (see Cost below); `Ghost` in `[TimeAttack]` of `Config/chaotix.ini`.
- Every TIME ATTACK run is recorded. A run that finishes with a time, with no
  cheats on, is kept as the stage's last run, and as the best for its two
  characters when it beats the one there. The result message says "Ghost
  kept." when it became a best.
- On the next START of that stage the ghost runs alongside: its characters
  are drawn half see-through and tinted pale blue, where they are in the
  level, from the moment the level clock starts until the ghost reaches the
  goal. Its time is shown when it gets there ("GHOST 0'34"66"). It is drawn
  only where it is on your screen: a ghost ahead or behind out of view is not
  shown. A ghost recorded with other characters is still raced, and shows the
  characters that ran it.

## Files, exporting and importing

Ghosts are files in the user data folder (the folder `Config/` and `States/`
are in):

    Ghosts/<first 8 hex digits of the ROM's SHA-1>/
        <place>_<level>_<player>_<partner>.ghost   the best with those two
        <place>_<level>_last.ghost                 the last run of the stage

Place, level and characters are the game's own numbers (place 0 BOTANIC
BASE ... 6 INTRODUCTION; characters 0 Mighty, 2 Knuckles, 3 Charmy, 4
Vector, 5 Bomb, 6 Heavy, 7 Espio). With ROM patches or character mods that
change the game, the folder is named after the patched image instead, so
those runs keep records of their own.

To export a ghost, copy its file. To import one, put it in that folder under
any name that starts with the stage's `<place>_<level>_` and ends in
`.ghost` (for example `0_1_from_a_friend.ghost`): BEST looks at every such
file, and races the fastest this program can play.

A ghost plays only on the program that can replay it exactly: the same ROM,
the same ROM patches and character stats, and the same version of this
program (like a save state). Others are passed over. A ghost that does not
replay as it was recorded (it does not end on its recorded frame with its
recorded time) is renamed to `<file>.desynced`, never raced again, and the
message says GHOST DISCARDED. That cannot happen to a ghost this version
recorded, since the game is deterministic; it is the check that it really is
the run it claims to be.

## How it works

The game is deterministic: from the same machine state, the same pad on
every frame makes the same game (that is what lockstep validation checks,
bit for bit). So a ghost is

- the state of the machine at the end of the frame the game took the stage
  request (a save state without its picture, runtime/save_state.h), and
- each frame's pads from there (pad 1 and pad 2), and
- the host's settings that change what the game does but are not hardware
  state: the widescreen margins (they move the camera clamp and what the
  level engine streams), the HUD-at-the-edges and widen-everywhere switches,
  the 6-button pad, and which music the host replaces with files. Each
  change of them during the run is kept with its frame.

Starting from a state rather than the stage request alone is what makes it
exact: what the title screen had been doing (its counters, its random
numbers) carries into the level. The ghost plays with its own recorded
settings, so a ghost recorded in widescreen races a run in 4:3 and the other
way round.

When the live run's stage is taken, a second `Machine` loads the ghost's
start state and then runs one frame for each frame of the live run, with
the recorded pads. It runs on the reference interpreters on a thread of its
own: the recompiled code's dispatch state is process-wide and belongs to
the machine being played (the menu backdrops' machine works the same way,
docs/MENU_ART.md). The thread is given each frame as the live frame starts,
so the two run at the same time; the picture waits for the ghost's frame
(up to 8 ms) before it is shown.

### What is drawn

The ghost machine has a 32X sprite recorder (runtime/sprite_record.h), which
lists every blitter call of the picture it shows. The characters are banks
0-15 (docs/SPRITES_32X.md): those draws, and nothing else, are rasterised
host-side from their own bytes, exactly as the blitters draw them, with the
ghost machine's own palette. The clip rectangle is widened to the widest
picture, so a live view wider than the ghost's was still shows it whole.

The ghost is placed by world position: its screen position plus its camera,
less the live camera (the camera is the level's plane A position,
`patches::camera_x/camera_y`). Both pictures go with the camera of the frame
before (measured: a frame's picture is the 32X list made at the end of the
frame before, and its plane scroll is that frame's camera too), so the ghost
uses the list and camera of its previous frame and the live camera of the
previous frame. Replaying a run against its own ghost puts the ghost exactly
over the player.

It is drawn over the live picture, characters included, at 160/256 opacity
after a 50% tint towards pale blue. Over rather than behind: the live
picture is the finished image of both Mega Drive planes and the 32X layer,
and a ghost behind the player would need the compositor's per-pixel
priorities. A see-through ghost over the player hides nothing, and the
tint tells it apart from the same character.

Nothing of this reaches the live machine: the ghost is drawn over a copy of
its picture, and the live run's every picture and whole-machine state are
identical with and without a ghost (tests/test_ghosts.py).

## Cost

The ghost machine is a second game on the interpreters. Measured on the
development PC (chaotix_headless, BOTANIC BASE 1, the recorded run):

| | ms a frame |
| --- | --- |
| the live machine, recompiled code | 1.3 |
| a machine on the interpreters | 3.4 |
| ... with the sprite recorder (the ghost machine) | 4.0 average, 7.3 worst |
| the ghost in the app, on its own thread, in play | 3.6-4.6 (5.5 worst) |

In the app with a ghost, a 50-second run held 59.9 frames a second every
second, the same slowest passes as without one: on a PC the ghost costs no
frame rate. A phone is roughly 3-10 times slower than the PC (MENU_ART.md),
so its ghost frame would take some 12-40 ms on a core of its own, against
16.7 ms a frame: some phones keep up and many cannot. So GHOST starts OFF
there, and the row says it may slow a phone. If the ghost falls more than
half a second behind, it is stopped ("GHOST STOPPED") rather than drawn in
the wrong place or holding the game back.

## The file

Little-endian. A fixed header (172 bytes):

| bytes | field |
| --- | --- |
| 8 | `CHXGHOST` |
| 4 | format (1) |
| 4 | header size |
| 16 | program version |
| 48 | ROM SHA-1 |
| 48 | SHA-1 of the ROM image after patches |
| 2 x 5, 1, 1 | place, level, at-time, player, partner; two players; 0 |
| 4 | time (level-clock frames) |
| 8 | when recorded (Unix seconds) |
| 4 x 5 | frames, settings changes, start state size, packed inputs size, CRC-32 of the rest |

Then the settings changes (24 bytes each: frame, wide extra, wide extra
bottom, flags, music mask), the start state (a save state file without its
picture, LZ4 inside) and the inputs (one 32-bit word a frame, pad 1 low,
LZ4, runtime/lz.h). A run of Botanic Base 1 in 0'34"66 is 144 KB, almost
all of it the start state: 3,738 frames of input pack to under 1 KB.

## Testing

- `chaotix_headless --ghost-record FILE` writes the run started by `--stage`
  or a replay (`--replay-input`) as a ghost when it ends with a time.
- `--ghost FILE` races one: it starts when the live run's stage is taken,
  is drawn into the shots, and prints when it reaches the goal, when it ends
  (in sync or DESYNC) and a summary (frames drawn, pixels, any drawn outside
  its box). `--ghost-log N` prints its position every N frames;
  `--ghost-threaded` plays it on its own thread as the app does.
- `tests/test_ghost.cpp` (no ROM): the file round trip, damaged and foreign
  files, which ghost is kept and raced, the recorder, where it is drawn, the
  player and its desync check.
- `tests/test_ghosts.py` (`ctest -R time_attack_ghosts`): the recorded
  finished run (tests/recordings/finished_run.txt) as a ghost, raced by
  another run of the stage with other characters and random input. The ghost
  reaches the goal in the recorded 2086 frames and ends in sync; it is drawn
  only inside its box; the live run's pictures and machine state are
  identical frame by frame to the same run without it; the threaded ghost is
  drawn exactly as the one run in step; lockstep holds; and a ghost whose
  inputs were changed is caught as a desync.

## Limits

- A ghost is tied to the program version, like a save state: a new version
  passes over the old ghosts (they stay in the folder).
- The ghost is drawn from the 32X character banks only; a character's
  effects drawn from other banks (the tether's dots, item effects) are not
  part of it.
- No ghost in INTRODUCTION, which keeps no clock: a run there has no time to
  keep.
