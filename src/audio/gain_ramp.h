// Host volume changes eased in over about 40 ms.
//
// A level that jumps cuts whatever the waveform was doing at that sample: a
// click (muting the music jumped 3353 between two samples; the music's own
// biggest jump in the second before was 2294). It also takes away at once
// whatever DC the music carries, which the output's DC blocker turns into a
// thump. So the level actually applied walks to the one asked for by a fixed
// step each output sample: the whole way, 0 to 1, takes 2048 samples, 38 ms
// at 53 kHz, long against the DC blocker's 9 ms so that what is left of a DC
// step is below hearing.
// While nothing changes, the applied level is the asked-for one exactly, so
// at 100% the output is bit-identical to having no volume control at all.
#pragma once

namespace chaotix::audio {

inline constexpr float kGainStep = 1.0f / 2048.0f;

// One output sample's step of `applied` towards `target`; returns the level
// to use for this sample.
inline float ease_gain(float& applied, float target) {
    if (applied < target) applied = applied + kGainStep < target ? applied + kGainStep : target;
    else if (applied > target) applied = applied - kGainStep > target ? applied - kGainStep : target;
    return applied;
}

} // namespace chaotix::audio
