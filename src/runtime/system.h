// The Sega 32X + Mega Drive runtime ("Machine"). This is the minimal hardware
// compatibility layer that recompiled (or interpreted) game code runs on.
//
// Timing model: everything is scheduled in Mega Drive master clocks (MCLK,
// 53.693175 MHz NTSC). 68K = MCLK/7, SH-2 = MCLK*3/7, Z80 = MCLK/15.
// A frame is 262 lines x 3420 MCLK. Each line is split into fixed slices; in
// each slice the 68K, master SH-2 and slave SH-2 run (in that order) up to the
// slice end. This fixed order makes execution fully deterministic.
#pragma once
#include "cpu/m68k/m68k.h"
#include "cpu/sh2/sh2.h"
#include "cpu/z80/z80.h"
#include "audio/sn76489.h"
#include "audio/ym2612.h"
#include "input/input.h"
#include "runtime/rom.h"
#include "runtime/vdp.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace chaotix {

constexpr int kMclkPerLine = 3420;
constexpr int kLinesPerFrameNTSC = 262;
constexpr int kActiveLines = 224;
constexpr int kSlicesPerLine = 4;
constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;  // buffer height; 224 lines are active
constexpr int kMclkPerAudioSample = 1008;  // YM2612 native rate: MCLK / 7 / 144
constexpr double kAudioRate = 53693175.0 / kMclkPerAudioSample;

// SH-2 on-chip peripherals (per CPU).
struct Sh2OnChip {
    // Interrupt controller
    uint16_t ipra = 0, iprb = 0, vcra = 0, vcrb = 0, vcrc = 0, vcrd = 0, vcrwdt = 0, icr = 0;
    // Division unit
    uint32_t dvsr = 0, dvdnt = 0, dvcr = 0, vcrdiv = 0, dvdnth = 0, dvdntl = 0;
    // DMA controller
    struct Chan { uint32_t sar = 0, dar = 0, tcr = 0, chcr = 0, vcr = 0; } dma[2];
    uint32_t dmaor = 0;
    // Free-running timer
    uint8_t tier = 0, ftcsr = 0, frt_tcr = 0, tocr = 0;
    uint16_t frc = 0, ocra = 0xFFFF, ocrb = 0xFFFF, ficr = 0;
    uint8_t frt_temp = 0;
    uint32_t frt_accum = 0;
    // Watchdog timer
    uint8_t wtcsr = 0x18, wtcnt = 0, rstcsr = 0x1F;
    uint32_t wdt_accum = 0;
    // Misc
    uint8_t ccr = 0, sbycr = 0;
    uint32_t bsc[8] = {};
    uint8_t cache_data[4096];
};

struct Pwm {
    uint16_t ctrl = 0;       // 0x4030
    uint16_t cycle = 0;      // 0x4032
    uint16_t fifo_l[3] = {}, fifo_r[3] = {};
    int count_l = 0, count_r = 0;
    int timer = 0;           // PWM cycles until next timer interrupt
    uint32_t accum = 0;      // SH-2 clocks accumulated towards next PWM cycle
    int16_t out_l = 0, out_r = 0;
    std::vector<int16_t> samples;  // interleaved stereo at pwm rate (for audio)
};

struct Mars {
    // 68K-side registers
    uint16_t adapter_ctrl = 0;  // A15100: bit15 FM, bit1 nRES, bit0 ADEN
    uint16_t int_ctrl = 0;      // A15102
    uint16_t bank = 0;          // A15104
    uint16_t dreq_ctrl = 0;     // A15106: bit2 68S, bit1 DMA, bit0 RV
    uint32_t dreq_src = 0, dreq_dst = 0;
    uint16_t dreq_len = 0;
    uint16_t fifo[8] = {};
    int fifo_count = 0, fifo_rd = 0;
    uint32_t dreq_words_left = 0;
    uint16_t sega_tv = 0;
    uint16_t comm[8] = {};
    uint8_t hint_vector[4] = {};  // writable 68K vector at 0x70
    // SH-2 side system registers
    uint16_t sh_int_mask[2] = {};  // 0x4000 low bits: 0 PWM, 1 CMD, 2 H, 3 V; bit 7 HEN
    uint16_t hcount = 0;           // 0x4004
    int hcount_counter = 0;
    uint8_t irq_pending[2] = {};   // bits: 0 PWM, 1 CMD, 2 H, 3 V, 4 VRES
    // VDP
    uint16_t bitmap_mode = 0;      // 0x4100
    uint16_t screen_shift = 0;     // 0x4102
    uint16_t fill_len = 0, fill_addr = 0, fill_data = 0;
    uint8_t fb_display = 0;        // buffer currently displayed
    uint8_t fb_select_req = 0;     // requested FS value
    uint16_t pal[256] = {};
    uint8_t fb[2][0x20000];
};

struct FrameStats {
    uint64_t frame = 0;
    uint64_t m68k_cycles = 0, msh2_cycles = 0, ssh2_cycles = 0;
    uint64_t recomp_blocks = 0, interp_blocks = 0;
};

// Executes code for a CPU. The default executors interpret; the recompiled
// game installs executors that dispatch to generated code.
struct CpuExecutors {
    void (*m68k_block)(m68k::State* c) = nullptr;       // run >= 1 block
    void (*sh2_block)(sh2::State* c) = nullptr;
};

class Machine;

// Glue used by SH-2 bus callbacks.
struct Sh2BusCtx {
    Machine* m;
    int cpu;
};

// The CPU time slice currently executing (for mid-slice timestamps).
struct SliceInfo {
    bool running = false;
    uint64_t base = 0;
    int32_t budget = 0;
};

