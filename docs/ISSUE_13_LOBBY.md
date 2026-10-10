# Issue #13: widescreen glitches in the character / stage select area

Reported for WORLD ENTRANCE (zone 7, the lobby the game returns to between
levels): outdoor and indoor tiles drawn wrongly, the palette switching at the
wrong time, chunks missing when the catapult launches the player into a level,
and blank areas. All of it only in 16:9. The reporter suggested keeping that
area 4:3 as a fallback.

## What was wrong

Reproduced with `chaotix_headless --stage 300:7:0:0:2:7:0 --press
600:right:2600 --wide 53`, which walks from the lobby entrance through the
indoor rooms to the catapult and into SPEED SLIDER.

- The rooms are narrower than a widened view. The stage select room's camera
  range is 2816..2816, so the camera clamp centres it and the margins show
  whatever lies beside it in the layout: the outdoor entrance (drawn with the
  indoor tiles and palette) on one side, blocks the engine never streams on
  the other.
- The lobby changes palette where the camera reaches a point; the margins pass
  that point first and show the next area's tiles in the old palette.
- The catapult moves the camera 13-15 px a frame. The widescreen ring shift
  leaves 96 - E px of streamed tiles right of the view (43 px at 16:9) against
  192 natively, and the engine fell behind: black blocks appeared, some inside
  the 4:3 centre.

Blacking out the margins beyond the room's camera range fixed the first point
but not the other two.

## The repair

WORLD ENTRANCE stays 4:3 (`patches::wide_zone`): the plane ring is not
shifted there, the camera clamp is left alone and the margins are black, so it
looks exactly as a 4:3 window shows it. Every level, the special stage and the
attract demos stay widescreen.

## Validation

- Over the walk above, every captured lobby frame (every 50 frames from 700 to
  1950, catapult included) is pixel-identical to a 4:3 run in the centre, with
  all-black margins; SPEED SLIDER is widescreen again from frame 2187.
- `widescreen_leaves_the_lobby_4_3`: the margins must first go live in SPEED
  SLIDER, not in the lobby.
- Unit test `widescreen_render.the_lobby_stays_4_3`.
- `widescreen_lets_go_when_a_level_hands_over` still passes: a finished level
  now hands over to a 4:3 lobby.
