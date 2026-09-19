#include "system.h"
#include "log.h"
#include "cpu/m68k/m68k_interp.h"
#include "cpu/sh2/sh2_interp.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace chaotix {

namespace {

void m68k_irq_ack(void* user, int level) {
    Machine* m = static_cast<Machine*>(user);
    if (level == 6) m->vdp.vint_pending = false;
    else if (level == 4) m->vdp.hint_pending = false;
    m->update_m68k_irq();
}

uint16_t vdp_dma_read(void* user, uint32_t addr) {
    return uint16_t(static_cast<Machine*>(user)->m68k_read16(addr));
}

uint8_t z80_rd(void* u, uint16_t a) { return static_cast<Machine*>(u)->z80_bus_read(a); }
void z80_wr(void* u, uint16_t a, uint8_t v) { static_cast<Machine*>(u)->z80_bus_write(a, v); }
uint8_t z80_in(void*, uint16_t) { return 0xFF; }
void z80_out(void*, uint16_t, uint8_t) {}

} // namespace

Machine::Machine() {
    std::memset(sram, 0xFF, sizeof sram);
    exec.m68k_block = m68k::interp_block;
    exec.sh2_block = sh2::interp_block;
    std::memset(framebuffer, 0, sizeof framebuffer);
}

Machine::~Machine() = default;

bool Machine::load_rom(const std::string& path, std::string* err) {
    Rom r;
    if (!r.load(path, err)) return false;
    load_rom(std::move(r));
    return true;
}

void Machine::load_rom(Rom&& r) {
    rom = std::move(r);
    if (rom.version == RomVersion::Unknown)
        LOGW("rom", "unrecognised ROM (sha1 %s); continuing, but behaviour is unverified", rom.sha1.c_str());
    else
        LOGI("rom", "%s (sha1 %s)", rom_version_name(rom.version), rom.sha1.c_str());
}

void Machine::reset() {
    std::memset(wram, 0, sizeof wram);
    std::memset(sdram, 0, sizeof sdram);
    std::memset(zram, 0, sizeof zram);
    // The 32X boot ROMs are not used (boot is high-level emulated). The SH-2
    // BIOS area reads as an infinite loop in case anything jumps there.
    for (size_t i = 0; i < sizeof bios_stub; i += 4) {
        bios_stub[i] = 0xAF; bios_stub[i + 1] = 0xFE;  // bra $
        bios_stub[i + 2] = 0x00; bios_stub[i + 3] = 0x09;  // nop
    }
    vdp.reset();
    vdp.host.user = this;
    vdp.host.dma_read16 = vdp_dma_read;
    mars.~Mars();
    new (&mars) Mars{};
    pwm.ctrl = pwm.cycle = 0;
    pwm.count_l = pwm.count_r = 0;
    pwm.timer = 16;
    pwm.accum = 0;
    pwm.out_l = pwm.out_r = 0;
    pwm.samples.clear();
    for (auto& o : onchip) o = Sh2OnChip{};
    z80_busreq = 0;
    z80_reset = 1;
    z80_bank = 0;
    z80.bus = z80::Bus{this, z80_rd, z80_wr, z80_in, z80_out};
    z80::reset(&z80);
    z80_clock = 0;
    ym.reset();
    psg.reset();
    audio_out.clear();
    audio_pos = 0;
    psg_frac = 0;
    dc_x[0] = dc_x[1] = dc_y[0] = dc_y[1] = 0;
    sram_ctrl = 0;  // SRAM contents persist across resets (battery backed)
    std::memset(io_ctrl, 0, sizeof io_ctrl);
    std::memset(io_data, 0x7F, sizeof io_data);
    pad_th_count[0] = pad_th_count[1] = 0;

    mclk = 0;
    m68k_clock = 0;
    sh2_clock[0] = sh2_clock[1] = 0;
    line = 0;
    line_start_mclk = 0;
    frame_count = 0;
    boot_hle_done = false;
    stats = FrameStats{};

    m68k.irq_ack = m68k_irq_ack;
    m68k.irq_user = this;
    remap_m68k();
    m68k::reset(&m68k);
    for (int c = 0; c < 2; ++c) {
        sh2[c] = sh2::State{};
        sh2[c].id = uint8_t(c);
    }
    remap_sh2();
    update_m68k_irq();
}

