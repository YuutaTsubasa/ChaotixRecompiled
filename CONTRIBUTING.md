# Contributing

Thanks for looking. A few things about this project are unusual, so please
read the first two sections before opening anything.

## Never post game data

No ROM bytes, no files from `generated/`, no memory dumps containing game
code or assets, no disassembly listings. This applies to issues, pull
requests, comments and attachments. An issue containing any of it will be
deleted rather than edited. See [LEGAL.md](LEGAL.md).

Reporting a bug usually needs none of that: a screenshot of the *rendered
game* is fine, and so is a frame number.

## Correctness first

The project's rule, in order: **correctness, then portability, then speed.**

The recompiled code and the reference interpreter must agree exactly. Two
things enforce that and neither may be weakened to make a change pass:

- **Lockstep.** `chaotix_headless --lockstep` runs the interpreter and the
  generated code side by side and stops at the first divergent register,
  memory write or cycle count.
- **Golden frames.** Rendered frames are hashed and compared against stored
  hashes for 4:3, widescreen and 16:9.

If you change `src/cpu/*/*_ops.h` you are changing the semantics *both*
sides share, so run the long tests (`-DCHAOTIX_LONG_TESTS=ON`, `ctest -L
long`) before proposing it. If a golden hash changes, say so in the pull
request and explain why the new image is the correct one — do not regenerate
the hashes quietly.

Do not guess at ROM addresses. Anything not established by analysis is
written down as `UNKNOWN — requires ROM analysis` rather than filled in with
a plausible number.

## Building and testing

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCHAOTIX_ROM=/path/to/chaotix.32x
cmake --build build
cd build && ctest --output-on-failure
```

Without `CHAOTIX_ROM` you get an interpreter-only build and the unit tests,
which is what CI runs. With a ROM you additionally get the lockstep and
golden-frame tests; those are the ones that matter for a change to the CPU
cores, the runtime or the renderer.

Add `-DCHAOTIX_LONG_TESTS=ON` for the multi-minute runs.

## Style

- C++17, 4 spaces, no tabs, UTF-8, LF endings (`.editorconfig` has it).
- Warnings are on (`/W4` on MSVC, `-Wall -Wextra` elsewhere) and the tree is
  warning-free on GCC, Clang and MSVC. Keep it that way.
- Comments explain *why*, especially when the reason is a hardware quirk or
  something found in the ROM. Do not annotate the obvious.
- Match the surrounding code rather than introducing a second style.
- Platform-specific code lives in `src/platform/`; nothing else includes SDL.

## Pull requests

Say what changed and how you know it is right — which tests you ran, and on
what. A change to emulated behaviour without a lockstep run is not reviewable.

Small, focused commits with a real message are much easier to accept than one
large one. [ARCHITECTURE.md](ARCHITECTURE.md) explains how the pieces fit
together and is the best place to start.
