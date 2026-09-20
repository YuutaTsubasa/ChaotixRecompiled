# Golden output-image hashes (FNV-1a of the rendered frame, see image_hash()).
# These are hashes only — no game data. Update them deliberately when the
# renderer changes, after checking the new frames visually.
#   title screen ("PUSH START") at frame 1700
#   first level (Isolated Island) at frame 2600 with the standard input script
set(CHAOTIX_GOLDEN_TITLE "b776da693a35b150" CACHE STRING "Golden hash of frame 1700")
set(CHAOTIX_GOLDEN_LEVEL "2b3069aef5135ade" CACHE STRING "Golden hash of frame 2600")
# True widescreen (--wide 64, 448x224): title keeps 4:3 with side bars;
# level frame 2800 shows level tiles, 32X sprites and ring sprites in the margins.
set(CHAOTIX_GOLDEN_WIDE_TITLE "0db0afdee9faee50" CACHE STRING "Golden hash of frame 1700 at --wide 64")
set(CHAOTIX_GOLDEN_WIDE_LEVEL "8b368d372ff3934f" CACHE STRING "Golden hash of frame 2800 at --wide 64")
# 16:9 (--wide 53, odd width; regression for the SH-2 address error): level
# frames 2800 and 3200 with rings in the margins.
set(CHAOTIX_GOLDEN_169_A "50b83820fd709815" CACHE STRING "Golden hash of frame 2800 at --wide 53")
set(CHAOTIX_GOLDEN_169_B "d7ce2a41e46a0f50" CACHE STRING "Golden hash of frame 3200 at --wide 53")
