# Issue #50: the game's own SOUND TEST is silent

OPTIONS > SOUND TEST, choose a song (or a sound effect) and press A: nothing
plays. Found while building the music player (#48, docs/MUSIC_PLAYER.md),
which works around it by asking from the OPTIONS list instead.

Reproduced headless (title, OPTIONS, SOUND TEST, one step right to
01 DOOR INTO SUMMER, A at frame 2450): the sound is exactly zero from the
moment the OPTIONS music stops to the end of the run. The request is in the
Z80 sound driver's three request slots (Z80 RAM `$1C0A`) and stays there.

## Root cause: the Z80 never got its V-blank interrupt

The sound driver does its work, reading the request slots included, in its
interrupt handler (IM 1, `$0038`), and the only interrupt it gets is the
Mega Drive VDP's V-blank one: /INT asserted for one scan line from the start
of line 224 (`Machine::start_line`, as on the hardware, where the VDP holds
it for about 64 µs, one line). A Z80 that cannot run during that line misses
the interrupt for that frame.

The SOUND TEST's V-blank routine (`$887836`) is the one place in the game
that requests the Z80's bus right at the start of its V-blank handler and
then holds it for long: it copies the driver's track records into 68K RAM
for its level meters. Measured in the emulator (a temporary trace of BUSREQ
writes and of the Z80 at every slice end, frames 2455-2457):

| | before the fix |
| --- | --- |
| Z80 /INT asserted | line 224, master clock 0 of the line |
| 68K writes BUSREQ = 1 (`$887836`) | line 224, master clock 1578 (68K cycle 225 of the line, 105 Z80 cycles after /INT) |
| Z80 at that moment | its idle loop at `$009F`-`$00A2`, IFF1 = 1 (interrupts enabled) |
| Z80 at each of the line's four slice ends | held by BUSREQ, PC unchanged, /INT asserted, never acknowledged |
| BUSREQ released | after line 225; /INT was dropped at the start of line 225 |

On the hardware the Z80 runs alongside the 68K: during the 105 Z80 cycles
between /INT and the bus request it finishes the instruction of its idle
loop it is in (4-10 cycles) and takes the interrupt, then is merely paused
inside its handler while the 68K holds the bus, and carries on when it is
released. Every frame.

The emulator ran each CPU in turn over a quarter-line slice (855 master
clocks): the 68K first, to the slice's end, then the Z80 *if the bus was
not requested at that point*. Here the 68K took its V interrupt in the
first slice of line 224 and its handler's first block (exception, prologue,
the write to `$A11100`) ran past that slice's end, so by the time the Z80's
turn came the bus was already requested: the Z80 got none of the slice,
nor any of the next three (the bus stays requested), and /INT was gone
before the bus came back. The driver's interrupt never ran on that screen
at all, so it never took a request. The other scenes' V-blank routine
(`$8F6EFA`) holds the bus only for nine bytes (the PWM notes), short
enough that the Z80 still ran in a later slice of line 224 and took the
interrupt, which is why the same request plays from the OPTIONS list.

The same scheme was wrong in a smaller way everywhere: the Z80 got either
the whole slice or none of it depending on BUSREQ at the slice's end, not
the time up to the request (or from the release).

## The fix: run the Z80 up to the 68K's time on BUSREQ and RESET writes

The standard approach (Genesis Plus GX does the same in `gen_zbusreq_w`):
when the 68K writes BUSREQ (`$A11100`) or RESET (`$A11200`), first run the
Z80 up to the 68K's current time, then change the state.

- `Machine::z80_sync(now)` runs the Z80, unless it is held in reset or its
  bus is requested, from `z80_clock` up to `now`, and moves `z80_clock`
  there either way. The slice's Z80 step is now `z80_sync(slice end)`, so
  the Z80 runs exactly the time it was not stopped.
- `set_z80_busreq` and `set_z80_reset` call `z80_catch_up()` first:
  `z80_sync` at `m68k_now_mclk()`, the 68K's own time in the middle of its
  slice (the same clock the comm ports and HV counter reads use), but not
  past the end of the current line, since the 68K may have run past its
  slice and the Z80's /INT changes only at line starts.
