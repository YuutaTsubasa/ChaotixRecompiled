# Issue #44: lobby shortcuts

The request: in story mode, choose the partner directly instead of using the
Combi Catcher, and choose the time of day before entering a zone; saves and
progress as in the original; default the original behaviour.

What was built: OPTIONS > EXTRAS > LOBBY SHORTCUTS (SHORTCUTS off/on,
PARTNER, TIME OF DAY). With it on, the choice is written into the game's own
two variables each time the game starts WORLD ENTRANCE or one of the five
attractions. The code is `src/runtime/lobby.{h,cpp}`, called from the patch
hook at the mode dispatcher.

Addresses: `$8xxxxx` is 68K code in ROM (window `$880000` = ROM 0), `FFxxxx`
68K work RAM. Found with `rom_analyzer --refs-to` / `--disasm`,
`chaotix_headless --trace` and `--peek-every`.

## How the game does it

### The partner: FFE03A

`FFE038` is the player's character and `FFE03A` the partner's, each a
character id times 4 (0 Mighty, 2 Knuckles, 3 Charmy, 4 Vector, 5 Bomb,
6 Heavy, 7 Espio; 1 is the gap in the game's table, see CHARACTERS.md).

The Combi Catcher is in WORLD ENTRANCE, right after the ATTRACTION
INFORMATION board:

- Its box (the COMBI / CATCHER sign with the arrow, the object at x `$640`)
  runs `$8A8644`: when the player touches it (`$89B726`) and the pad byte
  reads exactly Down (`$8A8662`: `cmpi.b #2`), it creates the catcher's
  controller object and keeps it in `FFFDD0` (`FFFF` while there is none),
  with the player object at `+8` and the partner object at `+A`. Bit 5 of
  `FFC21D` is set once the catcher has been used.
- The partner is put in a pen with every character but the player's (with
  Knuckles playing: Mighty, Charmy, Vector, Bomb, Heavy and Espio). The
  claw is steered for ten seconds and drops by itself at TIME UP.
- The capsule it comes up with keeps its character at `+30`; `$8A9936`
  hands it to the partner object through `$88F85E` (`$8A9960`), which writes
  `FFE03A` (or `FFE038` if the object is the player), loads the palette
  (`$88F896`) and queues the 32X command that loads the art (`$88F878`).

Measured: from the TIME ATTACK lobby (Knuckles and Espio), walking to the
box and pressing Down at frame 1300, the claw came up with HEAVY: `FFE03A`
went from `1C` to `18` at frame 3050.

The partner is not saved. The save keeps the player (`$8BE586`).

### The time of day: FFDFF6

Everything that draws a time of day reads bits 1-2 of `FFDFF6` (MORNING,
DAY, SUNSET, NIGHT): `and #6` (`$8883D6`, `$8889D4`, `$8A7604`) or
`lsr #1; and #3` (`$888ACC`, `$8894C4`). Bit 0 is a half step:

- every level finished in an attraction adds one (`$890F72`), so the time
  moves on every second level;
- INTRODUCTION's end sets it from the total play time `FFE016`:
  `((time / 3600) mod 3) x 2` (`$890F52`-`$890F6A`);
- the save keeps `FFDFF6 & 7` (`$8BE590`) and loading puts it back
  (`$8BE536`).

The lobby and the levels both follow it: SPEED SLIDER and BOTANIC BASE at
NIGHT, and the night lobby that a time over returned to, were seen in the
runs below.

### The rest of the flow

