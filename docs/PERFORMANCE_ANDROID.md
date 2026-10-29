# Performance on Android, especially 32-bit (issue #54)

Issue #54 reports lag in the 32-bit (armeabi-v7a) APK, without a device model
yet. This is what was checked without a device, what changed, and what a
player can try.

## Measuring it: OPTIONS > VIDEO > SHOW FPS

SHOW FPS (off by default, `[Video] ShowFps` in `chaotix.ini`) draws a small
counter in the top right corner of the window:

```
FPS  59.9  EMU  60.0
MS    4.2  MAX   9.8
INTERPRETER
```

* **FPS**: frames presented per second (the display's refresh rate with
  V-SYNC on, when the phone keeps up).
* **EMU**: game frames emulated per second. **60.0 means full speed.** Below
  that the game runs slow (it does not skip frames to keep up).
* **MS**: the average time it took to emulate one game frame over the last
  second, in milliseconds; **MAX** is the worst. At 60 frames a second there
  are 16.7 ms per frame for everything, so an average over about 12 ms leaves
  too little for the picture and sound and the game slows down.
* **INTERPRETER** appears only if the game runs on the reference
  interpreters instead of the recompiled code (much slower). A release APK
  always has the recompiled code; it would only show with a ROM other than
  the one the code was generated from.

The counter is drawn by the host over the window, like the speedrun overlays:
the machine, the game's own picture and screenshots of it never see it. It
keeps clear of the award counter, a speedrun timer in that corner and the
touch buttons, is hidden while the menu is open, and grows with the screen
and with MENU SIZE so it is readable on a phone.

What the reporter should tell us: the EMU and MS figures in a level (and the
phone model). EMU at 60 with lag that is still visible would point at the
display or sound rather than the emulation; EMU under 60 with MS over ~16 means
the CPU is too slow for the emulation as it is.

## What was checked

### The armeabi-v7a build uses the recompiled code, at -O3

Read from the Gradle/CMake build of the 0.15.1 APKs
(`platforms/android/app/.cxx/Release/*/armeabi-v7a/compile_commands.json`):