- `z80_clock` is advanced before the Z80 runs, so a Z80 write through its
  bank window that reaches BUSREQ/RESET finds nothing left to run.

The interpreters and the recompiled code reach the same bus writes at the
same `m68k.cycles` (both take an instruction's cycles before executing it),
and the Z80 is interpreted in both, so lockstep is unaffected by
construction; the sweep below confirms it.

After the fix, the same frames: BUSREQ at line 224 clock 1550, the Z80
runs the 103 cycles up to it, takes the interrupt (PC `$0043` in the
handler, IFF1 = 0) and is paused there until the 68K lets go. Song 01
plays from about a quarter second after A.

## What changes elsewhere

Only the Z80's timing changes, and only around the 68K's bus requests and
resets. The Z80 affects the 68K only through what the 68K reads back from
Z80 RAM (the driver's state, PWM notes for the 32X), so pictures could
change only if the game depended on that timing.

- **Pictures: none changed.** Every frame's image hash, before and after:
  the standard boot-to-level script (3200 frames), two levels with
  `--fuzz` (`--stage 300:0:1:1:5:4:0` and `300:0:1:0:2:7:0`, 3000 frames
  each), the lobby (`300:7:0:0:2:7:0`) and a special stage
  (`300:10:0:0:3:0:0`), all identical frame by frame. No golden data was
  changed: every golden in the test suite (golden frames, the widescreen
  goldens, the attract-mode renders every 50 frames, the lobby goldens)
  passes as it was.
- **Sound: the same music, shifted by microseconds.** Elsewhere the Z80 now
  stops exactly at the 68K's bus request instead of at a slice boundary
  (up to a quarter line, ~57 Z80 cycles, earlier or later), so the driver's
  YM2612/PSG writes land a few samples apart. Sample for sample, the output
  of the standard script and of a fuzzed level differs from 6.5 s / 7.1 s
  on (the first bus requests after the boot), but it is the same music:
  per 2048-sample window the log spectra of before and after have a cosine
  similarity of 0.997 (median; worst 0.987), and the loudness envelopes
  correlate at 0.998 / 0.993. The new timing is the more accurate one: it is
  the Z80 running for exactly the time the hardware lets it run. There are
  no audio goldens in the suite.

## Every SOUND TEST entry

From a state in the SOUND TEST, each entry chosen with left/right and A, 4 s
recorded: all 41 songs (01-29) and the sound effects 2A-6E play, except
S.E. 6A, which is silent also when requested from the OPTIONS list (where
requests were always taken), so 6A is silent by itself and not part of this
issue. Before the fix every entry was silent.

## Validation

- `tests/test_sound_test.py` (ctest `sound_test_plays`): from a state saved
  in the SOUND TEST, entry 01 and S.E. 6E are heard after A (and not just
  before it), the interpreters render the song sample for sample like the
  recompiled code, and lockstep holds while it starts. It fails before the
  fix (silence after A for both) and passes after.
- Full ctest (`CHAOTIX_LONG_TESTS=ON`): 68 of 68 pass, the new test
  included, no golden data changed.
- Lockstep sweep (33 configurations: every special stage, levels with
  `--hud-edges`, the bonus stage, the lobby, the level-mode special stage;
  `--fuzz`, 3000 frames): 33 of 33 bit-identical.
- The music player (`--jukebox`, ctest `music_player`) passes unchanged.

## Cost

None measurable. A catch-up runs the Z80 for time it would have run at
the slice's end anyway, a few times a frame. `--profile`, a level with
fuzz (`--stage 300:0:1:0:2:7:0 --fuzz 11:700:3000`), three runs each,
alternating: 1.365 / 1.366 / 1.377 ms a frame before, 1.366 / 1.375 /
1.397 after (Z80 0.078 before, 0.076-0.078 after; the catch-ups are
counted in the 68K's share). The differences are run-to-run noise.
