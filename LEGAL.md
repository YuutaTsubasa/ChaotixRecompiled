# Legal

## What is in this repository

Only original code written for this project: the recompiler, the reference
interpreters, the hardware compatibility layer, the SDL3 frontend, the tests
and the documentation.

There is **no** game data here — no ROM, no BIOS, no graphics, no music, no
level data, and no bytes extracted from any of them. Nothing in this
repository is derived from a disassembly of the game's code.

## What you need to supply

Your own, legally obtained copy of *Knuckles' Chaotix (Japan, USA)* for the
Sega 32X — SHA-1 `0c2fff7bc79ed26507c08ac47464c3af19f7ced7`. The build and the
game both refuse to run without it. No 32X BIOS is required.

Where you may obtain that ROM, and whether you may dump it from your own
cartridge, depends on the law where you live. This project takes no position
on it and provides no ROM, no link to one, and no help finding one.

## What the build produces

Running the build with `CHAOTIX_ROM` set writes C++ into `generated/`. That
code is a translation of the game's own machine code and is therefore a
derivative work of Sega's copyrighted program. The same is true of any binary
compiled from it — the desktop executable, the Android APK, an iOS build.

Consequently:

- `generated/`, `coverage/*.cov` and build outputs are gitignored, and must
  stay that way.
- **Do not distribute the built game** — not the executable, not the APK, not
  a "portable" folder, not to friends, not on a release page. Each person
  builds it from their own ROM.
- Do not open issues or pull requests containing ROM bytes, generated sources,
  memory dumps of copyrighted data, or screenshots-as-attachments of extracted
  assets.

## Trademarks

*Knuckles' Chaotix*, *Sonic the Hedgehog*, *Sega*, *Mega Drive*, *Genesis* and
*32X* are trademarks of Sega. This project is not affiliated with, endorsed
by, or supported by Sega, and the names are used only to say what the software
is compatible with.

## No warranty

This is a hobby preservation and reverse-engineering project, provided as is.
See the project's licence for the exact terms.
