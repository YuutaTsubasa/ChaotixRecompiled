# Legal

## What is in this repository

Only original code written for this project: the recompiler, the reference
interpreters, the hardware compatibility layer, the SDL3 frontend, the tests
and the documentation.

There is **no** game data here — no ROM, no BIOS, no graphics, no music, no
level data, and no bytes extracted from any of them. Nothing in this
repository is derived from a disassembly of the game's code.

`coverage/*.cov` *is* tracked on purpose: those files are lists of program
counter values this project's own tools recorded, so the recompiler knows
which addresses are code. They contain no ROM bytes and no instructions.

## What you need to supply

Your own, legally obtained copy of *Knuckles' Chaotix (Japan, USA)* for the
Sega 32X — SHA-1 `0c2fff7bc79ed26507c08ac47464c3af19f7ced7`. Both the build
and the game refuse to run without it. No 32X BIOS is required.

Where you may obtain that ROM, and whether you may dump it from your own
cartridge, depends on the law where you live. This project takes no position
on it and provides no ROM, no link to one, and no help finding one.

## What the build produces, and what the releases contain

Running the build with `CHAOTIX_ROM` set writes C++ into `generated/`. That
code is a machine translation of the 68000 and SH-2 instructions in the ROM:
translated instructions, jump tables, entry-point addresses and the immediate
constants that are part of those instructions.

It is **not** the game. It contains no graphics, no music, no level data, no
palettes, no text — none of the data the ROM mostly consists of. A binary
built from it stops at the first-run setup screen and will not start the game
until you supply your own ROM, which it verifies by SHA-1.

The releases on this repository's GitHub Releases page ship such a binary,
following the practice of other static recompilation projects — [Unleashed
Recompiled](https://github.com/hedge-dev/UnleashedRecomp) and
[Zelda 64: Recompiled](https://github.com/Zelda64Recomp/Zelda64Recomp) — which
distribute the recompiled code and require each user to bring their own copy
of the game.

**Be aware that this position is not settled law.** A mechanical translation
of a program is reasonably argued to be a derivative work of it, and rights
holders have issued takedowns against comparable projects. This project ships
these builds on the view that the recompiled code is an interoperability work
that is inert without the user's own game data, but nobody should treat that
as a guarantee. If you redistribute a build of this project, you do so on your
own judgement and at your own risk.

What remains off limits, without qualification:

- The ROM itself, in any form, whole or in part.
- Game assets extracted from it: graphics, audio, level data, text.
- Committing `generated/`, ROM images or memory dumps of copyrighted data to
  this repository. They are gitignored and CI fails if any appear.
- Opening issues or pull requests containing any of the above.

## Trademarks

*Knuckles' Chaotix*, *Sonic the Hedgehog*, *Sega*, *Mega Drive*, *Genesis* and
*32X* are trademarks of Sega. This project is not affiliated with, endorsed
by, or supported by Sega, and the names are used only to say what the software
is compatible with.

## No warranty

This is a hobby preservation and reverse-engineering project, provided as is.
See [LICENSE](LICENSE) for the exact terms.
