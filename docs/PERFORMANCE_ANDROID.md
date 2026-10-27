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
SKIPPED    0.0
INTERPRETER
```

* **FPS**: frames presented per second (the display's refresh rate with
  V-SYNC on, when the phone keeps up).
* **EMU**: game frames emulated per second. **60.0 means full speed.** Below
  that the game runs slow. Frames whose picture FRAME SKIP left out (below)
  count: the game ran them, so EMU can be 60 while FPS is lower.
* **MS**: the average time it took to emulate one game frame over the last
  second, in milliseconds; **MAX** is the worst. At 60 frames a second there
  are 16.7 ms per frame for everything, so an average over about 12 ms leaves
  too little for the picture and sound and the game slows down.
  With FRAME SKIP the frames without a picture are cheaper, so MS falls
  when it skips.
* **SKIPPED** (only while FRAME SKIP is not OFF): pictures left out per
  second. 0 when the phone keeps up; with AUTO each of them is a picture
  that would never have been shown anyway.
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
10%, 68000 8%, Z80 5%, audio 2%. A phone CPU core is several times slower
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
* **FRAME SKIP** (below).
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

## FRAME SKIP (issue #62)

OPTIONS > VIDEO > FRAME SKIP, `[Video] FrameSkip` in `chaotix.ini`:

| Setting | What it does |
|---|---|
| OFF | Every frame's picture is drawn (as before). Default on desktop. |
| AUTO | Only the pictures that would never be shown are left out. Default on Android and iOS. |
| 1 / 2 | At most one picture in every two / three frames, all the time. |

**What a skipped frame leaves out.** The game runs every frame, skipped or
not: all four processors, the sound, the 32X frame buffers the SH-2s draw
into and swap, the interrupts and line counters. Only the host's picture of
the frame (`Machine::framebuffer`) is not made: the Mega Drive VDP lines, the
32X lines read out of the displayed frame buffer, their merge, the
widescreen margins and black bars, and the output-only work riding on them
(replaced tiles and text, the sprite pack's composition, character colours).
The window then shows the last picture drawn, and its texture is not
uploaded again.

One thing drawing does feed back into the game: the VDP's sprite overflow
and collision flags, which the 68000 can read in the status register. A
skipped line still works them out (`Vdp::sprite_status_line`): the same
sprite list walk with the same per-line and pixel limits and the original
pixels of the native columns, but no drawing. So does the VDP's host-side
state that save states carry (`Machine::set_vdp_output`). Everything else
on the drawing path only reads the machine.

**AUTO.** The main loop runs one game frame per elapsed frame period, up to
three in a pass when it has fallen behind, and presents the last one
(`plan_pass`, `src/frontend/fast_forward.h`). The pictures of the earlier
frames in such a pass were drawn and never shown. AUTO leaves out exactly
those (`FrameSkipper`, `src/frontend/frame_skip.h`), so what is on the screen
is the same as with OFF; the time saved is what lets the pass catch up. At
most two pictures in a row are left out (three frames a pass), so the picture
never stops; with headroom every pass runs one frame and every picture is
drawn. Fast-forward passes (2X, 4X, MAX) also draw only their last picture.
OFF on desktop, where a frame takes a fraction of its 16.7 ms and AUTO would
have nothing to skip; AUTO on phones and tablets, where it costs nothing to
look at and only ever helps. FRAME SKIP is OFF while DUMP GRAPHICS is on
(the tiles are collected as they are drawn) and has no effect in netplay or
while rewinding.

**What it saves.** `chaotix_headless --profile`, BOTANIC BASE 1
(`--stage 300:0:1:0:2:7:0`), 3,000 frames, recompiled code, desktop build,
three runs each (the machine was shared, so totals vary; the shares are
steady):

| | video, drawn frame | video, skipped frame | share of a drawn frame |
|---|---|---|---|
| 4:3 | 0.45-0.60 ms | 0.045-0.050 ms | 31% |
| 16:9 (`--wide 53 --wide-bottom 16`) | 0.60-0.90 ms | 0.06 ms | 37% |

A skipped frame does about 90% less video work: it costs about 70% (4:3)
or 65% (widescreen) of a drawn one. On a phone that needs, say, 20 ms a
frame with drawing, AUTO's three-frame passes take about 14 + 14 + 20 = 48
ms for 50 ms of game time, so EMU reaches 60 where it was 50. A phone that
needs more than about 14 ms without drawing still runs slow; that is the
SH-2 work issue #62's next part goes after.

**How it is checked.** `chaotix_headless --frame-skip N` draws one frame in
every N + 1 (and every frame a shot, a golden or a `--hash-every` hash is
taken of); `--game-hash-every N` prints a fingerprint of the machine without
its picture. Tests (`tests/CMakeLists.txt`):

* `lockstep_frame_skip_*`: the recompiled machine skips, the interpreter
  reference draws every frame; the machines stay bit-identical every frame
  (the sprite flags included, which the lockstep comparison now checks) and
  each picture drawn equals the reference's. A level from the title screen,
  a widescreen level with rows below, the special stage and the bonus stage.
* `frame_skip_leaves_the_game_alone`: the recompiled machine with and
  without `--frame-skip 2`, the same game fingerprint every frame and the
  same pictures where both draw.
* `golden_frames_with_frame_skip`: the golden frames, drawn after frames
  that were not.
* Unit tests (`test_frame_skip.cpp`): which pictures each setting draws, and
  `sprite_status_line` against `render_line` on random sprite tables at
  every widescreen width.

## What the player can try

1. **The 64-bit APK** (`ChaotixRecompiled-…-android.apk`) if the phone's
   processor is 64-bit, even with a 32-bit Android: most phones from the last
   eight years are. It installs only if the system is 64-bit too.
2. **SHOW FPS** on, and note EMU and MS in a level.
3. **FRAME SKIP**: AUTO (the default on phones) changes nothing on the
   screen; if EMU is still under 60, 1 or 2 draw fewer pictures all the
   time, so more of every frame is left for the game.
4. Keep REWIND (EXTRAS) and the GHOST (TIME ATTACK) off; they are off by
   default on phones.
5. WIDESCREEN off draws fewer columns (a smaller picture to compose).
6. MENU BACKGROUND: PLAIN if the menus themselves lag (no zone pictures made
   in the background).
7. V-SYNC on (the default) paces frames to the display; off can help on a
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