| | armeabi-v7a | arm64-v8a |
|---|---|---|
| Build type | Release (`-DCMAKE_BUILD_TYPE=Release` in build.gradle overrides the debug variant's) | same |
| Optimisation | `-O3 -DNDEBUG` | `-O3 -DNDEBUG` |
| Target | `--target=armv7-none-linux-androideabi26 -march=armv7-a -mthumb` | `aarch64-none-linux-android26` |
| NEON | on (`__ARM_NEON` is defined: NDK clang enables it for armv7 by default since r21) | always |
| Float ABI | softfp (the only one Android allows), VFPv3-D32 | hard |
| Integer divide | **none** (`__ARM_FEATURE_IDIV` undefined: armv7-a does not guarantee it) | hardware |
| LTO | off (as on every platform) | off |
| Recompiled code | compiled in: all 25 generated files, `CHAOTIX_HAVE_GENERATED=1` | same |

So 32-bit gets the same optimisation level and the same recompiled code as
64-bit; it does not fall back to the interpreter. `-mthumb` (Thumb-2) is the
NDK's default: on Cortex-A cores it runs about as fast as ARM mode with
smaller code, which matters for the instruction cache with 24 MB of generated
code. `-mfpu=neon` and `-mfloat-abi=softfp` are already in effect. Nothing to
change in the flags.

The debug variant (`assembleDebug`, how releases are built) only makes the
Java side debuggable; the native library is the Release build above.

### What 32-bit costs: division

The difference from 64-bit is mostly arithmetic the CPU has no instruction
for. Listing the calls to the compiler's helper routines in every armv7
object (`llvm-objdump -dr`, `__aeabi_*div*`) found two that run thousands of
times a frame:

1. **Block dispatch.** Every block of recompiled code is found through
   `std::unordered_map`, which reduces the hash modulo a prime: on armv7 a
   call to `__aeabi_uidivmod` per lookup (`m68k_exec`, `sh2_exec`,
   `m68k_call_dynamic`, `sh2_call_dynamic`, `sh2_callee_valid`). In a level
   that is about 1,100 68000 and 3,700 SH-2 dispatches a frame (counted on
   the desktop build in BOTANIC BASE 1), plus the dynamic calls.
2. **Scheduler clocks.** `Machine::run_slice` (4 slices a line, 1,048 a
   frame) and `audio_advance` divide the 64-bit master clock by 7 (68000,
   SH-2 at 3/7) and by 1008 (an audio sample): four `__aeabi_uldivmod`
   calls per slice, about 4,200 a frame. (Divisions by 3 and 15 are already
   turned into multiplies by clang; 7 and 1008 are not.)

Together roughly 9,000 library divisions a frame, each tens of cycles on a
Cortex-A7/A9 without a divide instruction: an estimated 0.3-0.7 ms of every
16.7 ms frame. Not the whole story, but pure overhead.

Other helper calls are in code that runs rarely (save states, speedrun text,
menu pictures, the mod tools) or is the guest's own DIVU/DIVS and the SH-2's
divide unit, which have to divide.

### Other per-frame work on mobile

* Rewind (LZ snapshots every other frame) and the TIME ATTACK ghost (a
  second machine on its own thread) are off by default on Android and iOS
  (`rewind_default`, `ghost::default_mode`).
* The menu's zone pictures are made on a worker thread only while a menu is
  open, and held otherwise (issue #31).
* Sprite and tile recorders exist only with a texture pack or DUMP GRAPHICS.
* The SDL renderer path is the default one (OpenGL ES 2 on Android): one
  480x240 XRGB8888 streaming texture updated per frame, no CPU format
  conversion.
* Audio: the machine makes 53 kHz samples and SDL's audio stream resamples on
  its own thread; the machine's audio is about 2% of a frame.

Where a frame goes, on the desktop build (`chaotix_headless --profile`,
BOTANIC BASE 1, 3,000 frames): master SH-2 37%, VDP/32X video 31%, slave SH-2
10%, 68000 8%, Z80 5%, audio 2%. (Since the wait loops are fast-forwarded,
below: video 42%, master SH-2 16%, slave SH-2 12%, Z80 8%, 68000 7%,
audio 3%.) A phone CPU core is several times slower
than a desktop one, and a 32-bit-only phone (Cortex-A7/A9/A53 running a 32-bit
system) is at the bottom of that range; the emulated 32X runs two SH-2s, a
68000 and a Z80 together, so such a phone may simply not reach 60.

### Not measured on an armv7 CPU

No 32-bit ARM device or emulator was available: WSL has no `qemu-arm`
(Ubuntu and Arch), and installing it needs root. The armv7 headless runner
cross-compiles with the NDK (`ANDROID_ABI=armeabi-v7a`, see below), and the
division helper calls were counted in its objects before and after.

## What changed

* **SHOW FPS** (above).
* **Dispatch without division** (`src/game/recomp_dispatch.cpp`): the 68000
  and SH-2 entry tables are now open-addressed power-of-two tables with a
  multiplicative hash; a lookup finds exactly what the map found. No
  `__aeabi_uidivmod` is left on the dispatch path.
* **Clock division without the library** (`src/runtime/divide.h`):
  `div_const<D>` divides a 64-bit count by a small constant as long division
  in 16-bit digits, each step a 32-bit division by the constant (a multiply).
  Exact for every input (`tests/test_divide.cpp` checks it against `/` on about
  230,000 values for each divisor); used on 32-bit hosts only, 64-bit hosts
  keep `/`. `run_slice` and `audio_advance` no longer call
  `__aeabi_uldivmod`.

Emulation is unchanged: the lockstep sweep (33 configurations, recompiled
against the interpreter) stays bit-identical, as do the golden frame hashes.

### Wait loops fast-forwarded (issue #62)

About four fifths of both SH-2s' cycles go to waiting (the master for
V-blank, polling the 32X VDP status, and for COMM; the slave in a delay loop
between sound interrupts), and a third or more of the 68000's (its V-int
flag). The generated code now charges the iterations of such a loop whose
outcome is certain in one step instead of running them, exactly: for the VDP
status, whose H-blank bits change during a slice, up to the cycle where a
read would first differ. Writes over recompiled code in SDRAM are also found
with a bitmap instead of a scan of 411 ranges. Design, proof and tests:
[IDLE_SKIP.md](IDLE_SKIP.md).

