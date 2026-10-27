// Mega Drive VDP (YM7101) compatibility layer: register/port behaviour, DMA,
// interrupts and a scanline renderer producing RGB + "backdrop" info that the
// compositor merges with the 32X layer.
#pragma once
#include <memory>
#include <cstdint>

namespace chaotix {

class TileReplacer;

struct VdpHost {
    void* user = nullptr;
    // Word read from the 68K bus for 68K->VDP DMA.
    uint16_t (*dma_read16)(void* user, uint32_t addr) = nullptr;
};

class Vdp {
public:
    // Widest line the renderer produces: native 320 plus up to 128 extra
    // columns on each side (widescreen).
    static constexpr int kMaxExtra = 128;
    static constexpr int kMaxWidth = 320 + 2 * kMaxExtra;

    uint8_t vram[0x10000];
    // Modders' tiles (issue #35), shared by copies of the machine; output only.
    std::shared_ptr<TileReplacer> tiles;
    uint16_t cram[64];
    uint16_t vsram[40];
    uint8_t reg[32];

    // Host-only: 2x4-cell sprites with this pattern at screen X 0 or 304 are
    // not drawn (they still count for the status flags). The widened special
    // stage hides the black columns its sprites frame the 4:3 picture with
    // (runtime/patches.h). 0xFFFF: none.
    uint16_t hidden_frame_tile = 0xFFFF;
    // Host-only: plane A outside the native columns is a picture of
    // concentric rings, given as colour (palette line 0) by distance from the
    // centre (cx2 / 2, cy2 / 2) in quarter pixels; the widened bonus stage
    // (runtime/patches.h, issue #38). radial_len 0: none.
    const uint8_t* radial_lut = nullptr;
    int radial_len = 0;
    int radial_cx2 = 0, radial_cy2 = 0;
    // Host-only: the plane pixels (and the backdrop) of palette lines 0 and 1
    // in columns [alt_x0, alt_x1) of the rendered line (0 = the left edge of
    // the left margin) take their colours from alt_cram instead: WORLD
    // ENTRANCE's margins showing the next part of the lobby, whose palette
    // the game loads only once the 4:3 view reaches it (runtime/patches.h,
    // issue #61). alt_cram null: none.
    const uint16_t* alt_cram = nullptr;
    int alt_x0 = 0, alt_x1 = 0;
    // Host-only: the widescreen margins the picture shows game content in
    // (columns beyond each side, rows below the active area); 0 where they
    // are black bars. Text replaced by sprite (sprite_text.h) treats a
    // sprite as off screen only beyond them.
    int shown_extra = 0, shown_extra_bottom = 0;

    // control port state
    bool cmd_pending = false;
    uint16_t cmd_first = 0;
    uint32_t addr = 0;
    uint8_t code = 0;
    bool fill_pending = false;

    // interrupt state
    bool vint_pending = false;
    bool hint_pending = false;
    int hint_counter = 0;

    // status bits maintained by timing
    bool in_vblank = false;
    bool odd_frame = false;
    bool sprite_overflow = false;
    bool sprite_collision = false;

    VdpHost host;
    // Tooling hook: called for every VRAM word write (address, value).
    void (*on_vram_write)(void* user, uint32_t addr, uint16_t v) = nullptr;
    void* on_vram_write_user = nullptr;
    int dma_stall_cycles = 0;   // 68K cycles consumed by the last DMA (caller drains)

    void reset();

    void write_ctrl(uint16_t v);
    void write_data(uint16_t v);
    uint16_t read_data();
    uint16_t read_status(bool hblank);
    uint16_t read_hv(int line, int hpos_mclk) const;

    bool display_enabled() const { return (reg[1] & 0x40) != 0; }
    bool vint_enabled() const { return (reg[1] & 0x20) != 0; }
    bool hint_enabled() const { return (reg[0] & 0x10) != 0; }
    int width() const { return (reg[12] & 0x01) ? 320 : 256; }
    int height() const { return (reg[1] & 0x08) ? 240 : 224; }

    // Timing hooks called by the scheduler.
    void on_line_start(int line, int active_lines);
    // Renders one line covering native columns [-extra, width()+extra).
    // out_rgb: XRGB8888; out_bg: 1 if the pixel shows the backdrop colour
    // (used for 32X priority). Status flags set while rendering (sprite
    // overflow/collision) follow native-width rules regardless of `extra`.
    void render_line(int line, uint32_t* out_rgb, uint8_t* out_bg, int extra = 0);
    // The same line, as CRAM indices where every pixel is a CRAM colour as it
    // is (no shadow/highlight, no alt_cram): then top[x] is the index, with
    // bit 6 set where it is the backdrop, the colours are palette_rgb(), and
    // it returns true. Otherwise it renders out_rgb/out_bg as render_line and
    // returns false.
    bool render_line_indexed(int line, uint8_t* top, uint32_t* out_rgb, uint8_t* out_bg, int extra = 0);
    // CRAM as XRGB8888 (alpha 0xFF), as of the last line rendered.
    const uint32_t* palette_rgb() const { return palrgb_; }

    static uint32_t cram_to_rgb(uint16_t c);

private:
    // Output only: CRAM as render_line last converted it, and the colours.
    uint16_t palrgb_cram_[64] = {};
    uint32_t palrgb_[64] = {};
    bool palrgb_valid_ = false;
    // Output only: the sprite list as render_line last walked it, in link
    // order, from a copy of the attribute table it was walked in. Walked
    // again whenever the table differs from the copy.
    struct SatEntry {
        uint16_t y, size, pattern, x;
        uint8_t index;  // place in the table
    };
    uint8_t sat_bytes_[80 * 8] = {};
    uint32_t sat_addr_ = 0;
    int sat_max_ = 0;  // 0: not walked yet
    int sat_len_ = 0;
    SatEntry sat_list_[80] = {};

    void do_dma_68k(uint32_t len);
    void do_dma_copy(uint32_t len);
    void do_dma_fill(uint16_t data, uint32_t len);
    void write_vram_word(uint32_t a, uint16_t v);
    uint32_t dma_length() const;
    uint32_t dma_source() const;
    void set_dma_source(uint32_t words);
    void set_dma_length_zero();
};

} // namespace chaotix
