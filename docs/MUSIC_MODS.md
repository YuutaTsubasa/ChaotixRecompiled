# Replacing the music with files

Requested in issue #3: "a setting for the game to use audio files for its
music ... would make audio modding a lot more accessible". This plays WAV
files instead of the game's own music; the sound effects are still the
game's.

## Using it

1. Run the game once. It creates `Mods/Music` in the user data folder (the
   folder with `Config` and `SaveData`; `--user-dir` changes it).
2. Put a WAV file there named after the music id in hex, two digits:
   `05.wav`.
3. Start the game. The log says how many pieces were found, and each time the
   game asks for music it logs the id, with "playing 05.wav instead" when a
   file replaces it.

| id | music |
| --- | --- |
| `01` | TRAINING |
| `02` | TECHNO TOWER |
| `03` | SPEED SLIDER |
| `04` | MARINA MADNESS |
| `05` | BOTANIC BASE |
| `07` | AMAZING ARENA |
| `11` | INTRODUCTION |
| `0D`, `0E` | heard first in the attract sequence: the opening and the title |

The table is the game's own: the place-to-music table at `$8F6ED0`. Ids go up
to `29`; the rest (jingles, the lobby, bosses, special stages, ...) show up in
the log when the game plays them.

- **Formats:** PCM WAV at 8, 16, 24 or 32 bits, or 32-bit float, any sample
  rate, mono or stereo (other channels are ignored). Files are resampled once,
  when first played.
- **Loops:** the first loop of the WAV's sampler (`smpl`) chunk, which most
  audio editors write when you set a loop. Or a text file next to it,
  `05.loop`, holding the loop's first sample frame and optionally its last,
  counted in the file's own sample rate: `529200 4762800`. Without a loop the
  piece plays once.
- **Volume:** `ModMusicVolume` in `[Audio]` of `Config/chaotix.ini`, a
  percentage of the game's volume. It defaults to 25 because the game's own
  music is quiet (around -27 dBFS) next to a mastered recording.

## How it works

Every sound request goes through one routine, `$8F6E76`, with the sound in
`d0`; ids below `$2A` are music. A patch hook just before it posts the id to
the Z80 driver (`runtime/patches.h`, `kSoundRequest`) passes music requests
and two driver commands to the player. For a piece with a file it posts `$F1`
instead, which stops the driver's music while its sound effects carry on
(measured: the level's music falls to silence, and sound effects requested
afterwards play). The player (`audio/music_mods.{h,cpp}`) mixes the file
into the machine's own audio output, so it runs on emulated time like the
rest of the sound:

- Another piece without a file (a jingle, invincibility): the file pauses,
  the driver plays. The game asks for the level's music again afterwards
  (`$88E962` replays `$FFFCFC`, the current music), and the file carries on
  from where it stopped.
- `$F0`: the driver fades its music out over about five seconds (measured
  second by second); the file fades the same way. `$F1`: stop.
- Pause: the driver holds its music while Z80 RAM `$1C10` is non-zero; the
  file pauses with it.

The substitution is made in the same instruction by the interpreter and the
recompiled code, so lockstep still holds with music replaced (2400 frames,
with a pause, checked). Without files nothing changes: no request is altered.

## Validation

- `music_mods.*` unit tests: WAV decoding (8/16/24-bit PCM, float, mono to
  stereo, resampling), loops from `smpl` and from `.loop`, rejected files, and
  the player's rules: play, carry on, a jingle holding the piece and the next
  request resuming it, stop, pause, loop, the five-second fade.
- With the local ROM, `replaced_music_silences_the_driver`: BOTANIC BASE 1 is
  above 800 RMS with its own music and below 50 with `05` replaced by
  silence. With the substitution removed from the patch, this test fails.
- A 440 Hz `05.wav` in BOTANIC BASE 1 (headless `--music-dir`, `--wav`): the
  output holds the tone at the expected level and nothing at 300 Hz or
  1 kHz, where the game's music had energy.

On Android the user data folder is the app's private storage, which other apps
cannot reach without a computer (`adb push`), so for now this is mainly for
the desktop builds.

Not done: other formats (OGG, FLAC, MP3 need a decoder library), replacing
sound effects, and per-act or per-time-of-day music, which the game does not
have as separate ids.
