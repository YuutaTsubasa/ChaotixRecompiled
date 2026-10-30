// Opposite directions held together (input/input.h, issue #93): the game must
// only ever see what a Mega Drive pad can send. No window needed.
#include "input/input.h"

#include "test_framework.h"

using namespace chaotix;

TEST(opposite_directions, one_direction_passes_through) {
    OppositeDirections o;
    CHECK_EQ(o.clean(PAD_RIGHT | PAD_C), uint16_t(PAD_RIGHT | PAD_C));
    CHECK_EQ(o.clean(PAD_UP | PAD_LEFT), uint16_t(PAD_UP | PAD_LEFT));
    CHECK_EQ(o.clean(0), uint16_t(0));
}

TEST(opposite_directions, the_newer_direction_wins) {
    // Holding right, then pressing left without letting go: a turn.
    OppositeDirections o;
    CHECK_EQ(o.clean(PAD_RIGHT | PAD_C), uint16_t(PAD_RIGHT | PAD_C));
    CHECK_EQ(o.clean(PAD_RIGHT | PAD_LEFT | PAD_C), uint16_t(PAD_LEFT | PAD_C));
    CHECK_EQ(o.clean(PAD_RIGHT | PAD_LEFT | PAD_C), uint16_t(PAD_LEFT | PAD_C));
    // Letting left go hands the axis back to right, still held.
    CHECK_EQ(o.clean(PAD_RIGHT | PAD_C), uint16_t(PAD_RIGHT | PAD_C));
    // And the other way round.
    CHECK_EQ(o.clean(PAD_RIGHT | PAD_LEFT), uint16_t(PAD_LEFT));
    CHECK_EQ(o.clean(PAD_LEFT), uint16_t(PAD_LEFT));
    CHECK_EQ(o.clean(PAD_LEFT | PAD_RIGHT), uint16_t(PAD_RIGHT));
}

TEST(opposite_directions, both_on_the_same_frame_cancel) {
    OppositeDirections o;
    CHECK_EQ(o.clean(PAD_LEFT | PAD_RIGHT | PAD_A), uint16_t(PAD_A));
    CHECK_EQ(o.clean(PAD_LEFT | PAD_RIGHT | PAD_A), uint16_t(PAD_A));
    CHECK_EQ(o.clean(PAD_LEFT | PAD_A), uint16_t(PAD_LEFT | PAD_A));
}

TEST(opposite_directions, the_axes_are_separate) {
    OppositeDirections o;
    CHECK_EQ(o.clean(PAD_UP | PAD_RIGHT), uint16_t(PAD_UP | PAD_RIGHT));
    CHECK_EQ(o.clean(PAD_UP | PAD_DOWN | PAD_RIGHT | PAD_LEFT), uint16_t(PAD_DOWN | PAD_LEFT));
    CHECK_EQ(o.clean(PAD_UP | PAD_RIGHT | PAD_LEFT), uint16_t(PAD_UP | PAD_LEFT));
}
