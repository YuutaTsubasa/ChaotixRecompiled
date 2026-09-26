# Achievements

The project ships a small achievement system in the spirit of
[RetroAchievements](https://retroachievements.org/): conditions are written
against the game's own memory and checked once per emulated frame. Everything
is local — the definitions are this project's own, nothing is downloaded, and
no account or network connection is involved.

- Definitions: [`assets/achievements.ini`](../assets/achievements.ini)
- Engine: [`src/game/achievements.{h,cpp}`](../src/game/achievements.h)
- Unlocks: `SaveData/achievements.ini` in the user data directory
- In game: a notification appears on unlock; `F7` lists everything.
  On a gamepad the list is the left stick click (every other button is
  part of the emulated 6-button pad); on a touch screen, the unlock
  counter in the top corner, where dragging scrolls the list. `B`, `F7`
  or a tap closes it again.
- Shipped definitions are compiled into the binary; a copy in the user data
  directory or next to the executable overrides them.
- Off switch: `[Achievements] Enabled = false` in `Config/chaotix.ini`
- Headless: `chaotix_headless --rom <rom> --frames N --achievements assets/achievements.ini`

## Definition format

```ini
[vars]
rings = word FFE008          ; byte | word | long, 68K work RAM address

[achievement First Ring]
id = first_ring              ; stable key stored in the save file
description = Collect a ring.
points = 5
when = mode == 0x38 and rings >= 1
```

`when` is a list of comparisons joined by `and`; every one must hold in the
same frame. Each side is a variable, a number (decimal or `0x` hex), or
`prev(variable)` — the value that variable had on the previous frame, which is
how "it just went up" conditions are written:

```ini
when = rings > prev(rings)
```

An achievement unlocks once and is then written to the save file immediately.

## Game variables

Found by watching 68K work RAM while the game runs and cross-checking against
what the HUD shows (`tools`-free: the scan programs live in the session logs,
the method is described below).

| Address | Width | Meaning |
|---|---|---|
| `FFDFDE` | word | game mode, see below |
| `FFDFF2` | word | attraction id: 0 Botanic Base, 1 Speed Slider*, 2 Amazing Arena, 3 Techno Tower, 4 Marina Madness, 6 Isolated Island, 7 = not in a level |
| `FFDFF4` | word | level number shown on the title card (1-5) |
| `FFE008` | word | ring count; reset to 0 when a level starts |

\* ids 0, 2, 3, 4 and 6 were confirmed by reading the title card of each
attract demo while logging the variable; 1 is inferred from the remaining
attraction and is the one to re-check first if a "Welcome to..." achievement
never unlocks.

### How these were found

1. Log a value the HUD shows (rings) at two frames of the attract demo, then
   search work RAM for words holding exactly those values at those frames.
   Three candidates survived; only `FFE008` also reset at every level start.
2. For the mode and attraction id, snapshot RAM in several scenes (title,
   five different attract demos) and keep the addresses that are constant
   within a scene and differ between scenes.
3. Confirm by replaying the whole attract loop and printing every change: the
   transitions line up exactly with the level start and end frames.

The level timer is *not* a plain RAM counter (no encoding of the displayed
time matched), so time-based achievements are not defined yet.

## Game modes, and why nothing is earned by the demo

`FFDFDE` takes more values than it first appeared to, and getting this wrong
meant every achievement fired during the attract demo and none of them while
anyone was playing:

| Mode | What is on screen |
|---|---|
| `0x00` | boot |
| `0x08` | the title screen and the game's own menus |
| `0x38` | **the attract demo** - the game playing itself |
| `0x48`, `0x60`, `0x68` | the selection screens behind Scenario Quest and Training |
| `0x18`, `0x58` | a level, with the player in control |

Each value was checked against a screenshot taken at that moment rather than
guessed. The `[rules] require` line excludes the modes that are not play,
which is safer than listing the ones that are: an unlisted play mode would
silently earn nothing, while an unlisted non-play mode at worst lets a
level's own title card count as starting that level.

```ini
[rules]
require = mode != 0x00 and mode != 0x08 and mode != 0x38 and ...
```

`require` is checked before any achievement, so an individual `when` only has
to say what the achievement itself is about.

## Adding achievements

Add a `[achievement ...]` block to `assets/achievements.ini`, give it a unique
`id`, and check it with the headless runner:

```bash
build/chaotix_headless --rom <rom> --frames 12000 --achievements assets/achievements.ini
```

It prints the frame at which each achievement unlocks, so a definition that
fires too early (or never) is obvious. `tests/test_achievements.cpp` covers
the parser and the trigger evaluation with a fake memory image, and the
`achievements_attract` test checks that the shipped definitions still unlock
during the attract demos.