// ---------------------------------------------------------------------------
// Interrupts
// ---------------------------------------------------------------------------
void Machine::update_m68k_irq() {
    int lvl = 0;
    if (vdp.vint_pending && vdp.vint_enabled()) lvl = 6;
    else if (vdp.hint_pending && vdp.hint_enabled()) lvl = 4;
    m68k.irq_level = uint8_t(lvl);
}

void Machine::update_sh2_irq(int cpu) {
    int lvl = 0, vec = 0;
    const uint8_t pend = mars.irq_pending[cpu];
    const uint8_t en = uint8_t((pend & (mars.sh_int_mask[cpu] & 0x0F)) | (pend & 0x10));
    if (en & 0x10) lvl = 14;
    else if (en & 0x08) lvl = 12;
    else if (en & 0x04) lvl = 10;
    else if (en & 0x02) lvl = 8;
    else if (en & 0x01) lvl = 6;
    if (lvl) vec = 64 + lvl / 2;

    const Sh2OnChip& o = onchip[cpu];
    auto cand = [&](int l, int v) { if (l > lvl) { lvl = l; vec = v; } };
    if ((o.dvcr & 3) == 3) cand((o.ipra >> 12) & 15, o.vcrdiv & 0x7F);
    for (int ch = 0; ch < 2; ++ch)
        if ((o.dma[ch].chcr & 6) == 6) cand((o.ipra >> 8) & 15, int(o.dma[ch].vcr & 0x7F));
    if ((o.wtcsr & 0xA0) == 0xA0 && !(o.wtcsr & 0x40)) cand((o.ipra >> 4) & 15, (o.vcrwdt >> 8) & 0x7F);
    const int frt_lvl = (o.iprb >> 8) & 15;
    if ((o.ftcsr & 0x80) && (o.tier & 0x80)) cand(frt_lvl, (o.vcrc >> 8) & 0x7F);
    if (((o.ftcsr & 0x08) && (o.tier & 0x08)) || ((o.ftcsr & 0x04) && (o.tier & 0x04))) cand(frt_lvl, o.vcrc & 0x7F);
    if ((o.ftcsr & 0x02) && (o.tier & 0x02)) cand(frt_lvl, (o.vcrd >> 8) & 0x7F);

    sh2[cpu].irq_level = uint8_t(lvl);
    sh2[cpu].irq_vector = uint8_t(vec);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------
uint64_t Machine::m68k_now_mclk() const {
    uint64_t c = m68k_clock;
    if (m68k_slice_.running) c = m68k_slice_.base + uint64_t(int64_t(m68k_slice_.budget) - m68k.cycles);
    return c * 7;
}

uint64_t Machine::sh2_now_mclk(int cpu) const {
    uint64_t c = sh2_clock[cpu];
    if (sh2_slice_[cpu].running) c = sh2_slice_[cpu].base + uint64_t(int64_t(sh2_slice_[cpu].budget) - sh2[cpu].cycles);
    return c * 7 / 3;
}

int Machine::hpos_mclk() const {
    int64_t p = int64_t(m68k_now_mclk()) - int64_t(line_start_mclk);
    if (p < 0) p = 0;
    if (p >= kMclkPerLine) p = kMclkPerLine - 1;
    return int(p);
}

// ---------------------------------------------------------------------------
// Boot HLE: replaces the three 32X boot ROMs.
// ---------------------------------------------------------------------------
void Machine::boot_hle_start_sh2() {
    const MarsHeader& h = rom.mars;
    LOGI("boot", "SH-2 reset released: copying %u bytes ROM %06X -> SDRAM %06X; M=%08X S=%08X",
         h.size, h.source, h.dest, h.master_entry, h.slave_entry);
    for (uint32_t i = 0; i < h.size; ++i)
        sdram[(h.dest + i) & 0x3FFFF] = rom.data[(h.source + i) & (rom.data.size() - 1)];

    const uint32_t entry[2] = {h.master_entry, h.slave_entry};
    const uint32_t vbr[2] = {h.master_vbr, h.slave_vbr};
    for (int c = 0; c < 2; ++c) {
        sh2::State& s = sh2[c];
        uint32_t sp = sh2::rd32(&s, vbr[c] + 4);  // reset SP from the game's vector table
        sh2::reset(&s, entry[c], sp, vbr[c]);
        sh2_slice_[c] = SliceInfo{};
        sh2_clock[c] = m68k_clock * 3;
    }
    // Hand-shake values the real boot ROMs leave in the communication ports.
    mars.comm[0] = 0x4D5F; mars.comm[1] = 0x4F4B;  // "M_OK"
    mars.comm[2] = 0x535F; mars.comm[3] = 0x4F4B;  // "S_OK"
    uint16_t sum = rom.compute_checksum();
    if (sum != rom.header_checksum)
        LOGW("boot", "ROM checksum %04X does not match header %04X", sum, rom.header_checksum);
    mars.comm[4] = sum;  // master BIOS reports the cartridge checksum in COMM8
    boot_hle_done = true;
}

// ---------------------------------------------------------------------------
// Scheduler
// ---------------------------------------------------------------------------
void Machine::run_slice(uint64_t end_mclk) {
    // 68000
    {
        uint64_t target = end_mclk / 7;
        if (target > m68k_clock) {
            int32_t budget = int32_t(target - m68k_clock);
            m68k.cycles = budget;
            m68k_slice_ = SliceInfo{true, m68k_clock, budget};
            while (m68k.cycles > 0) {
                m68k::check_irq(&m68k);
                if (m68k.stopped) { m68k.cycles = 0; break; }
                if (on_block) on_block(0, m68k.pc);
                exec.m68k_block(&m68k);
            }
            m68k_slice_.running = false;
            uint64_t done = uint64_t(int64_t(budget) - m68k.cycles);
            m68k_clock += done;
            stats.m68k_cycles += done;
        }
    }
    // Z80 (sound CPU), unless held in reset or its bus is taken by the 68K
    {
        uint64_t target = end_mclk / 15;
        if (target > z80_clock) {
            int32_t budget = int32_t(target - z80_clock);
            if (!z80_reset && !z80_busreq) z80::run(&z80, budget);
            z80_clock = target;
        }
    }
    // SH-2 master then slave (only after the 68K releases them from reset)
    const uint64_t sh_target = end_mclk * 3 / 7;
    for (int c = 0; c < 2; ++c) {
        if (!boot_hle_done || !(mars.adapter_ctrl & 2)) { sh2_clock[c] = sh_target; continue; }
        if (sh_target <= sh2_clock[c]) continue;
        sh2::State& s = sh2[c];
        int32_t budget = int32_t(sh_target - sh2_clock[c]);
        s.cycles = budget;
        sh2_slice_[c] = SliceInfo{true, sh2_clock[c], budget};
        while (s.cycles > 0) {
            sh2::check_irq(&s);
            if (s.sleeping) { s.cycles = 0; break; }
            if (on_block) on_block(1 + c, s.pc);
            exec.sh2_block(&s);
        }
        sh2_slice_[c].running = false;
        uint64_t done = uint64_t(int64_t(budget) - s.cycles);
        sh2_clock[c] += done;
        (c ? stats.ssh2_cycles : stats.msh2_cycles) += done;
        onchip_advance(c, uint32_t(done));
    }
    uint64_t start = mclk;
    mclk = end_mclk;
    if (boot_hle_done) pwm_advance(uint32_t((end_mclk * 3 / 7) - (start * 3 / 7)));
    audio_advance(end_mclk);
}

void Machine::audio_advance(uint64_t end_mclk) {
    const uint64_t target = end_mclk / kMclkPerAudioSample;
    if (target <= audio_pos) return;
    const int n = int(target - audio_pos);
    audio_pos = target;
    if (!audio_enabled) { ym.tick_timers(n); return; }
    size_t base = audio_out.size();
    ym.generate(n, audio_out);
    // PWM: 12-bit pulse widths centred on cycle/2; LMD/RMD route the FIFOs.
    auto pwm_level = [&](int sel) -> int32_t {
        if (pwm.cycle <= 1) return 0;
        int32_t v = sel == 1 ? pwm.out_l : sel == 2 ? pwm.out_r : int32_t(pwm.cycle / 2);
        return (v - int32_t(pwm.cycle / 2)) * 16384 / int32_t(pwm.cycle);
    };
    const int32_t pl = pwm_level(pwm.ctrl & 3), pr = pwm_level((pwm.ctrl >> 2) & 3);
    for (int i = 0; i < n; ++i) {
        psg_frac += kMclkPerAudioSample;
        int clocks = psg_frac / 15;
        psg_frac -= clocks * 15;
        int32_t p = psg.run(clocks) / 2;
        const int32_t in[2] = {audio_out[base + size_t(i) * 2] + p + pl, audio_out[base + size_t(i) * 2 + 1] + p + pr};
        for (int ch = 0; ch < 2; ++ch) {
            // One-pole DC blocker (~17 Hz corner at 53 kHz).
            float x = float(in[ch]);
            float y = x - dc_x[ch] + 0.998f * dc_y[ch];
            dc_x[ch] = x;
            dc_y[ch] = y;
            audio_out[base + size_t(i) * 2 + size_t(ch)] = int16_t(std::clamp(int32_t(y), -32768, 32767));
        }
    }
    if (audio_out.size() > 1 << 20) audio_out.erase(audio_out.begin(), audio_out.begin() + (1 << 19));
}

void Machine::start_line() {
    vdp.on_line_start(line, kActiveLines);
    z80.irq_line = line == kActiveLines ? 1 : 0;  // Z80 /INT for one line at V-blank
    if (line == kActiveLines) {
        // V-blank start: 32X frame buffer swap and V interrupt.
        if (mars.fb_display != mars.fb_select_req) mars.fb_display = mars.fb_select_req;
        for (int c = 0; c < 2; ++c) mars.irq_pending[c] |= 0x08;
    }
    // 32X H interrupt every (HCOUNT + 1) lines.
    if (line == 0) mars.hcount_counter = mars.hcount;
    else if (--mars.hcount_counter < 0) {
        mars.hcount_counter = mars.hcount;
        for (int c = 0; c < 2; ++c)
            if (line < kActiveLines || (mars.sh_int_mask[c] & 0x80)) mars.irq_pending[c] |= 0x04;
    }
    update_m68k_irq();
    update_sh2_irq(0);
    update_sh2_irq(1);
}

void Machine::end_line() {
    if (line < kActiveLines) {
        uint32_t md[Vdp::kMaxWidth];
        uint8_t bg[Vdp::kMaxWidth];
        vdp.render_line(line, md, bg);
        uint32_t* row = framebuffer + line * kScreenWidth;
        render_line32x(line, row);
        // Merge: render_line32x marks 32X pixels that should be shown with
        // alpha 0xFE; everything else takes the MD pixel.
        for (int x = 0; x < kScreenWidth; ++x) {
            uint32_t p = row[x];
            bool show32x = (p >> 24) == 0xFE;
            bool mdbg = bg[x] != 0;
            if (show32x || ((p >> 24) == 0xFD && mdbg)) row[x] = 0xFF000000u | (p & 0xFFFFFF);
            else row[x] = md[x];
        }
    }
}

void Machine::run_frame() {
    for (line = 0; line < kLinesPerFrameNTSC; ++line) {
        line_start_mclk = mclk;
        start_line();
        for (int s = 1; s <= kSlicesPerLine; ++s)
            run_slice(line_start_mclk + uint64_t(s) * kMclkPerLine / kSlicesPerLine);
        end_line();
    }
    line = 0;
    fb_width = vdp.width();
    fb_height = kActiveLines;
    ++frame_count;
    stats.frame = frame_count;
}

} // namespace chaotix
