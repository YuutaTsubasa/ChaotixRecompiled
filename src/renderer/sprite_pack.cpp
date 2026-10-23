#include "sprite_pack.h"

#include "image_io.h"
#include "sprite_sheet.h"
#include "runtime/sprite_record.h"
#include "runtime/sprite_replace.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <utility>

namespace chaotix {

namespace {

// The `n` hex digits at `at` in `s`.
bool hex(const std::string& s, size_t at, size_t n, uint64_t* out) {
    if (at + n > s.size()) return false;
    uint64_t v = 0;
    for (size_t i = at; i < at + n; ++i) {
        const int c = std::tolower(static_cast<unsigned char>(s[i]));
        if (c >= '0' && c <= '9') v = v << 4 | unsigned(c - '0');
        else if (c >= 'a' && c <= 'f') v = v << 4 | unsigned(c - 'a' + 10);
        else return false;
    }
    *out = v;
    return true;
}

} // namespace

std::string sprite_file_name(int bank, int frame, uint64_t hash) {
    char name[40];
    if (bank >= 0) std::snprintf(name, sizeof name, "32x_%02x_%02x.png", unsigned(bank) & 0xFF, unsigned(frame) & 0xFF);
    else std::snprintf(name, sizeof name, "32x_%016llx.png", static_cast<unsigned long long>(hash));
    return name;
}

int load_sprite_pack(SpritePack& pack, const std::string& dir, std::vector<std::string>* messages) {
    auto say = [messages](std::string m) { if (messages) messages->push_back(std::move(m)); };
    namespace fs = std::filesystem;
    std::error_code ec;
    // Whole-bank sheets first (sprite_sheet.h), so that a graphic's own file
    // overrides its cell.
    const size_t before = pack.size();
    load_sprite_sheets(pack, dir, messages);
    for (const auto& e : fs::directory_iterator(fs::u8path(dir), ec)) {
        const std::string name = e.path().filename().u8string();
        if (!e.is_regular_file(ec) || name.size() < 8 || name.compare(0, 4, "32x_") != 0 ||
            name.compare(name.size() - 4, 4, ".png") != 0)
            continue;
        if (name.size() == 10) continue;  // 32x_<bank>.png, a sheet
        // 32x_BB_FF.png or 32x_<16 hex digits>.png.
        uint64_t bank = 0, frame = 0, hash = 0;
        const bool slot = name.size() == 13 && name[6] == '_';
        if (slot) {
            if (!hex(name, 4, 2, &bank) || !hex(name, 7, 2, &frame)) {
                say(name + ": not a graphic's name (32x_<bank>_<frame>.png, in hex)");
                continue;
            }
            if (bank > 0x7F) { say(name + ": the bank table has banks 00 to 7f"); continue; }
        } else if (name.size() != 24 || !hex(name, 4, 16, &hash)) {
            say(name + ": not a graphic's name (32x_<bank>_<frame>.png or 32x_<hash>.png, in hex)");
            continue;
        }
        PngImage img;
        std::string err;
        if (!read_png(e.path().u8string(), img, &err)) { say(name + ": " + err); continue; }
        if (!img.indexed) {
            // A full-colour image cannot follow the game's palette (the
            // colour offsets, fades), so the colours must be indices into it.
            say(name + ": must be a palette-indexed PNG, like the dumped graphic");
            continue;
        }
        if (img.width > 255 || img.height > 255) { say(name + ": too large (at most 255 pixels each way)"); continue; }
        SpriteImage s;
        s.width = img.width;
        s.height = img.height;
        s.px = img.indices;
        if (slot) pack.add(int(bank), int(frame), std::move(s));
        else pack.add(hash, std::move(s));
    }
    return int(pack.size() - before);
}

int load_sprite_packs(SpritePack& pack, const std::vector<std::string>& dirs, std::vector<std::string>* messages) {
    const size_t before = pack.size();
    for (const auto& dir : dirs) load_sprite_pack(pack, dir, messages);
    return int(pack.size() - before);
}

int write_sprite_dumps(SpriteRecorder& sprites, const std::string& dir) {
    if (sprites.pending.empty()) return 0;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
    int written = 0;
    for (const auto& d : sprites.pending) {
        const std::string path = dir + "/" + sprite_file_name(d.bank, d.frame, d.hash);
        if (write_png_indexed(path, d.image.px.data(), d.image.width, d.image.height, d.palette.data(), 256)) ++written;
    }
    sprites.pending.clear();
    return written;
}

} // namespace chaotix
