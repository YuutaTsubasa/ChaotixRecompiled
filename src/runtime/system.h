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
#include "runtime/stage_select.h"
#include "runtime/vdp.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace chaotix {

class SpriteRecorder;
class CharacterPalettes;

constexpr int kMclkPerLine = 3420;
constexpr int kLinesPerFrameNTSC = 262;
constexpr int kActiveLines = 224;
constexpr int kSlicesPerLine = 4;
constexpr int kNativeWidth = 320;
constexpr int kScreenWidth = Vdp::kMaxWidth;  // framebuffer stride (native + widescreen margins)
constexpr int kScreenHeight = 224 + 16;  // active lines + widescreen rows (patches::kMaxWideExtraBottom)
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

// Optional wall-clock profile of the frame loop (enabled via Machine::profile).
enum ProfSection { PROF_M68K, PROF_Z80, PROF_MSH2, PROF_SSH2, PROF_VIDEO, PROF_AUDIO, PROF_COUNT };

struct FrameStats {
    uint64_t frame = 0;
    uint64_t m68k_cycles = 0, msh2_cycles = 0, ssh2_cycles = 0;
    uint64_t recomp_blocks = 0, interp_blocks = 0;
    uint64_t prof_ns[PROF_COUNT] = {};
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

// Save states (runtime/save_state.cpp, `transfer`) list the fields below that
// the emulation depends on: a new one belongs there too, or a loaded state
// will not carry on as the machine would have.
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
    uint32_t framebuffer[kScreenWidth * kScreenHeight];  // XRGB8888, stride kScreenWidth
    int fb_width = 320, fb_height = 224;
    int fb_extra = 0;    // widescreen columns on each side included in fb_width
    int fb_extra_bottom = 0;  // widescreen rows below, included in fb_height
    // Widescreen: extra columns rendered on each side of the native image
    // (0 = original 4:3). Non-zero values also enable the level-engine patch
    // (runtime/patches.h), which changes which tiles the game streams into
    // VRAM, so machines compared in lockstep must use the same value.
    int wide_extra = 0;
    int wide_extra_bottom = 0;  // extra rows below the screen (see patches.h)
    // Whether the player has widescreen turned on at all, as opposed to how
    // wide the margins currently are. The margins follow the window, which
    // changes while a level runs; the ring shift is latched at a level load
    // and must not, or resizing the window leaves that level in 4:3.
    bool wide_enabled = false;
    // The former WIDE LOBBY option (issue #21). It no longer changes anything
    // (runtime/patches.h, wide_zone: the lobby is widened without it, and the
    // INTRODUCTION Eggman scene stays 4:3); kept so that recordings, ghosts
    // and netplay settings that carry it still read.
    bool wide_everywhere = false;
    // Widened levels draw the HUD at the picture's left edge rather than at
    // its 4:3 place (issue #28; runtime/patches.h, kHudBaseX).
    bool wide_hud_edges = false;
    int debug_layer = 0;  // output only: 1 = the Mega Drive layer alone, 2 = the 32X layer alone
    int plane_shift = 0;               // W latched by the plane streaming patch
    uint64_t level_seen_frame = ~0ull; // last frame the level engine ran (patches.cpp)
    uint64_t full_redraw_frame = ~0ull; // last frame a scene redrew both planes
    uint64_t level_scene = 0;          // scene the level engine last ran in (patches.cpp)
    bool wide_active = false;          // margins show game content this frame
    bool clip_overridden = false;      // 32X clip rect widened by the patch
    // A scene drawn by the special stage's polygon code is the widescreen
    // scene this frame (wide_active is then also set): the stage, its results
    // screen or the bonus stage (poly_scene). Their polygons are filled, not
    // blitted: see patches.h.
    bool wide_special = false;
    bool special_rect_overridden = false;  // its polygon clip rect widened
    // Which polygon scene is widened (patches::PolyScene); wide_special is
    // set for all of them.
    uint8_t poly_scene = 0;
    bool results_table_patched = false;  // the results screen's polygons widened
    bool bonus_clip_overridden = false;  // the bonus stage's blitter clip widened
    // The bonus stage's rings by radius (Vdp::radial_lut); render only.
    uint8_t radial_lut[512 * 4] = {};
    int radial_len = 0;
    // WORLD ENTRANCE's margins (patches::lobby_margins, issue #61), worked
    // out at the start of every frame for its picture; render only. Columns
    // [lobby_alt_x0, lobby_alt_x1) of the widened line show the next part of
    // the lobby in its own colours (Vdp::alt_cram), and lobby_cut is black.
    uint16_t lobby_cram[32] = {};
    bool lobby_alt = false;
    int lobby_alt_x0 = 0, lobby_alt_x1 = 0;
    int lobby_cut_left = 0, lobby_cut_right = 0;
    int special_shift = 0;             // px its polygons are drawn right of true
    int cull_shift = 0;                // pending d2 offset inside the ring cull patch
    bool plane_fill_clamped = true;    // last plane fill used the unmasked (clamped) form
    // A stage the host has asked the game to start (TIME ATTACK). A game mode
    // runs its own loop, so this is applied by the patch hook at the mode
    // dispatcher, the one place the game passes through between scenes.
    stage_select::Request stage_request;
    bool stage_pending = false;
    // A game mode ($FFDFDE) and sub-mode ($FFDFE0) the host wants, applied by
    // the same hook once the boot scene is over; -1 for none. Only the music
    // player's own machine sets it (frontend/jukebox.h: the game's OPTIONS
    // screen), and that machine is never played or saved, so save states
    // leave it out.
    int mode_request = -1;
    int submode_request = 0;
    // Lobby shortcuts (issue #44, runtime/lobby.h): the partner (character
    // id) and time of day (0-3) the player chose, put into the game by the
    // same hook each time it starts the lobby or an attraction; -1 leaves
    // the game's own, and so does lobby::kPartnerNone (-2, NONE: the front
    // end plays the attractions SOLO, issue #55). A setting like the
    // widescreen ones: machines compared in lockstep must have the same
    // values.
    int lobby_partner = -1;
    int lobby_time = -1;
    // Ring tether options (issue #43, runtime/tether.h): tether::Strength
    // and tether::Partner, 0 the original game. Settings like the lobby
    // shortcuts': the app gives them again before every frame, and machines
    // compared in lockstep must have the same values.
    uint8_t tether_strength = 0;
    uint8_t tether_partner = 0;
    // Latched by the SOLO partner's hook: CALL brought the partner back, so
    // it stays until the next scene. In save states.
    bool tether_called = false;
    // FOLLOW's note of where the player stood before the game put one of the
    // two back on the tether's circle; used and dropped by the next
    // instruction's hook, within the same routine, so never in a save state.
    bool tether_clamp_saved = false;
    uint32_t tether_clamp_x = 0, tether_clamp_y = 0;
    // Music replaced by files (audio/music_mods.h): which music ids the host
    // plays itself, and the music requests and commands the game has made
    // since the host last drained them (patches.h, kSoundRequest).
    std::array<bool, 0x2A> music_replaced{};
    std::vector<uint8_t> music_events;
    // Host-side level of the 32X PWM output (see Ym2612::gain).
    float pwm_gain = 1.0f;
    // Host-side shadow of the 32X frame buffers for widescreen margins: while
    // wide_active, sprite (overwrite image) writes outside native columns land
    // here instead of in the line padding, which the game uses as storage.
    // Render-only; never read by the emulated CPUs.
    uint8_t fb_margin[2][0x20000] = {};
    // Tooling hooks: writes to the 32X draw buffer (offset, value, size,
    // overwrite image) and auto fills (word address, word count, data).
    void (*on_fb_write)(void* user, uint32_t off, uint32_t v, int size, bool overwrite) = nullptr;
    void (*on_fb_fill)(void* user, uint32_t word_addr, uint32_t words, uint16_t data) = nullptr;
    void* on_fb_write_user = nullptr;
    // 32X sprite recording (runtime/sprite_record.h): the blitters' calls as
    // draw lists, and with a sprite pack (runtime/sprite_replace.h) the 32X
    // layer composed again with replacement graphics, which render_line32x
    // shows. Output only, like the tile replacer: its SH-2 hooks write
    // nothing the machine sees. Copies of a machine share it.
    std::shared_ptr<SpriteRecorder> sprites;
    // Character mods' colours (runtime/character_mods.h): the 32X palette
    // render_line32x shows, with modded characters' entries substituted.
    // Output only: CRAM itself is never changed. Copies of a machine share it.
    std::shared_ptr<CharacterPalettes> characters;
    // Output only: the palette render_line32x last encoded, for the priority
    // bit in enc32x_pri (-1: none yet), and its encoded entries.
    uint16_t enc32x_pal[256] = {};
    uint32_t enc32x[256] = {};
    int enc32x_pri = -1;

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
    bool profile = false;

