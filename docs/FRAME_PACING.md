# Frontend frame pacing fix

The frontend could present an unchanged game frame when its next update was
not due yet. With VSync enabled, that presentation could block long enough to
require two game updates on the following pass, making motion uneven despite
an average close to 60 FPS.

The frontend now waits for the next game-frame deadline before polling input.
It preserves the original approximately 59.92 FPS NTSC rate and fractional
timing, keeps fast-forward and automated tests unthrottled, and retains bounded
catch-up after a stall. No emulation or rendering algorithm was changed.

## Windows measurements

Measured on 2026-09-27 with an i9-14900KF, RTX 4090, SDL Direct3D 11, and a
60 Hz ASUS VA325 display in a 960x540 window. Each run used isolated settings,
audio synthesis enabled at zero volume, and the same Botanic Base gameplay
script. Statistics cover seconds 18-35 of each 35-second run. Baseline: v0.3.2
(`3989d53`); fixed version: the changes included in v0.3.3.

| Measurement | v0.3.2 | Fixed |
| --- | --- | --- |
| VSync: presentations without a game update | 1.97-4.22% | 0% in both runs |
| VSync: new-frame presentation interval, P99 | 33.38-33.74 ms | 19.61-20.54 ms |
| VSync off: presentation calls per second | 592.95 | 59.92 |
| VSync off: game frames per second | 59.88 | 59.92 |

These are CPU-side `SDL_RenderPresent` return timings, not physical display
scanout or input-latency measurements. The window was not minimized but lacked
keyboard focus at startup. Results do not establish performance on every
display or a comparison against PicoDrive, which was not benchmarked.

## Validation

- 85 unit tests passed, including five new deterministic timing tests.
- All 13 regular CTest entries passed; the three long attract-mode tests were
  not rerun for this frontend-only change.
- The Windows executable navigated the scripted menu into gameplay and
  completed 1,800 autotest frames in 5.07 seconds.
- A diagnostic run forced fast-forward for two seconds, then injected a
  two-second stall. Fast-forward ran eight updates per pass; recovery kept the
  three-update catch-up limit and returned to approximately 59.98 FPS.
  This tests the fast-forward state transition, not physical Tab-key input.
- Android arm64-v8a and x86_64 builds passed. The existing unused `kNotATap`
  warning remains. This timing change has not been retested on Android hardware.
- Independent code review found no actionable issues.

Local raw measurements and validation scripts are in `build/perf/`.
