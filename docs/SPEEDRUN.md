# Speedrun tools (issue #47)

An in-game timer with splits, an input display, and LiveSplit auto-splitting.
None of it changes the emulation: the timer only reads the game's memory after
each frame, and the overlays are drawn by the program over the window, never
into the game's picture.

## Using them

OPTIONS > EXTRAS > SPEEDRUN (also from the pause menu):

| Row | What it does | Default |
| --- | --- | --- |
| TIMER | real time and game time of the run | OFF |
| SPLITS | each level finished, with its delta against your best | OFF |
| COMPARE | REAL TIME or GAME TIME: what the splits show and the best goes by | REAL TIME |
| INPUT DISPLAY | the pads' buttons, lit while held; both players when a second one is set up | OFF |
| TIMER CORNER / INPUT CORNER | where each is drawn on the picture | TOP RIGHT / BOTTOM LEFT |
| OPACITY | how opaque they are, 10-100% | 85% |
| START RUN / RESET RUN | start a run now, or reset the one going (closes the menu) | |
| START RUN KEY / RESET RUN KEY | keys for the two (captured like the other host keys) | Home / End |

Config: `[Speedrun]` in `Config/chaotix.ini` (`Timer`, `Splits`, `Compare` =
Real | Game, `InputDisplay`, `TimerCorner`, `InputCorner` = TopLeft |
TopRight | BottomLeft | BottomRight, `Opacity`, `StartKey`, `ResetKey`). A
file without the section keeps everything off.

On a phone or tablet nothing needs a key: a run starts by itself at DATA LOAD
and resets at the title screen, and the page's START RUN / RESET RUN do the
rest. The overlays are drawn over the picture and the touch controls, and
hidden while the menu is open; move them with the CORNER rows.

The timer runs whether or not anything is shown, so LiveSplit can follow a run
with the overlays off.

## Definitions

- **A run** is a story-mode playthrough. It starts on the frame the game leaves
  DATA LOAD (game mode `0x60`) for the game itself: START on a slot, a new game
  (into INTRODUCTION's opening, mode `0x58`) or a saved one. Going back from
  DATA LOAD to the title is not a start, and neither is an attract demo (mode
  `0x38`). The START RUN key or menu entry starts one by hand at any moment
  (a run going is reset first).
- **Reset**: by hand, or by the game going back to its title screen (mode
  `0x08`) or its boot (`0x00`) — BACK TO TITLE, a soft reset, or the end of
  the game.
- **RTA** (real time) runs from the start of the run on the computer's clock,
  like a stopwatch: through pauses, menus, loading and fast-forward.
- **Level time** is what the level's own clock shows on its HUD and its results
  screen: the level engine's frame counter `FFE002` from where the level began
  counting, less the frames the game's clock was stopped (`FFAEEC`), less the
  HUD's six frames of lag — exactly what TIME ATTACK measures (see
  `runtime/stage_select.h`). In frames at 60 Hz, written the game's way,
  `0'34"66`.
- **IGT** (game time) is the sum of the level times of the levels finished in
  the run, plus the running clock of the level being played. A level left
  without being finished (its ten minutes ran out) adds nothing, as it adds
  nothing to the game's own play time. Scenes without a level clock
  (INTRODUCTION, the lobby, the special and bonus stages, cutscenes) add
  nothing either.
- **A split** is a finished level, taken on the frame its clock stops at the
  goal: the goal flag `FFFDC5` goes up and the clock settles a few frames later
  (`kGoalSettle`), and the time on the results screen is the split's level
  time. A level the game counts as finished without that flag being seen (its
  attraction's progress byte at `FFDFFE + place` moves on to level + 1, which
  the level end does at `$890F86`) splits then instead. INTRODUCTION, which
  has no clock, splits when it hands over to the lobby having set all five
  attractions to their first level (`$890F28`). One split per visit to a level.
- **Personal best**: when a run ends (reset) with more splits than the best
  kept, or as many and a lower final time (by COMPARE), it becomes the best:
  `Splits/<first 8 hex digits of the ROM's SHA-1>.splits` in the user data
  folder (a patched game keeps its own, like ghosts). Each split of a run is
  compared with the best's split of the same number. Runs with a cheat on, and
  TIME ATTACK runs, are never kept.
