# The ring tether (Combi Ring), and options for it

Issue #43 asked for options for the tether that joins the two characters:
how strong it is, a partner that follows on its own the way Tails does in
Sonic 3 & Knuckles, and playing alone. This is how the tether works in the
68K code, found for that, and what the options do with it. Each option is
off by default, and off the game runs exactly as before (golden frame hashes
from the build before the options, `tether_off_*` tests).

Addresses: `$88xxxx` is 68K code in ROM at `xxxx` (the fixed window
`$880000-$8FFFFF` is ROM `0x000000-0x07FFFF`); `FFxxxx` is 68K work RAM.
Character ids, objects and the stats table are as in
[CHARACTERS.md](CHARACTERS.md); the lobby's partner choice is in
[LOBBY_SHORTCUTS.md](LOBBY_SHORTCUTS.md).

How it was found: `chaotix_headless --trace` of single frames in BOTANIC
BASE 1 (`--stage 300:0:1:0:2:7:0`, Knuckles and Espio) while running,
standing, holding and stretching the ring; the routines those traces went
through, disassembled (capstone, from the ROM); `--peek-every` of both
characters' objects and of the tether's variables with scripted input; and
`--poke` to put the partner where the game would not (behind a wall, far
away). "Measured" below means a run whose numbers are quoted; the tests in
`tests/CMakeLists.txt` (`tether_*`) repeat the measurements.

## 1. The two characters

| what | where | notes |
| --- | --- | --- |
| player / partner character | `FFE038` / `FFE03A` | id x 4 |
| player / partner object | `[FFE034]` / `[FFE036]` | `FFAD48` / `FFAE08` in a level |
| player / partner buttons | `FFE04C` / `FFE05C` | 16-byte records, below |
| "the other one" while one is updated | `FFFCEC` | set by `$889C46` / `$889C74` |

Both characters are ordinary objects in the first object list (`FFE028`)
and run the same code: `$889C10` (once a frame, `st FFFCCF` first) walks the
list, and the two character slots (object byte `+6` = 0 and 4) load their
character, their button record into `a5` and the other object into `FFFCEC`,
then `$889D8E` runs the state machine on object byte `+7` with the
character's stats record (`$88BE58`, CHARACTERS.md section 3). The fields
the tether uses:

| off | size | meaning |
| --- | --- | --- |
| +04 | b | draw priority (0x34 / 0x44 for the one in front) |
| +05 | b | bit 7 shown, bit 5 intangible (contact checks skip it, `$88F980`) |
| +07 | b | state: 02 ground, 04 jump, 08 airborne, 0A/0C holding the partner, 0E held (by the other or by an object), 12 hurt, 14 away, 16 bracing (held in place by HOLD), 1A flying back after CALL |
| +08, +0C | l | X, Y (16.16) |
| +18, +1C | l | X, Y velocity (16.16 per frame) |
| +25 | b | bit 0 on the ground, bit 3 facing left |
| +2A / +2B | b | surface angle / the tether's angle (towards the other) |
| +2C | w | ground speed (8.8) |
| +2E | w | the tether's pull while the character moves on its own (below) |
| +30 | w | invulnerability: a hit is ignored while it is not 0 (`$88FBBA`) |
| +32, +34 | w | state 14: how fast the return comes (-1: never) and its phase |

### Buttons, and the computer's partner

A button record (`FFE04C`, `FFE05C`): `+0` the source (bit 7: made up by the
computer), `+1` the button layout, `+2` held, `+3` pressed this frame, `+4`
held last frame, `+6` frames the buttons have stayed as they are (`$890598`).
After the layout (`$8905E2`) the bits are the game's own: 0x01-0x08 the
directions (0x04 left, 0x08 right), 0x10 B, 0x20 C, 0x40 A, 0x80 START.

`$890438` reads the pads once a frame. The player's buttons go into a
16-entry ring at `FFFD00` (index `FFFCFF`); `FFFCFE` is what the player held
16 frames ago. In one-player games `FFE05C` is `0xFE` (`$88696E`; the stage
select writes it too) and the partner's buttons come from `$8904D8`:

- the player's buttons 16 frames late, without A and START (`and #$3F`): the
  partner does what the player did, a little behind -- the same idea as
  Tails in Sonic 3;
- while `FFFCFD` counts down (`$890374` sets it to 255; an object at
  `$8A5164` calls it): "walk to the player" instead, jumping when the player
  stands higher; it stops once the partner is within 32 px and not below;
- within 64 px of the player and in the air, no left/right and no C.

With two players `FFE05C` is `0x10` (the partner reads pad 2) and the
partner is a person.