Desktop, `chaotix_headless --frames 3000`, best of two (ms per frame):

| Scene | Before | After | |
|---|---|---|---|
| BOTANIC BASE 1 | 1.090 | 0.755 | 1.44x |
| Lobby | 1.063 | 0.720 | 1.48x |
| Special stage | 0.846 | 0.805 | 1.05x |
| Title / attract | 0.865 | 0.689 | 1.26x |

Not yet measured on an ARM CPU; the armv7 runner builds, and the new code
calls no division helper there.

`--profile`, BOTANIC BASE 1, before -> after: master SH-2 0.520 -> 0.169 ms,
slave SH-2 0.148 -> 0.123, 68000 0.092 -> 0.076, video 0.45 -> 0.44 (now 42%
of a frame, the largest part). Lockstep stays bit-identical (sweep and
tests); `--no-idle-skip` runs every iteration for comparison.

### Drawing (issue #62)

Every frame is still drawn whole, every line of it, with the same pixels;
the code that draws it does less work per pixel. Where the time went, per
line of BOTANIC BASE 1 (cycle counters around each stage, desktop): the two
planes 33%, the 32X line 22%, the priority merge of planes and sprites 20%,
sprites 13%, merging the 32X line over the Mega Drive one 11%.

* **Planes**: with nothing replaced (no texture pack text, no tile
  replacements, not dumping graphics, which is how the game normally runs),
  each 8-pixel tile row is read from VRAM once and turned into its 8 pixels
  with an 8 KiB table (by pattern byte, palette line, priority and flip), made
  in registers and stored as one word. A stretch of the line with one
  V-scroll value reads one name table row. Planes A and B are no longer
  cleared first: they are drawn across the whole line.
* **One byte per layer pixel** (palette index and priority bit, was two),
  and the sprite/A/B priority order as six selections from the lowest up, which
  the compiler turns into SIMD (SSE2 on x86-64, NEON on ARM) for 16 pixels at
  a time.
* **Sprites**: the sprite list is walked once and kept while the 640 bytes of
  the attribute table stay the same (compared every line, so a change in the
  middle of a frame is seen on the next line, as before); a sprite's 8-pixel
  columns come from the same pattern-row table. The pixel budget and the
  overflow/collision flags are counted exactly as before.
* **Palettes**: CRAM's 64 colours and the 32X palette's 256 entries are
  converted once and again only when they change (compared every line);
  the 32X line was converting every pixel.
* **One pass for the merge**: the Mega Drive line is kept as CRAM indices
  (with a bit for the backdrop), and the 32X line merges each pixel with it as
  it is made, instead of writing both lines out in colour and merging them in
  a third pass.
* No division per line on 32-bit ARM (the sprite budget divides by the line
  width, now a constant 320 or 256).

The general paths (replacement graphics, the lobby's margin palette,
shadow/highlight, the debug layer views) are the code as it was. Tests
(`tests/test_vdp_render.cpp`) draw random VDP states line after line, changing
sprites, colours, scroll and registers between lines, through the fast and the
general paths and require the same pixels and status flags, and check the 32X
merge and the caches' invalidation.

Desktop (i9-14900KF, MinGW GCC 15), `chaotix_headless --frames 3000
--profile`, best of five, ms per frame (video / whole frame):

