# Issue #93: Knuckles drops out of the glide when turning round

Reported from a video review: while Knuckles glides, pressing back drops him
out of the air instead of turning the glide round.

## Reproduced

BOTANIC BASE 1 with Knuckles (`--stage 600:0:1:0:2:7:0`): run right from frame
1400, jump at 1500 and hold C from 1516 to glide, then press left at 1560 for
60 frames.

- Right let go first (frame 1559), then left: Knuckles turns and glides on to
  the left.
- Right still held while left is pressed, as on a keyboard: from about 1564
  Knuckles is on the ground. The glide ended the moment the game saw left and
  right together.

## Why

A Mega Drive pad's d-pad rocks one way: it cannot send left and right, or up
and down, at once, and the game was never written for it. A keyboard can send
both, and so can a stick and a d-pad, or a keyboard and a pad, which the
frontend reads together. Nothing stopped both reaching the game.

## Repair

`OppositeDirections` (`input/input.h`) cleans each player's pad every frame
before the game, netplay and the input recorder see it. The newer of two
opposite directions wins until it is let go, then the other one, still held,
takes over again. Two pressed on the same frame cancel out. The rule is
applied per axis, so diagonals are unchanged.

## Validation

- `opposite_directions.*` (new unit tests): passing through, the newer
  direction winning and handing back, both on the same frame, the two axes
  apart.
- `a_glide_turns_with_both_directions_held` (new): the glide above with right
  held throughout, through the same rule (headless `--clean-opposites`). It
  must match the clean turn frame for frame (the hash at frame 1600). Without
  the rule the frames differ and Knuckles lands.
- The frontend itself, scripted with `--hold` and recorded with
  `--record-input`: the game receives left+C (0x44) from the turn on, not
  left+right+C (0x4C).
- Full suite: 140/140.
