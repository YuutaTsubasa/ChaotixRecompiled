# Issue #5: rendering audit and bounded fixes

Reference: PicoDrive libretro, upstream revision
`26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee`, DRC and frameskip disabled,
verified Japan/USA ROM, no input during the opening. All captures and memory
probes stay outside version control. No reference emulator code or game data
is included in these changes.

## Proven defects repaired

- Palette index zero in packed/RLE mode and direct-colour zero now follow the
  ordinary 32X colour/priority rules. The old renderer discarded them. See
  [Sega hardware manual, printed pp. 40 and 46](https://consolemods.org/wiki/images/e/e9/32X_Hardware_Manual_1994_Sega_text.pdf#page=48):
  overwrite-image zero-byte suppression is a write operation, not display
  transparency. PicoDrive `pico/32x/draw.c` independently agrees.
- Extended rows below 223 read their centre pixels from the shadow buffer,
  matching where the existing widescreen blitter stores them.
- Bottom sprite clipping extends with the viewport in the game's 24.8 fixed-point
  format and restores the ROM's native bound when the extension deactivates.
- Both shadow banks initialize to zero and are cleared on reset.
- INTRODUCTION level 0 uses the native viewport during the first Eggman
  encounter. The game deliberately parks Eggman at screen X=-49, inside the
  53-pixel extension. The event sets WRAM C21C bit2 at 68K address 8A6CB8;
  the level initializer clears it. Restricting the mask to zone 6/level 0 and
  that bit keeps playable approach/training widescreen without timers or
  changing the output size. It also hides Metal Sonic's offscreen staging.
- The extra right edge at widths above 448 pixels can display MD rings at
  X384..399. Only the added H40 level range is reinterpreted; ordinary nine-bit
  coordinates and real X0 sprite masking retain their native behavior.

## Verification

- Regression-first synthetic tests reproduced 16 failed compositor checks,
  8 failed shadow/clip checks, and 4 failed widest-SAT checks before the fixes.
- 110 unit tests pass, including 15 new rendering cases (one uses the supplied local ROM). All 14 non-long CTest
  entries pass. Existing golden hashes were not changed.
- Five main attractions each completed 2,600 frames at `--wide 80 --wide-bottom 16`
  with interpreter/recompiled lockstep, stage entry, movement, jumping and
  reversal. Screenshots inspected at frames 800, 1200, 1600, 2000 and 2400.
- The full 36,000-frame attract sequence at `--wide 80 --wide-bottom 16`
  completes in lockstep with zero invalid SH-2 code validations.
- The native 36,000-frame attract run passes every existing golden checkpoint.
- The portable Windows preview passes all five ROM startup/import regressions
  with the development-tool DLL directory removed from PATH.
- The cutscene regressions reproduced 11 failed checks before the fix; all
  pass afterwards. A 4,000-frame route in native, 16:9 (E53), and widest (E80)
  modes completes in interpreter/recompiled lockstep. The fixed 16:9 centre
  matches the pre-fix centre at frames 2200, 2400, 2900, 3500, 3600 and 3800; only the
  intended margins change. The teaching segment reopens widescreen at frame 3800.
- Independent code reviews found no actionable regression in the fixes.

## Scope and outstanding investigation

Issue #4's verified import/persistence fix remains in 0.3.5; its unit and startup
regressions still pass. No new report or reproduction of that issue was found.

The reporter's screenshot is the SEGA logo's space scene (attract frames
~4390..4700), not the opening city. With 0.3.5's index-0-transparent rule,
16 Mega Drive pixels show through there: a yellow dash near X270,Y31 and a
pink dash at the left edge near Y128, exactly the screenshot's artefacts.
With index 0 drawn as an ordinary pixel they are covered. This reverses
commit 51b50ca, which made index 0 transparent to remove black blocks in level
transitions; dense captures of the attract sequence's first level transition
(frames 6900..7260, every 6) and the 149 changed golden checkpoints show no
black blocks, only one-step sky shade differences (the 32X palette entry
instead of the MD backdrop). `tests/golden/attract_every50.txt` was regenerated
accordingly.

Remaining for issue #5: the first 430 native opening frames contain no
other reproduction of the screenshot's artefacts. Scene-aligned SEGA captures match
PicoDrive geometry (for example native 340/reference 350 and 370/380), with only
small colour quantization differences. Comparing equal frame numbers is
misleading because the two runtimes progress at different rates.

The first 1,800 frames sampled every 10 frames are visually identical before and
after the zero-pixel correction, so that correction alone does not explain the
reported intro problem. Dense capture also resolves the apparent city-spotlight
discrepancy: native 789/790 matches PicoDrive 847/848, native 791/792 matches 849/850, and native
850 matches 907/908. Each pair has zero pixels differing by more than 16 in
any RGB channel (remaining differences fit RGB565 quantization). The lights
alternate two frames on/two off; ten-frame sampling at an assumed +60 offset
was comparing opposite phases. No raster arithmetic change is justified by
these observations. This is evidence for those sampled scenes, not a claim
of full emulator or original-hardware visual parity. Of 140 consecutive native
frames 726..865, 138 have reference matches within the above threshold using
local offsets +50..+70. Frames 757..758 retain a small transient upper-scene
difference with no established cause.

Other unverified areas include transitions, paused-scene detection, special
stages/bosses, and layouts streamed only for the original 4:3 viewport.

## Reproducing the first Eggman encounter

Use `chaotix_headless --rom <local-ROM> --frames 4000 --wide 53
--stage 600:6:0:0:2:7:0 --press 1400:right:2600`, with 14-frame C presses
starting at 1440 and repeated every 120 frames through 3720. The approach at
2200 and training at 3800 should retain wide content; captures 2400, 2900 and 3500
should have black side margins and visible native-width action.
