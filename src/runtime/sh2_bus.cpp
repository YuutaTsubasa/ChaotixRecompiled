// SH-2 address space and the 32X system registers (shared with the 68K side).
#include "system.h"
#include "patches.h"
#include "log.h"
#include <cstring>

namespace chaotix {

namespace {


uint32_t cb_r8(void* u, uint32_t a) { auto* c = static_cast<Sh2BusCtx*>(u); return c->m->sh2_read(c->cpu, a, 1); }
uint32_t cb_r16(void* u, uint32_t a) { auto* c = static_cast<Sh2BusCtx*>(u); return c->m->sh2_read(c->cpu, a, 2); }
uint32_t cb_r32(void* u, uint32_t a) { auto* c = static_cast<Sh2BusCtx*>(u); return c->m->sh2_read(c->cpu, a, 4); }
void cb_w8(void* u, uint32_t a, uint32_t v) { auto* c = static_cast<Sh2BusCtx*>(u); c->m->sh2_write(c->cpu, a, v, 1); }
void cb_w16(void* u, uint32_t a, uint32_t v) { auto* c = static_cast<Sh2BusCtx*>(u); c->m->sh2_write(c->cpu, a, v, 2); }
void cb_w32(void* u, uint32_t a, uint32_t v) { auto* c = static_cast<Sh2BusCtx*>(u); c->m->sh2_write(c->cpu, a, v, 4); }

inline uint32_t be_read(const uint8_t* p, int size) {
    if (size == 1) return p[0];
    if (size == 2) return (uint32_t(p[0]) << 8) | p[1];
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline void be_write(uint8_t* p, uint32_t v, int size) {
    if (size == 1) { p[0] = uint8_t(v); return; }
    if (size == 2) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); return; }
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}

} // namespace

void Machine::remap_sh2() {
    for (int cpu = 0; cpu < 2; ++cpu) {
        sh2_ctx_[cpu] = Sh2BusCtx{this, cpu};
        sh2::Bus& b = sh2[cpu].bus;
        b.user = &sh2_ctx_[cpu];
        b.read8 = cb_r8; b.read16 = cb_r16; b.read32 = cb_r32;
        b.write8 = cb_w8; b.write16 = cb_w16; b.write32 = cb_w32;
        for (int i = 0; i < sh2::Bus::kPages; ++i) { b.rpage[i] = nullptr; b.wpage[i] = nullptr; }
        const uint32_t pg = 1u << sh2::Bus::kPageShift;
        // SDRAM 0x06000000-0x0603FFFF. Pages holding recompiled code keep the
        // slow write path so modifications can invalidate generated code.
        for (uint32_t off = 0; off < 0x40000; off += pg) {
            b.rpage[sh2::page_of(0x06000000 + off)] = sdram + off;
            bool watched = false;
            for (const auto& r : sdram_code_ranges)
                if (r.first < off + pg && r.second > off) watched = true;
            if (!watched) b.wpage[sh2::page_of(0x06000000 + off)] = sdram + off;
        }
        // Cartridge ROM 0x02000000-0x023FFFFF
        for (uint32_t off = 0; off < 0x400000; off += pg)
            b.rpage[sh2::page_of(0x02000000 + off)] = rom.data.data() + (off & (rom.data.size() - 1));
    }
}

// ---------------------------------------------------------------------------
// Frame buffer
// ---------------------------------------------------------------------------
uint32_t Machine::fb_read(uint32_t off, int size) {
    off &= 0x1FFFF;
    const uint8_t* fb = mars.fb[mars.fb_display ^ 1];
    return be_read(fb + off, size);
}

void Machine::fb_write(uint32_t off, uint32_t v, int size, bool overwrite) {
    off &= 0x1FFFF;
    if (on_fb_write) on_fb_write(on_fb_write_user, off, v, size, overwrite);
    uint8_t* fb = mars.fb[mars.fb_display ^ 1];
    if (!overwrite) { be_write(fb + off, v, size); return; }
    if (wide_active) { fb_write_wide(fb, off, v, size); return; }
    // Overwrite image: zero bytes are transparent (not written).
    for (int i = 0; i < size; ++i) {
        uint8_t b = uint8_t(v >> (8 * (size - 1 - i)));
        if (b) fb[(off + uint32_t(i)) & 0x1FFFF] = b;
    }
}

