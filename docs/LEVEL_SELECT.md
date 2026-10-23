# LEVEL SELECT (issue #51)

The front end's LEVEL SELECT starts any level directly, the way Sonic 2's
level select does: no lobby walk, no Combi Catcher, no draw for the time of
day. It is practice, not a run.

## The way in

It is TIME ATTACK's way in. Knuckles' Chaotix carries a complete stage-select
screen the shipped game never reaches (game mode 0x30, `runtime/stage_select.h`);
its START routine ($8F72C8) is transcribed in `stage_select::apply`, which the
patch hook at the game's mode dispatcher runs when a request is pending. The
frontend taps Start until the title screen gets back to the dispatcher, so the
level starts in about a second. Nothing in the emulation differs from TIME
ATTACK: the request, the hook and the code the game then runs are the same,
and the interpreter and the recompiled code agree on every place offered
(`level_select_starts_place_*` in `tests/CMakeLists.txt`, lockstep).

What differs is the frontend (`frontend/level_select.h`, `platform/sdl_main.cpp`):

| | TIME ATTACK | LEVEL SELECT |
| --- | --- | --- |
| Clock, best times | yes | no |
| Ghost, speedrun split | yes | no (a speedrun run going is reset) |
| Awards | yes | no |
| Save states, rewind, fast-forward | no | yes |
| Lobby shortcuts | off | off (the page picks the partner and time) |
| DATA slot | NO SAVE | NO SAVE |
| When the level is over | back to the page, with the time | back to the page |

## What is offered

Measured on the stage select, from the title screen with Knuckles and Espio
(`--stage 300:PLACE:LEVEL:0:2:7:0`, shots at frames 700-1100):

| PLACE (game value) | Levels offered | Notes |
| --- | --- | --- |
| BOTANIC BASE .. MARINA MADNESS (0-4) | 1-5 | the game's own table, as TIME ATTACK |
| TRAINING (5) | 0-4 | |
| INTRODUCTION (6) | 0-5 | |
| WORLD ENTRANCE (7) | 1 only, shown as "-" | 0 and 1 both start the lobby; 1 is the one `apply` opens all five attractions for. 2-6 are the ending's scenes, 7 a black screen |
| BONUS STAGE (9) | 0 only, shown as "-" | every level is the same stage |
| SPECIAL STAGE (10) | 0-5, shown as 1-6 | the six stages (LIMITS 4, 6, 8, 10, 12, 14); 6 and 7 are wireframe copies of the first two |

NOT USED (8) is not offered.

## Saving

The stage select's own set-up ($8F6FD8) makes FFE01C -1, NO SAVE, and
`apply` does the same, so the end of a level ($890FD2) writes no DATA slot.
`level_select_a_finished_level_saves_nothing` replays a run finished at the
goal (`tests/recordings/finished_run.txt`) as a LEVEL SELECT session: FFE01C
is still FFFF afterwards and the cartridge save never changes, where the same
run played into DATA 1 saves at frame 4639.

A save state taken during a session keeps NO SAVE. Loading a state with a DATA
slot (one saved in story mode) during a session ends the session: that game
goes on as it was, and is not sent back to the page.

## When it is over

The game does not say a level is over; it moves on. A session is followed
once per frame (`level_select::Session`) and is over when:

- a zone's level (places 0-6): the game reaches its lobby (zone 7), where it
  sends a player however a level ends (`stage_select::kLobbyPlace`; the
  recorded run reaches it at frame 4639, after the goal and the tally);
- a special or bonus stage: its game mode (0x20, 0x50) is left. A special
  stage started this way goes on to the title screen (mode 8) after its
  results (`level_select_a_special_stage_ends_at_the_title`);
- any of them, WORLD ENTRANCE included: the game leaves the modes it plays in
  (0x18 level, 0x20 special, 0x50 bonus), for the title screen or a game over.

In WORLD ENTRANCE the player can walk into an attraction and back; the
session goes on until BACK TO TITLE. When it is over the frontend puts the
title screen back, as BACK TO TITLE does (the cartridge save is kept, and
nothing was written to it), and opens the LEVEL SELECT page again.

## Headless

`chaotix_headless --level-select --stage F:PLACE:LEVEL:TIME:PLAYER:COMBI:PLAYERS`
(or with `--replay-input`) follows the session as the frontend does and prints

    level select: reached scene MODE:ZONE:LEVEL at frame N, save slot FFFF
    level select: the level is over at frame N (mode M zone Z level L)

with the scene as `patches::scene_key` has it.
