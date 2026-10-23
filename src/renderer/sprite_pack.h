// 32X sprite replacements as files (issue #35): Mods/Textures/32x_<BB>_<FF>.png
// for graphics in the game's bank table (bank and frame, two hex digits each)
// and 32x_<hash>.png for the others, next to the Mega Drive tiles' md_<hash>.png
// (tile_pack.h); and the dumps the game writes for modders to start from.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace chaotix {

class SpritePack;
class SpriteRecorder;

// The file name of a graphic: 32x_02_51.png for bank 2, frame 0x51; for a
// graphic not in the bank table (bank < 0), 32x_ and its hash in 16 hex
// digits.
std::string sprite_file_name(int bank, int frame, uint64_t hash);

// Loads every 32x_*.png in `dir` into `pack`. A replacement must be a
// palette-indexed PNG (as the dumps are) whose indices are the graphic's own
// pixel values before the colour offset; 0 is transparent. Whole-bank sheets
// (32x_<BB>.png with 32x_<BB>.txt, sprite_sheet.h) are loaded too, each cell
// as its frame's replacement, and a frame's own file overrides its cell.
// Returns how many graphics have a replacement; `messages` says why any file
// was skipped.
int load_sprite_pack(SpritePack& pack, const std::string& dir, std::vector<std::string>* messages);
// Mod packs (issue #42): several Textures folders, lowest priority first,
// each loaded as above over the ones before it, so a graphic from a later
// folder (its own file or a sheet's cell) replaces an earlier folder's.
// Returns how many graphics have a replacement in all.
int load_sprite_packs(SpritePack& pack, const std::vector<std::string>& dirs, std::vector<std::string>* messages);

// Writes the graphics the recorder has collected for dumping to `dir`
// (created if needed) and clears them. Returns how many files were written.
int write_sprite_dumps(SpriteRecorder& sprites, const std::string& dir);

} // namespace chaotix
