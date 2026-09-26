// Headless runner: executes the game deterministically without a window.
// Used for automated validation (screenshots, state hashes, coverage traces).
//
//   chaotix_headless --rom <file> --frames N [--shot F1,F2,...] [--out dir]
//                    [--press FRAME:BUTTON[+BUTTON]:DURATION ...] [--state-every N]
//                    [--interp]   (force interpreter even if generated code exists)
#include "cpu/m68k/m68k_interp.h"
#include "cpu/m68k/m68k_ops.h"
#include "cpu/sh2/sh2_interp.h"
#include "cpu/sh2/sh2_ops.h"
#include "game/achievements.h"
#include "game/recomp_dispatch.h"
#include "renderer/image_io.h"
#include "runtime/log.h"
#include "runtime/patches.h"
#include "frontend/time_attack.h"
#include "runtime/system.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace chaotix;

namespace {

struct Press { uint64_t frame; uint16_t buttons; uint64_t duration; };

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t p = s.find(sep, start);
        out.push_back(s.substr(start, p == std::string::npos ? std::string::npos : p - start));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return out;
}

// Instruction-level tracing executors (interpreter semantics + printing).
FILE* g_trace = nullptr;
int g_trace_cpu = -1;  // 0 = 68K, 1 = MSH2, 2 = SSH2
bool g_trace_on = false;
uint64_t g_trace_lines = 0, g_trace_max = 2000000;

void trace_sh2_block(sh2::State* c) {
    if (!g_trace_on || g_trace_cpu != 1 + c->id || g_trace_lines > g_trace_max) { sh2::interp_block(c); return; }
    for (;;) {
        uint32_t pc = c->pc;
        sh2::Insn in = sh2::decode(pc, uint16_t(sh2::rd16(c, pc)));
        std::fprintf(g_trace, "S%d %08X %-28s r0=%08X r1=%08X r2=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X r14=%08X r15=%08X sr=%03X\n",
                     c->id, pc, sh2::disassemble(in).c_str(), c->r[0], c->r[1], c->r[2], c->r[3], c->r[4], c->r[5], c->r[6], c->r[7],
                     c->r[8], c->r[14], c->r[15], c->sr);
        ++g_trace_lines;
        c->cycles -= in.cycles;
        if (in.flags & sh2::IF_BRANCH) {
            if (in.flags & sh2::IF_DELAYED) {
                sh2::Insn slot = sh2::decode(pc + 2, uint16_t(sh2::rd16(c, pc + 2)));
                std::fprintf(g_trace, "S%d %08X   (slot) %s\n", c->id, pc + 2, sh2::disassemble(slot).c_str());
            }
            sh2::exec_branch(c, in);
            return;
        }
        c->pc = pc + 2;
        sh2::exec_simple(c, in);
        if (sh2::ends_block(in)) return;
    }
}

void trace_m68k_block(m68k::State* c) {
    if (!g_trace_on || g_trace_cpu != 0 || g_trace_lines > g_trace_max) { m68k::interp_block(c); return; }
    for (;;) {
        m68k::Insn in;
        m68k::decode(c->pc, m68k::fetch_bus, c, in);
        std::fprintf(g_trace, "M %06X %-34s d0=%08X d1=%08X d2=%08X d3=%08X d4=%08X a0=%08X a1=%08X a2=%08X a6=%08X sp=%08X ccr=%02X\n",
                     c->pc, m68k::disassemble(in).c_str(), c->d[0], c->d[1], c->d[2], c->d[3], c->d[4], c->a[0], c->a[1], c->a[2],
                     c->a[6], c->a[7], m68k::get_ccr(c));
        ++g_trace_lines;
        c->pc = c->pc + in.len;
        c->cycles -= in.cycles;
        m68k::execute(c, in);
        if (m68k::ends_block(in)) break;
    }
}

