# Mod packs

Requested in issue #42. There are many kinds of mods now — graphics
([TEXTURE_MODS.md](TEXTURE_MODS.md), [SPRITES_32X.md](SPRITES_32X.md)),
characters ([CHARACTER_MODS.md](CHARACTER_MODS.md)), music
([MUSIC_MODS.md](MUSIC_MODS.md)), drum samples ([SAMPLE_MODS.md](SAMPLE_MODS.md)), ROM patches ([ROM_PATCHES.md](ROM_PATCHES.md))
and touch control images — and mixing the loose files of several mods in one
folder does not scale. A *mod pack* keeps one mod in a folder of its own, so
it can be turned on and off, put in order and taken out again whole.

## The format

A pack is a folder in `Mods` (in the user data folder; on Android
`ChaotixRecompiled/Mods` in the phone's storage once OPTIONS > EXTRAS > MODS >
MODS FOLDER has access, on iOS `Mods` in the app's Documents) holding a
`mod.ini` and any of the folders the `Mods` folder itself has:

```
Mods/
  ChineseText/                 one pack (the folder name is its id)
    mod.ini
    preview.png                optional
    Textures/                  md_<hash>.png, 32x_*.png, mdtext_*, mdsprite_*, sheets
    Characters/                <name>.ini
    Music/                     <id>.wav, <id>.loop
    Patches/                   *.ips, *.bps
    Touch/                     dpad.bmp, a.bmp, ...
    Samples/                   <NN>.wav (the music's PWM drums)
  FasterKnuckles/
    mod.ini
    Characters/knuckles.ini
  Textures/                    loose files, as before packs (see below)
  Music/
  ...
```

Every folder inside a pack works exactly like the one of the same name in
`Mods` and is described in that kind's document. Folder names are matched
without regard to case (`textures` is fine). The dumps (`Textures/Dump`,
`Textures/Sheets`) are always written to the `Mods` folder's own `Textures`.

`mod.ini`, in UTF-8:

```ini
[Mod]
name = Traditional Chinese (zh-TW)
author = YuutaTsubasa
version = 1.0.1
description = Redraws the game's English text in Traditional Chinese.
description = A second description line adds a line (or write \n).
url = https://gamebanana.com/mods/000000
preview = preview.png
requires = 0.8.0
```

| Key | |
| --- | --- |
| `name` | Shown in the list. Without it, the folder's name. |
| `author`, `version` | Shown beside the name and on the pack's page. |
| `description` | The pack's page. Repeat the key, or write `\n`, for more lines. |
| `url` | Shown on the pack's page (where to find updates). |
| `preview` | A PNG in the pack's folder, shown on its page (any size; it is scaled down). |
| `requires` | The oldest game version the pack works with. On an older game the pack still loads, marked NEEDS A NEWER GAME, and the log says so. |

`[Mod]` is optional; lines starting with `;` or `#` are comments; unknown
keys are logged and ignored. A folder without `mod.ini` that holds one of the
mod folders (a pack unpacked without it) is a pack too, named after its
folder. Any other folder in `Mods` is ignored, and the log says so.

## The list: OPTIONS > EXTRAS > MODS

The page lists the packs, highest priority first, with their versions and
ON or OFF:

- **Left / Right** turns the highlighted pack on or off.
- **Enter** (or a second tap) opens the pack's page: its version, author,
  description, link, what it holds, its place in the list and its preview
  image, with **ENABLED**, **MOVE UP** and **MOVE DOWN**.
- **RELOAD MODS** reads the `Mods` folder again (a pack just copied in shows
  up) and applies the list at once.
- On Android, **MODS FOLDER** asks for access to the shared `Mods` folder
  (it used to be on EXTRAS itself).

A list longer than the screen scrolls with the selection; the `^` and `v`
markers step through it on a touch screen.

The choices are kept in `[Mods]` in `Config/chaotix.ini`, by folder name:

```ini
[Mods]
# highest priority first
Order = ChineseText, FasterKnuckles
Disabled = FasterKnuckles
LooseFiles = true
```

A pack that is new since the last time joins the end of the list (the lowest
priority), turned on. A pack taken out of the folder keeps its place and its
setting for when it comes back. A config from before packs has none of these
keys: every pack is on, and the loose files are on. Folder names containing
commas cannot be listed; avoid them.

## When two packs replace the same thing

The higher pack in the list wins, for every kind:

| Kind | "The same thing" | Winner |
| --- | --- | --- |
| Mega Drive tiles | the same `md_<hash>.png` | the higher pack's |
| 32X sprites | the same bank and frame (or hash) | the higher pack's, whether it is a `32x_<bank>_<frame>.png` or a cell of a `32x_<bank>.png` sheet; within one pack a frame's own file beats its sheet's cell, as before |
| Text by tile sequence or by sprite | the same file name (`mdtext_title.txt`) | the higher pack's file, whole; the lower one's is not loaded |
| | different files matching the same text equally | the higher pack's (its patterns are tried first) |
| Characters | the same `<name>.ini` | the higher pack's file, whole: a lower file's `[Stats]` do not survive under a higher file with only `[Palette]` |
| Music | the same `<id>.wav` | the higher pack's (with its own `.loop`) |
| Touch images | the same image (`a.bmp`, `a_pressed.bmp`) | the higher pack's |
| ROM patches | the same bytes | all enabled packs' patches are applied, the lowest pack's first and each pack's in name order, so the highest pack's bytes are the ones left |

A disabled pack has no effect at all: none of its files are read and its
patches are not applied.

### Loose files

The `Mods` folder's own `Textures`, `Characters`, `Music`, `Patches` and
`Touch` folders — the layout from before packs — still work, as the entry
LOOSE FILES. It is always at the top of the list: files you put there by
hand override every pack, which is what they did when they were the only
mods, and lets you adjust a pack without editing it. It can be turned off
(`LooseFiles = false`) but not moved. It appears once one of those folders
holds a file other than its README.

### Migrating loose files to a pack

Make a folder in `Mods`, move the mod's folders (`Textures`, ...) into it and
add a `mod.ini`. Nothing else changes: the file names and formats are the
same.

## What applies when

- **At once** (when you leave the MODS page after a change, or press RELOAD
  MODS): graphics, text, character *colours*, music and touch images. These
  change only what is shown and heard, never the game's logic (a music file
  only tells the sound driver to leave that piece to the file player), so
  lockstep, save states and replays are unaffected. A piece of music that
  is playing from a file carries on if a file is still there for it.