So in one-player games the partner never acts on its own: it copies the
player with a delay, and the tether drags it along.

## 2. The tether

Once a frame, after the characters (`$8910BA`, only while `FFFCCF` says they
were updated, i.e. not paused):

1. **CALL** for each player (`$891B72`, below).
2. **The camera.** With both in play (`FFFCEA` bit 0) it centres on the
   midpoint of the two (`$891162`, -0xA0/-0x70; a character standing still
   with up or down held looks up or down); otherwise on the player alone
   (`$89111E`; on the partner if the player is away). With the two at most
   175 px apart and the camera between them, both stay on screen: in play the
   partner is not off-screen.
3. **The physics** (`$8912C4`), while `FFFCEA` bit 2 is set and the partner
   is not flying back after CALL. `FFFCF0`/`FFFCF2` (and `FFFCF6`/`FFFCF8`)
   are posts the ring is caught on (objects set them, `$890402`): the length
   is then measured round them (`FFFCDA`, `FFFCDC`, `FFFCDE`; total
   `FFFCE0`), otherwise it is the distance between the two. The angle each
   way goes in `+2B`.

The table at **`$891B06`** (ROM `0x11B06`): a word **100**, the rest length
(no pull while the ring is shorter); a word **75**, the most it stretches;
then one byte per pixel of stretch, `4 x stretch` (0..0xFC), 0xFF from 64 px.
With `d3` = stretch (`FFFCE2`):

- **Past the limit** (`$891BDA`, stretch >= 75): `FFFCE2` = -1 and one of
  the two is put back on the circle of 175 px (round the posts, if any). With
  no post (`$891CE2`): the partner if the player is on the ground and the
  partner in the air, otherwise **the player**. That is the hard yank a
  player feels when the partner is stuck.
- **The force** is `(byte + 1)`, kept in `FFFCE2`, times 8 (`$8913A2`), and it
  works on each character in turn along `+2B`:
  - the player (`$8913A4`): not while a direction is held, nor C (unless
    the button layout is 0x10, or Espio clings, state 32): then the force
    goes into `+2E` instead, and `$88B71E` adds `+2E` x the tether's direction to the
    character's motion each frame it moves -- the drag a running player
    feels. In the air it is added to the velocity (`+18`/`+1C`). On the
    ground: with the computer's partner, only while B (HOLD) is held; then if
    the tether points up from the ground (0xA0-0xE0 from the surface) the
    character is **launched** along the tether (state 08, ground speed plus
    the force: the slingshot), otherwise the force's share along the ground
    is added to the ground speed.
  - the partner (`$891494`): the same, except that with the computer's
    partner it is the player's B that stops it (HOLD keeps the partner in
    place), and with two players the partner's own directions.

Measured (BOTANIC BASE 1, Knuckles walks right holding B from frame 900 for
90 frames; Espio braces, state 16): the ring gives way to 125 px (Knuckles at
x 0x155); on release Espio is flung from x 216 to 556 and swings back.

### HOLD and the slingshot

HOLD is B. With the computer's partner, `$889E1A` stops the partner (ground
speed 0) and, while the ring is taut, turns it to brace (state 16, animation
0x34) facing the tether. The player walks away and stretches the ring; on
release the pull works on the partner (B no longer held) and on the player
(no direction held): whichever is grounded with the tether pointing up is
launched.

### CALL

CALL is A (`$891B72`): pressed, with neither B nor C held, neither character
away or flying back, outside the lobby. It costs **10 rings** -- the counter
`FFE008` goes below zero if there are fewer, and once it is below -99 the run
is over (`$88FE86`: back to WORLD ENTRANCE; measured: random play that kept
pressing A took the counter from 0 to -99 in 4000 frames and ended there).
`$88FF5A` turns the partner into a ball flying to the caller (state 1A,
`$88AB7C`: it shrinks, travels for 64 frames and grows back at the caller's
side), clears the ring's posts and sets `FFFCEA` bit 0.

### Away: state 14

`$88FECE` sends a character out of play: `FFFCEA` bit 0 cleared, intangible
(`+5` bit 5), state 14, invulnerable for 255 frames. Phase 0 (`$88AA30`) it
shrinks and falls; phase 1 (`$88AA5E`) it is hidden and waits 400 px below
the other one, its phase advancing by `+32` each frame; phase 2 (`$88AA8E`)
it grows back at the other's side and is in play again (state 02, bit 0
set). `+32` is `0x400 / d0`; a negative `d0` stores -1, and the return never
comes.

The game uses it:

