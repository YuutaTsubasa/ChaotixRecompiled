#include "renderer/game_font.h"

#include <algorithm>
#include <map>
#include <utility>

#include "runtime/vdp.h"

namespace chaotix {

namespace {

// The ROM's glyph order after the digits and letters, as the TRAINING
// screen's tiles hold them. The two quotes and the apostrophe are curly in
// the font; plain ASCII stands for them here.
constexpr int kDigits = 0, kLetters = 10, kQuestion = 36, kExclaim = 37, kDash = 38, kComma = 39, kStop = 40,
              kUnderscore = 43, kQuote = 45, kApostrophe = 46;
constexpr int kRomGlyphs = kGameFontTileCount / 2;
// Made here, after the ROM's glyphs (see game_font_from_tiles).
constexpr int kColon = kRomGlyphs, kLess = kRomGlyphs + 1, kGreater = kRomGlyphs + 2, kSlash = kRomGlyphs + 3,
              kOpenBracket = kRomGlyphs + 4, kCloseBracket = kRomGlyphs + 5, kGlyphTotal = kRomGlyphs + 6;
// The palette entries the letters use: their yellow, and the black of the
// drop shadow every glyph has.
constexpr int kLetterInk = 10, kShadowInk = 5;

uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

} // namespace

bool nemesis_decode(const uint8_t* data, size_t size, std::vector<uint8_t>& out, size_t* consumed, std::string* why,
                    size_t max_tiles) {
    auto fail = [why](const char* m) { if (why) *why = m; return false; };
    out.clear();
    if (size < 3) return fail("too short for a Nemesis header");
    const uint16_t header = be16(data);
    const bool xor_mode = (header & 0x8000) != 0;
    const size_t tiles = header & 0x7FFF;
    if (tiles == 0 || tiles > max_tiles) return fail("implausible tile count");
    size_t pos = 2;
    // The code table: (bit length, code) -> (nybble, run length).
    std::map<std::pair<int, int>, std::pair<uint8_t, int>> codes;
    uint8_t nybble = 0;
    auto byte = [&](uint8_t& b) {
        if (pos >= size) return false;
        b = data[pos++];
        return true;
    };
    uint8_t b = 0;
    if (!byte(b)) return fail("the code table runs past the data");
    while (b != 0xFF) {
        if (b & 0x80) {
            nybble = b & 0x0F;
            if (!byte(b)) return fail("the code table runs past the data");
        }
        const int count = ((b >> 4) & 7) + 1;
        const int length = b & 0x0F;
        uint8_t code = 0;
        if (!byte(code)) return fail("the code table runs past the data");
        if (length < 1 || length > 8) return fail("a code length outside 1-8");
        codes[{length, code}] = {nybble, count};
        if (!byte(b)) return fail("the code table runs past the data");
    }
    size_t bit = pos * 8;
    const size_t bits_end = size * 8;
    auto read_bit = [&](int& v) {
        if (bit >= bits_end) return false;
        v = (data[bit >> 3] >> (7 - (bit & 7))) & 1;
        ++bit;
        return true;
    };
    const size_t total = tiles * 64;  // nybbles
    out.resize(tiles * 32);
    size_t done = 0;
    uint32_t row = 0, previous = 0;
    auto put = [&](uint8_t v, int count) {
        for (int i = 0; i < count && done < total; ++i, ++done) {
            row = row << 4 | v;
            if ((done & 7) == 7) {
                // A whole row: in XOR mode each is stored as its difference
                // from the row before it, across tile boundaries too.
                if (xor_mode) row ^= previous;
                previous = row;
                const size_t at = (done / 8) * 4;
                out[at] = uint8_t(row >> 24);
                out[at + 1] = uint8_t(row >> 16);
                out[at + 2] = uint8_t(row >> 8);
                out[at + 3] = uint8_t(row);
                row = 0;
            }
        }
    };
    while (done < total) {
        int code = 0, length = 0, v = 0;
        for (;;) {
            if (!read_bit(v)) return fail("the bit stream runs past the data");
            code = code << 1 | v;
            ++length;
            if (length == 6 && code == 0x3F) {
                // An inline run: three bits of length - 1, four of nybble.
                int count = 0, value = 0;
                for (int i = 0; i < 3; ++i) { if (!read_bit(v)) return fail("the bit stream runs past the data"); count = count << 1 | v; }
                for (int i = 0; i < 4; ++i) { if (!read_bit(v)) return fail("the bit stream runs past the data"); value = value << 1 | v; }
                put(uint8_t(value), count + 1);
                break;
            }
            auto it = codes.find({length, code});
            if (it != codes.end()) {
                put(it->second.first, it->second.second);
                break;
            }
            if (length >= 8) return fail("a code the table does not have");
        }
    }
    if (consumed) *consumed = (bit + 7) / 8;
    return true;
}

bool GameFont::supports(const std::string& s) const {
    if (empty()) return false;
    for (unsigned char c : s)
        if (c != ' ' && glyph_for(c) < 0) return false;
    return true;
}

bool game_font_from_tiles(const std::vector<uint8_t>& tiles, const std::array<uint16_t, 16>& palette, GameFont& out,
                          std::string* why) {
    if (tiles.size() < size_t(kGameFontTileCount) * 32) {
        if (why) *why = "fewer tiles than the font has";
        return false;
    }
    out = GameFont{};
    out.glyph_count = kGlyphTotal;
    out.width = kGlyphTotal * GameFont::kGlyphW;
    out.height = GameFont::kGlyphH + GameFont::kSmallGlyphH;
    out.pixels.assign(size_t(out.width) * size_t(out.height), 0);
    uint32_t colour[16];
    colour[0] = 0;
    for (int i = 1; i < 16; ++i) colour[i] = Vdp::cram_to_rgb(palette[size_t(i)]);
    // Palette indices first, so the made glyphs can copy and colour them.
    std::vector<uint8_t> index(out.pixels.size(), 0);
    auto at = [&](int g, int x, int y) -> uint8_t& {
        return index[size_t(y) * size_t(out.width) + size_t(g * GameFont::kGlyphW + x)];
    };
    for (int g = 0; g < kRomGlyphs; ++g)
        for (int half = 0; half < 2; ++half) {
            // A glyph is a 1x2 sprite: its top tile, then the one below.
            const uint8_t* t = tiles.data() + size_t(g * 2 + half) * 32;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    const uint8_t pair = t[y * 4 + x / 2];
                    at(g, x, half * 8 + y) = uint8_t(x & 1 ? pair & 0x0F : pair >> 4);
                }
        }
    // ':' is the full stop's dot twice: once where it is, once level with the
    // middle of a digit.
    int dot_top = GameFont::kGlyphH, dot_bottom = -1;
    for (int y = 0; y < GameFont::kGlyphH; ++y)
        for (int x = 0; x < GameFont::kGlyphW; ++x)
            if (at(kStop, x, y)) { dot_top = std::min(dot_top, y); dot_bottom = std::max(dot_bottom, y); }
    if (dot_bottom >= 0) {
        const int lift = (dot_bottom - dot_top + 1) + 4;
        for (int y = dot_top; y <= dot_bottom; ++y)
            for (int x = 0; x < GameFont::kGlyphW; ++x) {
                const uint8_t v = at(kStop, x, y);
                at(kColon, x, y) = v;
                if (y - lift >= 0) at(kColon, x, y - lift) = v;
            }
    }
    // '<' and '>': triangles in the letters' yellow with the font's drop
    // shadow (one pixel right and down), the height of a capital's middle.
    for (int g : {kLess, kGreater}) {
        for (int y = 0; y < 9; ++y) {
            const int span = 4 - (y < 4 ? 4 - y : y - 4);  // 0..4..0
            for (int i = 0; i <= span; ++i) {
                const int x = g == kGreater ? 1 + i : 5 - i;
                at(g, x, 4 + y) = kLetterInk;
            }
        }
        for (int y = 15; y >= 0; --y)
            for (int x = 7; x >= 0; --x)
                if (at(g, x, y) == kLetterInk && x + 1 < 8 && y + 1 < 16 && at(g, x + 1, y + 1) == 0)
                    at(g, x + 1, y + 1) = kShadowInk;
    }
    // '/': a stroke two pixels wide from the bottom left to the top right,
    // as tall as a capital, with the same shadow.
    for (int y = 1; y <= 14; ++y) {
        const int x = 1 + (14 - y) * 5 / 13;
        at(kSlash, x, y) = kLetterInk;
        at(kSlash, x + 1, y) = kLetterInk;
    }
    // '[' and ']': a stroke two pixels wide as tall as a capital, with feet
    // two pixels long at its top and bottom.
    for (int y = 1; y <= 14; ++y) {
        at(kOpenBracket, 2, y) = at(kOpenBracket, 3, y) = kLetterInk;
        at(kCloseBracket, 4, y) = at(kCloseBracket, 5, y) = kLetterInk;
    }
    for (int y : {1, 2, 13, 14}) {
        at(kOpenBracket, 4, y) = at(kOpenBracket, 5, y) = kLetterInk;
        at(kCloseBracket, 2, y) = at(kCloseBracket, 3, y) = kLetterInk;
    }
    for (int g : {kSlash, kOpenBracket, kCloseBracket})
        for (int y = 15; y >= 0; --y)
            for (int x = 7; x >= 0; --x)
                if (at(g, x, y) == kLetterInk && x + 1 < 8 && y + 1 < 16 && at(g, x + 1, y + 1) == 0)
                    at(g, x + 1, y + 1) = kShadowInk;
    // The small glyphs. A capital fills rows 1-14 with its drop shadow on row
    // 15 (row 0 is the gap above it), so rows 1-14 fold pairwise into seven
    // and the shadow row stays the last. In each pair a letter pixel wins
    // over the shadow, so strokes one row thick are kept.
    auto ink = [](uint8_t v) { return v != 0 && v != kShadowInk; };
    for (int g = 0; g < kGlyphTotal; ++g)
        for (int x = 0; x < GameFont::kGlyphW; ++x) {
            for (int y = 0; y < 7; ++y) {
                const uint8_t a = at(g, x, 1 + 2 * y), b = at(g, x, 2 + 2 * y);
                at(g, x, GameFont::kGlyphH + y) = ink(a) ? a : ink(b) ? b : a ? a : b;
            }
            at(g, x, GameFont::kGlyphH + 7) = at(g, x, 15);
        }
    for (size_t i = 0; i < index.size(); ++i) out.pixels[i] = colour[index[i] & 15];

