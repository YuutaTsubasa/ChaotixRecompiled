# Issue #7: the picture drops to 4:3 during play

Reported as "visual bug where it goes into 4:3 whenever a level starts", on
Windows, with a note that turning `Widescreen = false` made no difference, and
followed up with "it still goes to 4:3 and back part way through".

Two separate causes, both in when the level's widescreen margins are considered
valid. `patches::begin_frame` only shows the margins while `plane_shift > 0`
(the level engine has been streaming the wider plane ring) and while
`wide_scene_active()` says a level is on screen.

## 1. The margins were latched to the window shape at level load

The margins follow the window: `sdl_main.cpp` recomputes `Machine::wide_extra`
from the render output size every pass round the loop. The level engine has to
cooperate, though — the patches shift the plane ring by 96 px so the margins
have tiles to show — and that shift can only change where a full redraw
re-establishes the ring, which is when a level loads. It was latched from the
margin width at that moment:

```cpp
m->plane_shift = plane_shift_for(std::clamp(m->wide_extra, 0, kMaxWideExtra));
```

The default window is 4:3 (`WindowScale = 3` gives 896x672), so a level that
starts before the player resizes latches a shift of 0, and widening the window
afterwards changed nothing until the next level load.

Measured on the 0.3.5 release, same level, same frame, 896x672 window, aspect
switched to 16:9:

| when the aspect changed | picture on screen |
| --- | --- |
| before the level started | 895 px wide, full widescreen |
| while the level was running | 672 px wide, 4:3 with 111 px bars each side |

That also explains the reporter's note about `Widescreen = false`: what they
were seeing was the 4:3 fallback either way, so the setting made no visible
difference.

### Repair

`plane_shift_for()` returns the same 96 px for every margin width — the ring
always ends up covering `cam - 96 .. cam + 416`, wide enough for any margin up
to `kMaxWideExtra`. So the shift never needs to track the margin width; it only
needs to know whether widescreen is switched on at all. That is now separate
state, `Machine::wide_enabled`, set from the player's setting rather than from
the current window:

```cpp
m->plane_shift = plane_shift_for(m->wide_enabled ? kMaxWideExtra : 0);
```

The margins are streamed from a level's first frame whenever widescreen is
enabled, and widening them mid-level just shows more of what is already there.
Resizing the window, going fullscreen and changing the aspect ratio all take
effect on the next frame. Turning widescreen off and on again still waits for
the next level load, because that genuinely changes the ring.

## 2. Pausing looked like the level had ended

`wide_scene_active()` called a frame a level scene when the level engine's
per-frame plane update had run in the last 8 frames. Pausing stops that update
while the game keeps the level on screen, so eight frames into any pause the
margins went black and the picture snapped to 4:3 — and snapped back on
unpause. Traced on a real level: paused at frame 2204, margins dropped at 2210,
restored at 2804.

Nothing moves while the engine is stopped, so the ring still holds the level's
tiles and the margins remain valid. What has to be ruled out is another scene
taking the planes over. Two signals do that, neither of them guessed:

- Which scene the game says is on screen: the game mode at `$FFDFDE` that the
  mode dispatcher reads, together with the place and level the level engine
  reads at `$FFDFF2` and `$FFDFF4` (`scene_key`). Measured modes: `0x00` boot
  and the SEGA logo, `0x08` title, `0x18` a level **or the lobby**, `0x38` an
  attract demo. The mode alone is not enough, because finishing a level hands
  over to the lobby without leaving mode `0x18`; the place and level change
  there. A pause changes none of the three. `begin_frame` clears
  `level_seen_frame` as soon as the key stops matching the one the engine last
  drew under.
- A full redraw (`full_redraw_frame`), which catches a level restarting within
  the same scene, after a death.

So a stopped level keeps its margins until one of those says the planes now
belong to something else. As a side effect the attract demo now stops being
widescreen the frame it ends rather than 7 frames later, which is also correct.

Getting the scene identity wrong is visible, and was: with the mode alone, the
recorded run went on drawing the finished level's ring for 196 frames after the
hand-over to the lobby — the camera jumping about inside it as the lobby set
itself up — and then flashed 4:3 for four frames when the lobby was finally
drawn. With place and level in the key the margins let go at frame 4641, one
frame after the zone changes.

The camera itself is not affected by any of this: traced frame by frame over
the recorded run, the camera position is identical in 4:3 and in widescreen for
all 5000 frames, so the lobby sliding into place at frame 4844 is the game's
own movement, not the widescreen camera clamp.

Checked across the whole 36000-frame attract sequence: every stretch that is
4:3 is a genuine non-level scene (boot, SEGA logo, title, level loading), and
every demo level is widescreen end to end.

## Validation

- `widescreen_follows_a_resize_during_a_level` (new): a level starts with no
  margins and is widened 600 frames in; the margins must go live immediately.
  Headless gained `--wide-at FRAME:E[:EB]` for this.
- `widescreen_survives_a_pause` (new): a level is paused for 600 frames; the
  margins must stay live throughout, which the run's end-of-run count reports
  as a single stretch. Headless gained `--trace-wide` and that count.
- `widescreen_lets_go_when_a_level_hands_over` (new): the recorded run must
  drop the margins within a few frames of reaching the lobby, not 196.
- Swept for flicker: over the 36000-frame attract sequence, a 16000-frame level
  run and the 14000-frame recorded run, there is no stretch of either state
  shorter than 30 frames, and no camera jump larger than 24 px while the
  margins are live other than the lobby's own entrance.
- `widescreen_centre_matches_4_3` still passes: the widened rendering leaves the
  native 320 px pixel-identical to a 4:3 machine run side by side.
- `golden_frames`, `golden_frames_widescreen`, `golden_frames_16_9` unchanged.
- Frame hashes every 50 frames over a 3400-frame level are identical with the
  ring shift latched and not latched when no margins are shown, so the players
  who keep a 4:3 window get exactly the picture they got before.
- `lockstep_widescreen`, `lockstep_boot_to_level` and both long attract lockstep
  runs pass: the reference interpreter and the recompiled code take the patch at
  the same instruction.