| Scene | Video before | Video after | | Frame before | Frame after | |
|---|---|---|---|---|---|---|
| BOTANIC BASE 1 | 0.408 | 0.157 | 2.6x | 0.968 | 0.705 | 1.37x |
| BOTANIC BASE 1, widescreen 53 + 16 rows | 0.547 | 0.205 | 2.7x | 1.129 | 0.769 | 1.47x |
| Special stage | 0.432 | 0.181 | 2.4x | 1.009 | 0.749 | 1.35x |
| Lobby | 0.364 | 0.143 | 2.5x | 0.923 | 0.695 | 1.33x |
| Title / attract | 0.341 | 0.132 | 2.6x | 0.882 | 0.668 | 1.32x |

The app runs the same paths: with an empty texture pack
loaded, as the app always has one, BOTANIC BASE 1 is 0.158 ms of video.
Video went from 42% of a BOTANIC BASE 1 frame to 22%; the SH-2s are now the
largest parts again.

### SH-2 (issue #62)

The two SH-2s run the same instructions as before, cycle for cycle; what
changed is the work around them. Where their time went, sampled with `perf`
(WSL build of the headless runner, BOTANIC BASE 1): about a third in the
generated code itself, the rest in checking that code was still valid, the
interpreter, memory accesses that went through the general bus, and the
frame buffer auto fill.

* **Writes over code, per routine.** Generated code that runs from SDRAM is
  checked against the ROM before it runs, again whenever code may have
  changed, and running code hands over to the interpreter for the rest of its
  block if its code changes under it (the special stage rewrites its own
  routines). "Code may have changed" was one counter for all of SDRAM, moved
  by any write over any recompiled byte. In a level the game keeps data at
  `$06005310`-`$060053BD`, where the special stage's routines live while it
  runs: about 370 such writes a frame, each sending every routine to be checked
  again (memcmp against the ROM) and the running one to the interpreter.
  Now each routine has its own count, moved only by a write that changes one
  of *its* bytes (`Machine::sh2_fn_code_writes`); a write of the value already
  there counts for nothing. Checks over 3,000 frames: BOTANIC BASE 1
  4,049,824 -> 76, special stage 88,975 -> 139. Interpreted blocks in a
  level: from about 2.5% of the frame's samples to none.
* **The cache data array inline.** The slave keeps its sound mixer and its
  working data in the SH-2's 4 KiB cache RAM (`$C0000000`), which went through
  the general bus, a call per access: about 14,000 a frame. It is now read and
  written in place like SDRAM, in 64-byte granules; a granule holding
  recompiled code is still written through the bus, which notes the change.
* **Memory accesses always inlined.** Generated functions are large, and the
  compiler stopped inlining some accesses (and the block boundary check) once
  the cache path was added; they are now forced inline, the fast part only.
* **Auto fill in runs.** The 32X VDP's auto fill (the master clears the frame
  buffer line by line with it; 4.6% of a level's samples) wrote one word at
  a time with a widescreen check per word; without widescreen it is now at most
  two runs of words (to the end of the 256-word line, then from its start).
* **No division in the on-chip timers.** The FRT and WDT clock dividers are
  powers of two: shifts and masks instead of a division and remainder per
  slice and CPU, which on armeabi-v7a was a call to `__aeabi_uidivmod`, about
  2,100 a frame. The armv7 build's SH-2 code and scheduler call no division
  helper now (the remaining one in `onchip_advance` is the FRT's clear on
  compare match, which the game does not use).

Tests: `sh2_bus.cache_array_inline_as_the_bus` makes 400,000 random reads and
writes of every size, through the inline accesses on one machine and the bus
on another, and requires the same values, memory and code epochs;
`idle_skip.code_writes_found_by_bits_as_by_ranges` checks every write around
the edges of overlapping routines' code (including across 256-byte chunks)
counts for exactly the routines it overlaps, and only when it changes a byte;
`idle_skip.code_write_count_wrapping_checks_everything` the wrap of a count.

