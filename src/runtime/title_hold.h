// The title screen held while the front end is on it (issue #3).
//
// The front end is drawn on the game's own title screen. Left alone, the
// title gives way to an attract demo after about eight and a half seconds of
// PUSH START, and the front end's plaque and entries then floated over a
// level playing itself. While the front end is open the title is kept from
// timing out, the way it would wait if a player were about to press Start;
// everything else on it runs as it always does, music included.
//
// Found in the 68K code (frames counted from power-on, no input; see
// docs/MENU_ART.md for the trace):
//
//  - The title is game mode $08 at $FFDFDE (patches::kGameMode), frames 379
//    to 2023, intro (the city and the road) included; before it are the boot
//    screens and the SEGA logo (modes $00, $04, $0C, $10, $14).
//  - Its idle counter is the word $FFE200. The title's two logo objects
//    ($FFB008 and $FFB048, routine $884482) each add 1 to it every frame once
//    their zoom ($3C(a6)) has reached $100 -- the logo has settled, from about
//    frame 1511 -- so it counts two a frame from 0, and at $8844EE
//    (`cmpi.b #4,$FFE200`) the first to see it reach $400 writes mode $38,
//    the attract demo, and jumps to the mode dispatcher: frame 2024.
//  - The same counter gates Start: at $883BCE / $883BF8 Start on the settled
//    title leaves for the game's own menu only from $40 on (before that it
//    just finishes the logo's zoom), and $883EA2 runs the title's palette
//    cycle only while it is not 0.
//
// The hold keeps the counter at $40 from the frame it gets there: Start
// behaves as on the settled title, the palette cycle runs, and the demo is
// 480 frames away whenever the hold ends (the full wait less the 32 frames
// already counted). It is a write between two frames, made the same way on
// every machine that runs the frame, so lockstep compares like with like.
#pragma once
#include <cstdint>

namespace chaotix {

class Machine;

namespace title_hold {

constexpr uint32_t kIdleCounter = 0xE200;  // word, counts two a frame on the settled title
constexpr uint16_t kModeTitle = 0x08;
constexpr uint16_t kModeDemo = 0x38;
constexpr uint16_t kDemoAt = 0x400;        // `cmpi.b #4` on the counter's high byte
constexpr uint16_t kStartFrom = 0x40;      // Start leaves the title from here on
constexpr uint16_t kHeld = kStartFrom;

// The scenes before PUSH START: the boot screens, the SEGA logo, the intro
// and the logo's zoom (the title's mode with its counter still at 0).
bool before_title(const Machine& m);

// Whether PUSH START is on the screen: the title's mode, its counter going.
bool on_title(const Machine& m);

// Keeps the title's counter at kHeld once it has passed it, so the attract
// demo never starts. Only on the title (any other scene is left alone).
// Returns whether it wrote anything.
bool apply(Machine& m);

} // namespace title_hold
} // namespace chaotix