- **TIME ATTACK**: starting a stage starts the timer for it (timer only: no
  splits shown and no best kept; the stage's best time is TIME ATTACK's own).
  When the run is over its time stays on screen until the next start.

Measured with the recorded run that finishes BOTANIC BASE 1
(`tests/recordings/finished_run.txt`): the goal flag goes up on frame 3423 and
the split is taken on frame 3426 with level time and game time `0'34"66`
(2080 frames), the time on the results screen; the progress byte the level end
moves on later does not split again.

### The splits file

    chaotix-splits 1
    # split PLACE LEVEL RTA_MS IGT_FRAMES LEVEL_FRAMES FRAME NAME
    split 6 0 152340 0 0 9001 INTRODUCTION
    split 0 1 210456 2080 2080 12345 BOTANIC BASE 1

Times are cumulative from the start of the run (RTA in milliseconds, IGT in
frames). Names are the places as the ROM names them (`$8F739E`), so a ROM
patch that renames a place shows up.

## LiveSplit

`tools/livesplit/ChaotixRecompiled.asl` is an auto-splitter: in LiveSplit,
Edit Layout > + > Control > Scriptable Auto Splitter, and pick the file. It
starts, splits and resets with the program's timer and gives LiveSplit the
IGT as game time (compare against Game Time to race it). Options: split on
INTRODUCTION's end, reset at the title screen, and whether TIME ATTACK runs
are timed (off).

### Finding the game in memory

The 68K work RAM is part of an object on the heap, so it moves from run to
run. The program keeps a block at a fixed place in its own image (an
initialised global in its data section, `src/frontend/livesplit.h`) that a
script finds by its magic and reads at fixed offsets. It is written after
every emulated frame:

| Offset | Type | |
| --- | --- | --- |
| +0 | char[24] | `CHAOTIX_LIVESPLIT`, NUL padded |
| +24 | u32 | version, 1 |
| +28 | u32 | size of the block, 72 |
| +32 | u64 | address of the 68K work RAM (64 KB, `$FF0000-$FFFFFF`, big-endian words); 0 before the game starts |
| +40 | u32 | emulated frame counter (low 32 bits) |
| +44 | u32 | run state: 0 none, 1 running, 2 stopped (a TIME ATTACK run that is over) |
| +48 | u32 | run id, one more for each run started |
| +52 | u32 | splits taken in the run |
| +56 | u32 | IGT, frames at 60 Hz |
| +60 | u32 | the current level's clock, frames at 60 Hz |
| +64 | u32 | 1 in a TIME ATTACK run |
| +68 | u32 | reserved, 0 |

