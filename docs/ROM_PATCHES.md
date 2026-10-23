# ROM patches

Requested in issue #35: graphics and other mods without rebuilding against an
edited ROM. The game applies IPS and BPS patches, the formats ROM hacking
tools write, to the ROM in memory when it starts. Your ROM file is never
changed, and taking a patch out undoes it.

## Using it

1. Run the game once. It creates `Mods/Patches` in the user data folder (on
   Android, `ChaotixRecompiled/Mods/Patches` once OPTIONS > EXTRAS > MODS >
   MODS FOLDER has access; see [MUSIC_MODS.md](MUSIC_MODS.md)). A mod pack
   keeps its patches in its own `Patches` folder ([MOD_PACKS.md](MOD_PACKS.md)).
2. Put `.ips` or `.bps` files there. They are applied in name order, so
   `01_sprites.ips` comes before `02_palette.bps`.
3. Start the game. The log lists each patch as applied or skipped, with the
   reason (a damaged file, or a BPS made for another ROM).

Patches must be made for Knuckles' Chaotix (Japan, USA), SHA-1
`0c2fff7bc79ed26507c08ac47464c3af19f7ced7`, the only ROM the program accepts.
BPS checks this itself and refuses anything else; IPS cannot tell.

The game checks the ROM's header checksum as it boots and stops at a blank
screen if it is wrong. Patches made from a hacked ROM usually carry the fixed
checksum; if a patch does not, it is fixed after patching.

## What a patch may change

**Data** (graphics, palettes, level layouts, text, ...): anything. The
recompiled code reads data from the ROM as the original does, so it sees the
patched bytes.

**Code** also works, more slowly. The recompiled code was made from the
unpatched ROM, so a patch over code must not be quietly ignored:

- The 68K runs its code straight from ROM. The build records which ROM bytes
  that is (`g_rom_code` in the generated tables). If a patch changes any of
  them, the 68K runs in the interpreter; the log's `execution:` line says so.
- The SH-2s run code copied from the ROM into SDRAM. Recompiled SH-2 code is
  checked against the unpatched ROM before it runs, so a routine a patch
  changed fails that check and runs in the interpreter; the rest stays
  recompiled.

The interpreters are what the recompiled code is tested against, so a patched
game behaves as it would on the original hardware. The cost is speed, which
matters on a slow device.

## Not done

- Patches for other ROMs (the Japanese prototype, other regions).

For replacing graphics with image files instead, see
[TEXTURE_MODS.md](TEXTURE_MODS.md).

## Validation

- `rom_patch.*` unit tests: IPS records, runs, growth and truncation; BPS's
  four actions, wrong-ROM and damaged-patch refusals; a patched `Rom` keeping
  the unpatched image and the user ROM's SHA-1 for the recompiled code.
- `rom_patches` (with the ROM): a data patch keeps the 68K native; a patch
  over 68K code switches it to the interpreter; a patch over SH-2 code copied
  into SDRAM fails validation. In all three the recompiled machine stays
  bit-identical to the interpreter (lockstep).