// Widescreen sprite writes (see patches.h): bytes outside the native columns
// of a 512-byte frame buffer line go to the host-side margin shadow.
void Machine::fb_write_wide(uint8_t* fb, uint32_t off, uint32_t v, int size) {
    uint8_t* shadow = fb_margin[mars.fb_display ^ 1];
    for (int i = 0; i < size; ++i) {
        uint8_t b = uint8_t(v >> (8 * (size - 1 - i)));
        if (!b) continue;
        uint32_t o = (off + uint32_t(i)) & 0x1FFFF;
        if (((o - patches::kFbLineBase) & (patches::kFbLineStride - 1)) >= uint32_t(kNativeWidth)) shadow[o] = b;
        else fb[o] = b;
    }
}

// Clears the margin shadow around the frame buffer line starting at line_off
// (called when the game's auto fill clears that line).
void Machine::fb_margin_clear_line(uint32_t line_off, uint16_t fill) {
    uint8_t* shadow = fb_margin[mars.fb_display ^ 1];
    for (int x = -patches::kMaxWideExtra; x < kNativeWidth + patches::kMaxWideExtra; ++x) {
        if (x == 0) x = kNativeWidth;
        uint32_t o = (line_off + uint32_t(x)) & 0x1FFFF;
        shadow[o] = uint8_t((o & 1) ? fill : fill >> 8);
    }
}

// ---------------------------------------------------------------------------
// 32X VDP registers
// ---------------------------------------------------------------------------
uint32_t Machine::vdp32x_read(int who, uint32_t off) {
    switch (off & 0xE) {
    case 0x0: return 0x8000u | mars.bitmap_mode;  // bit15: 1 = NTSC
    case 0x2: return mars.screen_shift;
    case 0x4: return mars.fill_len;
    case 0x6: return mars.fill_addr;
    case 0x8: return mars.fill_data;
    case 0xA: {
        uint64_t now = who < 0 ? m68k_now_mclk() : sh2_now_mclk(who);
        int64_t pos = int64_t(now) - int64_t(line_start_mclk);
        bool vblank = line >= kActiveLines;
        bool hblank = pos >= 2600 || pos < 0;
        uint32_t v = mars.fb_display & 1;
        if (vblank) v |= 0x8000;
        if (hblank) v |= 0x4000;
        if (vblank || hblank || (mars.bitmap_mode & 3) == 0) v |= 0x2000;  // PEN
        return v;
    }
    default: return 0;
    }
}

