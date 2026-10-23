# The menu's font and backdrops (issue #8)

Issue #8 asked for the menu to look like the game: its own font instead of
the bundled pixel face, and pictures of its zones behind the menu instead of
plain colour. Both are made from the player's ROM when the program runs;
nothing of the game is in the repository or in a release.

| Setting | Values | Default | `[Video]` key |
| --- | --- | --- | --- |
| OPTIONS > EXTRAS > MENU FONT | GAME, PIXEL | GAME | `MenuFont = Game` |
| OPTIONS > EXTRAS > MENU BACKGROUND | CYCLE, WORLD ENTRANCE, ISOLATED ISLAND, BOTANIC BASE, MARINA MADNESS, SPEED SLIDER, AMAZING ARENA, TECHNO TOWER, PLAIN | CYCLE | `MenuBackground = Cycle` |

The `MenuBackground` values are `Cycle`, `Plain` and the zones' names
without spaces (`WorldEntrance`, `IsolatedIsland`, `BotanicBase`,
`MarinaMadness`, `SpeedSlider`, `AmazingArena`, `TechnoTower`), case ignored.
`Zone`, the value from before the zones could be chosen (issue #37), reads
as `Cycle`, and so does anything unknown.

PIXEL and PLAIN are the look from before. The new look is the default
because it is what was asked for, both parts come from the ROM the player
already has, and both fall back on their own: a ROM without the font at its
address keeps the pixel face, and until a zone's picture exists the menu is
plain. The two rows are under EXTRAS because VIDEO already has as many rows as
a phone's screen holds.

## The font

The TRAINING and DATA LOAD screens write zone names ("ISOLATED ISLAND") with
an 8x16 font of Mega Drive sprites: white digits shaded in lilac, yellow
capitals shaded in brown, a black drop shadow on every glyph. It was
found by dumping VRAM on the TRAINING screen, reading the VDP's sprite table
(each letter is a 1x2-tile sprite: `I` is tile 0x524, `S` 0x538, ...), and
logging the 68K writes into those tiles: the decompressor at $8F49C6 writes
them straight to the VDP data port from $9C4B20 (ROM bank 2).

- **Tiles:** ROM 0x2C4B20, Nemesis-compressed (the usual Sega format), header
  0x805E: XOR mode, 94 tiles. Decoded, they are exactly what the game puts in
  VRAM at 0xA000.
- **Glyphs:** 47 of them, two tiles each (top, then bottom), in this order:
  `0`-`9`, `A`-`Z`, `?` `!` `-` `,` `.` `♂` `♀` `_` `“` `”` `’`. There is no
  lower case and no space glyph (the game leaves a gap). Every glyph is 8
  pixels wide, ink in columns 1-7 (the shadow included) and lines 1-15.
- **Colours:** palette line 0 of those screens, 16 words at ROM 0x03E8CA.
  The digits use entries 1-3 (white, lilac, dark purple), the letters 10-12
  (yellow, brown, dark red), the shadow entry 5 (black).

`renderer/game_font.*` decodes it (`nemesis_decode`, `build_game_font`) and
builds an atlas of 50 glyphs, 400x16: the ROM's 47, plus `:` (the full
stop's dot twice) and `<` `>` (triangles in the letters' yellow with the
shadow), which the menu's values and arrows need. Lower case is drawn as
upper case, `"` and `'` as the curly ones. The header and the palette words
are checked, so a different ROM is refused rather than misread.

`ui::Ui` draws the pixel fonts (the menu's labels, values and headings) with
it when MENU FONT is GAME and the font has every character of the string; a
string with anything else (`%`, `/`) keeps the pixel face, whole, centred in
the same line. Glyph pixels become whole screen pixels: the scale is the
pixel face's size over 16, rounded (1 at 720p for labels and 2 for headings,
2 and 3 at 1080p), positions are rounded, and the texture is sampled
nearest-neighbour. The text's colour only sets the glyphs' brightness (full
for the selected entry and headings, about two thirds for the others), so
the game's own colours are kept. Captions and hints keep the small text
face; the touch controls and notifications keep the pixel face.

