# Chaos Forms

The classic "super form", powered by the game's Chaos Rings. A built-in mod
by [mechasalesman](https://github.com/mechasalesman), proposed with a
working patch in [issue #146](https://github.com/YuutaTsubasa/ChaotixRecompiled/issues/146)
(Archie's "Chaos Knuckles" is the model). Off until turned on in
OPTIONS > EXTRAS > MODS, under BUILT-IN MODS (see MOD_PACKS.md).

## How to transform

1. Hold all six Chaos Rings (win the six special stages, or use the
   ALL CHAOS RINGS cheat in OPTIONS > EXTRAS > CHEATS).
2. Have **50 rings or more** in a level.
3. Jump, then press jump again in the air (the second press of a double jump).
   Not during the fall and spin that starts a level: the form waits until you
   have control (the level clock, `FFE010`, is running).

Both characters, the one you play and the partner, transform at the same time.
The press is taken by the transformation, so Knuckles does not glide, Charmy
does not fly and so on on that jump; keep holding the button and the game
still does not see it until you let go.

## What a Chaos Form does

* It gives invincibility and speed shoes together, as the two monitors do, but
  with no music (nothing is requested from the sound driver).
* It costs one ring for every second of the game running (not while paused).
  At 0 rings or fewer the form ends, as the monitors' effects wear off.
* A new level never starts in a form. A form also ends the moment the level
  does: when the end-of-act sign is touched or a boss is beaten.
* It works in the attractions and in the final battle (WORLD ENTRANCE level 2),
  not in the lobby, the bonus stage or the ending's scenes.
* The palette changes, pulsing once a second between a brighter and a mid tone:

| character | change |
| --- | --- |
| Knuckles | red body -> lime green (the Archie "Chaos Knuckles") |
| Vector | greens -> gold |
| Charmy | yellow/orange stripes (and his cheek and ring) -> gold |
| Espio | body pulses through the rainbow colours the game itself cycles him through |
| Mighty | red back -> green |
| Bomb | red body -> lime green |
| Heavy | silver body -> gold |

## Settings

OPTIONS > EXTRAS > MODS > CHAOS FORMS (`[Mods] BuiltIn = chaos_forms`), off
by default. Once on, it does nothing until you hold all six Chaos Rings.

* It is off during TIME ATTACK, so records compare like with like.
* While it is on, awards are not earned and a speedrun is not kept as a best,
  as with cheats and a changed ring tether.
* In netplay the host's setting is used by both players.
* `chaotix_headless --chaos` turns it on for tests.

## How it works (for maintainers)

`runtime/chaos_forms.cpp`, called from `Machine::run_frame` before the frame
runs. A character's status object (the word at +$3A of its object, whose
address is at $FFE034 / $FFE036) holds the invincibility timer at +$32 and the
speed shoes timer at +$34 (the game's own code, $88E85A). A form holds both
from $7FFF and lets the game count them down (the stars circle by that timer; a whole turn is added before they near the mark), which is also how it is recognised: no host flag says one is on.
The palette is output only (`Machine::chaos_pal`, used by `render_line32x`
after the character mods), so CRAM, fades and both CPUs never see it.
`Machine::chaos_drain` (frames towards the next ring, and the "jump button
taken" top bit), `chaos_last_tick`, `chaos_jump_prev` and `chaos_forms` are
in save states (format 3).
