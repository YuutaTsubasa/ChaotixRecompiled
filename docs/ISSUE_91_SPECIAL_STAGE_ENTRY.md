# Issue #91: black bars on the way into the special stage

Reported from a video review: with widescreen on, jumping into the giant ring
at the end of a level, black bars come in from the sides before the special
stage starts.

## Reproduced

The recorded run that finishes BOTANIC BASE 1 (`recordings/finished_run.txt`)
reaches the goal with fewer than 50 rings. With 50 rings poked in at frame
3100 and the input after the goal changed to walk left and jump (C) into the
giant ring (`recordings/finished_run_giant_ring.txt`), the tally runs and the
game goes into the special stage at frame 4114. On v0.15.12, 53 px margins:

| frames | on screen | margins |
| --- | --- | --- |
| up to 4113 | the level, the tally | the level |
| 4114-4143 | a white flash | black bars |
| 4144-4151 | the stage's starfield fading in through a dissolve | black bars |
| 4152 on | the stage | the stage, widened |

So for 38 frames (about two thirds of a second) the picture was 4:3 with black
bars, then widened as the stage began. The stage select's special stage goes
the same way, at frames 381-418.

## Why

The level is over at 4114: the plane size changes from the level's 64x32
cells to the stage's 32x32, so the level's margins let go (as they should:
issue #7). The stage's own widening (issue #18, `special_stage_active`) waits
for the stage's rectangle, which the stage sets at 4151. In between nothing
claimed the margins.

Both pictures in between are the Mega Drive's. The white is the backdrop
colour, and the starfield is plane B, which the game fills wider than the
screen, as it does during the stage. The 32X layer is empty apart from the
dissolve's specks. So the margins only have to be shown. The one thing in the
way is the stage's black frame: 16 px Mega Drive sprites down both sides at X
0 and 304 (issue #21). Widened, they would stand in the picture as two black
columns, so they are hidden here as in the stage, and the margins stay black
on the rows above and below, where the frame's bars are.

## Repair

`patches::special_stage_entry` recognises the scene: the stage's plane size
(reg 16 = 0x00), the stage's code loaded, the full-screen rectangle still in
place, in either game mode the stage runs in (0x18 in play, 0x20 from the
stage select). `begin_frame` sets `Machine::special_entry` for it when no
other scene has the margins, and the compositor frames it like the stage:
margins shown, frame sprites hidden. Nothing in the game is patched.

## Validation

- `widescreen_the_way_into_the_special_stage` (new): the played route. The
  entry is wide for exactly frames 4114-4151, and a frame of the flash and of
  the starfield have the margins filled (hashes).
- `widescreen_the_way_into_the_special_stage_from_the_stage_select` (new): the
  same 38 frames from frame 381, then the stage's margins from 419.
- The special stage's end, its results screen and the return to the title
  look as before.
- The full test suite passes, including the golden frame and attract runs, so
  no other scene is taken for the entry.

## Not reproduced

The video also shows a galaxy circle growing over the level with parts of the
margins black. That was recorded around v0.15.0-0.15.3. On v0.15.12 the
same route goes white flash, starfield, stage, as above, and no circle was
seen. If it still happens on a newer version, the issue can be reopened with
how it was reached.
