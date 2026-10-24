// See portrait.h.
#include "frontend/portrait.h"

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

bool character_portrait(const Rom& rom, int id, const SpritePack* pack, const CharacterMod* mod, Portrait& out,
                        std::string* why) {
    auto fail = [why](const std::string& m) { if (why) *why = m; return false; };
    if (id < 0 || id >= characters::kCount) return fail("not a character");
    SpriteBank bank;
    if (!read_rom_bank(rom.data, id, bank, why)) return false;
    // The stand, or the first graphic there is if a patched bank has none
    // there; a frame that repeats an earlier one is that one.
    int frame = -1;
    for (int f = kPortraitFrame; f < int(bank.frames.size()) && frame < 0; ++f) {
        int g = f;
        if (bank.frames[size_t(g)].kind == BankFrame::Same) g = bank.frames[size_t(g)].same;
        if (g >= 0 && g < int(bank.frames.size()) && bank.frames[size_t(g)].kind == BankFrame::Graphic) frame = g;
    }
    if (frame < 0) return fail("no graphics");

    const SpriteImage* image = &bank.frames[size_t(frame)].image;
    out.replaced = false;
    if (pack) {
        SpriteDraw d;
        d.bank = int16_t(id);
        d.frame = int16_t(frame);
        if (const SpriteImage* r = pack->find(d); r && r->width > 0 && r->height > 0 &&
                                                  r->px.size() == size_t(r->width) * size_t(r->height)) {
            image = r;
            out.replaced = true;
        }
    }
    std::array<uint32_t, 256> palette = bank.palette;
    if (mod)
        for (int p = 1; p <= characters::kColours; ++p)
            if (mod->palette_set[size_t(p - 1)]) palette[size_t(p)] = argb_of(mod->palette[size_t(p - 1)]);
    palette[0] = 0;
    // The sheet's guide colours (0xFE, 0xFF) are not the character's.
    palette[kSheetGrid] = palette[kSheetOutside] = 0;

    out.width = image->width;
    out.height = image->height;
    out.argb.assign(image->px.size(), 0);
    for (size_t i = 0; i < image->px.size(); ++i) out.argb[i] = palette[image->px[i]];
    out.bank = id;
    out.frame = frame;
    return true;
}

} // namespace chaotix
