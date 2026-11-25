# Issue #5: widescreen fixes (special stage, lobby entrance, arena clock, title cards, final boss, ending)

A list of widescreen problems was reported on issue #5 (2026-11-24). All of
them were reproduced on 0.15.24 with `chaotix_headless --wide 53` (16:9).
0.15.25 fixed four that are plain bugs in a widened scene (1, 5, 6, 8).
Three were left for the owner, who then asked for all three to be widescreen:
the black strip at the lobby's outdoor entrance (2), the Combi Catcher and
roulette room in 4:3 (4, the margins now show what is beside the room) and the white
ending in 4:3 (9); all three are done. Not reproduced, and not touched: the
Combi Catcher's sky (3) and the launcher (7).

In every case the game runs exactly as before: the 4:3 picture, the 68K work
RAM and the timing are unchanged, and the interpreter and the recompiled code
agree frame for frame. Only what the margins show changes.

Screenshots: left or top before (0.15.24 for 1, 5, 6, 8; 0.15.25 for 2,
4 and 9), right or bottom after.

## 1. Special stage: rings, spheres and stars stop at the 4:3 picture

![special stage](images/issue-5-special-stage.png)

**Cause.** The stage's tube is widened (issue #18), but its objects are
sprites: projected by the add at `$06004BE0`, then blitted scaled
(`$06001F18`). The blitters clip them to the rectangle at SDRAM `$06003834`,
which the stage sets to its own 288 px picture, [16, 304). Anything outside
it was never drawn, so the rows of rings ended at the 4:3 edges while the
tube went on.

**Fix.** While the stage is widened that clip is [-C, 320 + C), as in the
bonus stage (C: the margin rounded up to 8), and it is put back to [16, 304)
when the stage ends. Sprites are drawn where the game puts them; what lands
outside the native columns goes to the margin shadow as before
(`patches::begin_frame`, `kSpecialSpriteClip`).

**Verification.** `widescreen_special_stage_objects_in_the_margins` (frame
1200 of the reproduction, rings and spheres in both margins; 0.15.24's hash
differs). `special_stage_objects_centre_and_ram_match_4_3`: over 2400
frames the stage's picture matches a 4:3 run (polygon edges as before) and
the 68K work RAM is identical on every frame (`--compare-ram`, new).

## 2. The lobby's entrance: a black strip on the left

![lobby entrance](images/issue-5-lobby-entrance.png)

(Frame 600 of `--stage 300:7:0:0:2:7:0 --press 600:right:2600 --wide 53`.)