`chaotix_headless --game-font FILE.png` writes the atlas.

**MENU SIZE (issue #49).** OPTIONS > VIDEO > MENU SIZE lays the menu out
larger: `ui::Ui::set_zoom` multiplies the layout scale by 1, 1.5 or 2 for
NORMAL, LARGE and EXTRA LARGE (the app sets it before each frame while the
menu is open, NORMAL otherwise, so the speedrun overlay, the rewind label and
the touch controls are untouched). The pixel faces cannot follow a factor:
they snap to their 8-pixel grid and the game's font to whole scales, so the
1.5 of LARGE and the 2 of EXTRA LARGE could round to the same letters. So
`ui::pixel_steps` keeps NORMAL exactly as it was and, for each size above,
takes the game's font to the whole scale the factor reaches (rounded down:
rounding up made LARGE's headings 80 pixels tall on a 1080-line phone, 48 at
NORMAL), but at least one scale more than the size below, with the pixel face
about as tall. On a 1080-line phone the rows' letters are 32, 48 and 64
pixels tall; in a 720-line window 16, 32 and 48. Glyphs stay on whole pixels
at every size. The rows are also spaced a little more at each size (4 units
a step), so the touch targets grow faster than the letters.

AUTO, the default, is LARGE on Android and iOS and NORMAL elsewhere. A phone
in landscape is about 1080 pixels and 6-7 cm tall: at NORMAL a settings row is
56 pixels, about 21 dp and under 4 mm, well below the 48 dp a touch target
should be and the "too small, too much empty space" of the report; LARGE's is
about 93 pixels (35 dp), EXTRA LARGE's 136 (52 dp).

Pages with more rows than fit (the front end, at a larger size on a short
screen, as well as the settings lists) show a window of them that follows the
selection, with a marker that steps it; the front end drops its KNUCKLES
CHAOTIX title first. The front end's picture gives way to the list's width,
and on a portrait window goes across the top with the list below it. The
pause menu's row carries on onto a second line rather than off the screen.
Anything still too wide for its column is shortened with an ellipsis.
`tests/test_menu_size.py` runs the program at each size in an 800x360, a
412x860 and a 1280x720 window and checks every touch target is on the screen
and taller than at the size below; `menu_size.*` in `tests/test_menu.cpp`
does the same for every page kind on an off-screen renderer, 2400x1080
included.

## The backdrops

A backdrop is a picture of a zone at a time of day, made by the game itself:
`render_backdrop` (`frontend/backdrop.*`) builds a second `Machine` from a
copy of the ROM, starts the stage the way TIME ATTACK does (the request at
the mode dispatcher, Start tapped until it is taken), runs 480 frames more so
the title card has gone, and keeps the frame buffer. The playing machine is
never involved.

- **Wide:** rendered with 40 extra columns a side (400x224, about 16:9), as
  WIDESCREEN renders levels. The lobby is 4:3, as the game keeps it.
- **No characters or HUD:** the backdrop machine's sprite recorder gets a pack
  that replaces every frame of banks 0-15 (the characters), 0x13 (the
  HUD) and 29 (the seven dots of the tether between the two characters,
  scaled; found with `--sprites-log`, the same bank in every zone) with an
  empty image, so they are left out of the picture
  (`runtime/sprite_replace.h`; output only, like every replacement).
  `BackdropImage::hidden_draws` counts the draws left out of the kept frame,
  and `--backdrop` prints it.
- **Interpreted:** the recompiled code keeps one machine's state in globals
  (`game/recomp_dispatch.cpp`), and that machine is the one being played, so
  the backdrop machine runs on the reference interpreters, which keep all
  their state in the machine. That is what lets it run on another thread.

