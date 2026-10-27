# Issue #61: real widescreen in WORLD ENTRANCE

WORLD ENTRANCE is zone 7: the lobby (levels 0 and 1), the final boss (2)
and the ending's scenes (3-6, LEVEL_SELECT.md). It was kept 4:3, with black
side bars: the lobby for issue #13 (ISSUE_13_LOBBY.md), the final boss for
issue #56 (ISSUE_56_FINAL_BOSS.md); OPTIONS > VIDEO > WIDE LOBBY widened the
lobby anyway, glitches and all. Now the lobby and the final boss are
widescreen by default, without the glitches, and WIDE LOBBY is gone. The
ending's scenes stay 4:3, and so does the INTRODUCTION Eggman scene (on
purpose: Eggman walks out to the left, and widened that looks wrong).

## Reproducing it

    chaotix_headless --rom <rom> --stage 300:7:0:0:2:7:0 --press 600:right:2600 \
        --wide 53 --wide-everywhere --shot 1400,1900
    chaotix_headless --rom <rom> --level-select --stage 300:7:2:0:4:7:0 \
        --wide 53 --frames 2400 --shot 2000

The first walks from the lobby's entrance through its rooms, takes the
catapult and lands in SPEED SLIDER (frame 1989); the second starts the final
boss. With the lobby widened as WIDE LOBBY did:

- frame 1400, the stage select room: the outdoor entrance in the left margin
  in the indoor colours, blocks never drawn on the right;
- frame 1900, the catapult: dark blocks in the sky, inside the 4:3 view too;
- the catapult landed in SPEED SLIDER at frame 1985, four frames before a 4:3
  run did: the widescreen changed the game.

## What was wrong, and the repair

### The camera

In a level the widescreen camera clamp keeps the margins inside the level by
holding the camera E px away from its range's ends. The lobby's rooms, its
palettes, its background pictures and the catapult all follow the camera, and
so does the final boss, so the clamp changed the run itself. In WORLD
ENTRANCE the camera is left alone: the centre of the widened picture is the
4:3 picture, frame for frame, and the margins show what lies beside it.

### Plane B: pictures, not a layer

The lobby's plane B is a set of 512 px pictures side by side, one per part of
the lobby (outdoors, the corridors, the stage select room, the sky beside the
catapult). The game picks one by moving plane B's X ($889144: constant over a
part, sliding with the camera between parts) and scrolls it by line within
the 512 px ring. In a level the patches shift every plane's ring 96 px left
so the left margin has tiles; for these pictures that put 96 px of the next
picture where the ring's own wrap-around belongs: the blocks missing from the
sky during the catapult, 96 px wide, inside the 4:3 view. Plane B keeps its
native ring in the lobby (`m68k_hook`, the plane struct in a1); plane A,
which scrolls with the camera, is shifted as in a level.

### Rooms narrower than the view

The stage select room holds the camera at 2816..2816, the Combi Catcher at
1552..1552. Their margins would show the corridor beside them and, as they
scroll down, blocks the engine never draws. Where the camera's range is
narrower than the widened view, the margins are black outside it, as a
modern re-release would show a room that small (`lobby_margins`). A wider
range is not a room: its ends move as the player goes on (1552 once past the
COMBI CATCHER, with the camera at 1573), and what lies beyond them was just in
view, so nothing is cut there. In a room the SH-2's clip stays the native one
too: nothing is shown in its margins, and the Combi Catcher's TIME UP, scaled
up past the screen, flipped a frame or two late when it was drawn there.

### The palette areas

