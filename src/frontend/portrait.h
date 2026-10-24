// Character portraits for the TIME ATTACK and LEVEL SELECT pages (issue #53):
// a character standing, as the game draws it, for the player and partner
// cards.
//
// Nothing is shipped for this either. The picture is the first standing
// frame of the character's body bank in the ROM's 32X bank table (banks 0-7,
// docs/CHARACTERS.md: frames 00-03 are the stand), read with the same code
// EXPORT SPRITES uses (renderer/sprite_sheet.h), in the colours the game
// shows the player in. Mods show: a sprite replacement for that bank and
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
    int bank = -1, frame = -1;   // where it came from
    bool replaced = false;       // a sprite mod's image, not the game's
};

// The frame a portrait is taken from: the character's first standing frame.
inline constexpr int kPortraitFrame = 0;

// Character `id` (0-7, FFE038 order). `pack` (sprite replacements) and `mod`
// (that character's mod, whose palette is used where it sets one) may be
// null. False (and `why`) for a character without graphics -- slot 1, or a
// ROM whose bank does not read as graphics.
bool character_portrait(const Rom& rom, int id, const SpritePack* pack, const CharacterMod* mod, Portrait& out,
                        std::string* why);

} // namespace chaotix