// Compares two machines; returns an empty string if identical.
std::string compare_machines(const Machine& a, const Machine& b) {
    char buf[256];
    auto mem = [&](const char* name, const void* x, const void* y, size_t n) -> std::string {
        const uint8_t* p = static_cast<const uint8_t*>(x);
        const uint8_t* q = static_cast<const uint8_t*>(y);
        for (size_t i = 0; i < n; ++i)
            if (p[i] != q[i]) {
                std::snprintf(buf, sizeof buf, "%s differs at +0x%zX: %02X vs %02X", name, i, p[i], q[i]);
                return buf;
            }
        return "";
    };
    const auto& ma = a.m68k;
    const auto& mb = b.m68k;
    for (int i = 0; i < 8; ++i) {
        if (ma.d[i] != mb.d[i]) { std::snprintf(buf, sizeof buf, "68K d%d %08X vs %08X", i, ma.d[i], mb.d[i]); return buf; }
        if (ma.a[i] != mb.a[i]) { std::snprintf(buf, sizeof buf, "68K a%d %08X vs %08X", i, ma.a[i], mb.a[i]); return buf; }
    }
    if (ma.pc != mb.pc || m68k::get_sr(&ma) != m68k::get_sr(&mb) || ma.other_sp != mb.other_sp) {
        std::snprintf(buf, sizeof buf, "68K pc/sr %06X/%04X vs %06X/%04X", ma.pc, m68k::get_sr(&ma), mb.pc, m68k::get_sr(&mb));
        return buf;
    }
    if (a.m68k_clock != b.m68k_clock) { std::snprintf(buf, sizeof buf, "68K clock %llu vs %llu", (unsigned long long)a.m68k_clock, (unsigned long long)b.m68k_clock); return buf; }
    for (int c = 0; c < 2; ++c) {
        const auto& sa = a.sh2[c];
        const auto& sb = b.sh2[c];
        for (int i = 0; i < 16; ++i)
            if (sa.r[i] != sb.r[i]) { std::snprintf(buf, sizeof buf, "SH2%d r%d %08X vs %08X", c, i, sa.r[i], sb.r[i]); return buf; }
        if (sa.pc != sb.pc || sa.sr != sb.sr || sa.pr != sb.pr || sa.gbr != sb.gbr || sa.vbr != sb.vbr || sa.mach != sb.mach || sa.macl != sb.macl) {
            std::snprintf(buf, sizeof buf, "SH2%d pc/sr/pr %08X/%03X/%08X vs %08X/%03X/%08X", c, sa.pc, sa.sr, sa.pr, sb.pc, sb.sr, sb.pr);
            return buf;
        }
        if (a.sh2_clock[c] != b.sh2_clock[c]) { std::snprintf(buf, sizeof buf, "SH2%d clock %llu vs %llu", c, (unsigned long long)a.sh2_clock[c], (unsigned long long)b.sh2_clock[c]); return buf; }
        std::string r = mem(c ? "SSH2 cache RAM" : "MSH2 cache RAM", a.onchip[c].cache_data, b.onchip[c].cache_data, sizeof a.onchip[c].cache_data);
        if (!r.empty()) return r;
    }
    std::string r;
    if (!(r = mem("68K work RAM", a.wram, b.wram, sizeof a.wram)).empty()) return r;
    if (!(r = mem("SDRAM", a.sdram, b.sdram, sizeof a.sdram)).empty()) return r;
    if (!(r = mem("Z80 RAM", a.zram, b.zram, sizeof a.zram)).empty()) return r;
    if (!(r = mem("VRAM", a.vdp.vram, b.vdp.vram, sizeof a.vdp.vram)).empty()) return r;
    if (!(r = mem("CRAM", a.vdp.cram, b.vdp.cram, sizeof a.vdp.cram)).empty()) return r;
    if (!(r = mem("VSRAM", a.vdp.vsram, b.vdp.vsram, sizeof a.vdp.vsram)).empty()) return r;
    if (!(r = mem("VDP regs", a.vdp.reg, b.vdp.reg, sizeof a.vdp.reg)).empty()) return r;
    if (!(r = mem("32X FB0", a.mars.fb[0], b.mars.fb[0], sizeof a.mars.fb[0])).empty()) return r;
    if (!(r = mem("32X FB1", a.mars.fb[1], b.mars.fb[1], sizeof a.mars.fb[1])).empty()) return r;
    if (!(r = mem("32X palette", a.mars.pal, b.mars.pal, sizeof a.mars.pal)).empty()) return r;
    if (!(r = mem("32X comm", a.mars.comm, b.mars.comm, sizeof a.mars.comm)).empty()) return r;
    if (!(r = mem("output image", a.framebuffer, b.framebuffer, sizeof a.framebuffer)).empty()) return r;
    return "";
}

