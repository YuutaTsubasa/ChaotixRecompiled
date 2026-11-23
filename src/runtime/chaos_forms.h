// Chaos Forms: the classic "super form", driven by the game's Chaos Rings.
//
// With all six Chaos Rings (the word at $FFE01A is 6 or more) and 50 rings or
// more in a level, a jump press while in the air (the second press of a
// double jump) transforms BOTH characters at once. Not before the level has
// been entered: the level clock ($FFE010, 0 until the player has control)
// must be running, so not during the fall and spin that starts a level.
//
// Where: the attractions' levels (zones 0-4) and the final battle (WORLD
// ENTRANCE level 2), not the lobby, the bonus stage or the ending's scenes.
// When the level is over (the goal sign touched, FFFDC5 = FF, or a boss beaten,
// FFFDE8) a form ends at once and no new one starts. The press is taken by the
// transformation: the character's own air move does not start on that frame.
//
// A form is the game's own two power-ups at the same time, with no music:
// every character has a status object (the word at +$3A of its object, whose
// address is at $FFE034 for the player and $FFE036 for the partner) holding
// timers that the game counts down once a frame (68K $88E85A):
//   +$32 invincibility (the game sets bit 4 of the object's +$25 while it runs)
//   +$34 speed shoes   (doubles ground acceleration)
// A form starts both at a value far above anything a monitor gives (kHold)
// and keeps them there, which is also how a form is recognised in RAM: no host
// state says that one is on. The game's own monitors and the end of a timer ask for music
// ($88E962); a form never starts any, and ends by clearing the timers
// directly, so the music stays as it was.
//
// Every 60 frames the game runs ($FFE006 advances, and not while paused) costs
// one ring (the word at $FFE008). At 0 or fewer the form ends, as the monitors'
// effects do when they wear off. A new level builds new status objects, so a
// form never carries across levels.
//
// The look is output only, like character mods: the palette shown has the
// character's body colours replaced (the table in chaos_forms.cpp), pulsing
// once a second between a brighter and a mid tone. Espio's body (pixels 2-6)
// shows the rainbow colour sets the game itself cycles it through
// ($88E70A, ROM $0E7BA). CRAM and both CPUs never see it.
//
// Off unless the front end turns Machine::chaos_forms on (OPTIONS > EXTRAS >
// CHAOS FORMS); it is off for TIME ATTACK runs, so their times compare.
#pragma once
#include <cstdint>

namespace chaotix {

class Machine;

namespace chaos_forms {

// Word in 68K work RAM: Chaos Rings held (special stages won).
inline constexpr uint32_t kChaosRings = 0xE01A;
inline constexpr int kAllRings = 6;
inline constexpr int kRingsNeeded = 50;
inline constexpr int kFramesPerRing = 60;
// A timer at or above this is a form's (a monitor gives far less). A form
// starts them at kHold and lets the game count them down, so the invincibility
// stars, whose circle the game takes from that timer (x4, $88E9A4), keep
// going round; when one falls under kRefill a whole turn of them (0x4000 x 4 =
// 0x10000) is added, so the stars do not jump.
inline constexpr uint16_t kMark = 0x4000;
inline constexpr uint16_t kHold = 0x7FFF;
inline constexpr uint16_t kRefill = 0x6000;
inline constexpr uint16_t kTurn = 0x4000;

// Once before each frame runs, from Machine::run_frame.
void begin_frame(Machine& m);

// Whether a form is on for the player (0) or the partner (1).
bool active(const Machine& m, int who);

// The palette to show: `pal` itself, or m.chaos_pal with the forms' colours.
const uint16_t* display(Machine& m, const uint16_t* pal);

} // namespace chaos_forms
} // namespace chaotix
