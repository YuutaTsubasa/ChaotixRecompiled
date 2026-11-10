// See portrait.h.
#include "frontend/portrait.h"

#include <algorithm>
#include <array>

#include "renderer/sprite_sheet.h"
#include "runtime/character_mods.h"
#include "runtime/rom.h"
#include "runtime/sprite_replace.h"

namespace chaotix {

namespace {

uint32_t argb_of(uint16_t c) {
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

} // namespace

std::vector<PortraitPiece> portrait_pieces(int id) {
    // The stand, then the arm (bank +8), from
    // `--stage 600:0:1:0:<id>:2:0 --sprites-log 1180`: standing still before
    // the partner comes up, the arm holds the ring down by the body.
    switch (id) {
    case 0: return {{false, 0, 0, 0}, {true, 5, 0, 0}};      // Mighty
    case 2: return {{false, 0, 0, 0}, {true, 20, -7, 1}};    // Knuckles
    case 3: return {{false, 1, 0, 0}, {true, 0, -4, 0}};     // Charmy
    case 4: return {{false, 0, 0, 0}, {true, 59, -8, -8}};   // Vector
    case 5: return {{false, 0, 0, 0}, {true, 16, -10, 0}};   // Bomb
    case 6: return {{false, 0, 0, 0}, {true, 8, 0, 0}};      // Heavy
    case 7: return {{false, 0, 0, 0}, {true, 28, 0, 0}};     // Espio
    default: return {};
    }
}

bool character_portrait(const Rom& rom, int id, const SpritePack* pack, const CharacterMod* mod, Portrait& out,
                        std::string* why) {
    auto fail = [why](const std::string& m) { if (why) *why = m; return false; };
    if (id < 0 || id >= characters::kCount) return fail("not a character");
    const std::vector<PortraitPiece> pieces = portrait_pieces(id);
    if (pieces.empty()) return fail("no graphics");
    SpriteBank banks[2];
    if (!read_rom_bank(rom.data, id, banks[0], why)) return false;
    const bool has_second = read_rom_bank(rom.data, id + 8, banks[1], nullptr);

    std::array<uint32_t, 256> palette = banks[0].palette;
    if (mod)
        for (int p = 1; p <= characters::kColours; ++p)
            if (mod->palette_set[size_t(p - 1)]) palette[size_t(p)] = argb_of(mod->palette[size_t(p - 1)]);
    palette[0] = 0;
    // The sheet's guide colours (0xFE, 0xFF) are not the character's.
    palette[kSheetGrid] = palette[kSheetOutside] = 0;

    // Each piece's image and where its top left goes, from its hotspot box.
    struct Placed { const SpriteImage* image; int x, y; };
    std::vector<Placed> placed;
    out.replaced = false;
    out.bank = out.frame = -1;
    for (const PortraitPiece& pc : pieces) {
        if (pc.second && !has_second) continue;
        const SpriteBank& bank = banks[pc.second ? 1 : 0];
        int g = pc.frame;
        if (g < 0 || g >= int(bank.frames.size())) continue;
        if (bank.frames[size_t(g)].kind == BankFrame::Same) g = bank.frames[size_t(g)].same;
        if (g < 0 || g >= int(bank.frames.size()) || bank.frames[size_t(g)].kind != BankFrame::Graphic) continue;
        const BankFrame& f = bank.frames[size_t(g)];
        const SpriteImage* image = &f.image;
        if (pack) {
            SpriteDraw d;
            d.bank = int16_t(id + (pc.second ? 8 : 0));
            d.frame = int16_t(pc.frame);
            if (const SpriteImage* r = pack->find(d); r && r->width > 0 && r->height > 0 &&
                                                      r->px.size() == size_t(r->width) * size_t(r->height)) {
                image = r;
                out.replaced = true;
            }
        }
        placed.push_back({image, pc.dx + f.x0, pc.dy + f.y0});
        if (&pc == &pieces.front()) {
            out.bank = id;
            out.frame = pc.frame;
        }
    }
    if (placed.empty() || out.frame < 0) return fail("no graphics");

    int x0 = placed[0].x, y0 = placed[0].y, x1 = x0, y1 = y0;
    for (const Placed& p : placed) {
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x + p.image->width);
        y1 = std::max(y1, p.y + p.image->height);
    }
    out.width = x1 - x0;
    out.height = y1 - y0;
    out.argb.assign(size_t(out.width) * size_t(out.height), 0);
    for (const Placed& p : placed)
        for (int y = 0; y < p.image->height; ++y)
            for (int x = 0; x < p.image->width; ++x) {
                const uint32_t c = palette[p.image->px[size_t(y) * size_t(p.image->width) + size_t(x)]];
                if (c) out.argb[size_t(p.y - y0 + y) * size_t(out.width) + size_t(p.x - x0 + x)] = c;
            }
    return true;
}

} // namespace chaotix
