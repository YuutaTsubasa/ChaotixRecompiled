# Golden output-image hashes (FNV-1a of the rendered frame, see image_hash()).
# These are hashes only — no game data. Update them deliberately when the
# renderer changes, after checking the new frames visually.
#   title screen ("PUSH START") at frame 1700
#   first level (Isolated Island) at frame 2600 with the standard input script
set(CHAOTIX_GOLDEN_TITLE "b776da693a35b150" CACHE STRING "Golden hash of frame 1700")
set(CHAOTIX_GOLDEN_LEVEL "2b3069aef5135ade" CACHE STRING "Golden hash of frame 2600")
