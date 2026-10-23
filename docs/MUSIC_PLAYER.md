# Music player

Issue #48 asked for a sound test with the songs' names, played through the
game's own FM, PSG and PWM sound, and through the player's replacement files
when music mods are installed. MUSIC PLAYER is on the front end's list, and
under OPTIONS > SOUND, so it can also be opened from the pause menu.

## Using it

The page lists the game's 41 songs by number and name, under the controls:

| row | what it does |
| --- | --- |
| NOW PLAYING | the track playing and how long it has played; Enter plays or stops |
| PLAY | the track chosen last (or the first) |
| STOP | stops the music |
| NEXT / PREVIOUS | the next or previous track in the list |
| FADE OUT | fades the music out now, as the game does (about five seconds) |
| REPEAT | what happens when a track ends: LOOP TRACK (it plays again), PLAY ONCE (stop), PLAY ALL (the next one) |
| FADE OUT AFTER | with PLAY ONCE and PLAY ALL, a track still playing after this long fades out (1:00 to 5:00, or NEVER) |
| 01 DOOR INTO SUMMER ... 41 DECISION 2ND | Enter plays that track; PLAYING marks it |

The game's music loops for ever, so a track "ends" either when it falls
silent for three seconds (the jingles, and a mod's file that does not loop)
or when its fade has finished. REPEAT and FADE OUT AFTER are kept in
`Config/chaotix.ini`, `[MusicPlayer]` (`Repeat` 0-2, `FadeAfter` in seconds).

A track a music mod replaces says REPLACED BY MOD, and plays the mod's file
(docs/MUSIC_MODS.md: the file's name is the id in hex, so track 41 is
`29.wav`). Keyboard, gamepad and touch drive the page like every other list:
up and down choose, Enter (the pad's confirm button, a second tap) plays.

While the page is open the game is held: not one frame of it runs, and its
music stops. Leaving the page, its sound comes back and it carries on from
the very frame it was held on.

## Where the list comes from

From the ROM, like TIME ATTACK's names. The game's own SOUND TEST (OPTIONS >
SOUND TEST) draws its entry N with the routine at `$887502`: N is counted
through the NUL-terminated strings its `lea $927338, a0` (at `$88751E`)
points to, entry 0 being the `--- SOUND  TEST ---` header, and entries from
the `cmpi.w #$2A, d0` at `$887524` on are sound effects (`S.E.` and a
number). Pressing a button there passes the entry's number to the sound
request routine (`$8F6E76`), so entry N is music id N: 01 DOOR INTO SUMMER
to 29 (41) DECISION 2ND. The font draws an apostrophe for `=` (`WALKIN=`,
`CHILD=S SONG`). `jukebox::track_list` reads both instructions and the
strings (in whichever ROM bank holds them all as text), so a ROM patch that
renames a song or offers fewer shows that; a ROM without them gets
`MUSIC 01`..`MUSIC 29`. Nothing of the list is in this program.

## How it plays

A second `Machine`, never the one being played, as the menu backdrops and
TIME ATTACK ghosts use (`frontend/jukebox.h`):

1. It boots from a copy of the player's ROM (patches included). Start is
   tapped through the SEGA screen, and at the next pass through the game's
   mode dispatcher (the hook TIME ATTACK starts stages from) it is sent to
   game mode `$40` sub-mode 0: the OPTIONS list. Not earlier: the boot scene
   (mode 0) is what loads and starts the Z80 sound driver. It is ready once
   its 68K is in the OPTIONS loop (`$8869C2..$886A8A`) at the end of three
   frames in a row: 139 frames from power-on.
2. OPTIONS is quiet, waits for a pad that is never pressed, and its
   vertical-blank routine (`$887EF2`, through `$8F6EFA`) passes the driver's
   PWM notes from Z80 RAM `$1BF7` to the 32X's communication registers as
   every scene does, so the 32X plays the PWM drums.
3. A track is asked for exactly as `$8F6E76` asks, between two frames: music
   writes the id into the driver's three request slots (Z80 RAM `$1C0A`) and
   `$80` at `$A0003F`; `$F0` (fade) and `$F1` (stop) take the first free
   slot. On the way it does what the hook at `kSoundRequest`
   (runtime/patches.h) does for every request of the game's: the request goes
   to the music mods, and a track a mod replaces stops the driver's music
   (`$F1`) and plays the file instead (audio/music_mods.h).
4. Its sound, at the machine's rate, goes to the audio device instead of the
   game's, at the MUSIC and mod music levels of OPTIONS > SOUND.

### Why not the game's own SOUND TEST

The first version booted that machine straight into the SOUND TEST and
pressed a button there. The request reached the driver's three slots and
stayed there, never taken: nothing played, and the same happens in the
player's own game (OPTIONS > SOUND TEST, choose a song, press A: silence,
measured headless). What sets that screen apart is its vertical-blank
routine (`$887836`): it requests the Z80's bus at once and copies all the
driver's track records for its meters, holding the bus far longer than the
other scenes' routine (`$8F6EFA`, nine bytes of PWM notes), and from the
very line on which the Z80 is interrupted to run its driver. The likely
cause is that this emulator runs the 68K's part of a time slice before the
Z80's, so the Z80 never gets to take that interrupt; that is an emulation
timing question to be looked into on its own, not something the music
player changes. The OPTIONS list plays every track.

### The player's game is untouched

The music player's machine has its own memory, CPUs and sound chips; nothing
of the player's machine is read or written while the page is open, and none
of its frames run. The one thing the two share is the recompiled code, which
serves one machine at a time (its SH-2 code checks are cached per machine
memory). While the game is held that is fine: `bind_recompiled_code` points
the code at the music player's machine for its frames and back at the game's
afterwards, and forgets every cached check each time, so each machine's code
is checked against its own memory. The recompiled code does exactly what the
interpreters do (lockstep validation), so this changes how fast the game runs
and nothing else. Tested (tests/test_music_player.py): a level run that opens
the music player at frame 1000 for 600 frames has every picture and
whole-machine state of all 1800 frames identical to the same run without it,
and lockstep holds through it.

The mode request is a field of `Machine` (`mode_request`), applied by the
same dispatcher hook as TIME ATTACK's stage request. Only the music player's
machine sets it; the game's machine never does, so its behaviour is as
before, and save states leave the field out.

## Cost

Measured on the development PC with `chaotix_headless --jukebox` (track 4,
1200 frames):

| | ms a frame | to get ready |
| --- | --- | --- |
| the music player, recompiled code | 0.67 (worst 1.6) | 139 frames, 0.12 s |
| the music player, interpreters | 2.96 (worst 4.6) | 139 frames, 0.42 s |
| the game itself, recompiled, for comparison | about 0.8 | |

It runs only while its page is open, in place of the game (which is held),
so it costs no more than playing the game does. The boot is spread over the
first frames the page is open, six emulated frames in each frame period, so
it is ready about 0.4 s after the page opens (NOW PLAYING says STARTING...
until then; a track chosen meanwhile starts when it is ready). A phone is 3-10 times slower than the PC (MENU_ART.md), so
2-7 ms a frame with the recompiled code: well within a frame.

Running only the sound hardware is not possible: the Z80 plays FM and PSG on
its own, but the PWM drums are notes the Z80 leaves in its RAM, which the
68K's vertical-blank routine passes to the 32X, whose slave SH-2 feeds the
PWM. On the interpreters the two SH-2s are three quarters of the time
(profile of the SOUND TEST: master 41%, slave 33%, 68K 6%, Z80 2%).

## Headless

```
chaotix_headless --rom R --jukebox-list [--music-dir D]
chaotix_headless --rom R --jukebox TRACK:FRAMES [--wav out.wav] [--music-dir D]
                 [--jukebox-repeat loop|once|all] [--jukebox-fade-after SECONDS]
                 [--jukebox-interp]
chaotix_headless --rom R --frames N ... --jukebox-at FRAME:TRACK:FRAMES
```

`--jukebox-list` prints the track list (and which tracks the mods in `D`
replace). `--jukebox` plays a track (its number, 1-41, or `0x29`) for
FRAMES frames on the music player's machine instead of running the game, and
`--wav` keeps its sound; it prints how long it took, its level, and whether a
mod's file played it. `--jukebox-at` opens the music player in the middle of
a run, at that frame, and carries on afterwards: with `--hash-every` and
`--state-hash-every` that shows the game untouched.