void Machine::vdp32x_write(uint32_t off, uint32_t v, int size) {
    // Byte writes land in the addressed half of the word register.
    auto merge = [&](uint16_t& reg) {
        if (size == 1) {
            if (off & 1) reg = uint16_t((reg & 0xFF00) | (v & 0xFF));
            else reg = uint16_t((reg & 0x00FF) | ((v & 0xFF) << 8));
        } else {
            reg = uint16_t(v);
        }
    };
    switch (off & 0xE) {
    case 0x0: merge(mars.bitmap_mode); mars.bitmap_mode &= 0x00C3; break;
    case 0x2: merge(mars.screen_shift); mars.screen_shift &= 1; break;
    case 0x4: merge(mars.fill_len); mars.fill_len &= 0xFF; break;
    case 0x6: merge(mars.fill_addr); break;
    case 0x8: {
        merge(mars.fill_data);
        // Auto fill: (len+1) words starting at fill_addr, wrapping inside 256 words.
        uint8_t* fb = mars.fb[mars.fb_display ^ 1];
        uint32_t addr = mars.fill_addr;
        if (on_fb_fill) on_fb_fill(on_fb_write_user, addr, mars.fill_len + 1u, mars.fill_data);
        for (uint32_t i = 0; i <= mars.fill_len; ++i) {
            uint32_t o = (addr & 0xFFFF) * 2;
            fb[o] = uint8_t(mars.fill_data >> 8);
            fb[o + 1] = uint8_t(mars.fill_data);
            if (wide_active && ((o - patches::kFbLineBase) & (patches::kFbLineStride - 1)) == 0)
                fb_margin_clear_line(o, mars.fill_data);
            addr = (addr & 0xFF00) | ((addr + 1) & 0xFF);
        }
        mars.fill_addr = uint16_t(addr);
        break;
    }
    case 0xA: {
        uint16_t reg = mars.fb_select_req;
        merge(reg);
        mars.fb_select_req = reg & 1;
        if (line >= kActiveLines || (mars.bitmap_mode & 3) == 0) {
            // In V-blank (or display blanked) the swap happens immediately.
            if (mars.fb_display != mars.fb_select_req) mars.fb_display = mars.fb_select_req;
        }
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------------------
// PWM
// ---------------------------------------------------------------------------
uint32_t Machine::pwm_read(uint32_t off) {
    switch (off & 0xE) {
    case 0x0: return pwm.ctrl;
    case 0x2: return pwm.cycle;
    case 0x4: return (pwm.count_l >= 3 ? 0x8000u : 0) | (pwm.count_l == 0 ? 0x4000u : 0);
    case 0x6: return (pwm.count_r >= 3 ? 0x8000u : 0) | (pwm.count_r == 0 ? 0x4000u : 0);
    case 0x8: {
        bool full = pwm.count_l >= 3 || pwm.count_r >= 3;
        bool empty = pwm.count_l == 0 && pwm.count_r == 0;
        return (full ? 0x8000u : 0) | (empty ? 0x4000u : 0);
    }
    default: return 0;
    }
}

void Machine::pwm_write(uint32_t off, uint32_t v) {
    auto push = [](uint16_t* f, int& n, uint16_t val) {
        if (n >= 3) { f[0] = f[1]; f[1] = f[2]; n = 2; }  // overwrite oldest when full
        f[n++] = val;
    };
    switch (off & 0xE) {
    case 0x0:
        pwm.ctrl = uint16_t(v & 0x0F8F);
        pwm.timer = ((pwm.ctrl >> 8) & 0xF) ? ((pwm.ctrl >> 8) & 0xF) : 16;
        break;
    case 0x2: pwm.cycle = uint16_t(v & 0x0FFF); break;
    case 0x4: push(pwm.fifo_l, pwm.count_l, uint16_t(v & 0xFFF)); break;
    case 0x6: push(pwm.fifo_r, pwm.count_r, uint16_t(v & 0xFFF)); break;
    case 0x8:
        push(pwm.fifo_l, pwm.count_l, uint16_t(v & 0xFFF));
        push(pwm.fifo_r, pwm.count_r, uint16_t(v & 0xFFF));
        break;
    default: break;
    }
}

void Machine::pwm_advance(uint32_t sh2_cycles) {
    if (pwm.cycle <= 1 || (pwm.ctrl & 0xF) == 0) return;
    const uint32_t period = uint32_t(pwm.cycle) - 1;
    pwm.accum += sh2_cycles;
    while (pwm.accum >= period) {
        pwm.accum -= period;
        auto pop = [](uint16_t* f, int& n, int16_t& out) {
            if (n) { out = int16_t(f[0]); f[0] = f[1]; f[1] = f[2]; --n; }
        };
        pop(pwm.fifo_l, pwm.count_l, pwm.out_l);
        pop(pwm.fifo_r, pwm.count_r, pwm.out_r);
        pwm.samples.push_back(pwm.out_l);
        pwm.samples.push_back(pwm.out_r);
        if (--pwm.timer <= 0) {
            pwm.timer = ((pwm.ctrl >> 8) & 0xF) ? ((pwm.ctrl >> 8) & 0xF) : 16;
            for (int c = 0; c < 2; ++c) { mars.irq_pending[c] |= 1; update_sh2_irq(c); }
            if (pwm.ctrl & 0x80) {  // RTP: DREQ1 to both SH-2 DMACs
                dma_try(0, 1, true);
                dma_try(1, 1, true);
            }
        }
    }
    if (pwm.samples.size() > 65536) pwm.samples.erase(pwm.samples.begin(), pwm.samples.begin() + 32768);
}

// ---------------------------------------------------------------------------
// 68K -> SH-2 DREQ FIFO
// ---------------------------------------------------------------------------
void Machine::fifo_push(uint16_t v) {
    if (!(mars.dreq_ctrl & 4)) return;  // 68S not set
    if (mars.fifo_count >= 8) { CHAOTIX_LOG_LIMITED(LogLevel::Warn, "32x", 8, "DREQ FIFO overflow"); return; }
    mars.fifo[(mars.fifo_rd + mars.fifo_count) & 7] = uint16_t(v);
    mars.fifo_count++;
    dreq_service();
}

void Machine::dreq_service() {
    // DREQ0 is asserted when at least 4 words are available.
    for (int guard = 0; guard < 8 && mars.fifo_count >= 4; ++guard) {
        int before = mars.fifo_count;
        dma_try(0, 0, true);
        if (mars.fifo_count == before) break;
    }
}

// ---------------------------------------------------------------------------
// 32X system registers
// ---------------------------------------------------------------------------
uint32_t Machine::mars_read(int who, uint32_t addr, int size) {
    uint32_t off;
    if (who < 0) {
        uint32_t x = addr - 0xA15100;
        if (x < 0x80) off = x;
        else if (x < 0x100) off = 0x100 + (x - 0x80);
        else off = 0x200 + (x - 0x100);
    } else {
        off = addr & 0x3FF;
    }
    if (size == 4) return (mars_read(who, addr, 2) << 16) | mars_read(who, addr + 2, 2);
    if (size == 1) {
        uint32_t w = mars_read(who, addr & ~1u, 2);
        return (addr & 1) ? (w & 0xFF) : (w >> 8);
    }
    off &= ~1u;
    if (off >= 0x200) return mars.pal[(off - 0x200) >> 1];
    if (off >= 0x100) return vdp32x_read(who, off);
    if (off >= 0x30 && off < 0x40) return pwm_read(off);
    if (off >= 0x20 && off < 0x30) return mars.comm[(off - 0x20) >> 1];
    switch (off) {
    case 0x00:
        if (who < 0) return (mars.adapter_ctrl & 0x8003) | 0x0080;  // REN=1
        return (mars.adapter_ctrl & 0x8000) | ((mars.adapter_ctrl & 1) << 9) | (mars.sh_int_mask[who] & 0x8F);
    case 0x02:
        if (who < 0) return mars.int_ctrl;
        return 0;
    case 0x04:
        if (who < 0) return mars.bank;
        return mars.hcount;
    case 0x06: {
        uint32_t v = mars.dreq_ctrl & 7;
        if (who < 0) { if (mars.fifo_count >= 8) v |= 0x80; }
        else { if (mars.fifo_count == 0) v |= 0x4000; if (mars.fifo_count >= 8) v |= 0x8000; }
        return v;
    }
    case 0x08: return (mars.dreq_src >> 16) & 0xFF;
    case 0x0A: return mars.dreq_src & 0xFFFE;
    case 0x0C: return (mars.dreq_dst >> 16) & 0xFF;
    case 0x0E: return mars.dreq_dst & 0xFFFF;
    case 0x10: return mars.dreq_len;
    case 0x12:
        if (who >= 0) {
            if (mars.fifo_count == 0) return 0;
            uint16_t v = mars.fifo[mars.fifo_rd];
            mars.fifo_rd = (mars.fifo_rd + 1) & 7;
            mars.fifo_count--;
            if (mars.dreq_words_left) {
                if (--mars.dreq_words_left == 0) mars.dreq_ctrl &= ~4;  // transfer complete: 68S clears
            }
            return v;
        }
        return 0;
    case 0x1A:
        if (who < 0) return mars.sega_tv;
        return 0;
    default: return 0;
    }
}

void Machine::mars_write(int who, uint32_t addr, uint32_t v, int size) {
    uint32_t off;
    if (who < 0) {
        uint32_t x = addr - 0xA15100;
        if (x < 0x80) off = x;
        else if (x < 0x100) off = 0x100 + (x - 0x80);
        else off = 0x200 + (x - 0x100);
    } else {
        off = addr & 0x3FF;
    }
    if (size == 4) {
        mars_write(who, addr, v >> 16, 2);
        mars_write(who, addr + 2, v & 0xFFFF, 2);
        return;
    }
    // Normalise byte writes into (word offset, merged word value, byte mask).
    const bool byte = size == 1;
    const bool lowbyte = byte && (off & 1);
    const uint32_t woff = off & ~1u;
    auto merge16 = [&](uint16_t old) -> uint16_t {
        if (!byte) return uint16_t(v);
        if (lowbyte) return uint16_t((old & 0xFF00) | (v & 0xFF));
        return uint16_t((old & 0x00FF) | ((v & 0xFF) << 8));
    };

    if (woff >= 0x200) {
        uint16_t& p = mars.pal[(woff - 0x200) >> 1];
        p = merge16(p);
        return;
    }
    if (woff >= 0x100) { vdp32x_write(off, v, size); return; }
    if (woff >= 0x30 && woff < 0x40) {
        uint16_t cur = uint16_t(pwm_read(woff));
        if (woff == 0x30) cur = pwm.ctrl;
        else if (woff == 0x32) cur = pwm.cycle;
        uint16_t nv = merge16(cur);
        if (who < 0 && woff == 0x30) nv = uint16_t((pwm.ctrl & 0x0F00) | (nv & 0x00FF));  // TM only from SH-2
        pwm_write(woff, nv);
        return;
    }
    if (woff >= 0x20 && woff < 0x30) {
        uint16_t& c = mars.comm[(woff - 0x20) >> 1];
        c = merge16(c);
        return;
    }
    switch (woff) {
    case 0x00:
        if (who < 0) {
            uint16_t old = mars.adapter_ctrl;
            uint16_t nv = merge16(old);
            mars.adapter_ctrl = uint16_t(nv & 0x8003);
            if ((old ^ mars.adapter_ctrl) & 1) remap_m68k();
            if (!(old & 2) && (mars.adapter_ctrl & 2)) boot_hle_start_sh2();
        } else {
            uint16_t cur = uint16_t((mars.adapter_ctrl & 0x8000) | (mars.sh_int_mask[who] & 0x8F));
            uint16_t nv = merge16(cur);
            mars.adapter_ctrl = uint16_t((mars.adapter_ctrl & 0x7FFF) | (nv & 0x8000));
            mars.sh_int_mask[who] = nv & 0x8F;
            update_sh2_irq(who);
        }
        return;
    case 0x02:
        if (who < 0) {
            uint16_t nv = merge16(mars.int_ctrl);
            mars.int_ctrl = nv & 3;
            if (nv & 1) { mars.irq_pending[0] |= 2; update_sh2_irq(0); }
            if (nv & 2) { mars.irq_pending[1] |= 2; update_sh2_irq(1); }
        }
        return;
    case 0x04:
        if (who < 0) { mars.bank = merge16(mars.bank) & 3; remap_m68k(); }
        else mars.hcount = merge16(mars.hcount) & 0xFF;
        return;
    case 0x06:
        if (who < 0) {
            uint16_t nv = merge16(mars.dreq_ctrl);
            uint16_t old = mars.dreq_ctrl;
            mars.dreq_ctrl = nv & 7;
            if (!(old & 4) && (nv & 4)) {  // 68S set: start a DREQ transfer
                mars.fifo_count = 0;
                mars.fifo_rd = 0;
                mars.dreq_words_left = mars.dreq_len;
            }
        }
        return;
    case 0x08: if (who < 0) mars.dreq_src = (mars.dreq_src & 0xFFFF) | (uint32_t(merge16(uint16_t(mars.dreq_src >> 16)) & 0xFF) << 16); return;
    case 0x0A: if (who < 0) mars.dreq_src = (mars.dreq_src & 0xFF0000) | (merge16(uint16_t(mars.dreq_src)) & 0xFFFE); return;
    case 0x0C: if (who < 0) mars.dreq_dst = (mars.dreq_dst & 0xFFFF) | (uint32_t(merge16(uint16_t(mars.dreq_dst >> 16)) & 0xFF) << 16); return;
    case 0x0E: if (who < 0) mars.dreq_dst = (mars.dreq_dst & 0xFF0000) | merge16(uint16_t(mars.dreq_dst)); return;
    case 0x10: if (who < 0) mars.dreq_len = merge16(mars.dreq_len) & 0xFFFC; return;
    case 0x12: if (who < 0) fifo_push(merge16(0)); return;
    case 0x14: if (who >= 0) { mars.irq_pending[who] &= ~0x10; update_sh2_irq(who); } return;
    case 0x16: if (who >= 0) { mars.irq_pending[who] &= ~0x08; update_sh2_irq(who); } return;
    case 0x18: if (who >= 0) { mars.irq_pending[who] &= ~0x04; update_sh2_irq(who); } return;
    case 0x1A:
        if (who < 0) mars.sega_tv = merge16(mars.sega_tv) & 1;
        else { mars.irq_pending[who] &= ~0x02; mars.int_ctrl &= ~(1u << who); update_sh2_irq(who); }
        return;
    case 0x1C: if (who >= 0) { mars.irq_pending[who] &= ~0x01; update_sh2_irq(who); } return;
    default:
        CHAOTIX_LOG_LIMITED(LogLevel::Debug, "32x", 16, "unhandled reg write %s off %03X = %04X", who < 0 ? "68K" : "SH2", off, v);
        return;
    }
}

// ---------------------------------------------------------------------------
// SH-2 bus
// ---------------------------------------------------------------------------
uint32_t Machine::sh2_read(int cpu, uint32_t a, int size) {
    const uint32_t area = a >> 29;
    if (area <= 1) {
        uint32_t p = a & 0x1FFFFFFF;
        if (p < 0x4000) return be_read(bios_stub + p, size);
        if (p < 0x4400) return mars_read(cpu, 0x4000 + (p & 0x3FF), size);
        if (p >= 0x02000000 && p < 0x04000000) return be_read(rom.data.data() + (p & 0x3FFFFF & (rom.data.size() - 1)), size);
        if (p >= 0x04000000 && p < 0x06000000) return fb_read(p & 0x1FFFF, size);
        if (p >= 0x06000000 && p < 0x08000000) return be_read(sdram + (p & 0x3FFFF), size);
        CHAOTIX_LOG_LIMITED(LogLevel::Debug, "sh2", 32, "SH2%c unmapped read%d %08X", cpu ? 'S' : 'M', size * 8, a);
        return 0;
    }
    if (area == 6) return be_read(onchip[cpu].cache_data + (a & 0xFFF), size);
    if (a >= 0xFFFFFE00) return onchip_read(cpu, a, size);
    if (area == 3) return 0;  // cache address array
    CHAOTIX_LOG_LIMITED(LogLevel::Debug, "sh2", 32, "SH2%c unmapped read%d %08X", cpu ? 'S' : 'M', size * 8, a);
    return 0;
}

void Machine::sh2_write(int cpu, uint32_t a, uint32_t v, int size) {
    const uint32_t area = a >> 29;
    if (area <= 1) {
        uint32_t p = a & 0x1FFFFFFF;
        if (p >= 0x4000 && p < 0x4400) { mars_write(cpu, 0x4000 + (p & 0x3FF), v, size); return; }
        if (p >= 0x04000000 && p < 0x06000000) { fb_write(p & 0x1FFFF, v, size, (p & 0x20000) != 0); return; }
        if (p >= 0x06000000 && p < 0x08000000) {
            uint32_t off = p & 0x3FFFF;
            be_write(sdram + off, v, size);
            for (const auto& r : sdram_code_ranges)
                if (off < r.second && off + uint32_t(size) > r.first) { ++sdram_code_epoch; break; }
            return;
        }
        CHAOTIX_LOG_LIMITED(LogLevel::Debug, "sh2", 32, "SH2%c unmapped write%d %08X=%X", cpu ? 'S' : 'M', size * 8, a, v);
        return;
    }
    if (area == 2) return;  // cache purge
    if (area == 3) return;  // cache address array
    if (area == 6) {
        uint32_t off = a & 0xFFF;
        be_write(onchip[cpu].cache_data + off, v, size);
        for (const auto& r : cache_code_ranges)
            if (off < r.second && off + uint32_t(size) > r.first) { ++cache_code_epoch[cpu]; break; }
        return;
    }
    if (a >= 0xFFFFFE00) { onchip_write(cpu, a, v, size); return; }
    if ((a & 0xFFFF0000) == 0xFFFF0000) return;  // SDRAM mode register setup
    CHAOTIX_LOG_LIMITED(LogLevel::Debug, "sh2", 32, "SH2%c unmapped write%d %08X=%X", cpu ? 'S' : 'M', size * 8, a, v);
}

} // namespace chaotix
