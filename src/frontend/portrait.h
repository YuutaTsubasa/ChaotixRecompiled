// Character portraits for the TIME ATTACK and LEVEL SELECT pages (issue #53):
// a character standing, as the game draws it, for the player and partner
// cards.
//
// Nothing is shipped for this either. In the game a character is drawn in
// pieces: its body from its own bank in the ROM's 32X bank table (banks 0-7,
// docs/CHARACTERS.md) and, for most, the arm holding the ring from bank +8.
// The body's stand alone (frame 0) has no arms (issue #96), so the picture is
// the pieces the game draws for the character standing at the start of a
// level, at the places it draws them (kPortraitPieces, measured with
// --sprites-log), read with the same code EXPORT SPRITES uses
// (renderer/sprite_sheet.h), in the colours the game shows the player in. Mods show: a sprite replacement for that bank and
// frame (Mods/Textures, 32x_<BB>_00.png or a sheet's cell, sprite_pack.h) is
// drawn instead of the game's graphic, and a character mod's [Palette]
// (runtime/character_mods.h) recolours it, as both do in play.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace chaotix {

struct Rom;
struct CharacterMod;
class SpritePack;

struct Portrait {
    int width = 0, height = 0;
    std::vector<uint32_t> argb;  // row-major; transparent pixels are 0
    int bank = -1, frame = -1;   // where its body came from
    bool replaced = false;       // a sprite mod's image, not the game's
};

// A piece of a portrait: a frame of the character's body bank, or of its
// second bank (+8), with its hotspot that far from the body's.
struct PortraitPiece {
    bool second = false;
    int frame = 0;
    int dx = 0, dy = 0;
};
// The pieces, back to front (the order the game draws them), by character id:
// what the game draws for the character standing still at the start of
// BOTANIC BASE 1, before the partner comes up (frame 1180).
std::vector<PortraitPiece> portrait_pieces(int id);

// Character `id` (0-7, FFE038 order). `pack` (sprite replacements) and `mod`
// (that character's mod, whose palette is used where it sets one) may be
// null. False (and `why`) for a character without graphics -- slot 1, or a
// ROM whose bank does not read as graphics.
bool character_portrait(const Rom& rom, int id, const SpritePack* pack, const CharacterMod* mod, Portrait& out,
                        std::string* why);

} // namespace chaotix