**Cause.** In WORLD ENTRANCE the camera is not clamped (issue #61: the
lobby's events follow it, so the run must be the 4:3 one), and at the
outdoor entrance it is at X 0..12. `margin_cut` blacked out the margin
columns left of the layout's origin, since the plane ring holds no layout
there: the part of the left margin left of world X 0 was black.

**What the game has there.** Plane A, the towers, signs and pillars, has no
layout left of X 0 (streamed there, it is whatever the ring last held:
pieces of the lobby further right). Plane B is not the layout: in the lobby
it is one 512 px picture per part, and the outdoor part's picture is the
sky, the trees, the rail and the floor the characters walk on. Its rows are
line-scrolled, the floor's with the camera, so the game itself wraps that
picture round its 512 px as the player walks: the columns left of X 0 are
the same picture, drawn the same way.

**Fix.** Left of the origin the lobby's margin shows plane B and leaves
plane A out (`MarginCut::backdrop`, drawn with `Vdp::plane_a_hide_*` like the
final boss's plane A). Render only: the machine state is identical to
0.15.25's on every frame checked (`--state-hash-every`). A level keeps its
black columns there (its camera clamp keeps them out of view in any case);
the lobby's own black columns (a room, a fade) stay black.

**Verification.** `widescreen_lobby_entrance_left_of_the_origin` (frame
600; 0.15.25's hash differs), the unit test
`widescreen_render.the_lobby_entrance_shows_plane_b_left_of_the_origin`,
and the lobby's existing centre, palette and lockstep tests. The return to
the lobby after a level (`finished_run.txt`, frames 4850-5600, at NIGHT)
shows the same picture in its own colours.

## 4. The Combi Catcher and the roulette room in 4:3

![rooms](images/issue-5-rooms.png)

Left 0.15.25, right this change. Top two rows show the Combi Catcher, frames
1450 (going down) and 2000 (the bottom of its pit), from `--stage
300:7:0:0:2:7:0 --press 600:right:200 --press 1100:left:35 --press
1300:down:10 --wide 53`. The bottom row shows the stage select (roulette)
room at its console, frame 1300 of `--stage 300:7:0:0:2:7:0 --press
600:right:2600 --wide 53`.

**Cause.** Both rooms hold the camera at one point (1552, 2816). Issue
#61 blacked out the whole of both margins for any room narrower than the
widened view.

**What the game has there.** Both rooms are exactly one 4:3 screen wide,
and both are reached along the corridor with the camera at Y 0. On the way
in and out, the 4:3 view shows what is beside them down to world row 224:

- **The Combi Catcher.** On the left is the corridor (its purple
  background columns, rail and floor). On the right is the next part of the
  lobby (a diamond-panel wall over a pink and teal floor band).
- **The roulette room.** A pink and teal pillar of the corridor's doorways
  stands at each side. On the right there is 8 px of the drum's dark
  edge before it.

Below world row 224, as the rooms scroll down, the layout beside them is
empty (the backdrop), except for the ring's last columns on the right.
The engine's row fills reach only about 368 px right of the camera, so at
E = 53 the last 5 columns of the right margin are never redrawn as the
camera goes down. They still hold tiles of another place: the pink, teal
and purple fragments that showed below the floor at the Combi Catcher's
lower right.

**Fix (the owner's choice).**

- **Above world row 224** (`patches::kRoomFloorY`) the margins show the
  planes as they are: what the corridor shows beside the room, fully drawn.
- **Below that row** they are black, the stale columns included.
- **The roulette room's pillars.** The doorway pillars carry on straight
  down to the bottom of the screen: screen columns [-32, 0) and [328, 344)
  from the room's left edge (`kStageSelectPillar`). Their 80 px pattern is
  repeated from world rows 96-175, above the floor band. Beyond them, and
  over the 8 px of the drum's dark edge on the right, it is black.
  - Those rows are kept as the pillars are drawn on the way in
    (`Vdp::room_ext_*`). Once the room has scrolled down, the plane's
    256 px ring holds other rows there.
  - A room entered from a save state already scrolled down has black there
    until the camera comes back up.
- **No sprites and no 32X** are drawn beside a room. The claw, the
  capsules, the signs, the monitors and the characters are the room's own.

The margins are black while the lobby fades, as before, and the SH-2's
clip stays native in the rooms. This is render only: the machine state
matches 0.15.25's, and the 4:3 centre matches a 4:3 run on every widened
frame.

**Verification.**

- Golden-frame tests (all three hashes differ on 0.15.25):
  - `widescreen_lobby_combi_catcher_beside_the_room` (frame 1450)
  - `widescreen_lobby_combi_catcher_pit_black_below_the_floor` (frame 2000)
  - `widescreen_lobby_stage_select_room_pillars_carry_down` (frame 1300)
- The existing centre checks, with 2740 and 1643 widened frames identical
  to 4:3.
- The unit test
  `widescreen_render.a_lobby_room_shows_its_surroundings_above_the_floor`
  (formerly `..._is_black_outside_its_range`).

**Left as it is.** Around frames 1317-1320, before the Combi Catcher's
camera locks, the margins still have the corridor-streaming transient
described in item 3 of the reproduction notes (a dark block at the bottom
right for about four frames).

## 5. AMAZING ARENA: the clocks have no hands in the margins

![AMAZING ARENA clock](images/issue-5-arena-clock.png)

**Cause.** The dial is plane tiles; the hand is a polygon object. The 68K
asks for it with command `0x0C` on its 32X command list (`$881118`, X and Y
from the screen's centre). The master SH-2 projects it with the special
stage's projection code, which the boot code's SDRAM image holds too, and
its rasterizer (`$06004858`) clips each polygon to the rectangle at SDRAM
`$06003844`, the full 4:3 screen in a level. The object itself is kept by
the 68K (its on-screen test is [-64, 384)), so only the clip was in the way.

Widening the game's own drawing, as the special stage's is, would change the
4:3 picture: the rasterizer cuts a polygon at the rectangle with integer
maths, and a polygon cut at another point has its slanted edge a pixel apart
inside the picture too. It showed on the title card's polygons (7 pixels at
frame 543 of AMAZING ARENA).

**Fix.** The game's drawing is left as it is. A hook at the rasterizer's
entry, run before the same instruction by the interpreter and the recompiled
code, reads the polygon (vertex count, vertices, fill word) and the host
draws the part outside the native columns, and on the rows below the screen,
into the margin shadow, in the order the game draws
(`patches::level_polygon_margins`). Nothing the machine reads changes. The
sprite recorder and the replacement layer are told those bytes are not
sprites'.

**Verification.** `widescreen_amazing_arena_clock_hands_in_the_margins`
(frame 1220, the clock wholly in the right margin, now with its hand),
`widescreen_amazing_arena_centre_matches_4_3` (the 4:3 picture identical on
every widened frame) and `lockstep_widescreen_amazing_arena_polygons`. A
level's work RAM is not identical to a 4:3 run's in any case (the plane ring
shift and the ring cull, issue #7), so the RAM check is not used there.

At the widest margin (80 px) an object more than 384 px right of the camera
is still not drawn: that is the 68K's own test, and moving it would change
the game.

## 6. Title cards stop just past the 4:3 edge

![title card](images/issue-5-title-card.png)

**Cause.** A widened level's margins are in the host-side shadow, which the
game never clears. They were cleared, with the fill's colour, whenever an
auto fill started at a line's first column: meant for the game's per-frame
clear (161 words from column 0). The title card's black bar, though, is a
polygon whose rows begin at the screen's left edge, filled from column 0 by
the rasterizer. On its rows it painted the whole of both margins: a black
box stayed in the right margin, the left margin was black whatever the bar's
length, and when the bar's right end passed the 4:3 edge it all went at once,
leaving the card's pieces standing in the margin.

**Fix.** Only a fill that covers the whole native line from its first column
clears the margins (`patches::fill_clears_margins`), in the machine, the
sprite recorder and the replacement layer alike. In every zone, the shorter
fills from column 0 all come from the rasterizer, and the game's clears are
161 or 256 words. The rasterizer clips at x 319, so a polygon's span is at
most 160 words; a 160-word fill is still a clear (a full-screen flash is one)
unless it is on a row of the polygon being drawn, in its fill word. The bar's part in the margin is
then drawn by the host like any level polygon (item 5), so it slides on into
the left margin with its text.

**Verification.** `widescreen_title_card_slides_into_the_margin` (frames 2420
and 2438 of the SPEED SLIDER landing; both hashes differ on 0.15.24). The
lobby walk's centre check (`widescreen_lobby_centre_matches_4_3`, which
lands in SPEED SLIDER and shows its card) still passes.

**Left as it is.** The card's pieces are removed on a timer (each piece's
routine counts down at `$28` of its object, then deletes itself, `$8AC8E4`),
not when they leave the screen. When the bar goes, its right end is about
25 px into a 53 px margin, so the last strip of it still vanishes there at
once. Taking it further would mean moving the pieces faster or keeping them
longer than the game does, which changes the game's objects.

## 8. Final boss: red Metal Sonic on both sides at once

![final boss](images/issue-5-final-boss.png)

**Cause.** Part of red Metal Sonic is a 128 x 128 px picture in plane A
(16 x 16 cells), which the boss moves by scrolling the plane, and parks at
screen X -128 (horizontal scroll -640), just off the 4:3 screen. A 512 px
ring seen 320 px wide leaves a 64 px gap in which a 128 px picture is hidden
however it is taken; seen 426 px wide there is no such gap. So the parked
picture showed in the left margin while the boss's body came in on the
right.

**Fix.** In the widened final boss, plane A's picture (what the longest run
of empty name table columns leaves) is drawn in the margins only where that
placement of it also reaches into the native columns, i.e. where it carries
on what the 4:3 picture shows. When it flies across the screen it still
continues into the margins. This is render-only
(`patches::boss_plane_a_margins`, `Vdp::plane_a_hide_*`).

**Verification.** `widescreen_final_boss_picture_not_round_the_ring` (frame
3400 of the reproduction) and the unit test
`vdp_widescreen.the_final_boss_picture_shows_in_the_margins_only_beside_the_screen`.
The existing `lockstep_final_boss_widescreen` and `widescreen_final_boss`
pass.

## 9. The ending's white scenes are 4:3

![ending](images/issue-5-ending.png)

(Frame 1200 of `--level-select --stage 300:7:4:0:4:7:0 --wide 53`; the
other scenes, WORLD ENTRANCE 3, 5 and 6, are the same.)

**Cause.** Issue #61 kept WORLD ENTRANCE 3-6 4:3 (`wide_zone`): widened, the
left margin was black (the camera is at X 0, and `margin_cut` blacks out
what is left of a level's origin) beside a white picture, and the sprites
were thought to be staged for the 4:3 screen.

**What the game has there.** The picture is the 32X's alone. The SH-2
clears its whole screen to white every frame (a clear of the whole native
line, which the margins take as well, `fill_clears_margins`) and draws the
characters over it. Both Mega Drive planes are empty: only the backdrop
colour is behind the 32X, so the plane ring holds nothing to hide. Over
4000 frames of each scene (every 10th frame looked at), nothing but white is
ever in the margins: the characters stay well inside the 4:3 picture.

**Fix.** These scenes are widened as a level (`patches::ending_scene`), and
nothing is cut from their margins. They are told by their number and by
their empty planes: the bonus stage loads itself as WORLD ENTRANCE 3 for
about 20 frames, with its picture in the planes and the level engine
stopped, and those frames stay as they were. The ending's camera is left
alone (no bottom clamp), as the lobby's. WORLD ENTRANCE 7 (a black screen)
and the INTRODUCTION Eggman scene stay 4:3.

**Verification.** `widescreen_ending_white_margins` (frame 1200; 0.15.25's
hash differs), `widescreen_ending_{3,5,6}_centre_and_ram_match_4_3` (3000
frames each: the 4:3 picture and the 68K work RAM are the 4:3 run's on
every frame), `lockstep_ending_widescreen` and the unit test
`widescreen_render.world_entrance_and_its_ending_are_widened`, which
replace `widescreen_ending_stays_4_3`. The bonus stage's tests pass as
before.

**Not checked.** The scenes were reached with the stage select, where each
loops; the real ending, reached by beating the final boss, was not played
through (no recording reaches it). A sprite that its script brings in from
off-screen there would show in the margins earlier than in 4:3.

## Also

- `chaotix_headless --compare-ram` (with `--compare-native`): the 68K work
  RAM of the 4:3 and the widescreen machine must be the same after every
  frame.
- `--compare-native`'s 4:3 machine now gets the same `--cheat` writes and
  second pad as the widescreen one; with a cheat the two runs used to differ
  from the first frame.

## Checks

Full `ctest` and the 33-run lockstep sweep (all special stages, levels,
bonus stage, hub, random input) on Windows (MinGW), with the ROM.

Items 2, 4 and 9: the same, 166/166 and 33/33 bit-identical, 0 warnings.
