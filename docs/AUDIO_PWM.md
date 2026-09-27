# PWM stereo routing fix

The 32X PWM mixer interpreted LMD and RMD as identical source selectors.
In normal stereo mode (`0x05`), both outputs therefore used the left FIFO's
sample and the right FIFO's sample did not reach the mix.

The mixer now keeps both sources independent in normal stereo (`0x05`) and
exchanges them in swapped stereo (`0x0a`). Other routing modes retain their
previous behavior pending hardware verification.

The change preserves sample generation, PWM gain and centering, filtering,
FIFO handling, and interrupt timing. It does not address other FM synthesis
or audio accuracy differences.

## Evidence

In a local 3,600-frame run of the verified ROM on v0.3.3, 3,598 end-of-frame
snapshots used PWM control `0x105` (normal stereo). In 1,975 snapshots, the
left and right PWM source values differed. The routing error therefore
affects the actual game, not only synthetic input.

An isolated routing experiment preserved every left output sample and
changed 2,382,158 of 3,200,142 right output samples. The recorded end-of-frame
game mode and audio-register snapshots remained identical. Those snapshots
are diagnostic evidence, not a complete CPU-state equivalence check.

## Regression coverage

`tests/test_pwm_audio.cpp` runs the real frame scheduler and mixer with a
synthetic address space, halted CPUs, muted FM/PSG, and distinct held PWM
levels. It checks normal and exchanged stereo, disabled outputs, and inactive
cycle values. Expected initial samples also
check that the change preserves gain and centering. No commercial ROM is
required for these tests.

Validation of the final fix:

- All 88 unit tests and all 13 non-long CTest cases passed.
- Windows executables and the Android debug APK (arm64-v8a and x86_64) built
  successfully. The Android build has not yet been tested on a device.
- A 3,600-frame gameplay run with audio enabled passed the existing
  interpreter/recompiled lockstep checks. Those checks compare execution
  state, memory, and video; they do not establish audio accuracy.
- A separate PCM comparison matched the stereo experiment exactly across
  3,200,142 samples per channel. Every left sample matched the pre-fix
  baseline; 2,382,158 right samples changed as expected.

The two stereo modes agree with the PWM control table on page 24 of the
[SEGA 32X Hardware Manual](https://consolemods.org/wiki/images/e/e9/32X_Hardware_Manual_1994_Sega_text.pdf)
and were cross-checked against
[PicoDrive's PWM implementation](https://github.com/notaz/picodrive/blob/26ecb2b6358fefba24e3d68b9eb2efba7f10d5ee/pico/32x/pwm.c).
No PicoDrive implementation code was copied into this project.

## Deferred single-source modes

Review exposed a disagreement between emulator references. For `0x02`,
PicoDrive and [ares](https://github.com/ares-emulator/ares/blob/4cb8d92b441557cb6bcaf133c4cbc7f6819b1122/ares/md/m32x/pwm.cpp)
route the right source to the left output;
[MAME's `handle_pwm_callback`](https://github.com/mamedev/mame/blob/1cfb79ea82d0e0173bc9bccd86eaffaf77c7572c/src/mame/shared/mega32x.cpp#L794)
routes the left source to the right output.
Their interpretation of `0x08` also differs. The manual's table alone did not
resolve this ambiguity during review, and no hardware test was available.

The initial diagnostic experiment used the MAME-style interpretation for
these modes. That assumption is excluded from the final fix and regression
expectations. This change therefore fixes the confirmed stereo defect, not
every possible PWM routing issue.