Every frame ($8891C6) the lobby picks palette lines 0 and 1 by where plane A
is: area k while its X is at least k of the bounds at $88922A (0x180, 0x4C0,
0x900, 0x1010), 32 colours from a table at $9BA764 by time of day and area.
The 4:3 view shows world [X, X + 320), so an area's colours are only ever
seen on [its bound, the next bound + 320); the margins reach E px past that
into the next area, whose scenery then showed in the old colours (the pillar
before the COMBI CATCHER blue-grey instead of orange, the sky beyond the
catapult exit navy). Those columns are drawn in the neighbouring area's
colours, read from the same table (`Vdp::alt_cram`; sprites keep the game's).
Which area's colours are on screen is found by matching the table against
what the game shows (it follows plane A a frame late), less the seven or so
colours it cycles; for those the game's current colour is kept. Colours that
match no area (a fade) have no counterpart, so those columns are black then;
in the runs below that only happened while the lobby fades in at its
entrance, where no margin reaches another area.

### The catapult

Issue #13 blamed the missing blocks on the camera (13-15 px a frame) outrunning
the shifted ring's 96 - E px of slack. Traced, plane A's ring kept up: the plane
moves at most 16 px a frame and gets a column every 16. The blocks were plane
B's, above.

### The final boss

The boss's arena (its floor, pillars and the beams overhead) is drawn by the
special stage's polygon code, loaded with the special stage's SDRAM image,
with the screen's own rectangle. Widened as a level only, the polygons ended
at the 4:3 edges, and spans starting at x = 0 cleared the margins to their
colour: the stretched bars of issue #56. It is now a polygon scene like the
stage (`PolyScene::Boss`, `final_boss_active`): the projection's centre moves
C px right, the rectangle becomes [0, 319 + 2C] and the host moves polygon
pixels back by C, so the arena's own geometry fills both margins. It is also
a level, so the plane ring, the 32X sprites (the boss's arms, the HUD) and the
rings that scatter when the player is hit reach the margins as in a level. Its
line clear is the bonus stage's, cleared over the whole widened line. The
fight begins with a white flash, one polygon with the screen's corners that
the rectangle clips to exactly the native line; moved back it left a C px
band of arena on the right, so it too fills the whole widened line.

Nothing the game parks outside the 4:3 view on purpose was found in the fight
or its opening: Eggman and Metal Sonic leave to the right inside the ring and
stay in sight a little longer, the boss's arms come in from the right.

### The ending's scenes

Levels 3-6, widened, showed a white picture with the left margin black (left
of the layout's origin) and the sprites staged for the 4:3 screen. They stay
4:3.

### WIDE LOBBY

With the lobby widened by default and the INTRODUCTION Eggman scene kept 4:3
on purpose, the option had nothing left to do. It is gone from the menu and
from new settings files; `WidescreenEverywhere`, `Machine::wide_everywhere`
and `--wide-everywhere` are still read (recordings, ghosts and netplay carry
it) and change nothing.

## Validation

- The walk above, widened at 53 px: the margins are live from frame 561, the
  centre matches a 4:3 run on all 1643 widened frames (the catapult included),
  and the catapult lands in SPEED SLIDER at frame 1989, as in 4:3.
- Frames every 20 of the walk (every palette bound, going right), and a walk
  there and back at NIGHT (701 widened frames, the centre identical) across
  the first two bounds both ways: each margin in its area's colours.
- The Combi Catcher (`--press 600:right:200 --press 1100:left:35 --press
  1300:down:10`): 2740 widened frames with the 4:3 centre.
- A finished level handing over to the lobby (`finished_run.txt`): the
  margins let go when the zone changes (frame 4640) and come back once the
  lobby is drawn (4840).
- The final boss: margins live from frame 482. The centre differs from a 4:3
  run only along polygon edges (90 to 165 pixels a frame, as in the special
  stage), except for two frames of the flash (1463-1464) and a zoom in the
  opening (from 1148), shown a frame late: the SH-2's extra polygon work
  beyond the 4:3 edges delays a flip now and then, as in the special and the
  bonus stage. Over 3000 frames of the fight that adds up to 16 frames of the
  game's own counter at $FFE006, and the run drifts from the 4:3 one (the
  camera a pixel apart at frame 2255).
- `lockstep_lobby_widescreen` and `lockstep_final_boss_widescreen`: 2400
  frames each, bit-identical.

Tests: `widescreen_lobby_centre_matches_4_3`,
`widescreen_lobby_margins_in_the_next_area_colours`,
`widescreen_lobby_combi_catcher_centre_matches_4_3`,
`lockstep_lobby_widescreen`, `widescreen_final_boss`,
`lockstep_final_boss_widescreen`, `widescreen_ending_stays_4_3`, the unit tests
`widescreen_render.world_entrance_is_widened_but_not_the_ending`,
`widescreen_render.a_lobby_room_is_black_outside_its_range` and
`vdp_widescreen.lobby_margins_take_the_next_area_colours`. They replace
`widescreen_leaves_the_lobby_4_3`, `widescreen_lobby_when_asked` and
`widescreen_final_boss_stays_4_3_with_wide_lobby`.