- **The next time the game starts**: ROM patches and character *stats*. They
  change the game itself, and are applied to the ROM before the machine is
  reset, never during a session. While the list differs from what the session
  started with, RELOAD MODS says "patches and stats: restart the game". Save
  states record the patched ROM's SHA-1, so a state saved with other patches
  is refused rather than loaded into the wrong game.

## Headless

`chaotix_headless --mods-dir DIR` loads a `Mods` folder as the app does: the
packs' patches, characters, graphics and music, in the list's order.
`--mods-order A,B` gives the order (highest first, folder names),
`--mods-disable C,D` turns packs off and `--mods-no-loose` leaves out the loose
files. `--tiles`, `--characters`, `--music-dir` and `--patch` given as well
come above every pack. The app takes `--mods-dir DIR` too (its config then
gives the order).

## Publishing a pack (GameBanana and elsewhere)

1. Make the pack folder as above, with a `mod.ini` (name, author, version,
   description, the page's URL once you have one) and, ideally, a
   `preview.png`.
2. Zip the pack folder itself, so that the zip holds `MyMod/mod.ini`,
   `MyMod/Textures/...`. Players unzip it into their `Mods` folder.
3. Say in the description which version of Knuckles' Chaotix Recompiled it
   needs (and put it in `requires`), and that the player needs their own ROM:
   never include ROM data. Graphics drawn over dumps are fine to share; a
   patch is shared as the `.ips` or `.bps`, never as a patched ROM.
4. A new version replaces the folder; the player's order and on/off setting
   stay, since they are kept by folder name. Keep the folder name the same
   from version to version.

Not done yet: one-click install from GameBanana (its `chaotixrecompiled:`-style
custom URL scheme, which would download and unzip a pack into `Mods`). Until
then packs are installed by unzipping them. Cheats are not part of packs:
they stay in OPTIONS > EXTRAS > CHEATS and `[Cheats]` in the config.
