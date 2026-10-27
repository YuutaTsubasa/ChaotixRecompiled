# Fast-forwarding wait loops, exactly (issue #62)

Much of what the 32X's CPUs do is wait: the master SH-2 for V-blank and for
the 68000's commands, the slave SH-2 in a delay loop between sound
interrupts, the 68000 for its V-int flag. An emulator such as PicoDrive saves
that time by noticing a CPU spinning on a register and stopping it until
another CPU writes there, which moves when things happen and so is not exact.
This port must stay bit-identical to its reference interpreter (lockstep), so
it only skips iterations whose outcome is certain, and charges their cycles
exactly.

## Where the cycles go

`chaotix_headless --interp --hot-blocks N` counts the emulated cycles spent in
each basic block. Over 3,000 frames, the wait loops' share of each CPU's
cycles:

| Scene | Master SH-2 | Slave SH-2 | 68000 |
|---|---|---|---|
| BOTANIC BASE 1 (`--stage 300:0:1:0:2:7:0`) | 83%: V-blank poll 51%, delay 25%, COMM poll 7% | 81%: delay | 37%: V-int flag |
| Lobby (`--stage 300:7:0:0:2:7:0`) | 81%: V-blank poll 52%, delay 26%, COMM poll 3% | 81% | 38% |
| Special stage (`--stage 300:10:0:0:2:7:0`) | 54%: frame counter poll 49%, V-blank poll 4% | 82% | 80%: V-int flag |
| Title / attract | 58%: V-blank poll 29%, delay 17%, COMM poll 12% | 80% | 61%: V-int flag, a frame counter, COMM |

The loops, in the master SH-2's SDRAM code:

| Address | Loop | Waits for |
|---|---|---|
| `$06000A24` | `mov.w @r1,r0; cmp/pz r0; bt self` (r1 = `$2000410A`) | V-blank (VBLK, the 32X VDP status) |
| `$06000A1E` | the same with `bf` | the end of V-blank |
| `$060030B0` | `dt r0; bf self` after each auto fill | the fill to finish (a fixed delay) |
| `$060008F8` | `mov.l @(lit),r1; mov.w @r1,r2; tst r2,r2; bt self` | a command in COMM0 |
| `$06001234` | `mov.l @r1,r0; cmp/hs r2,r0; bf self` (r1 = `$06003824`) | two V-ints (special stage) |

The slave runs `dt r0; bf self` at `$060002F8` between its PWM sound
interrupts. The 68000 spins on `tst.b ($FFFFD1).w; beq/bpl self` (set by its
V-int handler), `tst.w ($A15120).l; bne self` (COMM) and
`cmp.w ($FFFFE004).w,d0; beq self`.

## Why skipping is exact here

The scheduler (`Machine::run_slice`) runs each slice as 68000, Z80, master
SH-2, slave SH-2, one after the other. While one CPU runs its slice nothing
else does: no other CPU writes memory, and interrupts are raised and timers
advanced between slices (`start_line`, `onchip_advance`, `pwm_advance`).
Interrupts are taken only at block boundaries, in the interpreter and the
generated code alike.

So take a loop that is one block branching back to itself, whose instructions
only read memory and registers and write registers and flags, with no value
carried from one iteration to the next (every register or flag it reads it
either does not write, or writes earlier in the same iteration). If an
iteration reads exactly what the previous one read, it takes the branch again
and leaves exactly the same registers and flags: the only thing it changes is
the cycle count. Its cost is fixed (the timing model charges fixed cycles per
instruction; no bus wait states for these reads). And the boundary check after
it (slice end, a pending interrupt the mask lets through, SLEEP) can only fail
on the cycle count, since the rest does not change within the slice.

The generated code therefore runs one iteration normally and then, at the
branch back:

1. `n` = the iterations still to go before the slice's cycles run out,
   `ceil(cycles / cost)`, exactly where the interpreter's boundary check
   would stop.
2. `k` = how many further iterations are certain to read what this one read
   (below).
3. It subtracts `min(n, k) * cost` cycles. If that reaches the slice end it
   returns to the scheduler with the PC on the loop, as the interpreter's
   last iteration would; otherwise it runs the next iteration normally, which
   reads the new value (and leaves or comes round again).

The skipped iterations' state is overwritten by the real iteration that
follows, so only their cycles matter.

### What a read returns later

* **Memory and COMM** (`recomp::sh2_pure_load`, `m68k_pure_load`): ROM, SDRAM,
  the frame buffer, the cache data array and the COMM ports for the SH-2s;
  work RAM and COMM for the 68000. Only other CPUs change them, never within
  this one's slice, and reading has no side effects: `k` is unlimited. (This
  part existed for the SH-2s before; it now also covers the 68000.)
