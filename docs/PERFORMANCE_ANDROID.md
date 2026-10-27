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

Desktop, `chaotix_headless --frames 3000`, best of three (ms per frame):

| Scene | Before | After | |
|---|---|---|---|
| BOTANIC BASE 1 | 1.158 | 0.815 | 1.42x |
| Lobby | 1.150 | 0.763 | 1.51x |
| Special stage | 0.900 | 0.849 | 1.06x |
| Title / attract | 0.925 | 0.727 | 1.27x |

`--profile`, BOTANIC BASE 1, before -> after: master SH-2 0.520 -> 0.169 ms,
slave SH-2 0.148 -> 0.123, 68000 0.092 -> 0.076, video 0.45 -> 0.44 (now 42%
of a frame, the largest part). Lockstep stays bit-identical (sweep and
tests); `--no-idle-skip` runs every iteration for comparison.

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
