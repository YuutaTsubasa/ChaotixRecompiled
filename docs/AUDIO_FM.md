# FM intermediate-memory fix

The YM2612 synthesizer previously calculated every modulation connection
within the same sample. Algorithms 0, 1, 2, 3, and 5 require an intermediate
result from the previous sample, so the missing delay changed their sound.

Each channel now retains that result across audio-generation calls. Reset
clears it; algorithms 4, 6, and 7 preserve it while it is unused. Operator-1
routing, phase/envelope update order, feedback, and output gain are unchanged.

## Validation

- Four new ROM-free tests cover one-sample latency, switching through each
  unused-memory algorithm, generation chunk boundaries, and reset.
- The original implementation failed 12 latency/switching checks; the fix
  passes all 92 unit tests and all 13 non-long CTest cases.
- The final FM implementation exactly matches the validated memory-only
  experiment across 3,200,142 stereo sample pairs from the game trace.
- A separate 3,600-frame game run with audio enabled passed the existing
  interpreter/recompiled execution checks. These do not prove audio accuracy.
- Windows and Android builds succeeded, including Android arm64-v8a and
  x86_64. The Android APK has not yet been tested on a device.
- Code review found no correctness issues; its suggested coverage for all
  three unused-memory algorithms was added and the unit suite rerun.

The research comparison with Genesis Plus GX improved from 13.16% to 6.39%
normalized waveform error for the captured game stream after gain and
one-sample alignment. This is a reference comparison, not a hardware
accuracy percentage or listening-quality score. Other audio differences
remain; see [the investigation](AUDIO_FM_INVESTIGATION.md) for methods and limits.

No third-party audio-core implementation was added to production source.
The fix is included in version 0.3.4, together with the PWM stereo routing fix.