class Machine {
public:
    Machine();
    ~Machine();

    bool load_rom(const std::string& path, std::string* err);
    void load_rom(Rom&& rom);
    void reset();
    void run_frame();

    // Input for the next frame (unified; the frontend fills it).
    InputState input;

    // Output
    uint32_t framebuffer[kScreenWidth * kScreenHeight];  // XRGB8888
    int fb_width = 320, fb_height = 224;

    // Components
    Rom rom;
    uint8_t wram[0x10000];
    uint8_t sdram[0x40000];
    uint8_t zram[0x2000];
    uint8_t bios_stub[0x4000];
    Vdp vdp;
    Mars mars;
    Pwm pwm;
    Sh2OnChip onchip[2];
    m68k::State m68k;
    sh2::State sh2[2];
    z80::State z80;
    Ym2612 ym;
    Sn76489 psg;
    uint16_t z80_busreq = 0, z80_reset = 0;
    uint32_t z80_bank = 0;
    // Audio output: interleaved stereo int16 at kAudioRate, drained by the
    // frontend. Synthesis only runs when audio_enabled (timers always run).
    bool audio_enabled = false;
    std::vector<int16_t> audio_out;
    uint64_t audio_pos = 0;  // in audio samples
    int psg_frac = 0;
    float dc_x[2] = {0, 0}, dc_y[2] = {0, 0};  // output DC blocker state
    uint8_t io_ctrl[3] = {}, io_data[3] = {};
    // Battery-backed cartridge SRAM (odd bytes at cartridge 0x200001-0x20FFFF),
    // controlled by $A130F1: bit0 = SRAM mapped, bit1 = write protect.
    uint8_t sram[0x8000];
    uint8_t sram_ctrl = 0;
    bool sram_dirty = false;
    int pad_th_count[2] = {};
    uint64_t pad_th_time[2] = {};

    CpuExecutors exec;
    FrameStats stats;

    // Self-modification tracking for RAM-resident recompiled SH-2 code.
    // Ranges are byte offsets into SDRAM / the cache data array.
    std::vector<std::pair<uint32_t, uint32_t>> sdram_code_ranges, cache_code_ranges;
    uint32_t sdram_code_epoch = 1;
    uint32_t cache_code_epoch[2] = {1, 1};

    // Timing
    uint64_t mclk = 0;        // current master clock
    uint64_t m68k_clock = 0;  // 68K cycles executed
    uint64_t sh2_clock[2] = {};
    uint64_t z80_clock = 0;
    int line = 0;
    uint64_t line_start_mclk = 0;
    uint64_t frame_count = 0;
    bool boot_hle_done = false;

    // Hooks for tooling (coverage tracing, lockstep validation).
    std::function<void(int cpu, uint32_t pc)> on_block;  // cpu: 0=68K,1=MSH2,2=SSH2

    // ---- bus entry points (also used by generated code via CPU callbacks) ----
    uint32_t m68k_read8(uint32_t a);
    uint32_t m68k_read16(uint32_t a);
    void m68k_write8(uint32_t a, uint32_t v);
    void m68k_write16(uint32_t a, uint32_t v);
    uint32_t sh2_read(int cpu, uint32_t a, int size);
    void sh2_write(int cpu, uint32_t a, uint32_t v, int size);

    // State maintenance
    void update_m68k_irq();
    void update_sh2_irq(int cpu);
    void remap_m68k();
    void remap_sh2();
    uint64_t m68k_now_mclk() const;
    uint64_t sh2_now_mclk(int cpu) const;
    int hpos_mclk() const;  // position in current line for 68K observers

private:
    void run_slice(uint64_t end_mclk);
    void start_line();
    void end_line();
    void boot_hle_start_sh2();
    void render_line32x(int ln, uint32_t* out);

    // 32X register helpers
    uint32_t mars_read(int who, uint32_t off, int size);    // who: -1 = 68K, 0/1 = SH-2
    void mars_write(int who, uint32_t off, uint32_t v, int size);
    uint32_t vdp32x_read(int who, uint32_t off);
    void vdp32x_write(uint32_t off, uint32_t v, int size);
    uint32_t pwm_read(uint32_t off);
    void pwm_write(uint32_t off, uint32_t v);
    void pwm_advance(uint32_t sh2_cycles);
    void fifo_push(uint16_t v);
    void fb_write(uint32_t off, uint32_t v, int size, bool overwrite);
    uint32_t fb_read(uint32_t off, int size);
    uint32_t pad_read(int port);

    // SH-2 on-chip peripherals
    uint32_t onchip_read(int cpu, uint32_t a, int size);
    void onchip_write(int cpu, uint32_t a, uint32_t v, int size);
    void onchip_advance(int cpu, uint32_t cycles);
    void dma_try(int cpu, int ch, bool dreq);
    void dreq_service();

    // Z80 / sound
    uint32_t z80_space_read(uint32_t a);
    void z80_space_write(uint32_t a, uint32_t v);
    void audio_advance(uint64_t end_mclk);
public:
    uint8_t z80_bus_read(uint16_t a);
    void z80_bus_write(uint16_t a, uint8_t v);
    void set_z80_reset(bool held);
private:

    // Per-instance bus glue and time-slice bookkeeping (no globals, so several
    // machines can run side by side for lockstep validation).
    Sh2BusCtx sh2_ctx_[2] = {};
    SliceInfo m68k_slice_, sh2_slice_[2];

    friend struct MachineAccess;
};

} // namespace chaotix
