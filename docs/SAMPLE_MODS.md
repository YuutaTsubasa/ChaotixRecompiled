# Replacing the drum samples with files

The music's drums, cymbals and other sampled sounds are the 32X's PWM
samples: 11 kHz, 8-bit recordings, which is why they sound dull next to the
FM. This plays WAV files in their place, one sample at a time, so a cleaner
recording of the same kind of drum can stand in for the game's while the rest
of the music (FM, PSG and the samples left alone) stays the game's own.

## Using it

1. Run the game once. It creates the `Mods` folder in the user data folder
   (see [MUSIC_MODS.md](MUSIC_MODS.md)). Make `Mods/Samples`, or a pack with
   a `Samples` folder ([MOD_PACKS.md](MOD_PACKS.md)).
2. Put a WAV file there named after the sample's number in hex, two digits:
   `00.wav` replaces the bass drum.
3. Start the game. The log says which samples are played from files.

To hear the game's own samples, and to see their numbers, dump them:

```
chaotix_headless --rom R --frames 900 --dump-pwm-samples DIR
```

writes `00.wav` to `15.wav` (8-bit mono at the rate the game plays each) and
prints the table. The MUSIC PLAYER's PWM pads (docs/MUSIC_PLAYER.md) are the
same numbers, so playing a track there shows which samples it strikes.

| | length | what it sounds like |
| --- | --- | --- |
| `00`, `0A` | 0.2 s, 0.45 s | bass drums |
| `01`, `09`, `0B` | 0.33 s, 0.19 s, 0.53 s | snares and a clap |
| `02`, `03`, `08` | 1.2-1.9 s | cymbals |
| `07` | 0.33 s | an open hi-hat |
| `04`, `11`, `13` | 0.6-1.1 s | toms |
| `0C`-`0F` | 0.3-0.7 s | pitched hits (strong harmonics) |
| `05`, `06` | | `04` played slower (lower) |
| `10`, `12`, `14` | | `0F`, `11`, `13` played slower |
| `15` | 64 bytes | silence: what the game strikes to stop a voice |

The descriptions are from the samples' spectra and lengths; the game does not
name them.

- **Lower ones:** `05` and `06` are `04`'s recording at a lower step, and so
  on. A file for `04` plays for them too, slowed by the same amount, unless
  they have files of their own.
- **Formats:** as music files: PCM WAV at 8, 16, 24 or 32 bits, or 32-bit
  float, any rate, mono or stereo.
- **Level:** a file is brought to the level of the sample it replaces (their
  peaks matched; the log gives the factor), so the music keeps its balance
  whatever level the file was saved at.
- **Volume and pan:** each note's own, as the game plays it.

## How it works

The sound driver (Z80) decides the notes; once a frame the 68K
(`$8F6EFA`) copies four voices' notes from Z80 RAM `$1BF7` to COMM4-7, and the
slave SH-2's PWM interrupt (`$C0000060`, in its cache RAM) takes them. A note
is a word: the low byte the sample, the high byte the volume, left in its top
four bits and right in its bottom four, each `(n * 2 + 2) / 32`. The sample
table, whose address the driver keeps at `$C00001F0` (`$22058000`, in the
ROM), has four longs a sample: the first byte, the length, where to carry on
at the end (0 for all of the game's) and the step in 1/0x800ths of a byte an
interrupt. The bytes are unsigned 8-bit. On each interrupt the driver adds
each voice's byte times its volume / 16 to the pulse width and writes two
values, the new one and one halfway to it from the last; the FIFO takes one a
PWM cycle, so with the timer at 1 the interrupt comes every other cycle:
11,000 bytes a second at a step of 0x800 (measured, 183.5 bytes a frame).
`audio/pwm_samples.{h,cpp}` reads all of this.

With files (`audio/sample_mods.{h,cpp}`):

- The machine keeps each note the 68K writes to COMM4-7 with the master
  clock at the time. A note for a replaced sample is changed, before the
  SH-2 sees it, to the same volumes on sample `15`, the silent one: the voice
  stops what it was playing, as the note would have made it, and plays
  nothing. The change is made in the bus, where the interpreter and the
  recompiled code both write, so lockstep still holds (2400 frames with the
  test pack below, bit-identical).
- After each frame the player takes the notes and starts each file at its
  note's moment in the frame's sound, 6 output samples later (the driver
  takes a note at its next interrupt, and the FIFO holds what it wrote
  before: measured against the game's own drums, 5 to 7). One sound a voice,
  as in the driver: a later note on the voice, replaced or not, ends it.
- The level is the driver's: a file's +-1 is a byte's +-128, times the
  note's volume / 16, as pulse width, turned into output as the PWM is
  (`Machine::audio_advance`), with the PWM volume setting.

Without files nothing changes: no note is altered.

## Validation

- `sample_mods.*` unit tests: the note's volumes, the 8-bit WAV.
- With the local ROM, `the_table_is_the_games`: 22 samples, `05` made from
  `04`'s recording at step 0x659, `15` 64 bytes of 0x80.
- With the local ROM, `files_play_in_place_of_the_samples`: MIDNIGHT
  GREENHOUSE (track 5) on the music player's machine, three ways: as the game
  plays it; with every sample replaced by silence, where the drums are gone;
  and with every full-speed sample replaced by a file of the game's own
  sample (the slower ones playing those files slowed). The drums from the
  files against the game's own: correlation 0.96, level 1.02.
- By hand, the same with `--dump-pwm-samples` files and `--samples-dir`:
  tracks 5, 7 and 13 at 0.96-0.98 correlation in both channels, levels
  within 0.15 dB.

A test pack made from the CC0
[FreePats Electric and Synthesizer Percussion](https://freepats.zenvoid.org/Percussion/electric-percussion.html)
set (bass drums, snares, clap, cymbals, open hi-hat and toms; the pitched
hits left as they are) plays, and is how the lockstep check above was run.
