#pragma once

// How awards (achievements) look: the notification when one is earned and the
// AWARDS page (issue #3). Drawn from shapes only, in the manner of the game's
// own screens -- deep purple, magenta, ring gold, everything edged in black --
// with no picture taken from the game, so nothing of the ROM is needed and
// nothing of it is in the program.

#include "platform/ui.h"

#include <string>

namespace chaotix::award_art {

namespace color {
constexpr ui::Color ink{10, 4, 22};            // every edge
constexpr ui::Color night{30, 12, 58};         // a card
constexpr ui::Color purple{88, 36, 150};       // a banner
constexpr ui::Color magenta{232, 52, 146};     // stripes, the selected card
constexpr ui::Color gold{255, 210, 40};        // rings, earned emblems
constexpr ui::Color gold_dark{190, 104, 12};   // the shaded side of gold
constexpr ui::Color cream{255, 246, 214};      // highlights
constexpr ui::Color locked{70, 64, 92};        // what is not earned yet
constexpr ui::Color locked_dark{40, 36, 58};
constexpr ui::Color text{246, 240, 255};
constexpr ui::Color text_dim{176, 160, 206};
} // namespace color

// A parallelogram leaning right by `lean` pixels (its top edge `lean` to the
// right of its bottom edge), edged in `edge` `edge_w` thick.
void banner(ui::Ui& g, const SDL_FRect& r, float lean, ui::Color fill, ui::Color edge, float edge_w);

// An award's emblem: a medallion of radius r at (cx, cy) with the picture
// `icon` names (achievements.h: ring, chaos, goal, clock, place, team, boss,
// anything else a star). Not earned, it is grey. `turn` (0-1) turns a ring
// about its upright axis, the way rings spin in the game; 0 faces it front on.
// `seed` picks the colour of a Chaos Ring.
void emblem(ui::Ui& g, float cx, float cy, float r, const std::string& icon, bool earned, float turn = 0,
            unsigned seed = 0);

// A gold ring on its own, as the emblems draw it: radius r, turned by `turn`
// (0-1) about its upright axis. The front end's cursor is a pair of them, the
// two rings of the game's combi.
void ring(ui::Ui& g, float cx, float cy, float r, float turn);

// A page's title, as AWARDS has it: the words on a slanted purple banner
// with a magenta stripe under it, its top left at (x, y), the words in font
// `f`, `max_w` wide at most (the words are shortened past that). Returns the
// banner's rectangle.
SDL_FRect heading(ui::Ui& g, float x, float y, ui::Font f, const std::string& text, float max_w);

// An arrowhead pointing up or down, edged in black: s is half its width.
void arrowhead(ui::Ui& g, float cx, float cy, float s, bool up, ui::Color fill, float edge);

} // namespace chaotix::award_art