The zones, in the order they are made and shown: Marina Madness 2 at sunset
(the picture the issue showed), Botanic Base 3 in the morning, Speed Slider 1
at night, Techno Tower 1 at sunset, Amazing Arena 2 by day, World Entrance at
sunset, and Isolated Island by day. Botanic Base 1 is not used: it starts by
the level's left edge, which shows tiles from beyond it.

World Entrance and Isolated Island are the game's own places, reached
through the stage select like the attractions (`runtime/stage_select.h`):

- **World Entrance** is place 7 (WORLD ENTRANCE), level 0: the lobby's hall,
  with the EXIT and PLAY signs, 4:3 as the game keeps
  it. (Before issue #37 this picture was the one called Isolated Island.)
  Its other levels are no picture: level 1 is the same hall with the
  attractions open, and from level 2 on they are the bonus-stage and
  special-stage entrances, black or white with a few sprites.
- **Isolated Island** is place 6 (INTRODUCTION), level 0: the outdoor level
  the game opens with, palms and grass, rendered wide (its start is clean
  in the margins). The game's own times of day apply to it.

**Cost.** About 620 emulated frames each, interpreted: 1.8-2.3 s per picture
on the development PC (Windows, about 330 frames a second interpreted), 13-16
s for all seven. A phone is maybe 3-10 times slower, so tens of seconds per
picture there. It happens once per ROM, on a low-priority thread
(`platform/backdrops.*`), one picture at a time, and the menu does not wait:
it is plain until the first picture is ready and picks the others up as they
arrive. With MENU BACKGROUND on PLAIN nothing is made at all.

With one zone chosen, only that zone's picture is made (`MenuBackdrops::
choose`), and it goes first: a picture CYCLE had under way is given up
(cancelled, not marked failed) so the chosen one starts at once, and the
menu is plain until it is ready. The others are made only if CYCLE is
chosen again. Pictures already in the cache are loaded whatever the choice,
so stepping through the zones in the menu shows each one at once that has
been made before.

The worker never competes with the game: slow machines have no core to spare
(issue #31's Celeron needed 20-37 ms a frame; phones share caches and thermal
headroom between few cores). It starts only while the front end or a menu
page is open, and the moment the game is played with no menu open it is held
between two frames of its machine (`BackdropControl::pause`, polled before
each one; a frame takes about 3 ms on the development PC) -- paused, not
cancelled -- and it carries on from there when a menu opens again. So on a
slow machine the pictures appear over a few visits to the menu. Autotests
still wait for the first picture before they run.

**Cache.** Each picture is written as a PNG to
`<user data>/Cache/Backdrops/v2-<first 12 hex digits of the ROM's SHA-1>/<zone>.png`
(the patched image's SHA-1 when ROM patches are applied, since they can change
the levels' art), and later starts load them at once. Delete the folder to
make them again. They are derived from your ROM and stay on your device.
`v2` since the tether is left out too (issue #37); `v1` pictures are not used.

**On screen.** On CYCLE, each time the menu opens it shows the next zone; a
chosen zone stays, and a new choice shows at once (or plain until its picture
is made). The tint and accent below come from whichever picture is shown. The
picture
covers the area (cropped to its shape, never stretched) and a translucent
coat of the zone's own colour goes over it (`backdrop_tint`: the picture's
average colour at a third of its brightness) so that the white and yellow
letters stay readable: over the whole window behind the front end and the
awards, inside the panel of the list pages, inside the band of the pause row.
The frames, markers and arrows take the zone's most common vivid colour
(`backdrop_accent`) instead of the theme's blue. In a level the game stays
visible around the panel, dimmed as before.

`chaotix_headless --backdrop all|KEY|PLACE:LEVEL:TIME[:WIDE] DIR` renders
backdrops to `DIR` and says how long each took.

**Lockstep.** Nothing here writes to the playing machine. The ROM test
`game_font.a_backdrop_renders_beside_a_playing_machine` plays 700 frames on
the recompiled machine while a backdrop is made on another thread, and the
frames must hash the same as a run without it; `lockstep_*` and the golden
frame tests are unchanged.
