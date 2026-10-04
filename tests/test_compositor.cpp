// Synthetic 32X display cases; no ROM data. Zero-byte suppression belongs to
// overwrite-image writes, not the display pipeline (Sega manual pp.40,46).
#include "runtime/system.h"
#include "test_framework.h"
#include <memory>
#include <algorithm>
using namespace chaotix;

TEST(compositor, zero_pixels_obey_palette_and_priority_in_all_modes) {
    auto m = std::make_unique<Machine>();
    m->rom.data.resize(0x400000);
    m->reset();
    m->m68k.stopped = true;
    m->vdp.reg[12] = 1;
    m->vdp.cram[0] = 0x0e00; // blue MD backdrop
    m->mars.adapter_ctrl = 1;
    m->mars.fb_display = 0;
    std::fill_n(m->mars.fb[0], 0x20000, uint8_t(0));
    m->mars.fb[0][0] = 1; // word address 0x100
    const uint32_t* row = m->framebuffer;
    for (int mode = 1; mode <= 3; ++mode) {
        // RLE: 256 + 64 pixels of index zero.
        m->mars.fb[0][0x200] = mode == 3 ? 255 : 0;
        m->mars.fb[0][0x202] = mode == 3 ? 63 : 0;
        for (int pri = 0; pri < 2; ++pri) {
            m->mars.bitmap_mode = uint16_t(mode | (pri << 7));
            m->mars.pal[0] = 0x001f;
            m->run_frame();
            const uint32_t expected = 0xff000000u |
                                      (mode == 2 ? 0 : 0xff0000u);
            CHECK_EQ(row[0], expected);
            CHECK_EQ(row[319], expected);
            if (mode != 2) {
                m->mars.pal[0] |= 0x8000;
                m->run_frame();
                CHECK_EQ(row[0], 0xffff0000u);
            }
        }
    }
}

TEST(compositor, disabled_display_and_blank_mode_have_no_32x_pixels) {
    auto m = std::make_unique<Machine>();
    m->rom.data.resize(0x400000);
    m->reset();
    m->m68k.stopped = true;
    m->vdp.reg[12] = 1;
    m->vdp.cram[0] = 0x0e00;
    const uint32_t* row = m->framebuffer;
    m->mars.pal[0] = 0xffff;
    m->mars.bitmap_mode = 0;
    m->mars.adapter_ctrl = 1;
    m->run_frame();
    CHECK_EQ(row[0], 0xff0000ffu);
    CHECK_EQ(row[319], 0xff0000ffu);
    m->mars.bitmap_mode = 1;
    m->mars.adapter_ctrl = 0;
    m->run_frame();
    CHECK_EQ(row[0], 0xff0000ffu);
}

TEST(compositor, zero_pixels_can_cover_opaque_md_when_priority_is_inverted) {
    auto m = std::make_unique<Machine>();
    m->rom.data.resize(0x400000);
    m->reset();
    m->m68k.stopped = true;
    m->vdp.reg[1] = 0x40;
    m->vdp.reg[12] = 1;
    m->vdp.reg[2] = 0x08;  // plane A at 0x2000
    m->vdp.reg[4] = 0x02;  // plane B at 0x4000
    m->vdp.reg[5] = 0x38;  // sprite table at 0x7000
    m->vdp.reg[13] = 0x1e; // scroll table at 0x7800
    m->vdp.cram[1] = 0x0e00;
    m->vdp.vram[0x2001] = 1;
    std::fill_n(m->vdp.vram + 32, 32, uint8_t(0x11)); // opaque blue tile
    m->mars.adapter_ctrl = 1;
    m->mars.fb[0][0] = 1;
    for (int mode = 1; mode <= 3; ++mode) {
        m->mars.fb[0][0x200] = mode == 3 ? 255 : 0;
        m->mars.fb[0][0x202] = mode == 3 ? 63 : 0;
        for (int pri = 0; pri < 2; ++pri) {
            for (int through = 0; through < 2; ++through) {
                m->mars.bitmap_mode = uint16_t(mode | (pri << 7));
                m->mars.pal[0] = uint16_t(0x1f | (through << 15));
                if (mode == 2) m->mars.fb[0][0x200] = uint8_t(through << 7);
                m->run_frame();
                const uint32_t front = mode == 2 ? 0xff000000u : 0xffff0000u;
                CHECK_EQ(m->framebuffer[0], pri != through ? front : 0xff0000ffu);
            }
        }
    }
}
