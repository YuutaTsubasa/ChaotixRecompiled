# FM audio investigation — 2026-09-27

The next supported fix is the missing one-sample intermediate memory in
YM2612 algorithms 0, 1, 2, 3, and 5. The current implementation calculates
these connections entirely within the current sample. Both reference cores
retain an intermediate value for the following sample.

An isolated experiment implementing only that memory improved all five
affected synthetic cases and the captured game sequence. Algorithms 4, 6,
and 7 remained bit-identical to the pre-fix implementation. The approved
memory-only fix has now been implemented for version 0.3.4; see
[the implementation report](AUDIO_FM.md). The comparisons below describe
the research baseline and isolated experiments.

## Controlled comparison

The capture used the local verified ROM, 3,600 frames, and Start held for five
frames beginning at frames 1750, 1950, and 2300. It recorded every call to the
YM2612 write and sample-generation interfaces, including the generation
boundaries. It contains 34,339 data writes and 3,200,142 stereo sample pairs.

The identical command stream was replayed into the current Recomp YM2612,
isolated experimental variants, and two reference cores. This removes the
previous comparison's different scene timing, sample rates, PWM, PSG, and
frontend resampling. It does **not** validate the original register-write
timestamps against real hardware or another emulator's CPU execution.

References:

- [PicoDrive YM2612](https://github.com/notaz/picodrive/blob/26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee/pico/sound/ym2612.c):
  native-rate configuration, 53,267 samples/sec and clock exactly 144 times
  that rate, SSG enabled, DAC distortion disabled.
- [Genesis Plus GX YM2612](https://github.com/ekeeke/Genesis-Plus-GX/blob/939ce4f045f981f89965f24780cef045cc5e52d7/core/sound/ym2612.c):
  native-rate core, enhanced output mode to exclude analog DAC distortion
  and carrier truncation from this initial timing comparison.

The integer rate is a diagnostic time scale. It is not a change to the
runtime's existing MCLK/1008 rate. Reference output levels differ from the
Recomp mixer's scale, so comparisons fit one gain coefficient. Current
Recomp also advances oscillator phase before output, while these references
advance it afterward; comparisons below align the current and memory-only
outputs by one sample.

NRMSE is `sqrt(sum((recomp - gain * reference)^2) / sum(recomp^2))`, with gain
fitted by least squares over the compared samples. A smaller value means
closer waveforms under this procedure, not a percentage of hardware accuracy
or a listening-quality score.

## Intermediate-memory experiment

Synthetic cases use one channel, all four operators, distinct multipliers
and levels, instantaneous attack, constant envelopes, feedback disabled,
and each of the eight algorithms. They run for 53,267 samples each.

| Comparison with Genesis Plus GX | Current NRMSE | Memory only NRMSE |
| --- | ---: | ---: |
| Algorithm 0 | 13.14% | 0.29% |
| Algorithm 1 | 18.71% | 0.28% |
| Algorithm 2 | 12.66% | 0.35% |
| Algorithm 3 | 6.11% | 0.27% |
| Algorithm 5 | 0.94% | 0.45% |
| Entire captured game stream | 13.16% | 6.39% |
| Game stream, seconds 45–53 | 3.16% | 2.69% |

Algorithms 4, 6, and 7 were unchanged by this experiment. Their synthetic
NRMSE remains approximately 0.3%. All eight cases were additionally replayed
as single-sample generation calls; both the experimental and reference
outputs matched their respective whole-buffer outputs byte for byte.

The game stream used all five affected algorithms while operators were
keyed on. This links the missing behavior to game activity, rather than
only artificial test inputs. It does not establish that this defect explains
every audio concern in issue #3.

## Why not reproduce every PicoDrive difference?

PicoDrive also routes the previous sample's operator-1 result into the
algorithm. Adding that delay reduced the experimental difference against
PicoDrive further. However, Genesis Plus GX explicitly records removal of
that extra operator-1 delay in its 2017-03-12 changelog and uses the current
operator-1 result in `chan_calc()`.

The PicoDrive-oriented experiment (intermediate memory, delayed operator 1,
and phase advancement after output) reduced full-stream NRMSE against
PicoDrive to 8.67%, but measured 18.16% against Genesis Plus GX. That is worse
than the 6.39% memory-only result against Genesis Plus GX. This is evidence
that matching one emulator is not a sufficient correctness criterion.

The recommended first production change is therefore **intermediate memory
only**, retaining the current operator-1 routing. Phase and envelope update
ordering should be investigated separately: moving phase advancement after
output increased the game-stream error against Genesis Plus GX to 8.69%,
despite similarly close steady synthetic tones.

## Other findings and remaining work

- No SSG-EG, LFO, channel-3 special mode, or FM DAC enable writes occurred in
  this captured sequence. This is stronger evidence than the earlier
  end-of-frame snapshots, but does not cover every level or sound effect.
- Basic carrier pitch and the tested positive/negative detune settings
  agreed closely with PicoDrive after output-scale and phase alignment.
- FM output gain should not be changed based on raw reference loudness.
  The approximately 0.375 fitted scale is consistent with the current
  `acc >> 5`, then multiply-by-12 output path. Overall FM/PWM/PSG balance
  still needs separate validation.
- Remaining differences need targeted tests for feedback, envelope timing,
  retriggering, and algorithm changes during notes. No physical hardware
  recording or blind listening comparison was performed.
- Integration now includes ROM-free regression cases for delayed connections,
  reset, generation chunk boundaries, and dynamic algorithm changes. The
  implementation report records the completed build and integration checks.

## Local reproduction artifacts

All diagnostic code, third-party references, register traces, and recordings
are in the ignored `build/audio-investigation/` directory. No third-party
core code or game-derived trace/audio has been added to production source
or a release.

- `prepare_fm_trace.py`, `fm_trace.cpp`: instrumented capture build.
- `fm_replay.cpp`, `pico_ym_shim.c`: common-stream PicoDrive comparison.
- `fm_synthetic.py`, `fm_analyze_game.py`: synthetic cases and write analysis.
- `prepare_fm_memory_experiment.py`: isolated intermediate-memory variant.
- `prepare_fm_pipeline_experiment.py`: additional operator-1/phase experiments.
- `prepare_genplus_replay.py`, `genplus_shim.c`, `shared.h`: isolated Genesis
  Plus GX comparison. The shim supplies types and unused save/load plumbing.
- `fm_compare_memory.py`, `fm_compare_pipeline.py`, `genplus_compare.py`:
  waveform comparison tables, also saved as JSON.
- `verify_fm_research.py`: assertions for affected/unaffected algorithms,
  chunk invariance, reference-independent baseline replay, and game-stream
  improvement. Passed for all eight synthetic algorithms.
- `fm_remaining.py`: per-channel residual investigation and a diagnostic
  feedback-disabled stream; these are not production changes.
- `fm-current-30-36s.wav`, `fm-memory-experiment-30-36s.wav`, and
  `fm-genplus-reference-30-36s.wav`: aligned FM-only listening clips. The
  reference uses a fixed 0.375 gain; no independent peak normalization.
