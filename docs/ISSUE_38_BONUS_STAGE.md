# Issues #38 and #39: the bonus stage and the special stage results stay 4:3

Two reports from the same player after 0.6.x, both reproduced:

- **#38**: the bonus stage, reached from a level, is shown 4:3 while the rest
  of the game is widescreen.
- **#39**: the results screen after a cleared special stage ("GOT THE CHAOS
  RING") is 4:3.

Neither scene was one the widescreen patches knew about. Both turn out to be
drawn by the same SH-2 code as the special stage (issue #18), with the
screen's own clip rectangle (0, 319, 0, 223) instead of the stage's, so both
are widened with the stage's machinery: the polygon rectangle at SDRAM
`0x06003844` becomes [0, 319 + 2C], the polygons are drawn C px further right
and the host moves their pixels back by C (C is the margin rounded up to 8).
What differs is where each scene's polygons get their X, and what fills the
margins. `patches::PolyScene` names the three scenes; `Machine::poly_scene`
holds the one that is widened.

## The results screen (#39)

Reached headless by cutting a special stage short and handing the 68K a
cleared result:

```
chaotix_headless --stage 300:10:0:0:2:7:0 --poke-sdram 1000:7DF0:03 \
    --poke-fb 1200:1500:1FF:01 --wide 53 --shot 1450
```

The stage keeps its ring count at SDRAM `0x7DF0` (TIME OUT when it runs out),
and the SH-2 stores its result, 1 when the goal is reached, at frame buffer
`$1FF` (`$060074BA`, see issue #27). Headless gained `--poke-sdram` and
`--poke-fb` for this.

### How it draws

Entirely on the 32X. The master SH-2, idle between commands, calls the
routine at `0x06007640` once a vblank (`0x060011C4`). The special stage's SDRAM
image has one there that draws a fixed list of polygons at `0x06007688`: a
long `00 COLOUR FLAGS N`, then N vertices (int16 x, y), up to a zero long. It
is a 2D design laid out for 320 columns. Every background polygon touches
x = 0 or x = 319 (the grey panels, the yellow band, the bars on the right);
the cyan triangle and its shading are inside. The text and the ring are
blitted sprites, centred at 160. The Mega Drive layer shows only its
backdrop.

### The repair

While the results screen is up, the list's vertices get x + C, and vertices on
the screen's edges go to the widened edges: x = 0 stays 0, so after the move
back it is -C; x = 319 becomes 319 + 2C, so it ends at 319 + C. The background
panels then reach both margins, and nothing inside them moves. The sprites are
not touched: they are drawn with the overwrite image at their native places
and stay centred. The original list is written back when the screen ends
(only if it is still ours).

Telling the results screen apart takes care. Its code and list are in SDRAM
for the whole stage, and for a few frames of the stage's intro the rectangle
is the screen's too (frames 405-420 of a stage select run). The intro shows a
Mega Drive picture that does not reach the margins. So the results screen is
only widened when it follows the widened stage itself: the code at
`0x06007640` and the list must match the ROM's image (or our widened list),
the rectangle must be the screen's (or ours), and the scene widened the frame
before must be the stage or the results screen. A special stage played in 4:3
whose window is widened during the results screen stays 4:3 until the next
scene.

The stage fades out to white, which fills the widened picture too, and the
results screen fades in from it widened, so the margins stay live from the
stage through the results in one stretch (frames 419 to 1645 of the run
above). A stage that is not cleared goes to the next scene without the
results screen, and the margins let go where the stage ends, as before.

## The bonus stage (#38)

`--stage 300:9:0:...` (and `9:1`, the second bonus stage) starts it from the
stage select, in game mode `0x50`. In play the level calls it (`$892970 jsr
$8BCC9A`, after an object plays sound `$46`) and gets control back: the game
mode, zone and level stay the level's.

### How it draws

- **The tunnel** is a Mega Drive plane picture, 40 x 28 cells of plane A: one
  quarter drawn as unique tiles and mirrored into the other three, with a hole
  in the middle where plane B shows a moving pattern. It ends at the 4:3
  edges; beyond them the plane holds nothing. The picture never scrolls; the
  palette cycles. Measured from the tiles, every pixel of it, from r = 80 out,
  is a function of its distance from the screen's centre (160, 112): rings of
  palette indices 1-12, one index per w px, w growing by one every ring --
  1 px from r = 80, 2 from 92, 3 from 116, 4 from 152 to the corners at 195.
- **The items** are blitted, scaled, by the SH-2 boot code's handler at
  `0x06000CD0`. The 68K hands it offsets from the screen centre, and it puts
  that centre at X 160 with `MOV #-96,R10` (then `EXTU.B`, `SHLL16`) at
  `0x06000CE6`. The same handler draws the cubes' sides as polygons from that
  centre. The HUD and the items the 68K places itself are blitted from other
  commands, at the 68K's positions.
- Each frame the SH-2 clears the lines it draws on with one auto fill: word 0
  of the line, length register 160, data 0.

### The repair

- **The rings go on into the margins.** `patches::bonus_tunnel` measures the
  picture each frame: the colour of every distance, by a vote over the top-left
  quarter (in quarter pixels), then checks that the runs from r = 80 are
  1, 2 .. 12 repeating, and that at least 97% of a grid over the whole picture
  matches. Beyond the last whole run it carries the rings on as they were
  drawn: one index per w px, w one more for every ring. The Mega Drive
  renderer takes plane A's margin pixels from that table
  (`Vdp::radial_lut`), with low priority, so the palette cycles them with the
  picture and sprites still go over them. The seam at the 4:3 edges does not
  show. A picture that is not those rings centred on the screen is not
  widened, so nothing else is extended this way.
- **The polygons and items** go through the stage's machinery: the centre
  immediate becomes 160 + C (the recompiled code reads it as it runs, like the
  stage's `ADD #80` pairs), polygon pixels are moved back by C, and the clear
  is recognised by its exact fill and clears the whole widened line. The items
  that handler blits are drawn with the overwrite image, which is not moved
  back; a master SH-2 hook on the scaled blitter (`kSh2BlitScaled`, already a
  hook address for the sprite recorder) takes the shift off their X when it is
  called from that handler (return address `0x06000DF6`), reading the shift
  from the patched immediate. The blitters' clip at `0x06003834` is widened as
  in a level, so items reaching the margins are drawn there (to the shadow).
- **It comes first.** The level engine has only stopped while the bonus stage
  runs in play, and the stage's plane size is the level's, so the paused-level
  rule could otherwise claim it. It is looked for before the level, by its own
  picture, which no level has. The picture check starts with four cells (the
  corners must mirror each other), so it costs nothing in levels.

## What changes, and what does not

- Without margins (`--wide 0`, or a 4:3 window) nothing runs: the scenes are
  only ever widened when widescreen is on and there are margins.
- The 4:3 centre of the widened bonus stage matches a 4:3 machine run
  alongside on 1945 frames of both bonus stages, give or take 19 pixels along
  polygon edges (polygons clipped at the 4:3 edges natively are not when
  widened); the results screen matches on all 1226 widened frames, the same
  tolerance as the stage (at most 380 pixels, in the stage's own frames).
- The 68K work RAM of a widened bonus stage is identical to a 4:3 run's with
  no input, and with random input until a frame where the SH-2's extra polygon
  work (the parts beyond the 4:3 edges) delays a frame buffer flip by a frame
  (frame 891 of one run). From there the game's random items differ. The
  number of flips is the same (1544 in 3000 frames both ways): no frame is
  lost. The special stage behaves the same way (frame 2949 of a random run).

## Validation

- `bonus_stage_is_widescreen` (new): the margins go live as the bonus stage
  appears (frame 456) and stay, in one stretch, for all of it (1390 frames).
  Before the fix: 0 frames.
- `bonus_stage_is_widescreen_in_the_level_mode` (new): the same with the game
  mode set to the level's, `0x18`, mid-stage.
- `bonus_stage_centre_matches_4_3` (new), `sprites_check_bonus_stage_widescreen`
  (new, at 80 px margins: the recorder rebuilds every picture, moved items
  included) and `lockstep_bonus_stage_widescreen` (new, 2400 frames with
  random input, bit-identical).
- `results_screen_is_widescreen` (new): the margins stay live from the stage
  through the results screen in one stretch, 1226 frames. Before the fix they
  let go when the stage ended: 929 frames.
- `results_screen_centre_matches_4_3` (new) and
  `lockstep_results_screen_widescreen` (new, at 80 px margins).
- The full `ctest` suite passes, golden frames included, and the 33-config
  lockstep sweep is bit-identical.

## Issue #52: the bonus stage entered from a level stayed 4:3

A player's bonus stage, reached from a giant ring in a level, was 4:3 on a
recent version. The tests above only ever started the bonus stage from the
stage select.

### How a level enters it

The giant ring is object `0x30` (`$8927CC`, in the object table at `$891D72`).
It takes the player once they have 20 rings (`FFE008`), stores the
toucher's character in `FFE048`, plays sound `$46` and calls the bonus stage
(`$892970 jsr $8BCC9A`). Headless reaches it by turning the level's second
object into a giant ring on the player:

```
chaotix_headless --stage 300:0:1:4:2:7:0 --wide 53 --trace-wide --frames 4500 \
    --poke 1000:8046:00 --poke 1000:8047:30 --poke 1000:8068:00 \
    --poke 1000:8048:01 --poke 1000:8049:08 --poke 1000:804C:06 --poke 1000:804D:6C \
    --poke 1000:E008:00 --poke 1000:E009:32
```

(object `$FF8040`: id `0x30` at +6, +28 cleared so it sets itself up, X and Y
at +8 and +C the player's, 50 rings). The bonus stage begins at frame 1080,
ends at 3767 and hands back to the level.

### What varies between visits

- **The palette set** is the time of day, `FFDFF6 & 6` (`$8BCFCC`): four sets
  of tunnel colours (MORNING purple, DAY green, SUNSET red, NIGHT blue), each
  cycled. The picture, its rings and its radii are the same in all four, and
  the stage select reaches every one (`--stage 300:9:L:T:...`, T = 0, 2, 4, 6):
  all of them were already widened.
- **The character** (`FFE048`, copied to `FFE038`) only chooses the ball's
  art.
- **What the level leaves behind.** The bonus stage sets up its own planes but
  never writes VDP register 11, so it keeps the level's scroll modes. BOTANIC
  BASE, SPEED SLIDER and TECHNO TOWER scroll by line (`0x03`); AMAZING ARENA
  and MARINA MADNESS scroll as a whole (`0x00`).

### Why it stayed 4:3

`bonus_tunnel` refused any scroll mode but "the whole plane" (`reg[11] & 7`
must be 0), so in three of the five zones the picture was never measured. The
stage clears the whole scroll table, so the picture is as unscrolled as in the
stage select; only the mode differs.

### The fix

- The check now reads plane A's scroll the way `Vdp::render_line` does, for
  every line (per line or per 8 lines, as register 11 says) and, with
  2-cell vertical scroll, every column, and requires all of them to be 0. It
  runs after the corner cells, so it costs nothing in levels.
- **Plane B in the margins.** Entered from a level, plane B's name table
  beyond the stage's own 40 columns still holds whatever the level left in
  that VRAM (in SPEED SLIDER: tile data, read as cells, many with the
  priority bit). In the 4:3 picture those columns are never shown. In the
  margins the rings are opaque but low priority, so high-priority leftovers
  showed over them as white blocks. Where the margins carry the rings
  (`radial_lut`), plane B is now left out: the stage's own plane B cells have
  no priority bit and are wholly hidden behind the rings anyway.
- 4:3 is untouched: the detector runs only with margins, and the renderer
  change only where the rings are drawn into the margins.

### Coverage

Entered from level 1 of every zone at every time of day (18 of the 20 could
be scripted this way; TECHNO TOWER at SUNSET and NIGHT has no second object
to turn into a ring at frame 1000): before the fix BOTANIC BASE, SPEED SLIDER
and TECHNO TOWER stayed 4:3 (0 widened frames), AMAZING ARENA and MARINA
MADNESS were widened; after it all 18 are widened for the whole stage (2121
frames, MARINA MADNESS 2090) with clean margins.

The widened bonus stage's centre matches a 4:3 run's frame for frame when
entered from a level too, until the SH-2's extra work beyond the 4:3 edges
delays a frame buffer flip (frame 1270 of the run above); from there the two
runs are a frame apart, as described for random input above. With the level
played in 4:3 and only the bonus stage widened, the 68K work RAM stays
identical until frame 3638.

### Validation

- `bonus_stage_from_a_level_is_widescreen` (new): margins live in the level,
  in the bonus stage (from frame 1080 to 3767) and in the level again,
  3783 frames in 3 stretches. Before the fix: 1096 frames in 2 stretches.
- `bonus_stage_from_speed_slider_is_widescreen` (new), and
  `vdp_widescreen.bonus_stage_rings_hide_plane_b_in_the_margins` (new, fails
  without the renderer change).
- `bonus_stage_from_a_level_centre_matches_4_3` (new, 654 frames, the level
  and the bonus stage up to frame 1260) and `lockstep_bonus_stage_from_a_level`
  (new, random input from frame 1100: the player leaves the bonus stage at
  frame 2061 and plays on in the level).
- `bonus_stage_L_at_time_T_is_widescreen` (new, both bonus stages at DAY,
  SUNSET and NIGHT; MORNING is `bonus_stage_is_widescreen`) and
  `bonus_stage_at_sunset_centre_matches_4_3` (new).
- The full `ctest` suite passes and the 33-config lockstep sweep is
  bit-identical.