Values are little-endian (the host's order). The script scans the main module
for `43 48 41 4F 54 49 58 5F 4C 49 56 45 53 50 4C 49 54 00`; the magic is
nowhere else in the program (`tests/test_livesplit_signature.py` checks the
built programs: once, version 1, size 72). Reading a game variable is then
`ram[offset]` with offset the low word of its address, e.g. the game mode at
`ram + 0xDFDE`, big-endian.

The block is the program's on Windows (`ChaotixRecompiled.exe`), where
LiveSplit runs; it is in the Linux and macOS builds too, for tools of their
own. A LiveSplit Server (TCP) client is not included: the auto-splitter does
the same without a network connection.

### RAM map

`FFxxxx` is 68K work RAM (offset `xxxx` from the block's pointer); words and
longs are big-endian.

| Address | Size | What |
| --- | --- | --- |
| `FFDFDE` | word | game mode: `00` boot / SEGA, `08` title, `60` DATA LOAD, `58` INTRODUCTION's opening, `18` a level or the lobby, `20` special stage, `50` bonus stage, `38` attract demo |
| `FFDFF2` | word | place: 0 BOTANIC BASE, 1 SPEED SLIDER, 2 AMAZING ARENA, 3 TECHNO TOWER, 4 MARINA MADNESS, 5 TRAINING, 6 INTRODUCTION, 7 WORLD ENTRANCE (the lobby), 9 BONUS STAGE, 10 SPECIAL STAGE |
| `FFDFF4` | word | level (0 the lobby / a scene, 1-5 the attraction's levels) |
| `FFDFF6` | word | time of day: bits 1-2 MORNING / DAY / SUNSET / NIGHT, bit 0 a half step |
| `FFDFFE + place` | byte | progress: the level of attraction `place` (0-4) the game is up to |
| `FFE002` | long | the level engine's frame counter (the level clock counts from it) |
| `FFE010` | word | the game's own level timer in 1/64 s: high nibble minutes, low 12 bits ticks (`$A000`, ten minutes, is the time over) |
| `FFE016` | long | the game's play time, the level timers added up at each level's end (`$890FA8`) |
| `FFE01C` | word | the DATA slot a finished level is saved to (`FFFF` NO SAVE) |
| `FFE038` / `FFE03A` | word | player's / partner's character x 4 |
| `FFE052` | word | frames since play began in the level (reset by the player's input; see `stage_select.h`) |
| `FFAEEC` | word | frames the clock was stopped (pause), wrapping |
| `FFFDC5` | byte | `FF` from the frame the level's goal is reached |

The level end (`$890EFC`): INTRODUCTION sets the progress of every attraction
to 1; a level of an attraction moves the progress on, adds the level timer to
`FFE016`, saves to the DATA slot (`$8BE55A`, `$8BE3C0`) and returns to WORLD
ENTRANCE in mode `0x18`.

## The input display

Eleven buttons in a box 56 x 20 units: the D-pad as a cross, START, and A B C
under X Y Z (MODE is not shown). Held buttons are bright, the rest dim. The
layout is shared by the window and the headless tool
(`frontend/input_display.h`), and drawing is clipped to the box.

## Determinism

The timer is a reader: it runs after each emulated frame and writes nothing
the machine sees, like TIME ATTACK's tracking. The overlays are drawn by the
host over the window (or, in `chaotix_headless --input-display`, over a copy of
the picture written by `--shot`). Screenshots of the game's frames, golden
hashes and save states are the same with them on or off.

## Tests

- Unit (`tests/test_speedrun.cpp`): the time formats; a run starting at DATA
  LOAD and not at the title or an attract demo; a finished level splitting
  once with the results screen's time, the progress byte not splitting again,
  the next level adding to the game time; a level left unfinished adding
  nothing; a boss level split by its progress byte; INTRODUCTION's split; the
  title screen resetting a run and offering it as a best; cheated and TIME
  ATTACK runs never a best, TIME ATTACK's stopped time; the best rule and
  deltas; the splits file round trip; place names from the ROM; the input
  display's buttons inside the box, its drawing changing no pixel outside it
  (clipped at the picture's edges too); the LiveSplit block's magic, layout
  and pointer to the machine's work RAM. Menu: the SPEEDRUN page sets and
  keeps every setting (`menu.speedrun_tools_are_set_from_extras_and_kept`).
- `speedrun_splits_a_finished_level_once`: the recorded finished run, timed:
  split 1 BOTANIC BASE 1 on frame 3426, `0'34"66` as level and game time, no
  second split, the block read back as documented, lockstep bit-identical.
- `speedrun_starts_when_data_load_hands_over`: boot, title, DATA LOAD, START:
  the run starts on frame 2301, lockstep bit-identical.
- `speedrun_overlays_leave_the_frames_alone`: the lobby walk with the timer on
  and the input display drawn into shots: every frame hashes as the golden
  file has it, and no pixel outside the display's box changed.
- `livesplit_signature_is_in_the_programs`: the block is in
  `ChaotixRecompiled` and `chaotix_headless`, once, as documented.

## Limitations

- The end of the game is not detected: the final split is the last level
  finished (the final boss included, if its level reaches the goal or moves
  the progress on). The run then resets when the game returns to its title.
- Loading a save state or rewinding during a run moves the game but not the
  run's real time; the level time follows the game.
- The best is the run that got furthest, fastest; there are no best segments
  (gold splits) or sum of best.
