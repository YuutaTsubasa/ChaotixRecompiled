// 68000 address space of the Mega Drive + 32X.
#include "system.h"
#include "log.h"

namespace chaotix {

namespace {
uint32_t cb_read8(void* u, uint32_t a) { return static_cast<Machine*>(u)->m68k_read8(a); }
uint32_t cb_read16(void* u, uint32_t a) { return static_cast<Machine*>(u)->m68k_read16(a); }
void cb_write8(void* u, uint32_t a, uint32_t v) { static_cast<Machine*>(u)->m68k_write8(a, v); }
void cb_write16(void* u, uint32_t a, uint32_t v) { static_cast<Machine*>(u)->m68k_write16(a, v); }
} // namespace

void Machine::remap_m68k() {
    m68k::Bus& b = m68k.bus;
    b.user = this;
    b.read8 = cb_read8;
    b.read16 = cb_read16;
    b.write8 = cb_write8;
    b.write16 = cb_write16;
    for (int i = 0; i < 256; ++i) { b.rpage[i] = nullptr; b.wpage[i] = nullptr; }
    const bool aden = (mars.adapter_ctrl & 1) != 0;
    // Cartridge at 0x000000. With the adapter enabled the first 256 bytes are
    // the 32X vector ROM, so page 0 goes through the slow path.
    for (int i = aden ? 1 : 0; i < 0x40; ++i) b.rpage[i] = rom.data.data() + (size_t(i) << 16);
    if (aden) {
        for (int i = 0; i < 8; ++i) b.rpage[0x88 + i] = rom.data.data() + (size_t(i) << 16);
        uint32_t bank = uint32_t(mars.bank & 3) << 20;
        for (int i = 0; i < 16; ++i) b.rpage[0x90 + i] = rom.data.data() + ((bank + (uint32_t(i) << 16)) & (rom.data.size() - 1));
    }
    for (int i = 0xE0; i < 0x100; ++i) { b.rpage[i] = wram; b.wpage[i] = wram; }
    // SRAM overlays cartridge 0x200000-0x20FFFF when enabled: slow path there.
    if (sram_ctrl & 1) {
        b.rpage[0x20] = nullptr;
        if (aden && (mars.bank & 3) == 2) b.rpage[0x90] = nullptr;
    }
}

// Cartridge address seen by a 68K access (or -1 if not cartridge space).
static int64_t cart_address(const Machine& m, uint32_t a) {
    if (a < 0x400000) return a;
    if (m.mars.adapter_ctrl & 1) {
        if (a >= 0x880000 && a < 0x900000) return a - 0x880000;
        if (a >= 0x900000 && a < 0xA00000) return int64_t(uint32_t(m.mars.bank & 3) << 20) + (a - 0x900000);
    }
    return -1;
}

static bool sram_hit(const Machine& m, uint32_t a, uint32_t& idx) {
    if (!(m.sram_ctrl & 1)) return false;
    int64_t ca = cart_address(m, a);
    if (ca < 0x200000 || ca >= 0x210000) return false;
    idx = uint32_t(ca - 0x200000) >> 1;
    return true;
}

uint32_t Machine::pad_read(int port) {
    // 3/6-button pad protocol. Buttons are active low on the wire.
    // pad_th_count counts TH edges since the last timeout (6-button sequencing).
    uint16_t btn = input.pad[port];
    bool th = (io_data[port] & 0x40) || !(io_ctrl[port] & 0x40);
    int phase = input.six_button[port] ? pad_th_count[port] : 0;
    auto nb = [&](uint16_t mask, uint32_t bit) { return (btn & mask) ? 0u : bit; };
    uint32_t v;
    if (phase == 5 && !th) {
        v = nb(PAD_A, 0x10) | nb(PAD_START, 0x20);  // low nibble 0000 = 6-button id
    } else if (phase == 6 && th) {
        v = 0x40 | nb(PAD_Z, 1) | nb(PAD_Y, 2) | nb(PAD_X, 4) | nb(PAD_MODE, 8) | nb(PAD_B, 0x10) | nb(PAD_C, 0x20);
    } else if (phase == 7 && !th) {
        v = 0x0F | nb(PAD_A, 0x10) | nb(PAD_START, 0x20);
    } else if (th) {
        v = 0x40 | nb(PAD_UP, 1) | nb(PAD_DOWN, 2) | nb(PAD_LEFT, 4) | nb(PAD_RIGHT, 8) | nb(PAD_B, 0x10) | nb(PAD_C, 0x20);
    } else {
        v = nb(PAD_UP, 1) | nb(PAD_DOWN, 2) | nb(PAD_A, 0x10) | nb(PAD_START, 0x20);
    }
    uint32_t out_mask = io_ctrl[port] & 0x7F;
    return (v & ~out_mask) | (io_data[port] & out_mask) | (io_data[port] & 0x80);
}

// 68K access to the Z80 address space (0xA00000-0xA0FFFF).
uint32_t Machine::z80_space_read(uint32_t a) {
    a &= 0x7FFF;
    if (a < 0x4000) return zram[a & 0x1FFF];
    if (a < 0x6000) return ym.read_status();
    return 0xFF;
}

void Machine::z80_space_write(uint32_t a, uint32_t v) {
    a &= 0x7FFF;
    if (a < 0x4000) { zram[a & 0x1FFF] = uint8_t(v); return; }
    if (a < 0x6000) { ym.write((a >> 1) & 1, a & 1, uint8_t(v)); return; }
    if (a < 0x6100) { z80_bank = ((z80_bank >> 1) | ((v & 1) << 23)) & 0xFF8000; return; }
    if (a == 0x7F11 || a == 0x7F13 || a == 0x7F15 || a == 0x7F17) psg.write(uint8_t(v));
}

// The Z80's own view of its address space.
uint8_t Machine::z80_bus_read(uint16_t a) {
    if (a < 0x4000) return zram[a & 0x1FFF];
    if (a < 0x6000) return ym.read_status();
    if (a < 0x8000) return 0xFF;  // bank register / VDP area
    return uint8_t(m68k_read8(z80_bank | (a & 0x7FFF)));
}

void Machine::z80_bus_write(uint16_t a, uint8_t v) {
    if (a < 0x8000) { z80_space_write(a, v); return; }
    m68k_write8(z80_bank | (a & 0x7FFF), v);
}

void Machine::set_z80_reset(bool held) {
    if (held && !z80_reset) ym.reset();          // the reset line also resets the YM2612
    if (!held && z80_reset) z80::reset(&z80);    // released: Z80 restarts at 0
    z80_reset = held ? 1 : 0;
}

uint32_t Machine::m68k_read8(uint32_t a) {
    a &= 0xFFFFFF;
    if (a < 0x100 && (mars.adapter_ctrl & 1)) {
        uint32_t w = m68k_read16(a & ~1u);
        return (a & 1) ? (w & 0xFF) : (w >> 8);
    }
    {
        uint32_t si;
        if (sram_hit(*this, a, si)) return (a & 1) ? sram[si] : 0xFF;
    }
    if (a < 0x400000) return rom.data[a];
    if (a >= 0x840000 && a < 0x880000) {
        uint32_t w = m68k_read16(a & ~1u);
        return (a & 1) ? (w & 0xFF) : (w >> 8);
    }
    if (a >= 0xA00000 && a < 0xA10000) return z80_space_read(a);
    if (a >= 0xA10000 && a < 0xA10020) {
        switch (a & 0x1F) {
        case 0x01: return 0xA0;  // overseas, NTSC, version 0
        case 0x03: return pad_read(0);
        case 0x05: return pad_read(1);
        case 0x07: return io_data[2];
        case 0x09: return io_ctrl[0];
        case 0x0B: return io_ctrl[1];
        case 0x0D: return io_ctrl[2];
        default: return 0;
        }
    }
    if (a >= 0xA11100 && a < 0xA11200) {
        // bit 0 = 0 when the 68K owns the Z80 bus (request granted)
        if ((a & 1) == 0) return (z80_busreq && !z80_reset ? 0x00 : 0x01) | 0x80;
        return 0;
    }
    if (a >= 0xA130EC && a < 0xA130F0) {
        static const char id[] = "MARS";
        return uint8_t(id[a - 0xA130EC]);
    }
    if (a >= 0xA15100 && a < 0xA15400) {
        uint32_t w = mars_read(-1, a & ~1u, 2);
        return (a & 1) ? (w & 0xFF) : (w >> 8);
    }
    if (a >= 0xC00000 && a < 0xC00020) {
        uint32_t w = m68k_read16(a & ~1u);
        return (a & 1) ? (w & 0xFF) : (w >> 8);
    }
    if (a >= 0xE00000) return wram[a & 0xFFFF];
    {
        int64_t ca = cart_address(*this, a);
        if (ca >= 0) return rom.data[size_t(ca) & (rom.data.size() - 1)];
    }
    CHAOTIX_LOG_LIMITED(LogLevel::Debug, "68k", 32, "unmapped read8 %06X", a);
    return 0;
}

uint32_t Machine::m68k_read16(uint32_t a) {
    a &= 0xFFFFFE;
    if (a < 0x100 && (mars.adapter_ctrl & 1)) {
        // 32X 68K vector ROM: every vector jumps into the cartridge's jump table.
        // 0xC0-0xFF holds a small routine the security code calls (JSR $C0);
        // without the original ROM it is synthesized as NOPs ending in RTS.
        if (a >= 0xC0) return a == 0xFE ? 0x4E75 : 0x4E71;
        uint32_t vec = a >> 2;
        uint32_t val;
        if (vec == 0) val = rom.read32(0);
        else if (vec == 0x70 / 4) val = (uint32_t(mars.hint_vector[0]) << 24) | (uint32_t(mars.hint_vector[1]) << 16) | (uint32_t(mars.hint_vector[2]) << 8) | mars.hint_vector[3];
        else val = 0x880200 + (vec - 1) * 6;
        return (a & 2) ? (val & 0xFFFF) : (val >> 16);
    }
    {
        uint32_t si;
        if (sram_hit(*this, a, si)) return 0xFF00u | sram[si];
    }
    if (a < 0x400000) return (uint32_t(rom.data[a]) << 8) | rom.data[a + 1];
    if (a >= 0x840000 && a < 0x880000) {
        if ((mars.adapter_ctrl & 1) && !(mars.adapter_ctrl & 0x8000)) return fb_read(a & 0x1FFFF, 2);
        return 0;
    }
    if (a >= 0xA00000 && a < 0xA10000) {
        uint32_t b = z80_space_read(a);
        return (b << 8) | b;
    }
    if (a >= 0xA10000 && a < 0xA10020) return m68k_read8(a | 1);
    if (a >= 0xA11100 && a < 0xA11200) return (m68k_read8(a) << 8);
    if (a >= 0xA130EC && a < 0xA130F0) return (m68k_read8(a) << 8) | m68k_read8(a + 1);
    if (a >= 0xA15100 && a < 0xA15400) return mars_read(-1, a, 2);
    if (a >= 0xC00000 && a < 0xC00020) {
        switch (a & 0x1E) {
        case 0x00: case 0x02: return vdp.read_data();
        case 0x04: case 0x06: {
            int hp = hpos_mclk();
            bool hblank = hp >= 2600;  // approx. start of H-blank in H40
            return vdp.read_status(hblank);
        }
        case 0x08: case 0x0A: case 0x0C: case 0x0E: return vdp.read_hv(line, hpos_mclk());
        default: return 0xFFFF;
        }
    }
    if (a >= 0xE00000) return (uint32_t(wram[a & 0xFFFF]) << 8) | wram[(a & 0xFFFF) + 1];
    {
        int64_t ca = cart_address(*this, a);
        if (ca >= 0) { size_t o = size_t(ca) & (rom.data.size() - 1); return (uint32_t(rom.data[o]) << 8) | rom.data[o + 1]; }
    }
    CHAOTIX_LOG_LIMITED(LogLevel::Debug, "68k", 32, "unmapped read16 %06X", a);
    return 0;
}

void Machine::m68k_write8(uint32_t a, uint32_t v) {
    a &= 0xFFFFFF;
    v &= 0xFF;
    if (a >= 0xE00000) { wram[a & 0xFFFF] = uint8_t(v); return; }
    {
        uint32_t si;
        if (sram_hit(*this, a, si)) {
            if ((a & 1) && !(sram_ctrl & 2) && sram[si] != v) { sram[si] = uint8_t(v); sram_dirty = true; }
            return;
        }
    }
    if (a < 0x100 && (mars.adapter_ctrl & 1)) {
        if (a >= 0x70 && a < 0x74) mars.hint_vector[a - 0x70] = uint8_t(v);
        return;
    }
    if (a >= 0x840000 && a < 0x880000) {
        if ((mars.adapter_ctrl & 1) && !(mars.adapter_ctrl & 0x8000)) fb_write(a & 0x3FFFF, v, 1, (a & 0x20000) != 0);
        return;
    }
    if (a >= 0xA00000 && a < 0xA10000) { z80_space_write(a, v); return; }
    if (a >= 0xA10000 && a < 0xA10020) {
        int port = -1;
        switch (a & 0x1F) {
        case 0x03: port = 0; break;
        case 0x05: port = 1; break;
        case 0x07: io_data[2] = uint8_t(v); return;
        case 0x09: io_ctrl[0] = uint8_t(v); return;
        case 0x0B: io_ctrl[1] = uint8_t(v); return;
        case 0x0D: io_ctrl[2] = uint8_t(v); return;
        default: return;
        }
        uint8_t old = io_data[port];
        io_data[port] = uint8_t(v);
        // Count TH edges for 6-button sequencing; the pad resets after ~1.5 ms idle.
        if ((old ^ v) & 0x40) {
            uint64_t now = m68k_now_mclk();
            if (now - pad_th_time[port] > 80000) pad_th_count[port] = 0;
            pad_th_count[port]++;
            pad_th_time[port] = now;
        }
        return;
    }
    if (a >= 0xA11100 && a < 0xA11200) { if (!(a & 1)) z80_busreq = v & 1; return; }
    if (a >= 0xA11200 && a < 0xA11300) { if (!(a & 1)) set_z80_reset(!(v & 1)); return; }
    if (a >= 0xA14000 && a < 0xA14104) return;  // TMSS
    if (a == 0xA130F1) { sram_ctrl = uint8_t(v & 3); remap_m68k(); return; }
    if (a >= 0xA130F0 && a < 0xA13100) return;  // mapper registers (unused)
    if (a >= 0xA15100 && a < 0xA15400) { mars_write(-1, a, v, 1); return; }
    if (a >= 0xC00000 && a < 0xC00020) {
        if ((a & 0x1F) >= 0x10) { if ((a & 0x19) == 0x11) psg.write(uint8_t(v)); return; }
        m68k_write16(a & ~1u, (v << 8) | v);
        return;
    }
    CHAOTIX_LOG_LIMITED(LogLevel::Debug, "68k", 32, "unmapped write8 %06X=%02X", a, v);
}

void Machine::m68k_write16(uint32_t a, uint32_t v) {
    a &= 0xFFFFFE;
    v &= 0xFFFF;
    if (a >= 0xE00000) { wram[a & 0xFFFF] = uint8_t(v >> 8); wram[(a & 0xFFFF) + 1] = uint8_t(v); return; }
    {
        uint32_t si;
        if (sram_hit(*this, a, si)) { m68k_write8(a | 1, v & 0xFF); return; }
    }
    if (a < 0x100 && (mars.adapter_ctrl & 1)) {
        if (a >= 0x70 && a < 0x74) { mars.hint_vector[a - 0x70] = uint8_t(v >> 8); mars.hint_vector[a - 0x70 + 1] = uint8_t(v); }
        return;
    }
    if (a >= 0x840000 && a < 0x880000) {
        if ((mars.adapter_ctrl & 1) && !(mars.adapter_ctrl & 0x8000)) fb_write(a & 0x3FFFF, v, 2, (a & 0x20000) != 0);
        return;
    }
    if (a >= 0xA00000 && a < 0xA10000) { z80_space_write(a, v >> 8); return; }
    if (a >= 0xA10000 && a < 0xA10020) { m68k_write8(a | 1, v & 0xFF); return; }
    if (a >= 0xA11100 && a < 0xA11200) { z80_busreq = (v >> 8) & 1; return; }
    if (a >= 0xA11200 && a < 0xA11300) { set_z80_reset(!((v >> 8) & 1)); return; }
    if (a >= 0xA14000 && a < 0xA14104) return;
    if (a == 0xA130F0) { m68k_write8(0xA130F1, v & 0xFF); return; }
    if (a >= 0xA130F0 && a < 0xA13100) return;
    if (a >= 0xA15100 && a < 0xA15400) { mars_write(-1, a, v, 2); return; }
    if (a >= 0xC00000 && a < 0xC00020) {
        switch (a & 0x1E) {
        case 0x00: case 0x02: vdp.write_data(uint16_t(v)); break;
        case 0x04: case 0x06:
            vdp.write_ctrl(uint16_t(v));
            if (vdp.dma_stall_cycles) { m68k.cycles -= vdp.dma_stall_cycles; vdp.dma_stall_cycles = 0; }
            update_m68k_irq();
            break;
        case 0x10: case 0x12: case 0x14: case 0x16: psg.write(uint8_t(v)); break;
        default: break;  // debug registers
        }
        return;
    }
    CHAOTIX_LOG_LIMITED(LogLevel::Debug, "68k", 32, "unmapped write16 %06X=%04X", a, v);
}

} // namespace chaotix
