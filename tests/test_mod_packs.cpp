// Mod packs (issue #42): mod.ini, finding the packs in a Mods folder, the
// player's order and which pack wins for every kind of mod.
#include "runtime/mod_packs.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "renderer/image_io.h"
#include "renderer/sprite_pack.h"
#include "renderer/tile_pack.h"
#include "runtime/character_mods.h"
#include "runtime/sprite_record.h"
#include "runtime/sprite_replace.h"
#include "runtime/tile_replace.h"
#include "test_framework.h"

using namespace chaotix;
namespace fs = std::filesystem;

namespace {

void write_text(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

// A fresh, empty folder under the temp directory.
fs::path fresh(const std::string& name) {
    const fs::path d = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

// A solid palette-indexed image.
void write_solid(const fs::path& p, int w, int h, uint8_t index) {
    fs::create_directories(p.parent_path());
    std::vector<uint8_t> px(size_t(w * h), index);
    std::array<uint32_t, 16> pal{};
    for (size_t i = 0; i < pal.size(); ++i) pal[i] = 0xFF000000u | uint32_t(i * 0x111111u);
    CHECK(write_png_indexed(p.u8string(), px.data(), w, h, pal.data(), 16));
}

std::vector<std::string> ids(const std::vector<mod_packs::Pack>& packs) {
    std::vector<std::string> out;
    for (const auto& p : packs) out.push_back(p.loose ? "*" : p.id);
    return out;
}

} // namespace

TEST(mod_packs, mod_ini_is_read_with_its_keys_and_comments) {
    mod_packs::Info info;
    std::vector<std::string> messages;
    mod_packs::parse_info("\xEF\xBB\xBF; a comment\n[Mod]\nname = Chinese Text\nAuthor = Yuuta\nversion = \"1.2\"\n"
                          "description = First line\\nSecond line\ndescription = Third line\n"
                          "url = https://gamebanana.com/mods/1\npreview = preview.png\nrequires = 0.8.0\n"
                          "colour = red\nno equals sign\n",
                          info, "mod.ini", &messages);
    CHECK_STR(info.name, "Chinese Text");
    CHECK_STR(info.author, "Yuuta");
    CHECK_STR(info.version, "1.2");
    CHECK_STR(info.description, "First line\nSecond line\nThird line");
    CHECK_STR(info.url, "https://gamebanana.com/mods/1");
    CHECK_STR(info.preview, "preview.png");
    CHECK_STR(info.min_version, "0.8.0");
    CHECK_EQ(messages.size(), size_t(2));  // the unknown key and the line without '='
}

TEST(mod_packs, versions_compare_number_by_number) {
    CHECK(mod_packs::compare_versions("0.8.0", "0.8") == 0);
    CHECK(mod_packs::compare_versions("0.10.0", "0.9.5") > 0);
    CHECK(mod_packs::compare_versions("0.7.1", "0.8.0") < 0);
    CHECK(mod_packs::compare_versions("v1.2-beta", "1.2") == 0);
    CHECK(mod_packs::compare_versions("1", "1.0.1") < 0);
}

TEST(mod_packs, lists_split_and_join) {
    const auto l = mod_packs::split_list(" A , B B,,C ");
    CHECK_EQ(l.size(), size_t(3));
    CHECK_STR(l[1], "B B");
    CHECK_STR(mod_packs::join_list(l), "A, B B, C");
    CHECK(mod_packs::split_list("").empty());
}

TEST(mod_packs, packs_and_loose_files_are_found) {
    const fs::path root = fresh("chaotix_mod_packs_discover");
    write_text(root / "Textures" / "README.txt", "the app's README\n");  // no loose files yet
    write_text(root / "Music" / "README.txt", "x");
    write_text(root / "Zed" / "mod.ini", "name = Zed Pack\nversion = 2.0\nrequires = 99.0\n");
    write_text(root / "Zed" / "Textures" / "md_0000000000000001.png", "x");
    write_text(root / "Alpha" / "textures" / "32x_02_01.png", "x");  // no mod.ini, lower-case folder
    write_text(root / "Alpha" / "Patches" / "a.ips", "x");
    write_text(root / "Notes" / "readme.md", "not a mod");          // neither: skipped, said so
    std::vector<std::string> messages;
    auto packs = mod_packs::discover(root.u8string(), "0.8.0", &messages);
    CHECK_EQ(packs.size(), size_t(2));
    CHECK_STR(ids(packs)[0], "Alpha");
    CHECK_STR(ids(packs)[1], "Zed");
    CHECK(!packs[0].has_ini);
    CHECK_STR(packs[0].title(), "Alpha");
    CHECK(packs[0].textures && packs[0].patches && !packs[0].music);
    CHECK(packs[0].changes_game());
    CHECK_STR(packs[1].title(), "Zed Pack");
    CHECK(packs[1].too_new);
    CHECK_EQ(messages.size(), size_t(2));  // Notes, and Zed's version

    // A file of the user's own in Mods/Textures: the loose files come first.
    write_text(root / "Textures" / "md_0000000000000002.png", "x");
    packs = mod_packs::discover(root.u8string(), "0.8.0", nullptr);
    CHECK_EQ(packs.size(), size_t(3));
    CHECK(packs[0].loose && packs[0].textures && !packs[0].music);
    CHECK_STR(packs[0].title(), "Loose files");
}

TEST(mod_packs, the_order_puts_packs_in_place_and_new_ones_at_the_end) {
    std::vector<mod_packs::Pack> found(4);
    found[0].loose = true;
    found[1].id = "A";
    found[2].id = "B";
    found[3].id = "C";
    mod_packs::Order order;
    order.order = {"C", "Gone", "A"};
    order.disabled = {"A"};
    auto packs = mod_packs::arrange(found, order);
    CHECK_STR(mod_packs::join_list(ids(packs)), "*, C, A, B");
    CHECK(packs[0].enabled && packs[1].enabled && !packs[2].enabled && packs[3].enabled);

    // Written back: as listed, then the missing one kept for when it returns.
    mod_packs::remember(order, packs);
    CHECK_STR(mod_packs::join_list(order.order), "C, A, B, Gone");

    // B up over A; the loose files never move, nor does anything above them.
    CHECK(mod_packs::move(order, packs, 3, -1));
    CHECK_STR(mod_packs::join_list(ids(packs)), "*, C, B, A");
    CHECK(!mod_packs::move(order, packs, 1, -1));
    CHECK(!mod_packs::move(order, packs, 0, +1));
    CHECK(!mod_packs::move(order, packs, 3, +1));
    CHECK_STR(mod_packs::join_list(order.order), "C, B, A, Gone");

    mod_packs::set_enabled(order, packs, 3, true);
    mod_packs::set_enabled(order, packs, 1, false);
    mod_packs::set_enabled(order, packs, 0, false);
    CHECK(order.disabled.count("C") && !order.disabled.count("A") && !order.loose);
    CHECK(!mod_packs::arrange(found, order)[0].enabled);
}

TEST(mod_packs, folders_and_patches_come_lowest_first) {
    const fs::path root = fresh("chaotix_mod_packs_folders");
    write_text(root / "Patches" / "z.ips", "x");
    write_text(root / "High" / "mod.ini", "name = High\n");
    write_text(root / "High" / "Patches" / "b.bps", "x");
    write_text(root / "High" / "Patches" / "a.ips", "x");
    write_text(root / "High" / "Patches" / "notes.txt", "x");
    write_text(root / "Low" / "mod.ini", "name = Low\n");
    write_text(root / "Low" / "Patches" / "c.ips", "x");
    write_text(root / "Low" / "Textures" / "md_0000000000000001.png", "x");
    write_text(root / "Off" / "Patches" / "d.ips", "x");
    mod_packs::Order order;
    order.order = {"High", "Low", "Off"};
    order.disabled = {"Off"};
    const auto packs = mod_packs::arrange(mod_packs::discover(root.u8string(), "0.8.0", nullptr), order);
    std::vector<std::string> names;
    for (const auto& f : mod_packs::patch_files(packs)) names.push_back(fs::u8path(f).filename().u8string());
    CHECK_STR(mod_packs::join_list(names), "c.ips, a.ips, b.bps, z.ips");
    const auto tex = mod_packs::folders(packs, mod_packs::kTextures);
    CHECK_EQ(tex.size(), size_t(1));
    CHECK(tex[0].find("Low") != std::string::npos);
}

TEST(mod_packs, the_higher_pack_wins_a_tile) {
    const fs::path root = fresh("chaotix_mod_packs_tiles");
    std::array<uint8_t, 32> pattern{};
    for (size_t i = 0; i < pattern.size(); ++i) pattern[i] = uint8_t(i * 7 + 1);
    const std::string name = tile_file_name(TileReplacer::hash(pattern.data()));
    write_solid(root / "Low" / name, 8, 8, 3);
    write_solid(root / "High" / name, 8, 8, 9);
    std::vector<uint8_t> vram(0x10000, 0);
    std::copy(pattern.begin(), pattern.end(), vram.begin());
    const std::array<uint32_t, 64> cram{};
    for (int order = 0; order < 2; ++order) {
        std::vector<std::string> dirs = {(root / "Low").u8string(), (root / "High").u8string()};
        if (order) std::swap(dirs[0], dirs[1]);
        TileReplacer tiles;
        CHECK_EQ(load_tile_packs(tiles, dirs, nullptr), 2);
        tiles.refresh(vram.data(), cram.data());
        CHECK(tiles.slot(0) != nullptr);
        if (tiles.slot(0)) CHECK_EQ(int(tiles.slot(0)[0]), order ? 3 : 9);
    }
}

TEST(mod_packs, the_higher_pack_wins_a_32x_frame) {
    const fs::path root = fresh("chaotix_mod_packs_sprites");
    write_solid(root / "Low" / "32x_02_01.png", 4, 4, 5);
    write_solid(root / "High" / "32x_02_01.png", 6, 6, 7);
    SpriteDraw d;
    d.bank = 2;
    d.frame = 1;
    for (int order = 0; order < 2; ++order) {
        std::vector<std::string> dirs = {(root / "Low").u8string(), (root / "High").u8string()};
        if (order) std::swap(dirs[0], dirs[1]);
        SpritePack pack;
        CHECK_EQ(load_sprite_packs(pack, dirs, nullptr), 1);
        const SpriteImage* img = pack.find(d);
        CHECK(img != nullptr);
        if (img) CHECK_EQ(img->width, order ? 4 : 6);
    }
}

TEST(mod_packs, a_text_pattern_comes_from_the_highest_pack_with_its_name) {
    const fs::path root = fresh("chaotix_mod_packs_text");
    // The lower pack's file of the same name is broken: were it read, it
    // would be said so.
    write_text(root / "Low" / "mdtext_title.txt", "plane A\nrow nonsense\n");
    write_text(root / "High" / "mdtext_title.txt", "plane A\nrow 0123456789abcdef 0123456789abcdef\n");
    write_solid(root / "High" / "mdtext_title.png", 16, 8, 1);
    // A name only the lower pack has is still read.
    write_text(root / "Low" / "mdtext_other.txt", "plane A\nrow 1111111111111111\n");
    write_solid(root / "Low" / "mdtext_other.png", 8, 8, 1);
    TileReplacer tiles;
    std::vector<std::string> messages;
    CHECK_EQ(load_text_packs(tiles, {(root / "Low").u8string(), (root / "High").u8string()}, &messages), 2);
    CHECK(messages.empty());
    // The other way round, the broken one is the one read.
    TileReplacer other;
    messages.clear();
    CHECK_EQ(load_text_packs(other, {(root / "High").u8string(), (root / "Low").u8string()}, &messages), 1);
    CHECK_EQ(messages.size(), size_t(1));
}

TEST(mod_packs, a_character_file_comes_whole_from_the_highest_pack) {
    const fs::path root = fresh("chaotix_mod_packs_characters");
    write_text(root / "Low" / "knuckles.ini", "[Palette]\n1 = #FF0000\n[Stats]\nacceleration = 0x20\n");
    write_text(root / "Low" / "espio.ini", "[Palette]\n2 = #00FF00\n");
    write_text(root / "High" / "knuckles.ini", "[Palette]\n1 = #0000FF\n");
    const auto mods = load_character_mods(std::vector<std::string>{(root / "Low").u8string(), (root / "High").u8string()},
                                          nullptr);
    CHECK_EQ(mods.size(), size_t(2));
    const int knuckles = characters::id_for_name("knuckles");
    for (const auto& m : mods) {
        if (m.id != knuckles) continue;
        CHECK_EQ(m.palette[0], characters::colour_from_rgb(0x0000FF));
        CHECK(m.stats.empty());  // the lower file's stats went with it
    }
}