* **The 32X VDP status word** (`$4100`-`$41FF`, word `$A`): VBLK is fixed for
  the line (`line` only changes between slices), but HBLK and PEN follow the
  reader's own time in the line (`vdp32x_read`: `pos >= 2600 || pos < 0`,
  with `pos` from `sh2_now_mclk`). `Machine::sh2_read_stable_to` computes the
  last remaining-cycle count at which a read still returns what the previous
  read returned: the SH-2 clock is `base + budget - cycles`, the master clock
  `clock * 7 / 3` rounded down, so the next edge of H-blank (at `pos` 0, then
  2600) is reached at SH-2 clock `ceil(3 * (line_start + edge) / 7)`. With
  the offset of each read inside the iteration known at recompile time,
  `k = (cycles_at_read - last) / cost`. The status word's low byte (FS) and the
  other VDP registers do not change within a slice.
* Anything else (PWM, the FIFO, on-chip registers, the Mega Drive VDP): `k =
  0`, so every iteration runs as before.

A counted delay `dt rN; bf self` is skipped in closed form (this existed):
the counter and T after `k` iterations are known.

For the 68000 the generated code checks at run time that the iteration it
just ran cost what was computed at recompile time, and that each address it
read is work RAM or COMM, before skipping.

Loops with a host hook (patches.h) on any of their instructions are never
skipped, since the hook would run fewer times.

## Checking it

* Lockstep: the interpreter still runs every iteration, and the generated
  code is compared against it every frame (all CPU registers and cycle
  counts, all memory, the picture). `lockstep_idle_skip_level` and
  `lockstep_idle_skip_lobby` cover the V-blank poll in a level and the lobby
  with input; the existing lockstep tests and the 33-configuration sweep
  cover the rest.
* `chaotix_headless --no-idle-skip` makes the generated code run every
  iteration; `idle_skip_changes_nothing` (tests/test_idle_skip.py) checks the
  state hash every 50 frames is the same with it on and off, in a level, the
  lobby and a special stage, and that most of the SH-2s' cycles were skipped.
* `idle_skip.vdp_status_stays_as_read_until_hblank_changes` checks
  `sh2_read_stable_to` against the reads themselves at every cycle count
  around both edges of H-blank, in and out of V-blank, for word, byte and long
  reads and mirrors.
* The headless runner prints the share skipped, e.g. in BOTANIC BASE 1:
  `wait loops fast-forwarded: 68K 32.9%, MSH2 82.2%, SSH2 81.8% of cycles`.

## Also: finding writes over code

A write to SDRAM (or the cache data array) checks whether it touches
recompiled code, so that code is validated again before it next runs
(issue #31). That check scanned every code range, 411 of them, on each SDRAM
write to a page that holds code. It is now one bit per byte
(`Machine::code_ranges_changed`), giving the same answer for every write
(`idle_skip.code_writes_found_by_bits_as_by_ranges`).

## Results

Desktop (i9-14900KF, Windows, MinGW GCC 15, Release), `chaotix_headless
--frames 3000` (including the 79 frames to reach the stage), best of two
runs, before and after taken alternately:

| Scene | Before | After | Speed-up |
|---|---|---|---|
| BOTANIC BASE 1 | 1.090 ms (917 fps) | 0.755 ms (1,324 fps) | 1.44x |
| Lobby | 1.063 ms (941 fps) | 0.720 ms (1,388 fps) | 1.48x |
| Special stage | 0.846 ms (1,182 fps) | 0.805 ms (1,243 fps) | 1.05x |
| Title / attract | 0.865 ms (1,157 fps) | 0.689 ms (1,452 fps) | 1.26x |

Of that, in an earlier run (best of three) the wait loops alone took
BOTANIC BASE 1 from 1.158 to 1.005 ms and the lobby from 1.150 to 0.953 ms;
the code bitmap the rest, to 0.815 and 0.763 ms.

`--profile` in BOTANIC BASE 1 (ms per frame): master SH-2 0.520 -> 0.169,
slave SH-2 0.148 -> 0.123, 68000 0.092 -> 0.076; video (0.44) is now the
largest part, 42% of a frame.

The armeabi-v7a runner cross-compiles without warnings, and the generated
SH-2 code calls no division helper (`__aeabi_*div*`): the fast-forward's
divisions are by constants. It was not run on an ARM CPU (no device or
emulator here), so the phone gain is not measured; the SH-2s' share of a
frame there should shrink about as it does on the desktop.

The special stage gains least: its master SH-2 is mostly drawing polygons,
and its main wait (a frame counter in SDRAM) was already skipped.

## Not done

* Waits spread over several blocks or calls, e.g. the slave's outer loop
  (read a command byte, jump through a table, delay): each slice still runs
  one round of it. What is left per slice is the scheduler entering each CPU
  once; skipping whole slices would need the same proof for the outer loop.
* Reads that change with time other than the VDP status (the Mega Drive
  VDP's H/V counter, PWM status, on-chip timers) are not predicted; none is
  in a hot loop in this game.
