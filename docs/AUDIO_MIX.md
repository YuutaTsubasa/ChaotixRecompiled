# Mix levels of FM, PSG and PWM

The three sound sources are mixed at the levels PicoDrive uses. Before this,
the 32X's PWM channel was mixed at a quarter of its full range and FM well
below it, so the whole game was about 7 dB quieter than PicoDrive, and the
PWM drums sat about 3.5 dB further back against the FM than they should.
Players heard that as missing drums ("the music missed some 32X audio
channels").

## Levels

| Source | Peak per voice | Where it is scaled |
| --- | --- | --- |
| FM (YM2612), one channel | +-8191 | `Machine::audio_advance`: `Ym2612::generate` gives +-3072, times 8/3 |
| PSG (SN76489), one tone or the noise | +-2304 | `Machine::audio_advance`: `Sn76489::run` gives +-4096, times 9/16 |
| PWM, the whole channel | +-32768 | `pwm_level` in `Machine::audio_advance` |

The sum is then taken down by an eighth and clipped to 16 bits. The DC
blocker is unchanged.

These are PicoDrive's figures, read from its source
([libretro/picodrive](https://github.com/libretro/picodrive), `pico/sound/ym2612.c`,
`pico/sound/sn76496.c`, `pico/32x/pwm.c`, `pico/sound/mix.c`):

- An FM channel is saturated to 14 bits, +-8192.
- A PSG voice swings between 0 and a quarter of 0x4800 (4608), which is
  +-2304 about its middle. Noise is a voice like the tones, so it now swings
  both ways here too; before, it went from 0 up only, at half the tones' level.
- A PWM pulse width from 0 to the cycle maps to -0x8000..+0x8000. A width
  past the cycle is held at full scale.
- The final mix loses an eighth "to avoid clipping".

No PicoDrive code was copied.

## Measurement

PicoDrive (the Windows libretro core from buildbot.libretro.com, 2026-10-29)
was driven from a small host that runs it without a window, presses the same
buttons and writes the same stage request into 68K RAM. The same ROM was run
in `chaotix_headless`, and both outputs were compared as WAV files.

RMS level, dB below full scale:

| Stretch | PicoDrive | Before | After |
| --- | --- | --- | --- |
| Title music (both in step from power-on) | -23.5 | -31.2 | -23.3 |
| BOTANIC BASE 1, 26-56 s | -20.1 | -27.4 | -20.2 |
| BOTANIC BASE 1, 40-65 s | -20.1 | -27.2 | -20.1 |

Energy by band, BOTANIC BASE 1, 26-56 s (dB, relative):

| Hz | 40-150 | 150-400 | 400-1000 | 1000-2500 | 2500-5000 | 5000-10000 |
| --- | --- | --- | --- | --- | --- | --- |
| PicoDrive | 63.1 | 59.6 | 60.3 | 58.8 | 55.8 | 49.6 |
| Before | 53.7 | 57.1 | 52.2 | 50.7 | 47.8 | 44.5 |
| After | 63.8 | 60.5 | 58.9 | 58.0 | 55.8 | 51.4 |

The 40-150 Hz band is where the PWM bass drum lives. It was 9.4 dB short of
PicoDrive; now it is within a decibel. PicoDrive's drum hits fall on exactly
the same beats as the PWM output here, so the PWM content itself was right
all along. Only its level was off.

## Replaced music

A music mod's WAV is mixed at a percent of its own level (`ModMusicVolume`,
25 by default, because the files are mastered louder than the game's own
music). The game's music is now 7/3 louder than when that default was
chosen, so `audio::mixed_percent` scales the setting by 7/3. A saved setting
keeps the same balance against the game's music.

## No pop when a level changes

Muting the music, or moving any volume, used to pop, and at the levels above
the pop got louder. A level that jumps cuts whatever the waveform was doing
at that sample, which is a click. It also takes away at once whatever DC the
music carries, which the output's DC blocker turns into a thump. The DC is
there because, in a level, the game parks PWM at 465 of a 1047 cycle instead
of the middle, 523, and the FM and PSG sum sits a little below zero too
(about -320 and -180).

So FM, PSG and PWM levels now move to the one asked for by 1/2048 per sample:
0 to 1 takes 38 ms (`audio/gain_ramp.h`). That is long compared with the DC
blocker's 9 ms, so what is left of a DC step is below what can be heard.
At 100% nothing is scaled, so the output there is unchanged.

Muting the music in BOTANIC BASE 1, before and after:

- At the moment of muting, the biggest jump between two samples went from
  3353 to 1980. The music's own biggest jump in the second before is 2294.
- The music now fades out over two to three frames.

A separate DC blocker for PWM, ahead of its level, was also tried. Above
60 Hz it gave the same result as the easing alone (40 RMS against 44 in the
first frame after the fade), and its state would have had to go into save
states, which would make every existing save state unreadable. So it was
left out.

`pwm_audio.a_level_change_eases_in_without_a_jump` checks this without the
ROM. It fails if levels jump.
