// SH7604 on-chip peripherals: interrupt controller, division unit, DMA
// controller, free-running timer and watchdog timer. Only the behaviour 32X
// software relies on is modelled.
#include "system.h"
#include "log.h"

namespace chaotix {

namespace {

void divu_32(Sh2OnChip& o) {
    int32_t a = int32_t(o.dvdnt), b = int32_t(o.dvsr);
    o.dvdnth = uint32_t(a < 0 ? -1 : 0);
    if (b == 0 || (a == INT32_MIN && b == -1)) {
        o.dvcr |= 1;
        o.dvdntl = o.dvdnt = (a < 0) ? 0x80000000u : 0x7FFFFFFFu;
        return;
    }
    o.dvdntl = o.dvdnt = uint32_t(a / b);
    o.dvdnth = uint32_t(a % b);
}

void divu_64(Sh2OnChip& o) {
    int64_t a = int64_t((uint64_t(o.dvdnth) << 32) | o.dvdntl);
    int64_t b = int32_t(o.dvsr);
    if (b == 0) {
        o.dvcr |= 1;
        o.dvdntl = o.dvdnt = (a < 0) ? 0x80000000u : 0x7FFFFFFFu;
        o.dvdnth = (a < 0) ? 0x80000000u : 0x7FFFFFFFu;
        return;
    }
    int64_t q = a / b;
    if (q != int64_t(int32_t(q))) {
        o.dvcr |= 1;
        o.dvdntl = o.dvdnt = (q < 0) ? 0x80000000u : 0x7FFFFFFFu;
        o.dvdnth = (q < 0) ? 0x80000000u : 0x7FFFFFFFu;
        return;
    }
    o.dvdntl = o.dvdnt = uint32_t(q);
    o.dvdnth = uint32_t(a % b);
}

} // namespace

uint32_t Machine::onchip_read(int cpu, uint32_t a, int size) {
    Sh2OnChip& o = onchip[cpu];
    const uint32_t r = a & 0x1FF;  // offset from 0xFFFFFE00
    if (r >= 0x100) {
        uint32_t v = 0;
        switch (r & 0x1FC) {
        case 0x100: case 0x120: v = o.dvsr; break;
        case 0x104: case 0x124: v = o.dvdnt; break;
        case 0x108: case 0x128: v = o.dvcr; break;
        case 0x10C: case 0x12C: v = o.vcrdiv; break;
        case 0x110: case 0x130: v = o.dvdnth; break;
        case 0x114: case 0x134: v = o.dvdntl; break;
        case 0x118: case 0x138: v = o.dvdnth; break;
        case 0x11C: case 0x13C: v = o.dvdntl; break;
        case 0x180: v = o.dma[0].sar; break;
        case 0x184: v = o.dma[0].dar; break;
        case 0x188: v = o.dma[0].tcr; break;
        case 0x18C: v = o.dma[0].chcr; break;
        case 0x190: v = o.dma[1].sar; break;
        case 0x194: v = o.dma[1].dar; break;
        case 0x198: v = o.dma[1].tcr; break;
        case 0x19C: v = o.dma[1].chcr; break;
        case 0x1A0: v = o.dma[0].vcr; break;
        case 0x1A8: v = o.dma[1].vcr; break;
        case 0x1B0: v = o.dmaor; break;
        default:
            if (r >= 0x1E0) v = o.bsc[(r - 0x1E0) >> 2];
            break;
        }
        if (size == 4) return v;
        if (size == 2) return (r & 2) ? (v & 0xFFFF) : (v >> 16);
        return (v >> (8 * (3 - (r & 3)))) & 0xFF;
    }
    auto rd8 = [&](uint32_t off) -> uint32_t {
        switch (off) {
        case 0x10: return o.tier | 1;
        case 0x11: return o.ftcsr;
        case 0x12: o.frt_temp = uint8_t(o.frc); return o.frc >> 8;
        case 0x13: return o.frt_temp;
        case 0x14: return ((o.tocr & 0x10) ? o.ocrb : o.ocra) >> 8;
        case 0x15: return ((o.tocr & 0x10) ? o.ocrb : o.ocra) & 0xFF;
        case 0x16: return o.frt_tcr;
        case 0x17: return o.tocr | 0xE0;
        case 0x18: return o.ficr >> 8;
        case 0x19: return o.ficr & 0xFF;
        case 0x60: return o.iprb >> 8;
        case 0x61: return o.iprb & 0xFF;
        case 0x62: return o.vcra >> 8;
        case 0x63: return o.vcra & 0xFF;
        case 0x64: return o.vcrb >> 8;
        case 0x65: return o.vcrb & 0xFF;
        case 0x66: return o.vcrc >> 8;
        case 0x67: return o.vcrc & 0xFF;
        case 0x68: return o.vcrd >> 8;
        case 0x69: return o.vcrd & 0xFF;
        case 0x80: return o.wtcsr;
        case 0x81: return o.wtcnt;
        case 0x83: return o.rstcsr;
        case 0x91: return o.sbycr;
        case 0x92: return o.ccr;
        case 0xE0: return o.icr >> 8;
        case 0xE1: return o.icr & 0xFF;
        case 0xE2: return o.ipra >> 8;
        case 0xE3: return o.ipra & 0xFF;
        case 0xE4: return o.vcrwdt >> 8;
        case 0xE5: return o.vcrwdt & 0xFF;
        default: return 0;
        }
    };
    if (size == 1) return rd8(r);
    if (size == 2) return (rd8(r) << 8) | rd8(r + 1);
    return (rd8(r) << 24) | (rd8(r + 1) << 16) | (rd8(r + 2) << 8) | rd8(r + 3);
}

void Machine::onchip_write(int cpu, uint32_t a, uint32_t v, int size) {
    Sh2OnChip& o = onchip[cpu];
    const uint32_t r = a & 0x1FF;
    if (r >= 0x100) {
        // 32-bit modules. 16-bit writes to DIVU are treated as writes of the
        // sign-extended value (as some code writes DVSR/DVDNT with MOV.W).
        uint32_t val = v;
        if (size == 2) val = uint32_t(int32_t(int16_t(v)));
        switch (r & 0x1FC) {
        case 0x100: case 0x120: o.dvsr = val; break;
        case 0x104: case 0x124: o.dvdnt = val; divu_32(o); break;
        case 0x108: case 0x128: o.dvcr = val & 3; break;
        case 0x10C: case 0x12C: o.vcrdiv = val & 0x7F; break;
        case 0x110: case 0x130: o.dvdnth = val; break;
        case 0x114: case 0x134: o.dvdntl = val; divu_64(o); break;
        case 0x118: case 0x138: o.dvdnth = val; break;
        case 0x11C: case 0x13C: o.dvdntl = val; break;
        case 0x180: o.dma[0].sar = v; break;
        case 0x184: o.dma[0].dar = v; break;
        case 0x188: o.dma[0].tcr = v & 0xFFFFFF; break;
        case 0x18C: o.dma[0].chcr = (o.dma[0].chcr & 2 & v) | (v & 0xFFFD); dma_try(cpu, 0, false); break;
        case 0x190: o.dma[1].sar = v; break;
        case 0x194: o.dma[1].dar = v; break;
        case 0x198: o.dma[1].tcr = v & 0xFFFFFF; break;
        case 0x19C: o.dma[1].chcr = (o.dma[1].chcr & 2 & v) | (v & 0xFFFD); dma_try(cpu, 1, false); break;
        case 0x1A0: o.dma[0].vcr = v & 0x7F; break;
        case 0x1A8: o.dma[1].vcr = v & 0x7F; break;
        case 0x1B0: o.dmaor = (o.dmaor & 6 & v) | (v & 0x9); dma_try(cpu, 0, false); dma_try(cpu, 1, false); break;
        default:
            if (r >= 0x1E0) o.bsc[(r - 0x1E0) >> 2] = v;
            break;
        }
        update_sh2_irq(cpu);
        return;
    }
    auto wr8 = [&](uint32_t off, uint32_t b) {
        b &= 0xFF;
        switch (off) {
        case 0x10: o.tier = uint8_t(b); break;
        case 0x11: o.ftcsr = uint8_t((o.ftcsr & b & 0x8E) | (b & 1)); break;  // flags clear by writing 0
        case 0x12: o.frt_temp = uint8_t(b); break;
        case 0x13: o.frc = uint16_t((o.frt_temp << 8) | b); break;
        case 0x14: o.frt_temp = uint8_t(b); break;
        case 0x15:
            if (o.tocr & 0x10) o.ocrb = uint16_t((o.frt_temp << 8) | b);
            else o.ocra = uint16_t((o.frt_temp << 8) | b);
            break;
        case 0x16: o.frt_tcr = uint8_t(b); break;
        case 0x17: o.tocr = uint8_t(b & 0x13); break;
        case 0x60: o.iprb = uint16_t((o.iprb & 0x00FF) | (b << 8)); break;
        case 0x61: o.iprb = uint16_t((o.iprb & 0xFF00) | b); break;
        case 0x62: o.vcra = uint16_t((o.vcra & 0x00FF) | (b << 8)); break;
        case 0x63: o.vcra = uint16_t((o.vcra & 0xFF00) | b); break;
        case 0x64: o.vcrb = uint16_t((o.vcrb & 0x00FF) | (b << 8)); break;
        case 0x65: o.vcrb = uint16_t((o.vcrb & 0xFF00) | b); break;
        case 0x66: o.vcrc = uint16_t((o.vcrc & 0x00FF) | (b << 8)); break;
        case 0x67: o.vcrc = uint16_t((o.vcrc & 0xFF00) | b); break;
        case 0x68: o.vcrd = uint16_t((o.vcrd & 0x00FF) | (b << 8)); break;
        case 0x69: o.vcrd = uint16_t((o.vcrd & 0xFF00) | b); break;
        case 0x91: o.sbycr = uint8_t(b); break;
        case 0x92: o.ccr = uint8_t(b & ~0x10); break;  // CP (purge) bit reads back as 0
        case 0xE0: o.icr = uint16_t((o.icr & 0x00FF) | (b << 8)); break;
        case 0xE1: o.icr = uint16_t((o.icr & 0xFF00) | b); break;
        case 0xE2: o.ipra = uint16_t((o.ipra & 0x00FF) | (b << 8)); break;
        case 0xE3: o.ipra = uint16_t((o.ipra & 0xFF00) | b); break;
        case 0xE4: o.vcrwdt = uint16_t((o.vcrwdt & 0x00FF) | (b << 8)); break;
        case 0xE5: o.vcrwdt = uint16_t((o.vcrwdt & 0xFF00) | b); break;
        default: break;
        }
    };
    if (r == 0x80 && size == 2) {
        // WDT registers are written with a password in the upper byte.
        uint32_t hi = v >> 8;
        if (hi == 0xA5) o.wtcsr = uint8_t((o.wtcsr & 0x80 & v) | (v & 0x7F) | 0x18);
        else if (hi == 0x5A) o.wtcnt = uint8_t(v);
        update_sh2_irq(cpu);
        return;
    }
    if (r == 0x82 && size == 2) { o.rstcsr = uint8_t(v) | 0x1F; return; }
    if (size == 1) wr8(r, v);
    else if (size == 2) { wr8(r, v >> 8); wr8(r + 1, v); }
    else { wr8(r, v >> 24); wr8(r + 1, v >> 16); wr8(r + 2, v >> 8); wr8(r + 3, v); }
    update_sh2_irq(cpu);
}

void Machine::onchip_advance(int cpu, uint32_t cycles) {
    Sh2OnChip& o = onchip[cpu];
    bool changed = false;
    // FRT: clock select in TCR bits 1-0: /8, /32, /128, external.
    if ((o.frt_tcr & 3) != 3) {
        static const uint32_t div[3] = {8, 32, 128};
        o.frt_accum += cycles;
        uint32_t d = div[o.frt_tcr & 3];
        uint32_t ticks = o.frt_accum / d;
        o.frt_accum %= d;
        if (ticks) {
            uint32_t old = o.frc;
            uint32_t now = old + ticks;
            // Output compare A/B matches
            auto crossed = [&](uint16_t ocr) {
                uint32_t target = ocr;
                if (target <= old) target += 0x10000;
                return now >= target;
            };
            if (crossed(o.ocra)) {
                o.ftcsr |= 0x08;
                if (o.ftcsr & 1) now = (now - o.ocra - 1) % (uint32_t(o.ocra) + 1);  // CCLRA: clear on match A
                changed = true;
            }
            if (crossed(o.ocrb)) { o.ftcsr |= 0x04; changed = true; }
            if (now > 0xFFFF) { o.ftcsr |= 0x02; changed = true; }
            o.frc = uint16_t(now);
        }
    }
    // WDT in interval timer mode (TME set, WT/IT clear).
    if (o.wtcsr & 0x20) {
        static const uint32_t div[8] = {2, 64, 128, 256, 512, 1024, 4096, 8192};
        uint32_t d = div[o.wtcsr & 7];
        o.wdt_accum += cycles;
        uint32_t ticks = o.wdt_accum / d;
        o.wdt_accum %= d;
        if (ticks) {
            uint32_t now = o.wtcnt + ticks;
            if (now > 0xFF) { o.wtcsr |= 0x80; changed = true; }
            o.wtcnt = uint8_t(now);
        }
    }
    if (changed) update_sh2_irq(cpu);
}

// Performs DMA transfers for channel `ch`. Auto-request channels complete
// immediately; DREQ channels transfer one request unit per call.
void Machine::dma_try(int cpu, int ch, bool dreq) {
    Sh2OnChip& o = onchip[cpu];
    Sh2OnChip::Chan& c = o.dma[ch];
    if (!(o.dmaor & 1) || (o.dmaor & 6)) return;  // DME off, or NMIF/AE set
    if (!(c.chcr & 1) || (c.chcr & 2)) return;    // DE off or TE set
    const bool autoreq = (c.chcr & 0x200) != 0;
    if (!autoreq && !dreq) return;
    static const int unit_size[4] = {1, 2, 4, 16};
    const int ts = unit_size[(c.chcr >> 10) & 3];
    const int dm = (c.chcr >> 14) & 3, sm = (c.chcr >> 12) & 3;
    uint32_t count = c.tcr ? c.tcr : 0x1000000;
    uint32_t units = autoreq ? count : (ts == 16 ? 4 : 1);
    if (!autoreq && ch == 0) {
        // DREQ0 from the 68K FIFO: transfer what is available in 4-word blocks.
        units = 0;
        uint32_t words = uint32_t(mars.fifo_count);
        uint32_t bytes_per = uint32_t(ts == 16 ? 4 : ts);
        uint32_t avail_units = (words * 2) / bytes_per;
        units = avail_units < count ? avail_units : count;
        if (!units) return;
    }
    auto step = [](int mode, int size) -> int32_t { return mode == 1 ? size : mode == 2 ? -size : 0; };
    for (uint32_t u = 0; u < units && c.tcr != 0xFFFFFFFF; ++u) {
        if (ts == 16) {
            for (int k = 0; k < 4; ++k) {
                uint32_t val = sh2_read(cpu, c.sar, 4);
                sh2_write(cpu, c.dar, val, 4);
                c.sar += (sm ? 4 : 0);
                c.dar += step(dm, 4);
            }
        } else {
            uint32_t val = sh2_read(cpu, c.sar, ts);
            sh2_write(cpu, c.dar, val, ts);
            c.sar += step(sm, ts);
            c.dar += step(dm, ts);
        }
        c.tcr = (c.tcr - (ts == 16 ? 4 : 1)) & 0xFFFFFF;
        if (c.tcr == 0) break;
    }
    if (c.tcr == 0) {
        c.chcr |= 2;  // TE
        update_sh2_irq(cpu);
    }
}

} // namespace chaotix
