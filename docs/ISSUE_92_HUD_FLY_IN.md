# Issue #92: the HUD lands at its 4:3 place, then jumps to the edge

Reported from a video review: entering a level, "the UI comes down in the
wrong spot, then snaps into place". With the HUD at the picture's edge
(`WidescreenHud`, issue #28) it did exactly that.

## What the game does

At the start of a level the HUD flies in, scaled down from the middle of the
screen. That fly-in is not the SH-2's HUD command (`$06000C9C`, whose X
immediates issue #28 patches). The 68K draws it itself, as three ordinary 32X
objects (SCORE, TIME and RINGS, each with its numbers, at `$FFB448`, `$FFB608`
and `$FFB5C8` in the recorded walk). They come to rest at X 32, the HUD's 4:3
place, and stay there until the clock starts. Then the objects go and the
SH-2's HUD command draws the HUD from then on, at the patched place.

Measured on the walk the tests use (WORLD ENTRANCE, the catapult, SPEED SLIDER),
53 px margins, HUD at the edge:

| frames | drawn by | SCORE at X |
| --- | --- | --- |
| 2400-2419 | the fly-in objects, scaled | moving in towards 32 |
| 2420-2441 | the fly-in objects, at rest | 32 |
| 2442 on | the SH-2's HUD command | 32 - 53 = -21 |

So the HUD sat at its 4:3 place for twenty-two frames and then jumped 53 px.

## Repair

Every 32X object goes through one 68K routine that pushes its pieces onto the
32X command list (`0x880F50`, the same routine `kPiecesDone` hooks). It reads
the object's screen position into d2/d3 first. A hook right after that read
(`kObjectScreenXY`) takes the HUD's shift off d2 for the three fly-in objects,
which are told apart by their mapping (`kHudIntroMaps`). The shift is the one
the SH-2's HUD gets that frame (`Machine::hud_shift`, set by `set_hud_shift`),
so the fly-in comes to rest exactly where the SH-2's HUD then carries on, and
it flies in towards the edge rather than towards the 4:3 place.

Nothing else is moved: the shift is 0 outside widened levels and without
`WidescreenHud`, and no other object uses those mappings. The level-end tally,
also drawn in bank 19, is unchanged.

## Validation

- `widescreen_hud_flies_in_to_the_edge` (new): SCORE is at X -21 at frame
  2438, before the clock starts. It was at 32.
- `lockstep_widescreen_hud_flies_in` (new): the interpreter and the recompiled
  code take the hook alike over the whole walk and the fly-in.
- The level-end tally of the recorded run draws the same pieces at the same
  places as before.
