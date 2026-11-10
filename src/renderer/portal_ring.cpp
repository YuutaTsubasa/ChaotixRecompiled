#include "renderer/portal_ring.h"

#include <cmath>

#include "runtime/sprite_record.h"
#include "runtime/sprite_replace.h"

namespace chaotix {

namespace {

constexpr uint32_t kRomBankTable = 0x0A0000;  // ROM: 128 frame table pointers (SH-2 0x020A0000)
constexpr uint32_t kRomBase = 0x02000000;     // the cartridge as the SH-2s see it

uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

uint32_t argb555(uint16_t c) {
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

// The routine's bit reader (0x06000856): n bits, most significant first,
// a byte at a time; asked for none, it gives 0.
struct Bits {
    const uint8_t* data;
    size_t size, pos;
    int left = 0;
    uint8_t byte = 0;
    bool over = false;
    int32_t read(int n) {
        int32_t v = 0;
        for (int i = 0; i < n; ++i) {
            if (left == 0) {
                if (pos >= size) { over = true; return 0; }
                byte = data[pos++];
                left = 8;
            }
            v = v << 1 | (byte >> 7);
            byte = uint8_t(byte << 1);
            --left;
        }
        return v;
    }
};

void put16(std::vector<uint8_t>& out, int32_t v) {
    out.push_back(uint8_t(uint32_t(v) >> 8));
    out.push_back(uint8_t(v));
}

} // namespace

bool unpack_sprite_graphic(const uint8_t* data, size_t size, uint8_t colour, std::vector<uint8_t>& out,
                           std::string* why) {
    auto fail = [why](const char* m) { if (why) *why = m; return false; };
    out.clear();
    if (size < 9) return fail("too short for a packed graphic");
    if (data[2] != 'B') return fail("not a packed graphic");
    const size_t unpacked = be16(data);
    // The bases are signed bytes (the routine loads them with mov.b).
    int32_t base[6];
    for (int i = 0; i < 6; ++i) base[i] = int8_t(data[3 + i]);
    const int32_t x_base = base[0], x_bits = base[1], y_base = base[2], y_bits = base[3], px_base = base[4],
                  px_bits = base[5];
    if (x_bits > 16 || y_bits > 16 || px_bits > 8) return fail("implausible field widths");
    Bits b{data, size, 9};
    // The box: left and right columns, top and bottom lines (lines x 256).
    put16(out, x_base);
    put16(out, b.read(x_bits) + x_base);
    put16(out, y_base << 8);
    put16(out, (b.read(y_bits) + y_base) << 8);
    while (out.size() <= unpacked) {
        const int32_t xs = b.read(x_bits) + x_base, xe = b.read(x_bits) + x_base;
        if (b.over) return fail("the bit stream runs past the data");
        if (xs == xe) {
            put16(out, 0);  // the end: a span whose columns are equal
            if (out.size() != unpacked) return fail("the graphic is not the size its header says");
            return true;
        }
        if (xe < xs) return fail("a span that ends before it starts");
        out.push_back(uint8_t(xs));
        out.push_back(uint8_t(xe));
        put16(out, (b.read(y_bits) + y_base) << 8);
        for (int32_t i = xs; i < xe; ++i) {
            int32_t p = b.read(px_bits) + px_base;
            if (p != 0) p += colour;
            out.push_back(uint8_t(p));
        }
    }
    return fail("the graphic is larger than its header says");
}

int portal_ring_frame(float turn, bool* mirrored) {
    constexpr int n = PortalRing::kFrames;
    // The ring looks the same half a turn on, so a turn is two rounds of
    // 2n steps: n turning edge on, n turning back.
    float half = turn * 2.0f;
    half -= std::floor(half);
    const int step = std::min(2 * n - 1, int(half * float(2 * n)));
    if (mirrored) *mirrored = step >= n;
    return step < n ? step : 2 * n - 1 - step;
}

bool build_portal_ring(const std::vector<uint8_t>& rom, PortalRing& out, std::string* why) {
    auto fail = [why](const std::string& m) { if (why) *why = m; return false; };
    out = PortalRing{};
    if (rom.size() < kRomBankTable + 512 || rom.size() < kPortalRingPalette + 32) return fail("the ROM is too small");
    auto rom_offset = [&rom](uint32_t a, uint32_t* off) {
        if (a < kRomBase || a - kRomBase >= rom.size()) return false;
        *off = a - kRomBase;
        return true;
    };
    uint32_t table = 0;
    if (!rom_offset(be32(rom.data() + kRomBankTable + 4 * kPortalRingBank), &table) ||
        table + 4 * uint32_t(kPortalRingFirstFrame + PortalRing::kFrames) > rom.size())
        return fail("no ring bank at its address (a different ROM?)");
    // The frame table is as long as the distance to the first graphic.
    uint32_t first = 0;
    if (!rom_offset(be32(rom.data() + table), &first) || first <= table ||
        (first - table) / 4 < uint32_t(kPortalRingFirstFrame + PortalRing::kFrames))
        return fail("no ring bank at its address (a different ROM?)");

    uint32_t colours[16];
    for (int i = 0; i < 16; ++i) colours[i] = argb555(be16(rom.data() + kPortalRingPalette + 2 * size_t(i)));

    out.width = PortalRing::kCellW * PortalRing::kFrames;
    out.height = PortalRing::kCellH;
    out.pixels.assign(size_t(out.width) * size_t(out.height), 0);
    const int hot_x = PortalRing::kCellW / 2, half_h = PortalRing::kCellH / 2;
    for (int f = 0; f < PortalRing::kFrames; ++f) {
        uint32_t at = 0;
        if (!rom_offset(be32(rom.data() + table + 4 * uint32_t(kPortalRingFirstFrame + f)), &at))
            return fail("no ring frame at its address (a different ROM?)");
        std::vector<uint8_t> g;
        std::string w;
        if (!unpack_sprite_graphic(rom.data() + at, rom.size() - at, 0, g, &w)) return fail("the ring: " + w);
        SpriteImage img;
        if (!graphic_image(g.data(), uint32_t(g.size()), img)) return fail("the ring does not read as a graphic");
        const int x0 = int16_t(be16(g.data())), y0 = int16_t(be16(g.data() + 4)) >> 8;
        // The top half: lines y0 to y1 around the hotspot, which is where
        // the halves meet (the game draws the lower one flipped under it).
        if (img.height != half_h || y0 != -half_h / 2 || x0 < -hot_x || x0 + img.width > PortalRing::kCellW - hot_x)
            return fail("the ring is not the size it should be (a different ROM?)");
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x) {
                const uint8_t v = img.px[size_t(y) * size_t(img.width) + size_t(x)];
                if (!v) continue;
                if (v >= 16) return fail("the ring's colours are not one palette line (a different ROM?)");
                const size_t cx = size_t(f * PortalRing::kCellW + hot_x + x0 + x);
                out.pixels[size_t(y) * size_t(out.width) + cx] = colours[v];
                out.pixels[size_t(PortalRing::kCellH - 1 - y) * size_t(out.width) + cx] = colours[v];
            }
    }
    return true;
}

} // namespace chaotix