- **Hit without rings** with both in play (`$88FCD0`): the one hit is hurt
  and the other is sent away for about eight seconds (`d0` = 0x200). With
  rings, the player is hurt and loses them; the partner, hit, loses one.
  With `FFFCEA` bit 0 clear (the partner away), a hit without rings, or any
  hit on the one away, ends the run (`$88FC7C`). The hurt dispatcher at
  `$88FC0C` indexes a table by (which one, rings > 0, `FFFCEA` bits 0-1).
- **The Combi Catcher** in the lobby (`$8A92AE`) sends the partner away with
  `d0` = -1 -- away for good -- while the claw picks the next one, whom
  `$88FF3C` puts in play.

While a character is away: the camera follows the other one, the tether's
physics do not run (`$89111E` skips them), CALL and HOLD do nothing, and the
contact check every object uses (`$88F980`) skips it (`+5` bit 5), as do the
objects that grab characters (`$88FFA2`, `$88FFEA`, `$890054` test for
states 14 and 1A).

### The flags

| | |
| --- | --- |
| `FFFCEA` bit 0 | both characters in play |
| `FFFCEA` bit 2 | the tether's physics on (set by `$8912BA`, cleared during CALL's flight and by `$890430`) |
| `FFFCEA` bit 4 | camera held for a frame (scripted scenes; cleared every frame by `$8911EC`) |
| `FFFCEA` bit 7 | demo playback: the player's buttons come from a script (`FFFD28`) |
| `FFFCCF` | the characters were updated this frame (not paused) |
| `FFE05C` byte | 0xFE one player (the computer's partner), 0x10 two players |
| `FFFCE2` | the force level, -1 past the limit |
| `FFFCF0`-`FFFCF8` | posts the ring is caught on |

There is no stat for the tether in the characters' records: the rest length,
the limit and the force table are shared (`$891B06`), and the only
per-character fields involved are the grab reach (`+12`) and the throw speed
(`+10`) of the hold/throw move (CHARACTERS.md section 3).

## 3. Objects that need both characters

Most objects check each character on its own. These count the characters
standing in them (`$88F980` per character, into a byte) and go on only when
the count is 2:

| compare | object (type index into `$891D72`) | where it is | what it does |
| --- | --- | --- | --- |
| `$898C48`, `$898E6C` | `$898B42` (0x84) | TECHNO TOWER, every level | the lift: both step in, it closes and carries them up |
| `$896566` | `$8963A8` (0x218) | not met in any attraction level (the attract demo runs its code, `coverage/s02_attract.cov`) | built like the lift (same layout of states) |
| `$89A2C8` | `$89A21A` (0x1CC) | not met in any attraction level | puts both in place (`$88FFEA`) and moves on |
| `$89FF7A` | `$89FDEA` (0x258) | SPEED SLIDER 4 | puts both in place and moves on |
| `$8A3266` | `$8A31A2` (0x2DC) | SPEED SLIDER 4 | counts into `+2D`, plays a sound and moves on |

Found by looking for that pattern (two `$88F980` calls on `[FFE034]` and
`[FFE036]` and a compare with 2) over the whole program; where each one is
was found by playing every level of the five attractions with random input (two seeds, 5000 frames each)
and a `--break` on the object's code (an object not met that way may be in
more levels than listed). Measured: in TECHNO TOWER 1, walking right from
the start, the lift carries Knuckles from y 0xC4D to 0xB4C with Espio; alone
and without the SOLO hook below he stands in it at 0xC4D for ever.

Three routines of an object at `$8B36F8`-`$8B37A6` wait for `FFFCEA` bit 0
and place themselves on the partner; no session in `coverage/` and no
random play reached them, and they are not in the five attractions' levels
as far as could be found (probably a scripted scene).

## 4. The options

OPTIONS > GAMEPLAY (also from the pause menu), `[Tether]` in
`Config/chaotix.ini`, `chaotix_headless --tether-strength`, `--tether-partner`
and `--tether-at FRAME:STRENGTH:PARTNER`. They apply in the five attractions'
levels only (game mode 0x18, zones 0-4): not in the lobby, TRAINING,
INTRODUCTION, the bonus or special stages (which have no tether). Everything
is done by patch hooks on the 68K (`runtime/tether.{h,cpp}`, listed in
`patches::kM68kHooks`), which the interpreter and the recompiled code call
before the same instructions, and depends only on machine state and the
settings: lockstep holds with every option (tests, and the 33-config sweep
with the options on).

### TETHER: WEAK / NORMAL / STRONG

The force `d3` is scaled just before `$8913A2` turns it into velocity: x1/2
(rounded up, never 0) or x3/2. The rest length and the 175 px limit stay.
Measured with the stretch above: the ring gives way to x 0x168 (WEAK),
0x155 (NORMAL), 0x14E (STRONG) -- 144, 125 and 118 px. Two players too.