Desktop (i9-14900KF, MinGW GCC 15), `chaotix_headless --frames 3000` (79
frames to reach the stage included), best of two, before and after run
alternately, ms per frame:

| Scene | Before | After | |
|---|---|---|---|
| BOTANIC BASE 1 | 0.527 | 0.437 | 1.21x |
| Lobby | 0.507 | 0.427 | 1.19x |
| Special stage | 0.567 | 0.540 | 1.05x |
| Title / attract | 0.483 | 0.433 | 1.12x |

`--profile` (ms per frame; the timing itself adds about 0.2 ms), master /
slave SH-2:

| Scene | Before | After |
|---|---|---|
| BOTANIC BASE 1 | 0.157 / 0.112 | 0.096 / 0.092 |
| Lobby | 0.164 / 0.113 | 0.100 / 0.091 |
| Special stage | 0.202 / 0.121 | 0.195 / 0.095 |
| Title / attract | 0.162 / 0.113 | 0.131 / 0.093 |

The special stage's master gains least: its time is polygon drawing, and the
routines at `$060052DE` hold other code there than the one recompiled, so
about 1.4% of its blocks run in the interpreter, as before.

Emulation is unchanged: the lockstep sweep (33 configurations) is
bit-identical, as are the golden frame hashes and the full test suite; and
since lockstep compares two runs of the same runtime, the machine-state and
picture hashes every 10 frames of the 33 sweep configurations and the
title/attract (3,000 frames each) were compared with the build before these
changes: all the same.

Not measured on an ARM CPU here. What is left of the SH-2s' time on the
desktop: the generated code itself (mostly its memory accesses), about one
dispatch per CPU per slice (2,100 a frame, 3,700 with interrupts and returns:
looking the block up and checking its code is valid), and the frame buffer's
overwrite image, which the sprites are drawn into and which has to go through
the bus (zero bytes are transparent). Mapping the frame buffer itself in
place was tried and gave nothing measurable, for that reason.

Not measured on an ARM CPU here. The armv7 build now has no division call
and no stack round trip in the plane loop, and its composition loop is NEON
(clang vectorises it as on x86-64).

The picture of every frame is the same before and after: per-frame image and
machine-state hashes over 6,000 frames of 14 scenes (4:3 and widescreen,
BOTANIC BASE and other zones with random input, special and bonus stages, the
lobby, title and attract), the golden frame hashes, the full test suite and
the 33-configuration lockstep sweep.

## What the player can try

1. **The 64-bit APK** (`ChaotixRecompiled-…-android.apk`) if the phone's
   processor is 64-bit, even with a 32-bit Android: most phones from the last
   eight years are. It installs only if the system is 64-bit too.
2. **SHOW FPS** on, and note EMU and MS in a level.
3. Keep REWIND (EXTRAS) and the GHOST (TIME ATTACK) off; they are off by
   default on phones.
4. WIDESCREEN off draws fewer columns (a smaller picture to compose).
5. MENU BACKGROUND: PLAIN if the menus themselves lag (no zone pictures made
   in the background).
6. V-SYNC on (the default) paces frames to the display; off can help on a
   display that is not 60 Hz.

## Building the armv7 runner yourself

```
cmake -S . -B build-armv7 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=armeabi-v7a -DANDROID_PLATFORM=26 \
  -DCHAOTIX_BUILD_FRONTEND=OFF -DCHAOTIX_BUILD_TOOLS=OFF -DCHAOTIX_BUILD_TESTS=OFF \
  -DCHAOTIX_GENERATED_DIR=<a host build's generated code>
ninja -C build-armv7 chaotix_headless
```

On a device (`adb push` it with the ROM, then `adb shell`):
`./chaotix_headless --rom game.32x --frames 3000 --stage 300:0:1:0:2:7:0
--profile` prints the frames per second and where the time goes.