- The level number of an attraction is its progress byte, `FFDFFE + zone`
  (INTRODUCTION's end sets all five to 1, `$890F28`), not anything the lobby
  chooses. The attraction itself comes from the room of screens before the
  catapult (`FFDFF2`).
- A finished level ($8905FA, from `$890F72`): progress byte = level + 1,
  `FFDFF6` + 1, `FFE016` += the level's time, then if `FFE01C` < 4 the save
  (`$8BE55A` packs the progress, the player and `FFDFF6 & 7` into the slot's
  record at `FFFA04 + 32 x slot`; `$8BE3C0` checksums `FFFA00-FFFAFF` and
  writes it to SRAM), then WORLD ENTRANCE level 0 in mode `0x18` through the
  dispatcher (`$891002`).
- A time over (`FFE010` reaching `$A000`, `$8AB03A`) goes back to the lobby
  without saving.
- TIME ATTACK's stage request makes `FFE01C` = `FFFF` (NO SAVE), as the
  game's own stage select does; story mode leaves the DATA slot there.

## The design

With LOBBY SHORTCUTS on, `lobby::apply` runs in the patch hook at the mode
dispatcher (`$883262`, `patches::kModeDispatch`), the same place and the same
way TIME ATTACK's request is applied. When the scene about to start is
WORLD ENTRANCE level 0 or a level (1-5) of places 0-4, in mode `0x18`:

- PARTNER: `FFE03A` = id x 4, unless it is the player's own character or
  slot 1, which the catcher could never give; then the game's partner stays.
- TIME OF DAY: `FFDFF6` = (`FFDFF6` & 1) | time x 2. The half step is kept,
  so the level end counts on as before; a count the game has carried past 7
  comes back into range.

The scene then loads with them, exactly as it loads with TIME ATTACK's
COMBI and AT-TIME. Nothing else is touched: not `FFE01C`, not the progress,
not the player. So:

- Every value written is one the game produces itself (any character but the
  player's comes out of the catcher; every time of day comes round), and a
  save written afterwards is one the game could have written.
- Applied at the lobby too, the lobby shows the choice and a partner chosen
  beforehand walks with you; the catcher still works there, but the chosen
  partner is put back when the attraction starts. CATCHER and GAME leave
  each to the game.
- Changing the choice from the pause menu while in the lobby takes effect
  when the attraction starts. It is also put back if a level restarts through
  the dispatcher; the values are the same unless the choice was changed.
- INTRODUCTION, TRAINING, the special and bonus stages and the title are not
  touched. TIME ATTACK's own start returns before `lobby::apply`, and the
  front end turns the shortcuts off while a run is going
  (`apply_lobby_shortcuts` in `sdl_main.cpp`).
- Two players: the partner is the second player's character; the choice
  applies the same way.

Determinism: the hook runs at the same instruction in the interpreter and the
recompiled code, and only reads `Machine::lobby_partner/lobby_time`, which
every machine of a run is given (headless `--lockstep` sets the reference's
too). Off (both -1), it returns before reading anything, so the frames are
the same as before. The choice is a host setting, like the volumes: it is not
in save states (the app gives it before every frame), and a session recording
carries each change as a `lobby FRAME PARTNER TIME` line that
`--replay-input` applies.

### PARTNER: NONE (issue #55)

The PARTNER row goes CATCHER, the characters, NONE. NONE is story mode
played alone: the attractions' levels are played with the ring tether's
PARTNER SOLO (docs/RING_TETHER.md), which sends whichever partner the game
has away the way the Combi Catcher does and keeps it away (hold CALL to bring
it back for that life). It writes no partner: `lobby::apply` leaves `FFE03A`
to the game, as CATCHER does, so the lobby and the catcher work as before
and the partner walks with you there; only in an attraction's level is it
sent away. In INTRODUCTION, TRAINING, the special and bonus stages it stays,
as with GAMEPLAY > PARTNER SOLO, which applies in the attractions only.

Precedence: with LOBBY SHORTCUTS on and PARTNER NONE, the attractions are
played SOLO whatever OPTIONS > GAMEPLAY > PARTNER says; otherwise (the
shortcuts off, or any other PARTNER) GAMEPLAY's setting applies. GAMEPLAY's
own setting is never changed, so turning the shortcut off (or picking a
character) brings it back from the next frame. TETHER strength is
GAMEPLAY's either way. Not in TIME ATTACK (the shortcuts are off there) and
not in LEVEL SELECT, whose own PARTNER card offers NONE (docs/LEVEL_SELECT.md).
Two players: SOLO is for one player, so NONE then does nothing.

How: NONE is `lobby::kPartnerNone` (-2) in `Machine::lobby_partner`, given
before every frame like any other choice. `apply_tether` in `sdl_main.cpp`
works out the PARTNER to play with each frame through
`tether::partner_in_force` (SOLO when NONE is picked, else GAMEPLAY's) and
puts that in `Machine::tether_partner`, so the emulation sees an ordinary
SOLO. Being in the machine, both travel to a netplay guest with the host's
settings (`net::NetSettings`: `lobby=-2,..;tether=..,2`). Fairness is SOLO's:
awards pause and a speedrun run is not kept as a best while it is in force
(`tether::any`).

Considered and not done: a menu popping up when the catcher starts, with the
claw made to yield the pick (overriding `d0` at `$8A9960`). The claw would
still lift the capsule it grabbed, showing one character and handing over
another, and the host cannot pause the game in the middle of a frame to ask;
a setting applied at the dispatcher has neither problem.

## UI

OPTIONS > EXTRAS > LOBBY SHORTCUTS opens a page of its own (like CHEATS), so
EXTRAS stays short enough for a phone. It is the ordinary menu, so keyboard,
gamepad and touch all work, and it is reachable from the pause menu. Config:
`[Lobby] Shortcuts / Partner / Time` (-1 for CATCHER / GAME; Partner -2
for NONE).

## Tests

- Unit: `lobby.*` (which scenes take the choice, what is written, what is
  left alone; NONE writes nothing and travels in the netplay host settings),
  `menu.lobby_shortcuts_are_set_from_extras_and_kept` and
  `menu.the_lobby_shortcut_partner_can_be_none` (the cycle, the config round
  trip of -2, the precedence over GAMEPLAY).
- `lobby_shortcut_partner_none_plays_the_attractions_alone`: the walk below
  with PARTNER NONE (and GAMEPLAY > PARTNER FOLLOW): the tether plays SOLO
  from the start, BOTANIC BASE 1 starts with the game's partner (Espio,
  `FFE03A` = `1C`), which is then sent away (state `$14`, `FFFCEA` = 4) and
  not drawn; lockstep bit-identical. Headless: `--lobby-partner none`.
- `lobby_shortcuts_pick_the_partner_and_the_time`: WORLD ENTRANCE as story
  mode leaves it (DATA 1, every attraction at level 1), walked into BOTANIC
  BASE 1 with VECTOR and NIGHT chosen; the level runs with `FFE03A` = `10`,
  `FFDFF6` = 6, `FFE01C` still 0, lockstep bit-identical. (The stage request
  only stands in for INTRODUCTION, which a script could not get through; see
  below.)
- `lobby_shortcuts_a_finished_level_still_saves_to_its_data_slot`: the
  recorded finished run, saved to DATA 1, with the shortcuts on: the save
  happens on the same frame and the slot's record is byte for byte the one
  written with them off; the lobby that follows has VECTOR and DAY (`FFDFF6`
  7 -> 3); lockstep bit-identical.
- `lobby_shortcuts_off_leave_the_walk_as_it_was` and
  `lobby_shortcuts_off_leave_the_combi_catcher_as_it_was`: off, the walk and a
  use of the catcher (which comes up with HEAVY) give the frame hashes taken
  from the build before this change (`tests/golden/lobby_*_every50.txt`).
  Those runs, and the recorded finished run, were also compared frame by
  frame and state by state with that build: identical.
- The 33-configuration lockstep sweep with `--lobby-partner 4
  --lobby-time 2`: bit-identical.

## Limits

- A real story start (DATA LOAD > NEW) was driven through INTRODUCTION's
  first level (holding right, jumping, a spin dash at the quarter pipe), but
  its second level needs the tether tricks the tutorial teaches, so the
  tests start the lobby with a stage request and set the story state by
  hand, as `lockstep_a_finished_level_saves_to_its_data_slot` already does.
- No script reaches an attraction's goal: a level entered through the
  shortcut and then finished is not in the tests. The save routine reads
  `FFDFF6 & 7` and the progress as it always has, and the shortcut only ever
  writes a time the game itself produces.
