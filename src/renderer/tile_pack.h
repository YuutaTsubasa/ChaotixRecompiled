// Mega Drive tile replacements as files (issue #35): Mods/Textures/md_<hash>.png,
// and the dumps the game writes for modders to start from.
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace chaotix {

class TileReplacer;

// The file name of a tile: md_ and its hash in 16 hex digits.
std::string tile_file_name(uint64_t hash);

// Loads every md_<hash>.png in `dir` into `tiles`. A replacement must be an
// 8x8 palette-indexed PNG (as the dumps are); its indices 0-15 are drawn with
// the tile's palette in the game, 0 being transparent. Returns how many were
// loaded; `messages` says why any file was skipped.
int load_tile_pack(TileReplacer& tiles, const std::string& dir, std::vector<std::string>* messages);

// Writes the tiles the replacer has collected for dumping to `dir` (created
// if needed) and clears them. Returns how many files were written.
int write_tile_dumps(TileReplacer& tiles, const std::string& dir);

// Text replaced by tile sequence (runtime/text_replace.h): every pair of
// mdtext_<name>.txt (the pattern) and mdtext_<name>.png (the image, palette-
// indexed, 8 pixels high per row of the pattern) in `dir`; and text replaced
// by sprite (load_sprite_text). Returns how many were loaded; `messages`
// says why any was skipped.
// With `taken` (mod packs, issue #42), a file whose name (case ignored) is
// already in it is skipped, as one a pack above has given; the others are
// added to it.
int load_text_pack(TileReplacer& tiles, const std::string& dir, std::vector<std::string>* messages,
                   std::set<std::string>* taken = nullptr);

// Text replaced by sprite (runtime/sprite_text.h): every pair of
// mdsprite_<name>.txt (the sprites) and mdsprite_<name>.png (the image,
// palette-indexed, any size) in `dir`. Returns how many were loaded.
int load_sprite_text(TileReplacer& tiles, const std::string& dir, std::vector<std::string>* messages,
                     std::set<std::string>* taken = nullptr);
// Mod packs (issue #42): several Textures folders as one, given lowest
// priority first. A tile from a later folder replaces an earlier one's; a
// text pattern (mdtext_ or mdsprite_) comes from the last folder with a file
// of its name, and the later folders' patterns are loaded first, so they
// also win where two different patterns match equally. Each returns how
// many files were loaded.
int load_tile_packs(TileReplacer& tiles, const std::vector<std::string>& dirs, std::vector<std::string>* messages);
int load_text_packs(TileReplacer& tiles, const std::vector<std::string>& dirs, std::vector<std::string>* messages);

// Writes the lists of on-screen tile runs and sprite groups the replacers
// have collected for dumping to `dir` (created if needed) as text_<n>.txt
// and sprites_<n>.txt, each with a .png of the same name: the picture
// `pixels` (XRGB8888, `width` x `height`, the native picture starting
// `extra` columns in) at twice the size, every run or group boxed and
// numbered as in the list. Returns how many lists were written.
int write_text_dumps(TileReplacer& tiles, const std::string& dir, const uint32_t* pixels, int width, int height,
                     int stride_pixels, int extra);

} // namespace chaotix