### PARTNER: FOLLOW (one player)

The partner keeps up, and the player is never held back:

- the player's share of the force is 0 (`$8913A2` gives `d3` = 0 for the
  player's part; `$891494` gives the partner its share back from `FFFCE2`),
  so no drag while running, no tug in the air;
- past the limit the partner, never the player, is put back on the circle:
  `$89138E`/`$891392` note where the player stood before `$891BDA` and, if it
  moved the player, put the player back and move the partner by as much the
  other way, which leaves them exactly as far apart as the game made them
  (with or without posts).

While HOLD is held none of this applies, so HOLD and the slingshot work as
before. The partner still copies the player 16 frames late and is pulled
as hard as before. Measured: with Espio put 250 px behind a running
Knuckles, the game yanks Knuckles back to x 0xCE on the next frame; with
FOLLOW he runs on to 0x110 and Espio is pulled onto the circle (x 0x62).

### PARTNER: SOLO (one player)

The partner is sent away the way the game does it and kept away -- the
Combi Catcher's way (`$88FECE` with `d0` = -1, from the hook at `$889C10`
once the partner is in play, tangible, and not held, hurt or flying back).
So: hidden, intangible, no tether, no ring drawn between them, the camera on
the player alone, CALL and HOLD inert -- every one of those is the game's own
handling of a partner that is away. A partner the game itself sends away (a
hit without rings) is kept away too.

What the game needs the partner for:

- **The objects of section 3**: at their compare, with the partner away, a
  count of 1 (the player) becomes 2, and they take the player alone (their
  grab of the partner skips one that is away). Measured: the TECHNO TOWER 1
  lift carries Knuckles up (y 0xB4C).
- **Anything else** (a jump only the slingshot reaches, an object not found):
  holding CALL (A on its own) for half a second brings the partner back, free,
  the game's way (its return at the player's side), for the rest of that life:
  it is sent away again when the level starts again or the next scene does.
  Held, not tapped, so that a habit of tapping CALL does not end SOLO.
- **Hits**: as the game has it without a partner in play, a hit without
  rings ends the run (back to WORLD ENTRANCE), like Sonic without rings. The
  partner, out of play, is kept invulnerable.
- **The goal**: a level finishes as before (measured: the recorded BOTANIC
  BASE 1 run, SOLO from frame 3000, reaches the goal at the same frame and
  hands over to the lobby).

Not done, and why: making the partner truly absent (no object) -- the game
keeps both objects for the whole level, and the camera, the tether and
dozens of objects read `[FFE036]`; and a partner that stays in play but
hidden -- it would still take hits, collect rings, trip switches and be
grabbed by objects in the player's place.

### NONE where the partner is picked (issue #55)

SOLO can also be picked as a partner of NONE: on LEVEL SELECT's PARTNER card
(for that session, 1 player, in the attractions' levels;
docs/LEVEL_SELECT.md) and as the lobby shortcuts' PARTNER (story mode;
docs/LOBBY_SHORTCUTS.md). Either way the game is still given a partner (the
one picked before, or the game's own), and the frame is played with PARTNER
SOLO whatever GAMEPLAY says: `tether::partner_in_force(gameplay,
time_attack, picked_none)` is SOLO when NONE is picked, nothing changed in
TIME ATTACK, and GAMEPLAY's setting otherwise. The app works it out before
every frame (`apply_tether`) and never changes the GAMEPLAY setting, so that
is back as soon as the session ends or the shortcut is off. TETHER strength
stays GAMEPLAY's. The emulation sees an ordinary SOLO in
`Machine::tether_partner`, so everything below (fairness, state, netplay)
is SOLO's.

### Fairness

- **TIME ATTACK** runs ignore the options (the app gives the machine the
  original settings while a run is on; a ghost replays with them off), so
  times and ghosts compare like with like.
- **Achievements** are not earned while any option differs from the
  original, as with cheats; a speedrun with one on is not kept as a personal
  best. PARTNER NONE (above) counts as SOLO.
- A recorded session (`--record-input`) keeps each change of the options
  (`tether FRAME STRENGTH PARTNER` lines) and replays with them.

### State

The settings are, like the lobby shortcuts, given to the machine before
every frame and are not in save states. What SOLO latches -- the partner was
called back -- is (`Machine::tether_called`; save state format 2). FOLLOW's
note of the player's place lives only between two instructions of one
routine.

## 5. Not determined

- `FFFCEA` bits 1, 3 and 5.
- What the object at `$8B36F8` is.
- Whether objects other than those of section 3 need both characters in a
  way random play did not meet; SOLO's held CALL is the way out of those.