void dump_state(const Machine& m) {
    std::printf("frame %llu | 68K pc=%06X sr=%04X sp=%08X | MSH2 pc=%08X sr=%03X | SSH2 pc=%08X sr=%03X | FS=%d mode=%X\n",
                (unsigned long long)m.frame_count, m.m68k.pc, m68k::get_sr(&m.m68k), m.m68k.a[7],
                m.sh2[0].pc, m.sh2[0].sr, m.sh2[1].pc, m.sh2[1].sr, m.mars.fb_display, m.mars.bitmap_mode);
    std::printf("  comm: %04X %04X %04X %04X %04X %04X %04X %04X\n", m.mars.comm[0], m.mars.comm[1], m.mars.comm[2],
                m.mars.comm[3], m.mars.comm[4], m.mars.comm[5], m.mars.comm[6], m.mars.comm[7]);
}

} // namespace

int main(int argc, char** argv) {
    std::string rom_path, out_dir = ".";
    uint64_t frames = 600;
    std::set<uint64_t> shots;
    std::map<uint64_t, uint64_t> expect_hash;  // frame -> expected image hash
    int hash_failures = 0;
    std::vector<Press> presses;
    uint64_t state_every = 0, hash_every = 0;
    bool force_interp = false;
    bool lockstep = false;
    bool profile = false;
    int wide = 0, wide_bottom = 0;
    // --compare-native: run a 4:3 machine alongside the widescreen one and
    // require the centre 320 px to be pixel-identical. Comparison stops when
    // the cameras legitimately differ (the widescreen camera clamp keeps the
    // margins inside the level near its edges).
    bool compare_native = false;
    std::string achievements_file;  // --achievements FILE: report unlocks
    uint64_t min_centre_frames = 0;  // fail if fewer frames could be compared
    std::string wav_path;
    std::string coverage_path;
    int break_cpu = -1;
    uint64_t trace_from = 0, trace_to = 0;
    uint32_t break_pc = 0;
    // --stage FRAME:PLACE:LEVEL:TIME:PLAYER:COMBI:PLAYERS -- ask the game to
    // start a stage the way the front end's TIME ATTACK does.
    uint64_t stage_frame = 0;
    bool stage_set = false;
    stage_select::Request stage;
    // -1 leaves the machine's own default ({true, false}); a replay or the
    // command line can say otherwise. Setting it changes how the game reads
    // the pad, which changes the run.
    int six_button = -1;
    std::string sram_path;            // --sram FILE: the save the session ran with
    bool want_audio = false;          // --audio: the frontend runs with it on
    int stage_kick = -1;              // frames since the request, while it waits
    time_attack::Run ta;              // the same run tracking the frontend uses
    // --replay-input FILE: a session recorded by the frontend, so that what a
    // person did can be studied here, deterministically, as often as needed.
    std::vector<uint32_t> replay;
    struct ReplayStage {
        stage_select::Request request;
        int wide = 0, wide_bottom = 0;
        bool six_button = false;
        bool has_video = false;   // older recordings do not carry these
    };
    std::map<uint64_t, ReplayStage> replay_stages;
    int replay_result = -1;           // what the session it came from produced
    uint64_t stage_started = 0;       // the frame the game took it
    bool stage_pending_last = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--rom") rom_path = next();
        else if (a == "--frames") frames = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--out") out_dir = next();
        else if (a == "--shot") { for (auto& s : split(next(), ',')) shots.insert(std::strtoull(s.c_str(), nullptr, 10)); }
        else if (a == "--press") {
            auto parts = split(next(), ':');
            if (parts.size() < 2) { std::fprintf(stderr, "bad --press\n"); return 2; }
            Press p{std::strtoull(parts[0].c_str(), nullptr, 10), 0, parts.size() > 2 ? std::strtoull(parts[2].c_str(), nullptr, 10) : 4};
            for (auto& b : split(parts[1], '+')) p.buttons |= pad_button_from_name(b.c_str());
            presses.push_back(p);
        }
        else if (a == "--script") {
            // Lines: FRAME BUTTON[+BUTTON] [DURATION]   ('#' starts a comment)
            FILE* sf = std::fopen(next().c_str(), "r");
            if (!sf) { std::fprintf(stderr, "cannot open script\n"); return 2; }
            char line[256];
            while (std::fgets(line, sizeof line, sf)) {
                char btns[128] = {};
                unsigned long long fr = 0, dur = 4;
                if (line[0] == '#' || std::sscanf(line, "%llu %127s %llu", &fr, btns, &dur) < 2) continue;
                Press p{fr, 0, dur};
                for (auto& b : split(btns, '+')) p.buttons |= pad_button_from_name(b.c_str());
                presses.push_back(p);
            }
            std::fclose(sf);
        }
        else if (a == "--fuzz") {
            // SEED:FROM:TO — deterministic pseudo-random play (no Start/Mode),
            // biased towards moving right, used to widen code coverage.
            auto parts = split(next(), ':');
            uint32_t seed = uint32_t(std::strtoul(parts[0].c_str(), nullptr, 10));
            uint64_t from = std::strtoull(parts[1].c_str(), nullptr, 10), to = std::strtoull(parts[2].c_str(), nullptr, 10);
            auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
            static const uint16_t dirs[] = {PAD_RIGHT, PAD_RIGHT, PAD_RIGHT, PAD_LEFT, PAD_DOWN, PAD_UP, 0, PAD_RIGHT | PAD_DOWN};
            static const uint16_t acts[] = {PAD_A, PAD_B, PAD_C, PAD_C, 0, 0, PAD_X, PAD_Y, PAD_Z};
            for (uint64_t f = from; f < to;) {
                uint64_t len = 20 + rnd() % 90;
                presses.push_back({f, dirs[rnd() % 8], len});
                for (uint64_t k = f; k < f + len; k += 10 + rnd() % 30) presses.push_back({k, acts[rnd() % 9], 1 + rnd() % 25});
                f += len;
            }
        }
        else if (a == "--state-every") state_every = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--hash-every") hash_every = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--expect-hashes") {
            // File of "hash FRAME HASH" lines (as printed by --hash-every).
            FILE* hf = std::fopen(next().c_str(), "r");
            if (!hf) { std::fprintf(stderr, "cannot open hash file\n"); return 2; }
            unsigned long long fr, h;
            while (std::fscanf(hf, " hash %llu %llx", &fr, &h) == 2) expect_hash[fr] = h;
            std::fclose(hf);
        }
        else if (a == "--interp") force_interp = true;
        else if (a == "--lockstep") lockstep = true;
        else if (a == "--profile") profile = true;
        else if (a == "--wide") wide = std::atoi(next().c_str());
        else if (a == "--wide-bottom") wide_bottom = std::atoi(next().c_str());
        else if (a == "--compare-native") compare_native = true;
        else if (a == "--achievements" && i + 1 < argc) achievements_file = next();
        else if (a == "--min-centre-frames") min_centre_frames = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--wav") wav_path = next();
        else if (a == "--expect-hash") {
            auto parts = split(next(), ':');
            if (parts.size() == 2) expect_hash[std::strtoull(parts[0].c_str(), nullptr, 10)] = std::strtoull(parts[1].c_str(), nullptr, 16);
        }
        else if (a == "--coverage") coverage_path = next();
        else if (a == "-v") log_set_level(LogLevel::Debug);
        else if (a == "--trace") {
            // --trace CPU:FROM_FRAME:TO_FRAME:FILE
            auto parts = split(next(), ':');
            g_trace_cpu = std::atoi(parts[0].c_str());
            trace_from = std::strtoull(parts[1].c_str(), nullptr, 10);
            trace_to = std::strtoull(parts[2].c_str(), nullptr, 10);
            g_trace = std::fopen(parts.size() > 3 ? parts[3].c_str() : "trace.txt", "w");
        }
        else if (a == "--break") {
            auto parts = split(next(), ':');
            break_cpu = std::atoi(parts[0].c_str());
            break_pc = uint32_t(std::strtoul(parts[1].c_str(), nullptr, 16));
        }
        else if (a == "--six-button") six_button = 1;
        else if (a == "--three-button") six_button = 0;
        else if (a == "--audio") { want_audio = true; }
        else if (a == "--sram") sram_path = next();
        else if (a == "--replay-input") {
            const std::string path = next();
            FILE* rf = std::fopen(path.c_str(), "r");
            if (!rf) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); return 2; }
            char line[256];
            while (std::fgets(line, sizeof line, rf)) {
                if (!std::strncmp(line, "chaotix-input", 13)) continue;
                if (!std::strncmp(line, "result ", 7)) {
                    replay_result = std::atoi(line + 7);
                    continue;
                }
                unsigned long long fr = 0;
                unsigned pl = 0, lv = 0, at = 0, py = 0, cb = 0;
                int two = 0, wd = 0, wdb = 0, six = 0;
                if (std::sscanf(line, "stage %llu %u %u %u %u %u %d %d %d %d", &fr, &pl, &lv, &at,
                                &py, &cb, &two, &wd, &wdb, &six) >= 7) {
                    ReplayStage q;
                    q.request.place = uint16_t(pl);
                    q.request.level = uint16_t(lv);
                    q.request.attime = uint16_t(at);
                    q.request.player = uint16_t(py);
                    q.request.combi = uint16_t(cb);
                    q.request.two_players = two != 0;
                    q.wide = wd;
                    q.wide_bottom = wdb;
                    q.six_button = six != 0;
                    q.has_video = std::sscanf(line, "stage %llu %u %u %u %u %u %d %d %d %d", &fr,
                                              &pl, &lv, &at, &py, &cb, &two, &wd, &wdb, &six) == 10;
                    replay_stages[fr] = q;
                    continue;
                }
                replay.push_back(uint32_t(std::strtoul(line, nullptr, 16)));
            }
            std::fclose(rf);
            std::printf("replay: %zu frames, %zu stage requests\n", replay.size(),
                        replay_stages.size());
        }
        else if (a == "--stage") {
            auto parts = split(next(), ':');
            if (parts.size() < 7) { std::fprintf(stderr, "bad --stage\n"); return 2; }
            stage_frame = std::strtoull(parts[0].c_str(), nullptr, 10);
            stage.place = uint16_t(std::atoi(parts[1].c_str()));
            stage.level = uint16_t(std::atoi(parts[2].c_str()));
            stage.attime = uint16_t(std::atoi(parts[3].c_str()));
            stage.player = uint16_t(std::atoi(parts[4].c_str()));
            stage.combi = uint16_t(std::atoi(parts[5].c_str()));
            stage.two_players = std::atoi(parts[6].c_str()) != 0;
            stage_set = true;
        }
        else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
    }
    if (rom_path.empty()) { std::fprintf(stderr, "usage: chaotix_headless --rom <file> [--frames N] ...\n"); return 2; }

    auto m = std::make_unique<Machine>();
    std::string err;
    if (!m->load_rom(rom_path, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    m->reset();
    m->audio_enabled = !wav_path.empty() || want_audio;
    m->profile = profile;
    m->wide_extra = wide;
    m->wide_extra_bottom = wide_bottom;
    if (six_button >= 0) m->input.six_button[0] = m->input.six_button[1] = six_button != 0;
    if (!sram_path.empty()) {
        // Cartridge saves change what the game does, so a replay of somebody
        // else's session needs theirs.
        if (FILE* sf = std::fopen(sram_path.c_str(), "rb")) {
            const size_t n = std::fread(m->sram, 1, sizeof m->sram, sf);
            std::fclose(sf);
            std::printf("loaded %zu bytes of cartridge SRAM\n", n);
        } else {
            std::fprintf(stderr, "cannot open %s\n", sram_path.c_str());
            return 2;
        }
    }
    std::vector<int16_t> wav;
    RecompStatus rs = install_recompiled_code(*m, !force_interp);
    std::printf("execution: %s\n", rs.description.c_str());
    // Lockstep validation: an interpreter-only reference machine runs next to
    // the recompiled one and full state is compared after every frame.
    std::unique_ptr<Machine> ref;
    if (lockstep) {
        ref = std::make_unique<Machine>();
        ref->load_rom(rom_path, &err);
        ref->reset();
        ref->wide_extra = wide;
        ref->wide_extra_bottom = wide_bottom;
        if (!rs.active) std::printf("warning: --lockstep without generated code compares the interpreter with itself\n");
    }
    // The 4:3 comparison machine runs on the interpreter: the recompiled
    // code's SH-2 validation state is process-wide, so only one machine at a
    // time may use it (same reason the lockstep reference is interpreted).
    std::unique_ptr<Machine> nat;
    bool centre_compare = false;
    if (compare_native && (wide > 0 || wide_bottom > 0)) {
        nat = std::make_unique<Machine>();
        nat->load_rom(rom_path, &err);
        nat->reset();
        centre_compare = true;
    }
    uint64_t centre_ok_frames = 0;
    uint64_t lockstep_ok_frames = 0;
    CoverageRecorder cov;
    if (!coverage_path.empty()) cov.attach(*m);
    // Block-level history ring for --break (per CPU).
    static uint32_t hist[3][64];
    static unsigned hpos[3];
    bool broke = false;
    if (break_cpu >= 0) {
        Machine* mp = m.get();
        m->on_block = [&, mp](int cpu, uint32_t pc) {
            hist[cpu][hpos[cpu]++ & 63] = pc;
            if (!broke && cpu == break_cpu && pc == break_pc) {
                broke = true;
                std::printf("BREAK cpu %d pc %08X at frame %llu line %d\n", cpu, pc, (unsigned long long)mp->frame_count, mp->line);
                for (int c = 0; c < 3; ++c) {
                    std::printf("  cpu %d history:", c);
                    for (unsigned k = 0; k < 64; ++k) std::printf(" %X", hist[c][(hpos[c] + k) & 63]);
                    std::printf("\n");
                }
                const auto& r = mp->m68k;
                std::printf("  68K d: %08X %08X %08X %08X %08X %08X %08X %08X\n", r.d[0], r.d[1], r.d[2], r.d[3], r.d[4], r.d[5], r.d[6], r.d[7]);
                std::printf("  68K a: %08X %08X %08X %08X %08X %08X %08X %08X\n", r.a[0], r.a[1], r.a[2], r.a[3], r.a[4], r.a[5], r.a[6], r.a[7]);
                std::printf("  68K stack:");
                for (int k = 0; k < 8; ++k) std::printf(" %04X", mp->m68k_read16(r.a[7] + 2 * k));
                std::printf("\n");
                for (int c = 0; c < 2; ++c) {
                    const auto& s = mp->sh2[c];
                    std::printf("  SH2%d r:", c);
                    for (int k = 0; k < 16; ++k) std::printf(" %08X", s.r[k]);
                    std::printf(" pr=%08X vbr=%08X sr=%03X irq=%d/%d mask=%02X pend=%02X\n", s.pr, s.vbr, s.sr, s.irq_level,
                                s.irq_vector, mp->mars.sh_int_mask[c], mp->mars.irq_pending[c]);
                }
            }
        };
    }

    if (g_trace) {
        m->exec.m68k_block = trace_m68k_block;
        m->exec.sh2_block = trace_sh2_block;
    }
    achievements::Tracker tracker;
    if (!achievements_file.empty()) {
        std::string aerr;
        if (!tracker.load_definitions(achievements_file, &aerr)) {
            std::fprintf(stderr, "achievements: %s\n", aerr.c_str());
            return 2;
        }
        std::printf("achievements: %zu definitions, %d points\n", tracker.list().size(), tracker.points_total());
    }
    auto t0 = std::chrono::steady_clock::now();
    for (uint64_t f = 0; f < frames; ++f) {
        uint16_t btn = 0;
        for (const auto& p : presses)
            if (f >= p.frame && f < p.frame + p.duration) btn |= p.buttons;
        if (stage_set && f == stage_frame) {
            // Both machines, so lockstep still compares like with like.
            m->stage_request = stage;
            m->stage_pending = true;
            if (ref) { ref->stage_request = stage; ref->stage_pending = true; }
            stage_kick = 0;
            ta.begin(stage);
        }
        // The same taps of Start the frontend gives a waiting request: the
        // title screen needs one press to get past its animation and another
        // to leave, and only then does the game come back to the dispatcher
        // where the request is taken.
        // However the request got in, note the frame the game took it.
        if (stage_set && stage_pending_last && !m->stage_pending) stage_started = f;
        stage_pending_last = m->stage_pending;
        if (stage_kick >= 0) {
            if (!m->stage_pending) {
                stage_kick = -1;
            } else {
                if (stage_kick % 24 < 6) btn |= PAD_START;
                ++stage_kick;
            }
        }
        uint16_t btn2 = 0;
        if (!replay.empty()) {
            // A recorded session drives everything: its pads replace the
            // scripted ones, and its stage requests are applied where they
            // were made.
            auto it = replay_stages.find(f);
            if (it != replay_stages.end()) {
                const ReplayStage& q = it->second;
                // The same conditions the run was played under, or it
                // diverges. A recording from before these were kept leaves
                // whatever the command line asked for.
                if (q.has_video) {
                    m->wide_extra = q.wide;
                    m->wide_extra_bottom = q.wide_bottom;
                    m->input.six_button[0] = m->input.six_button[1] = q.six_button;
                }
                m->stage_request = q.request;
                m->stage_pending = true;
                if (ref) {
                    if (q.has_video) {
                        ref->wide_extra = q.wide;
                        ref->wide_extra_bottom = q.wide_bottom;
                        ref->input.six_button[0] = ref->input.six_button[1] = q.six_button;
                    }
                    ref->stage_request = q.request;
                    ref->stage_pending = true;
                }
                ta.begin(q.request);
                stage_set = true;
                stage_frame = f;
            }
            if (f < replay.size()) {
                btn = uint16_t(replay[size_t(f)] & 0xFFFF);
                btn2 = uint16_t(replay[size_t(f)] >> 16);
            }
        }
        m->input.pad[0] = btn;
        m->input.pad[1] = btn2;
        if (ref) { ref->input.pad[0] = btn; ref->input.pad[1] = btn2; }
        g_trace_on = g_trace && f >= trace_from && f < trace_to;
        m->run_frame();
        if (ta.update(*m)) {
            if (!ta.timed())
                std::printf("stage: the run ended without a time (this level keeps no clock)\n");
            else
                std::printf("stage: the run ended after %d frames (%s)%s\n", ta.time,
                            stage_select::format_time(ta.time).c_str(),
                            ta.timed_out() ? " - the level's own limit" : "");
            if (replay_result >= 0)
                std::printf("replay: %s -- the session this came from ended at %d frames\n",
                            ta.time == replay_result ? "faithful" : "DRIFTED", replay_result);
        }
        // The running clock, so a screenshot at the same frame can be held
        // against what the game's own HUD says.
        if (ta.running && ta.started && shots.count(f))
            std::printf("stage: at frame %llu the run clock reads %s (%d frames)\n",
                        (unsigned long long)f, stage_select::format_time(ta.time).c_str(), ta.time);
        if (m->audio_enabled) { wav.insert(wav.end(), m->audio_out.begin(), m->audio_out.end()); m->audio_out.clear(); }
        if (ref) {
            ref->run_frame();
            std::string diff = compare_machines(*ref, *m);
            if (!diff.empty()) {
                std::printf("LOCKSTEP DIVERGENCE at frame %llu (reference vs recompiled): %s\n", (unsigned long long)m->frame_count, diff.c_str());
                dump_state(*ref);
                dump_state(*m);
                return 3;
            }
            ++lockstep_ok_frames;
        }
        if (nat && centre_compare) {
            nat->input.pad[0] = btn;
            nat->run_frame();
            if (!m->wide_active) { /* not a widescreen scene */ }
            else if (patches::camera_x(*nat) != patches::camera_x(*m) || patches::camera_y(*nat) != patches::camera_y(*m)) {
                // The widescreen camera clamp keeps the margins inside the
                // level near its edges, so the runs stop being comparable.
                std::printf("centre check: cameras diverge at frame %llu (level edge); %llu frames verified\n",
                            (unsigned long long)m->frame_count, (unsigned long long)centre_ok_frames);
                centre_compare = false;
            } else {
                for (int y = 0; y < nat->fb_height; ++y)
                    for (int x = 0; x < nat->fb_width; ++x)
                        if (nat->framebuffer[y * kScreenWidth + x] != m->framebuffer[y * kScreenWidth + x + m->fb_extra]) {
                            std::printf("CENTRE MISMATCH at frame %llu, native pixel (%d,%d): widescreen rendering changed the 4:3 image\n",
                                        (unsigned long long)m->frame_count, x, y);
                            return 4;
                        }
                ++centre_ok_frames;
            }
        }
        if (!achievements_file.empty())
            tracker.update(*m, [&](const achievements::Achievement& a) {
                std::printf("frame %llu: unlocked %s (%d points) - %s\n", (unsigned long long)m->frame_count,
                            a.title.c_str(), a.points, a.description.c_str());
            });
        if (shots.count(m->frame_count)) {
            char name[512];
            std::snprintf(name, sizeof name, "%s/frame_%05llu.png", out_dir.c_str(), (unsigned long long)m->frame_count);
            write_png(name, m->framebuffer, m->fb_width, m->fb_height, kScreenWidth);
            std::printf("wrote %s hash=%016llx\n", name,
                        (unsigned long long)image_hash(m->framebuffer, m->fb_width, m->fb_height, kScreenWidth));
        }
        if (expect_hash.count(m->frame_count)) {
            uint64_t h = image_hash(m->framebuffer, m->fb_width, m->fb_height, kScreenWidth);
            bool ok = h == expect_hash[m->frame_count];
            std::printf("frame %llu image hash %016llx %s\n", (unsigned long long)m->frame_count, (unsigned long long)h,
                        ok ? "matches golden" : "DOES NOT MATCH golden");
            if (!ok) ++hash_failures;
        }
        if (hash_every && m->frame_count % hash_every == 0)
            std::printf("hash %llu %016llx\n", (unsigned long long)m->frame_count,
                        (unsigned long long)image_hash(m->framebuffer, m->fb_width, m->fb_height, kScreenWidth));
        if (state_every && m->frame_count % state_every == 0) dump_state(*m);
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("ran %llu frames in %.2fs (%.1f fps)\n", (unsigned long long)frames, secs, frames / secs);
    print_recomp_stats(*m);
    if (profile) {

        static const char* names[PROF_COUNT] = {"68K", "Z80", "MSH2", "SSH2", "video", "audio/pwm"};
        double total = secs * 1e9;
        std::printf("profile (ms/frame):");
        for (int i = 0; i < PROF_COUNT; ++i)
            std::printf(" %s %.3f (%.0f%%)", names[i], m->stats.prof_ns[i] / 1e6 / double(frames), 100.0 * m->stats.prof_ns[i] / total);
        std::printf(" | total %.3f\n", total / 1e6 / double(frames));
    }
    if (!wav_path.empty()) {
        if (write_wav(wav_path, wav, int(kAudioRate + 0.5))) std::printf("audio written to %s (%zu samples)\n", wav_path.c_str(), wav.size() / 2);
    }
    if (compare_native && centre_ok_frames < min_centre_frames) {
        std::printf("centre check: only %llu frames compared, expected at least %llu\n",
                    (unsigned long long)centre_ok_frames, (unsigned long long)min_centre_frames);
        return 5;
    }
    if (compare_native) std::printf("centre check: %llu widescreen frames with a 4:3 identical centre\n", (unsigned long long)centre_ok_frames);
    if (ref) std::printf("lockstep: %llu frames bit-identical between interpreter and recompiled execution\n", (unsigned long long)lockstep_ok_frames);
    if (!coverage_path.empty()) {
        if (cov.save(coverage_path)) std::printf("coverage written to %s (%zu entries)\n", coverage_path.c_str(), cov.size());
    }
    if (stage_set) {
        const auto w16 = [&m](uint32_t o) { return unsigned(m->wram[o] << 8 | m->wram[o + 1]); };
        // On one line: a test can then require the stage to be both the one
        // asked for and reached promptly.
        std::printf("stage: taken after %llu frames, ",
                    (unsigned long long)(stage_started - stage_frame));
        std::printf("pending=%d mode=%04X zone=%u level=%u player=%u combi=%u padoff=%02X time=%u\n",
                    int(m->stage_pending), w16(0xDFDE), w16(0xDFF2), w16(0xDFF4), w16(0xE038),
                    w16(0xE03A), m->wram[0xE05C], w16(0xE052));
    }
    dump_state(*m);
    return hash_failures ? 4 : 0;
}