    // Self-modification tracking for RAM-resident recompiled SH-2 code.
    // Ranges are byte offsets into SDRAM / the cache data array.
    std::vector<std::pair<uint32_t, uint32_t>> sdram_code_ranges, cache_code_ranges;
    // The same as one bit per byte, made by code_ranges_changed(): a write
    // checks the bytes it covers instead of every range (issue #62).
    std::vector<uint8_t> sdram_code_bits, cache_code_bits;
    void code_ranges_changed();
    // Moved on when SDRAM may have changed under the recompiled code as a
    // whole (a state loaded, the machine replaced; also when a count in
    // sh2_fn_code_writes wraps): every function is checked again.
    uint32_t sdram_code_epoch = 1;
    // Moved on by a write that changes a byte of cache-resident code.
    uint32_t cache_code_epoch[2] = {1, 1};
    // Writes over SDRAM code, per recompiled function (issue #62). Each
    // function's validation ranges in SDRAM ([lo, hi) offsets, `fn` its
    // number) are its "owners"; a write that changes one of its bytes counts
    // in sh2_fn_code_writes[fn]. A function only needs checking again, and
    // running code only needs to hand over to the interpreter, when its own
    // count moved: writing data next to another routine's code (the special
    // stage's routines at $06005xxx hold data in a level) used to send every
    // function to be checked again. The SH-2 buses point at the counts
    // (sh2::Bus::code_writes) for the generated code.
    struct CodeOwner { uint32_t lo, hi, fn; };
    void set_sdram_code_owners(const std::vector<CodeOwner>& owners, size_t functions);
    std::vector<uint32_t> sh2_fn_code_writes;

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
    // For the polling-loop fast-forward (recomp_api.h, issue #62): the lowest
    // remaining cycle count (sh2[cpu].cycles) down to which a `size`-byte read
    // of `a` returns, without side effects, what it returns when made at
    // remaining count `at`, in this CPU's current slice. INT32_MIN when that
    // holds for the whole slice; `at` itself when it cannot tell.
    int32_t sh2_read_stable_to(int cpu, uint32_t a, int size, int32_t at) const;
    SliceInfo& sh2_slice_for_tests(int cpu) { return sh2_slice_[cpu]; }
    // Renders a 32X line of kNativeWidth + 2 * extra pixels. With wide_src the
    // margins come from the frame buffer bytes around the line (packed pixel
    // mode; see patches.h), otherwise they are transparent. With md_top (the
    // Mega Drive line as Vdp::render_line_indexed gives it, as wide) and its
    // colours md_rgb, the pixels are written merged with it instead. Public
    // for tests.
    void render_line32x(int ln, uint32_t* out, int extra, bool wide_src, const uint8_t* md_top = nullptr,
                        const uint32_t* md_rgb = nullptr);

private:
    void run_slice(uint64_t end_mclk);
    void start_line();
    void end_line();
    void boot_hle_start_sh2();
    void render_output_line(int src_line, int dst_row);

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
    void fb_write_wide(uint8_t* fb, uint32_t off, uint32_t v, int size);
    void fb_write_special(uint8_t* fb, uint32_t off, uint32_t v, int size, bool overwrite);
    static bool fb_in_margin(uint32_t off);
    static bool fb_in_picture(uint32_t off);
    void fb_margin_clear_line(uint32_t line_off, uint16_t fill);
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
    void set_z80_busreq(bool requested);
    // Runs the Z80 (unless it is held in reset or stopped by a bus request)
    // up to the given master-clock time. Called at the end of every slice
    // and, before the 68K changes BUSREQ or RESET, at the 68K's current time,
    // so the Z80 runs exactly until the 68K stops it (or from when it starts
    // it), as on the hardware.
    void z80_sync(uint64_t now_mclk);
    void z80_catch_up();
private:

    // Per-instance bus glue and time-slice bookkeeping (no globals, so several
    // machines can run side by side for lockstep validation).
    Sh2BusCtx sh2_ctx_[2] = {};
    SliceInfo m68k_slice_, sh2_slice_[2];

    // The owners (above) by 256-byte chunk of SDRAM: chunk k's are
    // code_owners_[code_owner_at_[k] .. code_owner_at_[k + 1]).
    static constexpr int kCodeChunkShift = 8;
    std::vector<CodeOwner> code_owners_;
    std::vector<uint32_t> code_owner_at_;
    void sdram_code_written(uint32_t off, int size);

    friend struct MachineAccess;
};

} // namespace chaotix
