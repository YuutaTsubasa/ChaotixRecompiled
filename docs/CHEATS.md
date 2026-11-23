# Cheats

Requested in issue #20, from the codes collected on Sonic Retro's Knuckles'
Chaotix page.

## What is supported

**Action Replay codes**: a byte or word of the game's work RAM
(`$FF0000-$FFFFFF`) set before every frame. These work exactly as on a cheat
cartridge, in the interpreter and the recompiled code alike.

**Not Game Genie codes.** Those patch ROM bytes, and most of them patch
instructions. This program runs the game as statically recompiled code, so a
patched instruction in the ROM is not the one that executes. They are
rejected rather than silently doing nothing.

## Using them

OPTIONS > EXTRAS > CHEATS switches the built-in ones on and off:

| key | menu | effect | checked by |
| --- | --- | --- | --- |
| `rings` | 99 RINGS | the ring count stays at 99 (`FFE009:63`) | the HUD and RAM read 99 throughout a level |
| `speed` | SPEED SHOES | both characters keep speed shoes (`FFADFC:01`, `FFAEBC:01`) | holding right in BOTANIC BASE 1 takes the camera over 100 px further than without |
| `chaosrings` | ALL CHAOS RINGS | you hold all six Chaos Rings (`FFE01B` raised to 6, never lowered) | with `ChaosForms` on, 50 rings and a jump in the air transform both characters |

Any other codes go in `Config/chaotix.ini`:

```ini
[Cheats]
Enabled = rings, speed
Codes = FFE01B:06 + FFADB8:01
```

`Codes` takes `ADDRESS:BYTE` or `ADDRESS:WORD` in hex, joined by `+`, commas
or spaces. If any part is not such a code, none of them are used, and the
CHEATS page shows INVALID.

While any cheat is on, achievements are not earned and a time attack run is
not recorded as a best time ("Cheats were on: not recorded").

## Codes that were tried and left out

These codes from the same list showed no effect when tried, so they are not
offered in the menu. They can still be entered under `Codes`:

- `FFADB8:01` / `FFAE78:01` "always have shield": no barrier appears, and the
  frame is pixel-identical to a run without it.
- `FFFFEC:FF00` "debug mode": pressing B in a level does nothing new.
- `FFFCEA:00` "not attached to partner": the partner stays tethered.

`FFE01B:06` (all Chaos Rings) was not tried: its effect is the ending.

Headless runs take `--cheat KEY` or `--cheat CODES`, which is how the tests
drive them (`cheats.*`, `menu.cheats_are_switched_on_from_options`).