    out.glyph.fill(-1);
    for (int d = 0; d < 10; ++d) out.glyph[size_t('0' + d)] = int16_t(kDigits + d);
    for (int l = 0; l < 26; ++l) {
        out.glyph[size_t('A' + l)] = int16_t(kLetters + l);
        out.glyph[size_t('a' + l)] = int16_t(kLetters + l);
    }
    out.glyph[size_t('?')] = kQuestion;
    out.glyph[size_t('!')] = kExclaim;
    out.glyph[size_t('-')] = kDash;
    out.glyph[size_t(',')] = kComma;
    out.glyph[size_t('.')] = kStop;
    out.glyph[size_t('_')] = kUnderscore;
    out.glyph[size_t('"')] = kQuote;
    out.glyph[size_t('\'')] = kApostrophe;
    out.glyph[size_t(':')] = kColon;
    out.glyph[size_t('<')] = kLess;
    out.glyph[size_t('>')] = kGreater;
    out.glyph[size_t('/')] = kSlash;
    out.glyph[size_t('[')] = kOpenBracket;
    out.glyph[size_t(']')] = kCloseBracket;
    return true;
}

bool build_game_font(const std::vector<uint8_t>& rom, GameFont& out, std::string* why) {
    if (rom.size() < size_t(kGameFontTiles) + 4 || rom.size() < size_t(kGameFontPalette) + 32) {
        if (why) *why = "the ROM is too small";
        return false;
    }
    // The header must say exactly this font, so a different ROM is not
    // decoded as one.
    if (be16(rom.data() + kGameFontTiles) != (0x8000 | kGameFontTileCount)) {
        if (why) *why = "no font at its address (a different ROM?)";
        return false;
    }
    std::vector<uint8_t> tiles;
    if (!nemesis_decode(rom.data() + kGameFontTiles, rom.size() - kGameFontTiles, tiles, nullptr, why)) return false;
    std::array<uint16_t, 16> palette{};
    for (int i = 0; i < 16; ++i) {
        palette[size_t(i)] = be16(rom.data() + kGameFontPalette + 2 * size_t(i));
        // Mega Drive colours use bits 1-3, 5-7 and 9-11 only.
        if (palette[size_t(i)] & ~0x0EEE) {
            if (why) *why = "no palette at its address (a different ROM?)";
            return false;
        }
    }
    return game_font_from_tiles(tiles, palette, out, why);
}

} // namespace chaotix
