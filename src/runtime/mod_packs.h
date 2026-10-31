// Mod packs (issue #42, docs/MOD_PACKS.md): one folder per mod under Mods/,
// with a mod.ini saying what it is, and inside it the same folders the Mods
// folder itself has always had (Textures, Characters, Music, Patches,
// Touch). The player turns packs on and off and puts them in order; where
// two enabled packs replace the same thing, the one higher in the list wins.
//
// The Mods folder's own Textures, Characters, ... folders (the layout from
// before packs) still work, as one more entry, "loose files", which is
// always at the top of the list: files put there by hand override every
// pack, exactly as they were the only mods before.
//
// This file only finds packs and puts them in order; each kind of mod is
// still loaded by its own loader, given the folders of that kind lowest
// priority first (folders()).
#pragma once
#include <set>
#include <string>
#include <vector>

namespace chaotix::mod_packs {

// The folders a pack, or the Mods folder itself, may hold.
inline constexpr const char* kTextures = "Textures";
inline constexpr const char* kCharacters = "Characters";
inline constexpr const char* kMusic = "Music";
inline constexpr const char* kPatches = "Patches";
inline constexpr const char* kTouch = "Touch";
inline constexpr const char* kSamples = "Samples";
inline constexpr const char* kKinds[] = {kTextures, kCharacters, kMusic, kPatches, kTouch, kSamples};
inline constexpr const char* kIniName = "mod.ini";

// mod.ini: what the pack says about itself.
struct Info {
    std::string name;         // shown in the list; the folder's name without one
    std::string author;
    std::string version;
    std::string description;  // several description lines, or \n, make several lines
    std::string url;
    std::string preview;      // an image in the pack's folder, e.g. preview.png
    std::string min_version;  // `requires`: the oldest game version it works with, e.g. 0.8.0
};

// Parses mod.ini's text: "key = value" lines, an optional [Mod] heading,
// comments starting with ; or #. Lines it cannot use are said so in
// `messages`, prefixed with `where`.
void parse_info(const std::string& text, Info& out, const std::string& where, std::vector<std::string>* messages);

struct Pack {
    std::string id;     // its folder's name; empty for the loose files
    std::string dir;    // its folder, ending in '/'
    Info info;
    bool loose = false;
    bool has_ini = false;
    bool enabled = true;
    // Which kinds of mod it holds (a folder of that name with a file in it
    // other than a README).
    bool textures = false, characters = false, music = false, patches = false, touch = false, samples = false;
    // mod.ini's `requires` names a newer version than this one.
    bool too_new = false;

    bool has(const char* kind) const;
    // Patches and character stats change the game itself, so they are only
    // applied when the game starts (the rest apply at once).
    bool changes_game() const { return patches || characters; }
    std::string title() const;  // name, or "Loose files"
};

// The player's choices, kept in the settings ([Mods] in Config/chaotix.ini).
struct Order {
    std::vector<std::string> order;   // pack ids, highest priority first
    std::set<std::string> disabled;   // pack ids turned off
    bool loose = true;                // the loose files on
};

// The packs in the Mods folder `root` (ending in '/'), sorted by id, after
// the loose files when the Mods folder's own folders hold any. A pack is a
// folder with a mod.ini, or one holding any of kKinds (a mod unpacked without
// one); the kKinds folders themselves are the loose files. `game_version`
// is compared with each pack's `requires`.
std::vector<Pack> discover(const std::string& root, const std::string& game_version,
                           std::vector<std::string>* messages);

// The packs in the player's order, highest priority first: the loose files,
// then the packs `order` lists, then any new ones (by id), enabled unless
// `order` turns them off.
std::vector<Pack> arrange(std::vector<Pack> found, const Order& order);

// Writes the list's order back into `order`: the packs as listed, then any
// it named that are not there now (a pack taken out for a while keeps its
// choices for when it comes back).
void remember(Order& order, const std::vector<Pack>& arranged);
// Turns a pack on or off, in `order` and in `arranged`.
void set_enabled(Order& order, std::vector<Pack>& arranged, size_t index, bool on);
// Moves a pack up (delta -1, higher priority) or down (+1). The loose files
// stay at the top. Returns false when it cannot move that way.
bool move(Order& order, std::vector<Pack>& arranged, size_t index, int delta);

// The folders of one kind (kTextures, ...) of the enabled packs that have it,
// lowest priority first: the order the loaders read them in, a later folder
// replacing what an earlier one gave.
std::vector<std::string> folders(const std::vector<Pack>& arranged, const char* kind);
// The .ips and .bps files in a folder, in name order.
std::vector<std::string> patch_files(const std::string& dir);
// Every enabled pack's patches, applied in this order: the lowest pack's
// first, each pack's in name order, so a higher pack's bytes are the ones
// left where two patches change the same bytes.
std::vector<std::string> patch_files(const std::vector<Pack>& arranged);

// "1.2.3" against "1.10": < 0, 0 or > 0, number by number; missing numbers
// count as 0 and anything after a number (a "-beta") is ignored.
int compare_versions(const std::string& a, const std::string& b);
// "A, B , C" as {A, B, C}, and back.
std::vector<std::string> split_list(const std::string& text);
std::string join_list(const std::vector<std::string>& items);

} // namespace chaotix::mod_packs
