// Headless runner: executes the game deterministically without a window.
// Used for automated validation (screenshots, state hashes, coverage traces).
//
//   chaotix_headless --rom <file> --frames N [--shot F1,F2,...] [--out dir]
//                    [--press FRAME:BUTTON[+BUTTON]:DURATION ...] [--state-every N]
//                    [--interp]   (force interpreter even if generated code exists)
//                    [--no-idle-skip]   (run wait loops iteration by iteration, issue #62)
//                    [--hot-blocks N]   (where the emulated cycles go, per block)
//                    [--save-state FRAME:FILE] [--load-state FILE] [--state-hash-every N]
//                    [--rewind INTERVAL[:BUDGET_MB]]   (issue #40, runtime/save_state.h)
//                    [--lobby-partner ID|none] [--lobby-time 0-3]   (issue #44, runtime/lobby.h)
//                    [--tether-strength weak|normal|strong] [--tether-partner normal|follow|solo]
//                    [--tether-at FRAME:STRENGTH:PARTNER]   (issue #43, runtime/tether.h)
//                    [--hold-title FROM:TO]   (the front end's hold, issue #3, runtime/title_hold.h)
//                    [--mods-dir DIR [--mods-order A,B] [--mods-disable C] [--mods-no-loose]]
//                                 (mod packs as the app loads them, issue #42, runtime/mod_packs.h)
//                    [--ghost FILE] [--ghost-record FILE] [--ghost-log N] [--ghost-threaded]
//                    (issue #41, frontend/ghost.h)
//                    [--netplay-loopback MS:MS:PCT | --netplay-host PORT | --netplay-join ADDR ...]
//                                 (issue #46, frontend/netplay_headless.cpp)
#include "audio/music_mods.h"
#include "cpu/m68k/m68k_interp.h"
#include "cpu/m68k/m68k_ops.h"
#include "cpu/sh2/sh2_interp.h"
#include "cpu/sh2/sh2_ops.h"
#include "game/achievements.h"
#include "game/cheats.h"
#include "game/recomp_dispatch.h"
#include "renderer/image_io.h"
#include "runtime/log.h"
#include "runtime/character_mods.h"
#include "renderer/sprite_pack.h"
#include "renderer/sprite_sheet.h"
#include "renderer/tile_pack.h"
#include "runtime/lobby.h"
#include "runtime/mod_packs.h"
#include "runtime/patches.h"
#include "runtime/rewind.h"
#include "runtime/save_state.h"
#include "runtime/sprite_record.h"
#include "runtime/sprite_replace.h"
#include "runtime/tether.h"
#include "runtime/tile_replace.h"
#include "runtime/title_hold.h"
#include "frontend/backdrop.h"
#include "frontend/ghost.h"
#include "frontend/jukebox.h"
#include "frontend/time_attack.h"
#include "frontend/level_select.h"
#include "frontend/input_display.h"
#include "frontend/livesplit.h"
#include "frontend/netplay_headless.h"
#include "frontend/speedrun.h"
#include "renderer/game_font.h"
#include "runtime/system.h"
#include <cctype>
#include <chrono>
#include <cmath>
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
    if (!(r = mem("cartridge SRAM", a.sram, b.sram, sizeof a.sram)).empty()) return r;
    if (!(r = mem("output image", a.framebuffer, b.framebuffer, sizeof a.framebuffer)).empty()) return r;
    return "";
}

// The interpreter and the recompiled code must call the sprite hooks at the
// same points with the same registers: their draw lists must be equal.
std::string compare_sprite_lists(const SpriteList& a, const SpriteList& b) {
    char buf[512];
    if (a.draws.size() != b.draws.size()) {
        std::snprintf(buf, sizeof buf, "%zu draws vs %zu", a.draws.size(), b.draws.size());
        return buf;
    }
    for (size_t i = 0; i < a.draws.size(); ++i) {
        const SpriteDraw& x = a.draws[i];
        const SpriteDraw& y = b.draws[i];
        const bool same = x.graphic == y.graphic && x.source == y.source && x.x == y.x && x.y == y.y && x.flags == y.flags &&
                          x.sx == y.sx && x.sy == y.sy && x.kind == y.kind && x.warp == y.warp && x.colour == y.colour && x.bank == y.bank &&
                          x.frame == y.frame && x.route == y.route && x.hash == y.hash && x.size == y.size &&
                          std::memcmp(&x.clip, &y.clip, sizeof x.clip) == 0 &&
                          std::equal(a.bytes(x), a.bytes(x) + x.size, b.bytes(y));
        if (!same) {
            std::snprintf(buf, sizeof buf, "draw %zu: %s vs %s", i, describe(x).c_str(), describe(y).c_str());
            return buf;
        }
    }
    if (a.pool != b.pool) return "the graphics' bytes differ";
    return "";
}

// The first byte a sprite self-test picture got wrong, and which draws of the
// list cover it.
void report_sprite_mismatch(const Machine& m, const SpriteCheck& r, const SpriteList& list) {
    const int rel = r.first - 0x200;
    std::printf("SPRITE MISMATCH frame %llu buffer %d: %llu wrong, %llu missing, %llu extra of %llu; first at offset %05X (line %d, x %d): frame buffer %02X, rebuilt %02X\n",
                (unsigned long long)m.frame_count, m.mars.fb_display, (unsigned long long)r.wrong,
                (unsigned long long)r.missing, (unsigned long long)r.extra, (unsigned long long)r.compared, r.first,
                rel >= 0 ? rel / 512 : -1, rel >= 0 ? rel % 512 : rel, r.first_expected, r.first_rebuilt);
    std::printf("  offsets (frame buffer/shadow):");
    for (int o : r.some)
        std::printf(" %05X=%02X/%02X", o, m.mars.fb[m.mars.fb_display][o], m.fb_margin[m.mars.fb_display][o]);
    std::printf("\n");
    SpriteCanvas one;
    for (size_t k = 0; k < list.draws.size(); ++k) {
        one.clear();
        const SpriteDraw& d = list.draws[k];
        rasterise_sprite(d, list.bytes(d), d.size, one, d.kind == SpriteKind::Warped ? list.pool.data() + d.warp : nullptr);
        if (one.cov[size_t(r.first)])
            std::printf("  covered by %zu: %s; clip %d,%d,%d,%d scale %d..%d\n", k, describe(d).c_str(), d.clip.left,
                        d.clip.right, d.clip.top, d.clip.bottom, d.clip.scale_min, d.clip.scale_max);
    }
}

// --sprites-oracle: random calls of the three blitters, each run by the
// game's own code (the interpreter, on a machine of its own) and drawn by
// rasterise_sprite; the two must write the same bytes. Graphics with pixels
// of every value and spans outside their box (the plain blitter trusts the
// box), flags, hotspots, clip rectangles and scales at random. Spans outside
// the box are left to the plain blitter: the scaled ones then read their
// tables out of range, which is undefined (stack contents).
SpriteCanvas* g_oracle_canvas = nullptr;
int sprite_oracle(const std::string& rom_path, int calls, uint32_t seed) {
    auto o = std::make_unique<Machine>();
    std::string err;
    if (!o->load_rom(rom_path, &err)) return -1;
    o->reset();
    const MarsHeader& h = o->rom.mars;
    for (uint32_t i = 0; i < h.size; ++i) o->sdram[(h.dest + i) & 0x3FFFF] = o->rom.data[(h.source + i) & (o->rom.data.size() - 1)];
    o->on_fb_write = [](void*, uint32_t off, uint32_t v, int size, bool overwrite) {
        for (int i = 0; i < size; ++i) {
            const uint8_t b = uint8_t(v >> (8 * (size - 1 - i)));
            if (overwrite && !b) continue;
            const uint32_t at = (off + uint32_t(i)) & 0x1FFFF;
            g_oracle_canvas->px[at] = b;
            g_oracle_canvas->cov[at] = 1;
        }
    };
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    auto rr = [&rnd](int a, int b) { return a + int(rnd() % uint32_t(b - a + 1)); };
    auto put16 = [](uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); };
    auto put32 = [](uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v); };
    constexpr uint32_t kGraphic = 0x30000, kParams = 0x31000, kStop = 0x0603E000;
    SpriteCanvas blitter, raster;
    int differ = 0;
    for (int t = 0; t < calls; ++t) {
        SpriteDraw d;
        d.kind = SpriteKind(rr(0, 2));
        std::vector<uint8_t> g;
        auto p16 = [&g](int v) { g.push_back(uint8_t(uint16_t(v) >> 8)); g.push_back(uint8_t(v)); };
        const int x0 = -2 * rr(0, 30), x1 = 2 * rr(1, 30), y0 = -rr(0, 30), y1 = rr(1, 30);
        p16(x0); p16(x1); p16(y0 * 256); p16(y1 * 256);
        for (int k = rr(1, 12); k > 0; --k) {
            int xs = 2 * rr(x0 / 2, x1 / 2 - 1), line = rr(y0, y1);
            int len = 2 * rr(1, (x1 - xs) / 2);
            if (d.kind == SpriteKind::Plain && rnd() % 16 == 0) { xs += 2 * rr(-10, 10); line += rr(-5, 5); }
            if (xs + len > 126) len = (126 - xs) & ~1;
            if (len <= 0) continue;
            g.push_back(uint8_t(xs)); g.push_back(uint8_t(xs + len)); p16(line * 256);
            for (int i = 0; i < len; ++i) g.push_back(rnd() % 3 == 0 ? 0 : uint8_t(rr(1, 255)));
        }
        g.push_back(0); g.push_back(0); p16(0);
        std::copy(g.begin(), g.end(), o->sdram + kGraphic);
        d.x = rr(-80, 400);
        d.y = rr(-40, 270);
        d.flags = uint32_t(d.kind == SpriteKind::Plain ? rr(0, 11) : rr(0, 7));
        const bool wide = rnd() & 1;
        d.clip.left = int16_t(wide ? -56 : 0);
        d.clip.right = int16_t(wide ? 376 : 320);
        d.clip.top = rnd() % 4 ? 0 : rr(0, 100) << 8;
        d.clip.bottom = (rnd() % 4 ? 223 : rr(100, 239)) << 8;
        d.clip.scale_min = 0x10;
        d.clip.scale_max = 0x800;
        d.sx = rr(0x20, 0x300);
        d.sy = rnd() % 8 ? rr(0x20, 0x300) : rr(4, 0x20);
        if (rnd() % 4 == 0) d.sx = d.sy = 0x100;
        std::vector<uint8_t> warp(kWarpParamBytes + kWarpSineBytes);
        for (int axis = 0; axis < 2; ++axis) {
            put16(&warp[size_t(axis * 8)], uint32_t(rr(0x60, 0x200)));
            put16(&warp[size_t(axis * 8 + 2)], uint32_t(rr(0, 255)));
            put16(&warp[size_t(axis * 8 + 4)], uint32_t(rr(0, 512)));
            put16(&warp[size_t(axis * 8 + 6)], uint32_t(rr(0, 0xC0)));
        }
        std::copy(warp.begin(), warp.begin() + kWarpParamBytes, o->sdram + kParams);
        std::copy(o->sdram + 0x3C48, o->sdram + 0x3C48 + kWarpSineBytes, warp.begin() + kWarpParamBytes);
        // The game's blitter, from the call to its return.
        uint8_t* s = o->sdram;
        put16(s + 0x3834, uint16_t(d.clip.left));
        put16(s + 0x3836, uint16_t(d.clip.right));
        put32(s + 0x3838, uint32_t(d.clip.top));
        put32(s + 0x383C, uint32_t(d.clip.bottom));
        put16(s + 0x3840, uint16_t(d.clip.scale_min));
        put16(s + 0x3842, uint16_t(d.clip.scale_max));
        sh2::State& c = o->sh2[0];
        c.r[1] = uint32_t(d.x); c.r[2] = uint32_t(d.y); c.r[3] = d.flags; c.r[4] = 0x06000000 + kGraphic;
        c.r[5] = d.kind == SpriteKind::Warped ? 0x06000000 + kParams : uint32_t(d.sx);
        c.r[6] = uint32_t(d.sy);
        c.r[15] = 0x0603F000; c.pr = kStop; c.sr = 0xF0;
        c.pc = d.kind == SpriteKind::Scaled ? patches::kSh2BlitScaled : d.kind == SpriteKind::Warped ? patches::kSh2BlitWarped : patches::kSh2Blit;
        o->mars.fb_display = 1;
        blitter.clear();
        g_oracle_canvas = &blitter;
        for (int n = 0; n < 1000000 && c.pc != kStop; ++n) { c.cycles = 1 << 20; sh2::interp_block(&c); }
        raster.clear();
        rasterise_sprite(d, g.data(), uint32_t(g.size()), raster, warp.data());
        int bytes = 0, first = -1;
        for (int i = 0; i < 0x20000; ++i)
            if (blitter.cov[size_t(i)] != raster.cov[size_t(i)] || (blitter.cov[size_t(i)] && blitter.px[size_t(i)] != raster.px[size_t(i)])) {
                if (first < 0) first = i;
                ++bytes;
            }
        if (bytes || c.pc != kStop) {
            if (++differ <= 8)
                std::printf("SPRITE ORACLE call %d: %s; clip %d,%d,%d,%d: %d bytes differ, first at %05X%s\n", t, describe(d).c_str(),
                            d.clip.left, d.clip.right, d.clip.top >> 8, d.clip.bottom >> 8, bytes, first,
                            c.pc != kStop ? " (the blitter did not return)" : "");
        }
    }
    std::printf("sprites oracle: %d calls, %d differ from the game's blitters\n", calls, differ);
    return differ;
}

void dump_state(const Machine& m) {
    std::printf("frame %llu | 68K pc=%06X sr=%04X sp=%08X | MSH2 pc=%08X sr=%03X | SSH2 pc=%08X sr=%03X | FS=%d mode=%X\n",
                (unsigned long long)m.frame_count, m.m68k.pc, m68k::get_sr(&m.m68k), m.m68k.a[7],
                m.sh2[0].pc, m.sh2[0].sr, m.sh2[1].pc, m.sh2[1].sr, m.mars.fb_display, m.mars.bitmap_mode);
    std::printf("  comm: %04X %04X %04X %04X %04X %04X %04X %04X\n", m.mars.comm[0], m.mars.comm[1], m.mars.comm[2],
                m.mars.comm[3], m.mars.comm[4], m.mars.comm[5], m.mars.comm[6], m.mars.comm[7]);
}

// --hot-blocks N: emulated cycles spent per block (with --interp: one entry
// per guest basic block; with recompiled code, per dispatch), to find the
// wait loops (issue #62). Keys: cpu (0 = 68K, 1/2 = SH-2) << 32 | pc.
std::map<uint64_t, uint64_t> g_hot;
void (*g_hot_m68k)(m68k::State*) = nullptr;
void (*g_hot_sh2)(sh2::State*) = nullptr;
void hot_m68k(m68k::State* c) {
    const uint32_t pc = c->pc;
    const int32_t before = c->cycles;
    g_hot_m68k(c);
    g_hot[pc] += uint64_t(int64_t(before) - c->cycles);
}
void hot_sh2(sh2::State* c) {
    const uint32_t pc = c->pc;
    const int32_t before = c->cycles;
    g_hot_sh2(c);
    g_hot[(uint64_t(1 + c->id) << 32) | pc] += uint64_t(int64_t(before) - c->cycles);
}
void print_hot_blocks(Machine& m, size_t n) {
    const uint64_t total[3] = {m.stats.m68k_cycles, m.stats.msh2_cycles, m.stats.ssh2_cycles};
    static const char* names[3] = {"68K", "MSH2", "SSH2"};
    for (int cpu = 0; cpu < 3; ++cpu) {
        std::vector<std::pair<uint64_t, uint32_t>> v;
        for (const auto& [k, cyc] : g_hot)
            if (int(k >> 32) == cpu) v.push_back({cyc, uint32_t(k)});
        std::sort(v.rbegin(), v.rend());
        std::printf("hot blocks %s (%llu cycles):\n", names[cpu], (unsigned long long)total[cpu]);
        for (size_t i = 0; i < v.size() && i < n; ++i) {
            std::printf("  %08X %6.2f%%", v[i].second, total[cpu] ? 100.0 * double(v[i].first) / double(total[cpu]) : 0.0);
            if (!cpu) {
                for (uint32_t pc = v[i].second, k = 0; k < 8; ++k) {
                    m68k::Insn in;
                    m68k::decode(pc, m68k::fetch_bus, &m.m68k, in);
                    std::printf(" | %s", m68k::disassemble(in).c_str());
                    if (m68k::ends_block(in)) break;
                    pc += in.len;
                }
            } else {
                sh2::State& s = m.sh2[cpu - 1];
                for (uint32_t pc = v[i].second, k = 0; k < 8; ++k, pc += 2) {
                    const sh2::Insn in = sh2::decode(pc, uint16_t(sh2::rd16(&s, pc)));
                    std::printf(" | %s", sh2::disassemble(in).c_str());
                    if (sh2::ends_block(in) || (in.flags & sh2::IF_BRANCH)) break;
                }
            }
            std::printf("\n");
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    // Netplay runs (issue #46, frontend/netplay_headless.cpp) are a mode of
    // their own: two peers, through the netplay session.
    if (is_netplay_headless(argc, argv)) return netplay_headless_main(argc, argv);
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
    size_t hot_blocks = 0;
    int wide = 0, wide_bottom = 0;
    // --wide-at FRAME:E[:EB]: widen the margins part way through, the way
    // resizing the window does. The ring shift is latched when a level loads,
    // so this is what shows it does not need re-latching.
    uint64_t wide_at_frame = 0;
    int wide_at = -1, wide_at_bottom = 0;
    bool wide_live_reported = false;
    // --trace-wide: one line whenever the widescreen state changes, and a
    // count at the end. The count is what tells a level that stayed wide from
    // one that flickered back to 4:3 part way through.
    bool trace_wide = false;
    bool wide_everywhere = false;  // the former WIDE LOBBY: no effect since issue #61
    bool hud_edges = false;        // the HUD at the widened picture's edge
    int debug_layer = 0;
    // Mod folders, lowest priority first: the packs of --mods-dir, then
    // the ones given on their own (issue #42).
    std::vector<std::string> tile_dirs;
    std::string dump_tiles_dir;
    std::string export_sheets_dir;  // --export-sheets DIR: every frame of each bank, after the run
    // Issue #8, the menu's art: --backdrop KEY|all DIR renders menu backdrops
    // (frontend/backdrop.h) as PNGs; --game-font PNG writes the font atlas.
    std::string backdrop_key, backdrop_dir, game_font_png;
    std::vector<std::string> patch_files;  // --patch FILE: IPS/BPS applied to the ROM in memory
    std::vector<std::string> character_dirs;  // --characters DIR: character mods (runtime/character_mods.h)
    // --mods-dir DIR: mod packs and loose files as the app reads its Mods
    // folder (runtime/mod_packs.h), in the order --mods-order gives
    // (highest first, comma-separated folder names), less --mods-disable
    // ones, and without the loose files with --mods-no-loose.
    std::string mods_dir;
    mod_packs::Order mods_order;
    std::vector<cheats::Write> cheat_writes;  // --cheat, set before every frame
    uint64_t wide_live_frames = 0;
    int wide_stretches = 0;
    // The way into the special stage, shown wide (issue #91).
    uint64_t entry_frames = 0, entry_first = 0;
    bool wide_was_live = false;
    // --compare-native: run a 4:3 machine alongside the widescreen one and
    // require the centre 320 px to be pixel-identical. Comparison stops when
    // the cameras legitimately differ (the widescreen camera clamp keeps the
    // margins inside the level near its edges).
    bool compare_native = false;
    std::string achievements_file;  // --achievements FILE: report unlocks
    uint64_t min_centre_frames = 0;  // fail if fewer frames could be compared
    std::string wav_path;
    struct Poke { uint64_t frame; uint32_t addr; uint8_t value; bool sdram = false, fb = false; };
    std::vector<Poke> pokes;
    struct Peek { uint64_t every; uint32_t addr; uint32_t bytes; };
    std::vector<Peek> peeks;
    std::vector<std::string> music_dirs;  // --music-dir: music replaced by files (audio/music_mods.h)
    // The music player (issue #48, frontend/jukebox.h). --jukebox TRACK:FRAMES
    // plays a track on the player's own machine instead of running the game
    // (--wav keeps its sound); --jukebox-at FRAME:TRACK:FRAMES does it in the
    // middle of the run, which must carry on as if it had not happened.
    int jukebox_track = -1;
    uint64_t jukebox_frames = 0, jukebox_at = 0;
    bool jukebox_mid_run = false, jukebox_list = false, jukebox_interp = false;
    std::string jukebox_repeat = "loop";
    double jukebox_fade_after = 0;
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
    // --level-select: the stage requests are LEVEL SELECT ones (issue #51):
    // the session is followed as the frontend follows it, and where it
    // reaches and ends is printed.
    bool level_select_on = false;
    level_select::Session ls;
    // --level-select-none: and with PARTNER NONE (issue #55), so each session
    // plays with the ring tether's PARTNER SOLO, as the app does it.
    bool ls_solo = false;
    // --speedrun: the speedrun timer (frontend/speedrun.h) follows the session
    // as the frontend's does, on a clock of emulated frames (60 a second),
    // with the LiveSplit block published after every frame. --speedrun-start
    // FRAME starts a run by hand then. --input-display draws the input
    // display over the pictures written by --shot (a copy: the hashes are
    // the machine's own picture's).
    bool speedrun_on = false;
    int64_t speedrun_start = -1;
    bool input_display_on = false;
    speedrun::Tracker run;
    uint64_t input_display_shots = 0, input_display_outside = 0, input_display_changed = 0;
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
    // 32X sprite recording (runtime/sprite_record.h): --sprites installs the
    // recorder; --sprites-log FRAME[,FRAME...] prints the displayed buffer's
    // draw list at those frames; --sprites-check rebuilds every new picture's
    // sprites host-side and compares them with the emulated frame buffer.
    bool want_sprites = false, sprites_check = false;
    std::set<uint64_t> sprites_log;
    // --sprites-oracle N[:SEED]: N random blitter calls, game code vs rebuild.
    int oracle_calls = 0;
    uint32_t oracle_seed = 1;
    // Save states (issue #40): --save-state FRAME:FILE writes one after that
    // frame; --load-state FILE loads one before running (into the lockstep
    // reference too), and the run then carries on from its frame up to
    // --frames, so frame numbers in the other options keep their meaning.
    // --state-hash-every N prints a fingerprint of the whole machine.
    std::map<uint64_t, std::vector<std::string>> save_states;
    std::string load_state_path;
    uint64_t state_hash_every = 0;
    // --rewind INTERVAL[:BUDGET_MB]: a snapshot every INTERVAL frames into a
    // rewind buffer (runtime/rewind.h), timed; at the end every snapshot is
    // stepped back to and checked against the state it was taken from.
    int rewind_interval = 0;
    size_t rewind_budget_mb = 64;
    // Lobby shortcuts (issue #44, runtime/lobby.h): --lobby-partner ID (a
    // character id, as FFE038/4) and --lobby-time T (0 MORNING .. 3 NIGHT)
    // stand in for the choices made on OPTIONS > EXTRAS > LOBBY. A recorded
    // session carries its own ("lobby FRAME PARTNER TIME" lines).
    // --lobby-partner none (or -2, lobby::kPartnerNone, issue #55) is NONE:
    // the attractions are played with the tether's PARTNER SOLO.
    int lobby_partner = -1, lobby_time = -1;
    std::map<uint64_t, std::pair<int, int>> replay_lobby;
    // Ring tether options (issue #43, runtime/tether.h): --tether-strength
    // and --tether-partner stand in for OPTIONS > EXTRAS > RING TETHER;
    // --tether-at FRAME:S:P changes both before that frame (S and P as the
    // enums' numbers or names), as a recorded session's "tether FRAME S P"
    // lines do.
    int tether_strength = 0, tether_partner = 0;
    std::map<uint64_t, std::pair<int, int>> tether_events;
    // The title held while the front end is on it (issue #3,
    // runtime/title_hold.h): --hold-title FROM:TO holds it for those frames,
    // as the frontend does while its front menu is open; a recorded session's
    // "title_hold FRAME 0|1" lines say when it did.
    std::map<uint64_t, bool> title_hold_events;
    bool title_holding = false;
    uint64_t title_held_frames = 0;
    auto tether_value = [](const std::string& v, const char* const* names, int count) {
        for (int k = 0; k < count; ++k) {
            std::string n = names[k];
            for (char& ch : n) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            if (v == n) return k;
        }
        const int k = std::atoi(v.c_str());
        return k >= 0 && k < count ? k : -1;
    };
    // TIME ATTACK ghosts (issue #41, frontend/ghost.h): --ghost-record FILE
    // writes the run started by --stage or a replay as a ghost when it ends;
    // --ghost FILE races one, started when the stage is taken, and draws it
    // into the shots; --ghost-log N prints where it is every N frames;
    // --ghost-threaded plays it on its own thread, as the app does.
    std::string ghost_path, ghost_record_path;
    uint64_t ghost_log = 0;
    bool ghost_threaded = false;
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
        else if (a == "--save-state") {
            const std::string spec = next();
            const size_t colon = spec.find(':');
            if (colon == std::string::npos) { std::fprintf(stderr, "bad --save-state (FRAME:FILE)\n"); return 2; }
            save_states[std::strtoull(spec.c_str(), nullptr, 10)].push_back(spec.substr(colon + 1));
        }
        else if (a == "--load-state") load_state_path = next();
        else if (a == "--state-hash-every") state_hash_every = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--rewind") {
            const auto parts = split(next(), ':');
            rewind_interval = std::max(1, std::atoi(parts[0].c_str()));
            if (parts.size() > 1) rewind_budget_mb = size_t(std::max(1, std::atoi(parts[1].c_str())));
        }
        else if (a == "--lockstep") lockstep = true;
        else if (a == "--profile") profile = true;
        else if (a == "--hot-blocks") hot_blocks = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--no-idle-skip") set_idle_skip(false);
        else if (a == "--wide") wide = std::atoi(next().c_str());
        else if (a == "--wide-bottom") wide_bottom = std::atoi(next().c_str());
        else if (a == "--trace-wide") trace_wide = true;
        else if (a == "--wide-everywhere") wide_everywhere = true;
        else if (a == "--hud-edges") hud_edges = true;
        else if (a == "--patch") patch_files.push_back(next());
        // Character mods: <name>.ini files with [Palette] and [Stats].
        else if (a == "--characters") character_dirs.push_back(next());
        else if (a == "--mods-dir") mods_dir = next();
        else if (a == "--mods-order") mods_order.order = mod_packs::split_list(next());
        else if (a == "--mods-disable") {
            for (const auto& id : mod_packs::split_list(next())) mods_order.disabled.insert(id);
        } else if (a == "--mods-no-loose") mods_order.loose = false;
        // Graphics replacements, as the frontend's Mods/Textures: Mega Drive
        // tiles (md_<hash>.png), plane text by tile sequence (mdtext_*.txt
        // with .png), text by sprite (mdsprite_*.txt with .png) and 32X
        // sprites (32x_*.png) from DIR; and the dump of what is drawn, with
        // the plane text runs and the sprite groups on screen in DIR/Text
        // (--dump-tiles is its older name).
        else if (a == "--tiles") tile_dirs.push_back(next());
        else if (a == "--dump-graphics" || a == "--dump-tiles") dump_tiles_dir = next();
        // Whole-bank sprite sheets (renderer/sprite_sheet.h): the characters'
        // banks from the ROM, and the banks the scene has loaded at the end of
        // the run.
        else if (a == "--export-sheets") export_sheets_dir = next();
        else if (a == "--backdrop") { backdrop_key = next(); backdrop_dir = next(); }
        else if (a == "--game-font") game_font_png = next();
        else if (a == "--layer") debug_layer = next() == "md" ? 1 : 2;  // md | 32x: one layer in the shots
        else if (a == "--sprites") want_sprites = true;
        else if (a == "--sprites-check") want_sprites = sprites_check = true;
        else if (a == "--sprites-oracle") {
            const std::vector<std::string> parts = split(next(), ':');
            oracle_calls = std::atoi(parts[0].c_str());
            if (parts.size() > 1) oracle_seed = uint32_t(std::strtoul(parts[1].c_str(), nullptr, 10));
        }
        else if (a == "--sprites-log") {
            want_sprites = true;
            for (auto& s : split(next(), ',')) sprites_log.insert(std::strtoull(s.c_str(), nullptr, 10));
        }
        else if (a == "--cheat") {
            // A built-in cheat by its key, or Action Replay codes.
            const std::string c = next();
            bool known = false;
            for (const auto& b : cheats::builtin())
                if (c == b.key) { cheat_writes.insert(cheat_writes.end(), b.writes.begin(), b.writes.end()); known = true; }
            if (!known && !cheats::parse_codes(c, cheat_writes)) { std::fprintf(stderr, "bad --cheat %s\n", c.c_str()); return 2; }
        }
        else if (a == "--wide-at") {
            const std::vector<std::string> parts = split(next(), ':');
            if (parts.size() >= 2) {
                wide_at_frame = std::strtoull(parts[0].c_str(), nullptr, 10);
                wide_at = std::atoi(parts[1].c_str());
                wide_at_bottom = parts.size() > 2 ? std::atoi(parts[2].c_str()) : 0;
            }
        }
        else if (a == "--compare-native") compare_native = true;
        else if (a == "--achievements" && i + 1 < argc) achievements_file = next();
        else if (a == "--min-centre-frames") min_centre_frames = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--wav") wav_path = next();
        else if (a == "--music-dir") music_dirs.push_back(next());
        else if (a == "--jukebox" || a == "--jukebox-at") {
            auto parts = split(next(), ':');
            const bool mid = a == "--jukebox-at";
            if (parts.size() != (mid ? 3u : 2u)) { std::fprintf(stderr, "bad %s\n", a.c_str()); return 2; }
            size_t k = 0;
            if (mid) jukebox_at = std::strtoull(parts[k++].c_str(), nullptr, 10);
            jukebox_track = int(std::strtol(parts[k++].c_str(), nullptr, 0));
            jukebox_frames = std::strtoull(parts[k].c_str(), nullptr, 10);
            jukebox_mid_run = mid;
        }
        else if (a == "--jukebox-list") jukebox_list = true;
        else if (a == "--jukebox-interp") jukebox_interp = true;
        else if (a == "--jukebox-repeat") jukebox_repeat = next();   // loop | once | all
        else if (a == "--jukebox-fade-after") jukebox_fade_after = std::atof(next().c_str());
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
        else if (a == "--lobby-partner") {
            const std::string v = next();
            lobby_partner = v == "none" ? lobby::kPartnerNone : std::atoi(v.c_str());
        }
        else if (a == "--lobby-time") lobby_time = std::atoi(next().c_str());
        else if (a == "--tether-strength" || a == "--tether-partner") {
            const bool strength = a == "--tether-strength";
            const int k = strength ? tether_value(next(), tether::kStrengthNames, tether::kStrengthCount)
                                   : tether_value(next(), tether::kPartnerNames, tether::kPartnerCount);
            if (k < 0) { std::fprintf(stderr, "bad %s\n", a.c_str()); return 2; }
            (strength ? tether_strength : tether_partner) = k;
        }
        else if (a == "--tether-at") {
            auto parts = split(next(), ':');
            const int ts = parts.size() == 3 ? tether_value(parts[1], tether::kStrengthNames, tether::kStrengthCount) : -1;
            const int tp = parts.size() == 3 ? tether_value(parts[2], tether::kPartnerNames, tether::kPartnerCount) : -1;
            if (ts < 0 || tp < 0) { std::fprintf(stderr, "bad --tether-at\n"); return 2; }
            tether_events[std::strtoull(parts[0].c_str(), nullptr, 10)] = {ts, tp};
        }
        else if (a == "--hold-title") {
            auto parts = split(next(), ':');
            const uint64_t from = parts.size() == 2 ? std::strtoull(parts[0].c_str(), nullptr, 10) : 0;
            const uint64_t to = parts.size() == 2 ? std::strtoull(parts[1].c_str(), nullptr, 10) : 0;
            if (to <= from) { std::fprintf(stderr, "bad --hold-title\n"); return 2; }
            title_hold_events[from] = true;
            title_hold_events[to] = false;
        }
        else if (a == "--ghost") ghost_path = next();
        else if (a == "--speedrun") speedrun_on = true;
        else if (a == "--speedrun-start") { speedrun_on = true; speedrun_start = std::atoll(next().c_str()); }
        else if (a == "--input-display") input_display_on = true;
        else if (a == "--ghost-record") ghost_record_path = next();
        else if (a == "--ghost-log") ghost_log = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--ghost-threaded") ghost_threaded = true;
        else if (a == "--peek-every") {
            // N:ADDR:BYTES, ADDR in hex: prints those 68K work RAM bytes every
            // N frames (tests measure what the game did, a player's speed say).
            auto parts = split(next(), ':');
            if (parts.size() < 3) { std::fprintf(stderr, "bad --peek-every\n"); return 2; }
            Peek p{std::strtoull(parts[0].c_str(), nullptr, 10), uint32_t(std::strtoul(parts[1].c_str(), nullptr, 16) & 0xFFFF),
                   uint32_t(std::strtoul(parts[2].c_str(), nullptr, 10))};
            if (!p.every || !p.bytes || p.bytes > 64) { std::fprintf(stderr, "bad --peek-every\n"); return 2; }
            peeks.push_back(p);
        }
        else if (a == "--poke") {
            // FRAME:ADDR:BYTE, all but FRAME in hex: a 68K work RAM byte set
            // before that frame runs (tests reach states a script cannot).
            auto parts = split(next(), ':');
            if (parts.size() < 3) { std::fprintf(stderr, "bad --poke\n"); return 2; }
            pokes.push_back({std::strtoull(parts[0].c_str(), nullptr, 10),
                             uint32_t(std::strtoul(parts[1].c_str(), nullptr, 16) & 0xFFFF),
                             uint8_t(std::strtoul(parts[2].c_str(), nullptr, 16))});
        }
        else if (a == "--poke-sdram") {
            // FRAME:OFFSET:BYTE, the same for an SH-2 SDRAM byte (the special
            // stage keeps its rings and its goal there).
            auto parts = split(next(), ':');
            if (parts.size() < 3) { std::fprintf(stderr, "bad --poke-sdram\n"); return 2; }
            pokes.push_back({std::strtoull(parts[0].c_str(), nullptr, 10),
                             uint32_t(std::strtoul(parts[1].c_str(), nullptr, 16) & 0x3FFFF),
                             uint8_t(std::strtoul(parts[2].c_str(), nullptr, 16)), true});
        }
        else if (a == "--poke-fb") {
            // FROM:TO:OFFSET:BYTE, a byte of both 32X frame buffers set before
            // every frame from FROM to TO: the special stage's result at $1FF
            // (1: cleared), which the SH-2 stores when the goal is reached
            // ($060074BA) and the 68K reads after the stage.
            auto parts = split(next(), ':');
            if (parts.size() < 4) { std::fprintf(stderr, "bad --poke-fb\n"); return 2; }
            const uint64_t from = std::strtoull(parts[0].c_str(), nullptr, 10), to = std::strtoull(parts[1].c_str(), nullptr, 10);
            for (uint64_t fr = from; fr <= to; ++fr)
                pokes.push_back({fr, uint32_t(std::strtoul(parts[2].c_str(), nullptr, 16) & 0x1FFFF),
                                 uint8_t(std::strtoul(parts[3].c_str(), nullptr, 16)), false, true});
        }
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
                {
                    unsigned long long lf = 0;
                    int lp = -1, lt = -1;
                    if (std::sscanf(line, "lobby %llu %d %d", &lf, &lp, &lt) == 3) {
                        replay_lobby[lf] = {lp, lt};
                        continue;
                    }
                    if (std::sscanf(line, "tether %llu %d %d", &lf, &lp, &lt) == 3) {
                        tether_events[lf] = {lp, lt};
                        continue;
                    }
                    if (std::sscanf(line, "title_hold %llu %d", &lf, &lp) == 2) {
                        title_hold_events[lf] = lp != 0;
                        continue;
                    }
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
        else if (a == "--level-select") level_select_on = true;
        else if (a == "--level-select-none") level_select_on = ls_solo = true;
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
    if (oracle_calls > 0) return sprite_oracle(rom_path, oracle_calls, oracle_seed) ? 6 : 0;

    if (!mods_dir.empty()) {
        // As the app does: each kind from the enabled packs, lowest first,
        // before the folders given on their own.
        std::vector<std::string> messages;
        const std::vector<mod_packs::Pack> packs =
            mod_packs::arrange(mod_packs::discover(mods_dir, savestate::game_version(), &messages), mods_order);
        for (const auto& msg : messages) std::printf("mods: %s\n", msg.c_str());
        for (const auto& p : packs)
            std::printf("mods: %s %s (%s)\n", p.enabled ? "on " : "off", p.title().c_str(), p.loose ? "loose" : p.id.c_str());
        auto before = [](std::vector<std::string>& list, const std::vector<std::string>& first) {
            list.insert(list.begin(), first.begin(), first.end());
        };
        before(patch_files, mod_packs::patch_files(packs));
        before(character_dirs, mod_packs::folders(packs, mod_packs::kCharacters));
        before(tile_dirs, mod_packs::folders(packs, mod_packs::kTextures));
        before(music_dirs, mod_packs::folders(packs, mod_packs::kMusic));
    }

    auto m = std::make_unique<Machine>();
    std::string err;
    if (!m->load_rom(rom_path, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    if (!patch_files.empty()) {
        std::vector<std::string> messages;
        const int applied = m->rom.apply_patches(patch_files, &messages);
        for (const auto& msg : messages) std::printf("patch: %s\n", msg.c_str());
        std::printf("patch: %d applied, %zu changed range(s)\n", applied, m->rom.patched.size());
    }
    // Character mods: the stats are a data patch over the ROM (after the patch
    // files), on every machine below that runs the same game; the colours are
    // output only, one CharacterPalettes shared by them all.
    std::vector<CharacterMod> character_mods;
    std::shared_ptr<CharacterPalettes> character_palettes;
    if (!character_dirs.empty()) {
        std::vector<std::string> messages;
        character_mods = load_character_mods(character_dirs, &messages);
        const int changed = m->rom.apply_edits(stat_edits(m->rom, character_mods, &messages), &messages);
        for (const auto& msg : messages) std::printf("characters: %s\n", msg.c_str());
        character_palettes = std::make_shared<CharacterPalettes>(m->rom, character_mods);
        if (character_palettes->empty()) character_palettes = nullptr;
        std::printf("characters: %zu file(s), stats changed %d byte(s), %s\n", character_mods.size(), changed,
                    character_palettes ? "colours replaced" : "no colours");
        m->characters = character_palettes;
    }
    if (!game_font_png.empty()) {
        GameFont font;
        std::string why;
        if (!build_game_font(m->rom.data, font, &why)) { std::printf("game font: %s\n", why.c_str()); return 1; }
        std::printf("game font: %d glyphs of %dx%d\n", font.glyph_count, GameFont::kGlyphW, GameFont::kGlyphH);
        if (!write_png(game_font_png, font.pixels.data(), font.width, font.height, font.width)) return 1;
        if (backdrop_key.empty()) return 0;
    }
    if (!backdrop_key.empty()) {
        int failed = 0;
        // PLACE:LEVEL:TIME[:WIDE] tries a stage that is not one of the list.
        std::vector<BackdropSpec> specs = backdrop_specs();
        std::string custom_key;
        if (backdrop_key.find(':') != std::string::npos) {
            const auto parts = split(backdrop_key, ':');
            BackdropSpec s;
            s.place = uint16_t(std::atoi(parts[0].c_str()));
            s.level = parts.size() > 1 ? uint16_t(std::atoi(parts[1].c_str())) : 1;
            s.attime = parts.size() > 2 ? uint16_t(std::atoi(parts[2].c_str())) : 0;
            s.wide = parts.size() > 3 ? std::atoi(parts[3].c_str()) != 0 : true;
            custom_key = "custom_" + parts[0] + "_" + std::to_string(s.level) + "_" + std::to_string(s.attime);
            s.key = custom_key.c_str();
            specs = {s};
            backdrop_key = "all";
        }
        for (const BackdropSpec& spec : specs) {
            if (backdrop_key != "all" && backdrop_key != spec.key) continue;
            BackdropImage img;
            std::string why;
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = render_backdrop(m->rom, spec, img, &why);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!ok) { std::printf("backdrop %s: %s\n", spec.key, why.c_str()); ++failed; continue; }
            const std::string path = backdrop_dir + "/" + spec.key + ".png";
            write_png(path, img.pixels.data(), img.width, img.height, img.width);
            std::printf("backdrop %s: %dx%d in %.0f ms, tint %08X, %d of %d sprites left out -> %s\n", spec.key,
                        img.width, img.height, ms, unsigned(backdrop_tint(img)), img.hidden_draws, img.sprite_draws,
                        path.c_str());
        }
        return failed ? 1 : 0;
    }
    // The same data patch on another machine, so that it runs the same game.
    auto same_rom = [&](Machine& o) {
        if (!patch_files.empty()) o.rom.apply_patches(patch_files, nullptr);
        if (!character_mods.empty()) o.rom.apply_edits(stat_edits(o.rom, character_mods, nullptr), nullptr);
        o.characters = character_palettes;
    };
    m->reset();
    m->audio_enabled = !wav_path.empty() || want_audio;
    m->profile = profile;
    m->wide_enabled = wide > 0 || wide_bottom > 0 || wide_at > 0 || wide_at_bottom > 0;
    m->wide_extra = wide;
    m->wide_extra_bottom = wide_bottom;
    m->wide_everywhere = wide_everywhere;
    m->wide_hud_edges = hud_edges;
    m->debug_layer = debug_layer;
    if (!tile_dirs.empty() || !dump_tiles_dir.empty()) {
        // Output only: the --lockstep reference draws with the same tiles,
        // so the machines' state and their pictures must still be identical.
        m->vdp.tiles = std::make_shared<TileReplacer>();
        std::vector<std::string> messages;
        if (!tile_dirs.empty())
            std::printf("tiles: %d replacement(s) loaded\n", load_tile_packs(*m->vdp.tiles, tile_dirs, &messages));
        for (const auto& msg : messages) std::printf("tiles: %s\n", msg.c_str());
        if (!tile_dirs.empty()) {
            // Text replaced by tile sequence (mdtext_<name>.txt + .png) and
            // by sprite (mdsprite_<name>.txt + .png), counted together.
            messages.clear();
            std::printf("text: %d pattern(s) loaded\n", load_text_packs(*m->vdp.tiles, tile_dirs, &messages));
            for (const auto& msg : messages) std::printf("text: %s\n", msg.c_str());
        }
        m->vdp.tiles->dumping = !dump_tiles_dir.empty();
    }
    // 32X sprite replacements from the same folder; the recorder is only
    // installed when there are some, or for dumping.
    std::shared_ptr<SpritePack> sprite_pack;
    if (!tile_dirs.empty()) {
        sprite_pack = std::make_shared<SpritePack>();
        std::vector<std::string> messages;
        std::printf("sprites: %d replacement(s) loaded\n", load_sprite_packs(*sprite_pack, tile_dirs, &messages));
        for (const auto& msg : messages) std::printf("sprites: %s\n", msg.c_str());
        if (sprite_pack->empty()) sprite_pack = nullptr;
    }
    if (sprite_pack || !dump_tiles_dir.empty()) want_sprites = true;
    if (want_sprites) {
        m->sprites = std::make_shared<SpriteRecorder>();
        m->sprites->track_writes = sprites_check;
        m->sprites->set_pack(sprite_pack);
        m->sprites->dumping = !dump_tiles_dir.empty();
    }
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
    if (hot_blocks) {
        g_hot_m68k = m->exec.m68k_block;
        g_hot_sh2 = m->exec.sh2_block;
        m->exec.m68k_block = hot_m68k;
        m->exec.sh2_block = hot_sh2;
    }
    // Lockstep validation: an interpreter-only reference machine runs next to
    // the recompiled one and full state is compared after every frame.
    std::unique_ptr<Machine> ref;
    if (lockstep) {
        ref = std::make_unique<Machine>();
        ref->load_rom(rom_path, &err);
        same_rom(*ref);
        ref->reset();
        ref->wide_enabled = wide > 0 || wide_bottom > 0 || wide_at > 0 || wide_at_bottom > 0;
        ref->wide_extra = wide;
        ref->wide_extra_bottom = wide_bottom;
        ref->wide_everywhere = wide_everywhere;
        ref->wide_hud_edges = hud_edges;
        ref->vdp.tiles = m->vdp.tiles;  // the same picture, so the output images compare too
        // The same save and the same pad, or the two part at the first
        // DATA LOAD or the first read of the controller.
        std::copy(m->sram, m->sram + sizeof m->sram, ref->sram);
        ref->input.six_button[0] = m->input.six_button[0];
        ref->input.six_button[1] = m->input.six_button[1];
        // Its own recorder: the interpreter must record the same draws as
        // the recompiled code (compared every frame below).
        // With the same replacements, so that its pictures compare too.
        if (want_sprites) {
            ref->sprites = std::make_shared<SpriteRecorder>();
            ref->sprites->set_pack(sprite_pack);
        }
        if (!rs.active) std::printf("warning: --lockstep without generated code compares the interpreter with itself\n");
    }
    audio::MusicMods music;
    if (!music_dirs.empty()) {
        int replaced = 0;
        for (const auto& dir : music_dirs) replaced = music.scan(dir);
        std::printf("music: %d replaced from %zu folder(s)\n", replaced, music_dirs.size());
        // The patch tells the driver to stop replaced music, so a lockstep
        // reference must be told the same.
        music.attach(*m);
        if (ref) music.attach(*ref);
    }
    // The music player's track list and machine (issue #48).
    if (jukebox_list) {
        bool from_rom = false;
        const std::vector<jukebox::Track> tracks = jukebox::track_list(m->rom, &from_rom);
        std::printf("jukebox: %zu tracks %s\n", tracks.size(), from_rom ? "from the ROM" : "(not found in the ROM)");
        for (const auto& t : tracks)
            std::printf("jukebox track %02d id %02X %s%s\n", int(t.id), unsigned(t.id), t.name.c_str(),
                        music.has(t.id) ? " [replaced by mod]" : "");
    }
    // Plays the track for the frames asked; true if it got going.
    auto play_jukebox = [&](std::vector<int16_t>* keep) {
        jukebox::Player jb;
        std::string why;
        const auto t0 = std::chrono::steady_clock::now();
        if (!jb.start(m->rom, music_dirs.empty() ? nullptr : &music, rs.active && !jukebox_interp, &why)) {
            std::fprintf(stderr, "jukebox: %s\n", why.c_str());
            return false;
        }
        const double boot_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        jb.set_tracks(jukebox::track_list(m->rom));
        jb.set_repeat(jukebox_repeat == "once" ? jukebox::Repeat::Once
                      : jukebox_repeat == "all" ? jukebox::Repeat::All : jukebox::Repeat::Loop);
        jb.set_fade_after(jukebox_fade_after);
        jb.play(uint8_t(jukebox_track));
        std::vector<int16_t> sound;
        double worst = 0;
        const auto t1 = std::chrono::steady_clock::now();
        uint8_t last_id = uint8_t(jukebox_track);
        for (uint64_t i = 0; i < jukebox_frames; ++i) {
            const auto f0 = std::chrono::steady_clock::now();
            jb.run_frames(1, sound);
            worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - f0).count());
            const jukebox::Status st = jb.status();
            if (st.id != last_id) {
                std::printf("jukebox: frame %llu now %02d%s\n", (unsigned long long)i, int(st.id), st.id ? "" : " (stopped)");
                last_id = st.id;
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        double sum = 0;
        int peak = 0;
        for (int16_t v : sound) {
            sum += double(v) * double(v);
            peak = std::max(peak, std::abs(int(v)));
        }
        const jukebox::Status st = jb.status();
        std::printf("jukebox: track %02d, %s, ready in %llu frames (%.0f ms), %llu frames in %.0f ms "
                    "(%.3f ms a frame, worst %.3f), peak %d, rms %.1f, %s%s\n",
                    jukebox_track, rs.active && !jukebox_interp ? "recompiled" : "interpreted",
                    (unsigned long long)jb.boot_frames(), boot_ms, (unsigned long long)jukebox_frames, ms,
                    jukebox_frames ? ms / double(jukebox_frames) : 0.0, worst, peak,
                    sound.empty() ? 0.0 : std::sqrt(sum / double(sound.size())),
                    st.playing ? "playing" : "stopped", st.replaced ? ", from the mod's file" : "");
        if (keep) *keep = std::move(sound);
        return true;
    };
    if (jukebox_track >= 0 && !jukebox_mid_run) {
        std::vector<int16_t> sound;
        if (!play_jukebox(&sound)) return 4;
        if (!wav_path.empty()) {
            if (!write_wav(wav_path, sound, int(kAudioRate + 0.5))) return 1;
            std::printf("audio written to %s (%zu samples)\n", wav_path.c_str(), sound.size() / 2);
        }
        return 0;
    }
    if (jukebox_list && jukebox_track < 0) return 0;
    // The 4:3 comparison machine runs on the interpreter: the recompiled
    // code's SH-2 validation state is process-wide, so only one machine at a
    // time may use it (same reason the lockstep reference is interpreted).
    std::unique_ptr<Machine> nat;
    bool centre_compare = false;
    if (compare_native && (wide > 0 || wide_bottom > 0)) {
        nat = std::make_unique<Machine>();
        nat->load_rom(rom_path, &err);
        same_rom(*nat);
        nat->reset();
        centre_compare = true;
    }
    // Every machine alike: the choice changes what the game does.
    auto set_lobby = [&](int partner, int time) {
        for (Machine* x : {m.get(), ref.get(), nat.get()})
            if (x) { x->lobby_partner = partner; x->lobby_time = time; }
    };
    set_lobby(lobby_partner, lobby_time);
    // The GAMEPLAY setting (as --tether-* and the tether events give it),
    // and the one in force, as the app works it out each frame
    // (tether::partner_in_force, issue #55): SOLO while a level select
    // session with PARTNER NONE runs, or with the lobby shortcut's NONE.
    int gameplay_partner = tether_partner;
    auto set_tether = [&](int strength, int partner) {
        gameplay_partner = partner;
        for (Machine* x : {m.get(), ref.get(), nat.get()})
            if (x) { x->tether_strength = uint8_t(strength); x->tether_partner = uint8_t(partner); }
    };
    set_tether(tether_strength, tether_partner);
    auto partner_in_force = [&](uint64_t frame) {
        const bool picked_none = ls.solo_now() || (!ls.running && m->lobby_partner == lobby::kPartnerNone);
        const int want = int(tether::partner_in_force(gameplay_partner, false, picked_none));
        if (want == m->tether_partner) return;
        for (Machine* x : {m.get(), ref.get(), nat.get()})
            if (x) x->tether_partner = uint8_t(want);
        std::printf("tether: partner %s from frame %llu\n", tether::kPartnerNames[want], (unsigned long long)frame);
    };
    uint64_t start_frame = 0;
    if (!load_state_path.empty()) {
        std::vector<uint8_t> file;
        std::string why;
        savestate::Info info;
        if (!savestate::read_file(load_state_path, file)) {
            std::fprintf(stderr, "cannot open %s\n", load_state_path.c_str());
            return 2;
        }
        if (!savestate::load(*m, file, &why, nullptr, &info) || (ref && !savestate::load(*ref, file, &why))) {
            std::fprintf(stderr, "cannot load %s: %s\n", load_state_path.c_str(), why.c_str());
            return 2;
        }
        start_frame = m->frame_count;
        // A 4:3 comparison machine would have to start from the same moment.
        nat.reset();
        centre_compare = false;
        std::printf("state: loaded %s (frame %llu, %zu bytes, %zu raw)\n", load_state_path.c_str(),
                    (unsigned long long)info.frame, file.size(), info.raw_size);
    }
    RewindBuffer rewind;
    rewind.set_budget(rewind_budget_mb << 20);
    std::map<uint64_t, uint64_t> rewind_hashes;  // snapshot frame -> state fingerprint
    double rewind_push_ms = 0, rewind_push_worst = 0;
    uint64_t rewind_pushes = 0;
    uint64_t centre_ok_frames = 0;
    int centre_worst = 0;  // most pixels a compared frame differed by (special stage only)
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
    SpriteCanvas sprite_canvas;
    SpriteCheck sprite_total;
    uint64_t sprites_key = ~0ull, sprite_pictures = 0, sprite_bad_pictures = 0;
    int sprites_dumped = 0;
    uint64_t sprite_others_changed = 0;  // --sprites-check with a pack: see check_composed_others
    double worst_ms = 0;
    uint64_t worst_frame = 0, slow_frames = 0, sram_frames = 0, sram_last = 0;
    ghost::Recorder ghost_recorder;
    ghost::Ghost ghost_file;
    ghost::Player ghost_player;
    ghost::CameraTrail live_camera;
    bool ghost_waiting = false;   // loaded, waiting for the live run's stage
    struct GhostTotals {
        uint64_t frames = 0, drawn = 0, pixels = 0, outside = 0, misplaced = 0;
        double ms = 0, worst_ms = 0;
        bool goal_told = false;
    } gt;
    std::vector<uint32_t> ghost_picture;
    if (!ghost_path.empty()) {
        std::vector<uint8_t> bytes;
        std::string why;
        if (!savestate::read_file(ghost_path, bytes) || !ghost::decode(bytes, ghost_file, &why) ||
            !ghost::compatible(ghost_file, *m, &why)) {
            std::fprintf(stderr, "cannot use the ghost %s: %s\n", ghost_path.c_str(), why.empty() ? "unreadable" : why.c_str());
            return 2;
        }
        std::printf("ghost: %s, stage %u-%u, time %s (%d frames), %zu input frames, %zu bytes\n", ghost_path.c_str(),
                    unsigned(ghost_file.request.place), unsigned(ghost_file.request.level),
                    stage_select::format_time(ghost_file.time).c_str(), ghost_file.time, ghost_file.inputs.size(),
                    bytes.size());
        ghost_waiting = true;
    }
    auto t0 = std::chrono::steady_clock::now();
    for (uint64_t f = start_frame; f < frames; ++f) {
        uint16_t btn = 0;
        for (const auto& p : presses)
            if (f >= p.frame && f < p.frame + p.duration) btn |= p.buttons;
        if (stage_set && f == stage_frame) {
            // Both machines, so lockstep still compares like with like.
            m->stage_request = stage;
            m->stage_pending = true;
            if (ref) { ref->stage_request = stage; ref->stage_pending = true; }
            if (nat) { nat->stage_request = stage; nat->stage_pending = true; }
            stage_kick = 0;
            ta.begin(stage);
            if (level_select_on) ls.begin(stage, ls_solo);
            if (!ghost_record_path.empty()) ghost_recorder.arm(stage);
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
        {
            auto te = tether_events.find(f);
            if (te != tether_events.end()) set_tether(te->second.first, te->second.second);
        }
        if (!replay.empty()) {
            // A recorded session drives everything: its pads replace the
            // scripted ones, and its stage requests are applied where they
            // were made.
            auto lob = replay_lobby.find(f);
            if (lob != replay_lobby.end()) set_lobby(lob->second.first, lob->second.second);
            auto it = replay_stages.find(f);
            if (it != replay_stages.end()) {
                const ReplayStage& q = it->second;
                // The same conditions the run was played under, or it
                // diverges. A recording from before these were kept leaves
                // whatever the command line asked for.
                if (q.has_video) {
                    m->wide_enabled = q.wide > 0 || q.wide_bottom > 0;
                    m->wide_extra = q.wide;
                    m->wide_extra_bottom = q.wide_bottom;
                    m->input.six_button[0] = m->input.six_button[1] = q.six_button;
                }
                m->stage_request = q.request;
                m->stage_pending = true;
                if (ref) {
                    if (q.has_video) {
                        ref->wide_enabled = q.wide > 0 || q.wide_bottom > 0;
                        ref->wide_extra = q.wide;
                        ref->wide_extra_bottom = q.wide_bottom;
                        ref->input.six_button[0] = ref->input.six_button[1] = q.six_button;
                    }
                    ref->stage_request = q.request;
                    ref->stage_pending = true;
                }
                ta.begin(q.request);
                if (level_select_on) ls.begin(q.request, ls_solo);
                if (!ghost_record_path.empty()) ghost_recorder.arm(q.request);
                stage_set = true;
                stage_frame = f;
            }
            if (f < replay.size()) {
                btn = uint16_t(replay[size_t(f)] & 0xFFFF);
                btn2 = uint16_t(replay[size_t(f)] >> 16);
            }
        }
        partner_in_force(f);
        if (wide_at >= 0 && f == wide_at_frame) {
            m->wide_extra = wide_at;
            m->wide_extra_bottom = wide_at_bottom;
            if (ref) { ref->wide_extra = wide_at; ref->wide_extra_bottom = wide_at_bottom; }
        }
        m->input.pad[0] = btn;
        m->input.pad[1] = btn2;
        if (ref) { ref->input.pad[0] = btn; ref->input.pad[1] = btn2; }
        g_trace_on = g_trace && f >= trace_from && f < trace_to;
        for (const Poke& p : pokes)
            if (p.frame == f) {
                for (Machine* x : {m.get(), ref.get(), nat.get()}) {
                    if (!x) continue;
                    if (p.fb) x->mars.fb[0][p.addr] = x->mars.fb[1][p.addr] = p.value;
                    else (p.sdram ? x->sdram : x->wram)[p.addr] = p.value;
                }
            }
        cheats::apply(*m, cheat_writes);
        if (ref) cheats::apply(*ref, cheat_writes);
        {
            auto th = title_hold_events.find(f);
            if (th != title_hold_events.end()) title_holding = th->second;
        }
        if (title_holding) {
            // Every machine alike, as the frontend holds the one it runs.
            if (title_hold::apply(*m)) ++title_held_frames;
            for (Machine* x : {ref.get(), nat.get()})
                if (x) title_hold::apply(*x);
        }
        // The music player opened mid-run: the game is held (no frame of it
        // runs) while the player's machine plays, then goes on.
        if (jukebox_mid_run && f == jukebox_at && !play_jukebox(nullptr)) return 4;
        ghost_recorder.before_frame(*m);
        const size_t first_sample = m->audio_out.size();
        const auto frame_t0 = std::chrono::steady_clock::now();
        m->run_frame();
        ghost_recorder.after_frame(*m);
        live_camera.push(patches::camera_x(*m), patches::camera_y(*m));
        if (ghost_player.active()) {
            const auto g0 = std::chrono::steady_clock::now();
            ghost_player.advance();
            ghost::Frame gf;
            ghost_player.latest(gf, ghost_threaded ? 50.0 : 0.0);
            const double gms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g0).count();
            gt.ms += gms;
            gt.worst_ms = std::max(gt.worst_ms, gms);
            ++gt.frames;
            const ghost::Status gs = ghost_player.status();
            if (gs.goal && !gt.goal_told) {
                gt.goal_told = true;
                std::printf("ghost: reached the goal at ghost frame %llu, time %s (%d frames)\n",
                            (unsigned long long)gs.frames, stage_select::format_time(gs.time).c_str(), gs.time);
            }
            // The live picture, untouched, and a copy with the ghost drawn
            // over it: every pixel that differs must lie where the ghost's
            // box lands on the live screen.
            ghost_picture.assign(m->framebuffer, m->framebuffer + kScreenWidth * kScreenHeight);
            int cx = 0, cy = 0;
            live_camera.get(cx, cy);
            const ghost::Placed pl = ghost::compose(ghost_picture.data(), kScreenWidth, m->fb_width, m->fb_height,
                                                    m->fb_extra, gf, cx, cy);
            if (pl.pixels) {
                ++gt.drawn;
                gt.pixels += uint64_t(pl.pixels);
                const int bx = gf.x + gf.cam_x - cx + m->fb_extra, by = gf.y + gf.cam_y - cy;
                int changed = 0;
                for (int y = 0; y < m->fb_height; ++y)
                    for (int x = 0; x < m->fb_width; ++x) {
                        const size_t k = size_t(y) * kScreenWidth + size_t(x);
                        if (ghost_picture[k] == m->framebuffer[k]) continue;
                        ++changed;
                        if (x < bx || x >= bx + gf.w || y < by || y >= by + gf.h) ++gt.outside;
                    }
                if (changed > pl.pixels) ++gt.misplaced;
            }
            if (ghost_log && m->frame_count % ghost_log == 0)
                std::printf("ghost f=%llu gf=%llu visible=%d gcam=%d,%d cam=%d,%d box=%d,%d,%dx%d drawn=%d at=%d,%d,%dx%d\n",
                            (unsigned long long)m->frame_count, (unsigned long long)gf.index, int(gf.visible),
                            gf.cam_x, gf.cam_y, cx, cy, gf.x, gf.y, gf.w, gf.h, pl.pixels, pl.x, pl.y, pl.w, pl.h);
            if (gs.ended) {
                std::printf("ghost: ended after %llu frames, time %s (%d frames), recorded %s (%d frames): %s\n",
                            (unsigned long long)gs.frames, stage_select::format_time(gs.time).c_str(), gs.time,
                            stage_select::format_time(ghost_file.time).c_str(), ghost_file.time,
                            gs.desynced ? "DESYNC" : "in sync");
                ghost_player.stop();
            }
        }
        if (ghost_waiting && ta.running && !m->stage_pending) {
            // The live run's stage was taken this frame: the ghost starts
            // from the same moment of its own run.
            ghost_waiting = false;
            std::string why;
            if (!ghost_player.start(m->rom, ghost_file, ghost_threaded, &why)) {
                std::fprintf(stderr, "ghost: cannot start: %s\n", why.c_str());
                return 2;
            }
            std::printf("ghost: started with the live run at frame %llu%s\n", (unsigned long long)m->frame_count,
                        ghost_threaded ? " (on its own thread)" : "");
        }
        if (!dump_tiles_dir.empty() && m->vdp.tiles) {
            write_tile_dumps(*m->vdp.tiles, dump_tiles_dir);
            // The plane text on screen, for mdtext_ patterns (the lockstep
            // reference shares the replacer and lists nothing new).
            write_text_dumps(*m->vdp.tiles, dump_tiles_dir + "/Text", m->framebuffer, m->fb_width, m->fb_height,
                             kScreenWidth, m->fb_extra);
        }
        if (!dump_tiles_dir.empty() && m->sprites) sprites_dumped += write_sprite_dumps(*m->sprites, dump_tiles_dir);
        {
            // The worst frames, not just the average: a stall now and then
            // shows up as lag even when the average is far below 16 ms.
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_t0).count();
            if (ms > worst_ms) { worst_ms = ms; worst_frame = f; }
            if (ms > 8.0) ++slow_frames;
            // And how often the save changed: the frontend writes it to disk
            // a second after a change.
            if (m->sram_dirty) { ++sram_frames; sram_last = f; m->sram_dirty = false; }
        }
        if (!music_dirs.empty()) music.after_frame(*m, first_sample);
        if (m->wide_active) {
            ++wide_live_frames;
            if (!wide_was_live) { ++wide_stretches; wide_was_live = true; }
        } else {
            wide_was_live = false;
        }
        if (m->special_entry && !entry_frames++) entry_first = m->frame_count;
        if (trace_wide) {
            const patches::CameraRange cr = patches::camera_range(*m);
            std::printf("WCAM f=%llu active=%d cam=%d,%d range=%d..%d\n", (unsigned long long)m->frame_count,
                        m->wide_active ? 1 : 0, patches::camera_x(*m), patches::camera_y(*m), cr.left, cr.right);
            static int pa = -1, pr = -1, ps = -1, pz = -1, pl = -1, pm = -1;
            const int a = m->wide_active ? 1 : 0, rg = m->vdp.reg[16], sh = m->plane_shift;
            const int z = (m->wram[0xDFF2] << 8) | m->wram[0xDFF3];
            const int lv = (m->wram[0xDFF4] << 8) | m->wram[0xDFF5];
            const int md = (m->wram[0xDFDE] << 8) | m->wram[0xDFDF];
            if (a != pa || rg != pr || sh != ps || z != pz || lv != pl || md != pm) {
                std::printf("WIDE f=%llu active=%d reg16=%02X shift=%d zone=%d level=%d mode=%04X seen=%lld\n",
                            (unsigned long long)m->frame_count, a, rg, sh, z, lv, md,
                            m->level_seen_frame == ~0ull ? -1LL : (long long)(m->frame_count - m->level_seen_frame));
                pa = a; pr = rg; ps = sh; pz = z; pl = lv; pm = md;
            }
        }
        // The margins go live a frame or two after they are asked for (the
        // level engine's plane update has to run), so report when, not that.
        if (m->wide_active && !wide_live_reported) {
            wide_live_reported = true;
            std::printf("widescreen: margins live from frame %llu\n", (unsigned long long)m->frame_count);
        }
        if (level_select_on && ls.running) {
            const bool was_reached = ls.reached;
            if (ls.update(*m))
                std::printf("level select: the level is over at frame %llu (mode %04X zone %u level %u)\n",
                            (unsigned long long)m->frame_count, unsigned(m->wram[0xDFDE] << 8 | m->wram[0xDFDF]),
                            unsigned(m->wram[0xDFF2] << 8 | m->wram[0xDFF3]), unsigned(m->wram[0xDFF4] << 8 | m->wram[0xDFF5]));
            else if (ls.reached && !was_reached)
                std::printf("level select: reached scene %04X:%04X:%04X at frame %llu, save slot %04X\n",
                            unsigned(ls.scene >> 32), unsigned((ls.scene >> 16) & 0xFFFF), unsigned(ls.scene & 0xFFFF),
                            (unsigned long long)m->frame_count, unsigned(m->wram[0xE01C] << 8 | m->wram[0xE01D]));
        }
        if (ta.update(*m)) {
            if (!ghost_record_path.empty()) {
                ghost::Ghost g;
                const bool keep = ta.timed() && !ta.timed_out() && ghost_recorder.finish(*m, ta.time, 0, g);
                if (!keep) {
                    std::printf("ghost: the run was not recorded (no time)\n");
                } else {
                    const std::vector<uint8_t> bytes = ghost::encode(g);
                    if (!savestate::write_file(ghost_record_path, bytes)) {
                        std::fprintf(stderr, "cannot write %s\n", ghost_record_path.c_str());
                        return 2;
                    }
                    std::printf("ghost: recorded %s, %zu frames, time %s (%d frames), %zu bytes (start state %zu)\n",
                                ghost_record_path.c_str(), g.inputs.size(), stage_select::format_time(g.time).c_str(),
                                g.time, bytes.size(), g.start.size());
                }
            }
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
        if (speedrun_on) {
            const double now = double(m->frame_count) / 60.0;
            if (speedrun_start >= 0 && f == uint64_t(speedrun_start)) {
                run.set_place_names(stage_select::place_names(m->rom));
                run.start(now, false);
                std::printf("speedrun: run started by hand at frame %llu\n", (unsigned long long)m->frame_count);
            }
            const unsigned ev = run.update(*m, now);
            if (ev & speedrun::kReset)
                std::printf("speedrun: run reset at frame %llu after %zu split(s)\n", (unsigned long long)m->frame_count,
                            run.ended_splits().size());
            if (ev & speedrun::kStarted)
                std::printf("speedrun: run started at frame %llu\n", (unsigned long long)m->frame_count);
            if (ev & speedrun::kSplit) {
                const speedrun::Split& s = run.splits().back();
                std::printf("speedrun: split %zu %s at frame %llu, level %s (%d frames), game time %s (%d frames), real time %s\n",
                            run.splits().size(), s.name.c_str(), (unsigned long long)s.frame,
                            speedrun::format_game(s.level_frames).c_str(), s.level_frames,
                            speedrun::format_game(s.igt_frames).c_str(), s.igt_frames,
                            speedrun::format_rta(s.rta_ms).c_str());
            }
            livesplit::publish(m.get(), run);
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
            if (ref->sprites) {
                for (int b = 0; b < 2; ++b) {
                    const std::string sd = compare_sprite_lists(ref->sprites->list(b), m->sprites->list(b));
                    if (sd.empty()) continue;
                    std::printf("LOCKSTEP DIVERGENCE at frame %llu (reference vs recompiled): sprite list of buffer %d: %s\n",
                                (unsigned long long)m->frame_count, b, sd.c_str());
                    return 3;
                }
            }
            ++lockstep_ok_frames;
        }
        if (m->sprites) {
            const SpriteList& shown = m->sprites->displayed(*m);
            if (sprites_log.count(m->frame_count)) {
                std::printf("sprites: frame %llu, buffer %d on display, %zu draws\n", (unsigned long long)m->frame_count,
                            m->mars.fb_display, shown.draws.size());
                for (size_t k = 0; k < shown.draws.size(); ++k) std::printf("  %3zu %s\n", k, describe(shown.draws[k]).c_str());
            }
            // Each picture once: a buffer's list starts again (a new serial)
            // when it becomes the back buffer.
            const uint64_t key = shown.serial << 1 | m->mars.fb_display;
            if (sprites_check && key != sprites_key) {
                sprites_key = key;
                const SpriteCheck r = check_sprites(*m, *m->sprites, sprite_canvas);
                ++sprite_pictures;
                sprite_total.compared += r.compared;
                sprite_total.wrong += r.wrong;
                sprite_total.missing += r.missing;
                sprite_total.extra += r.extra;
                sprite_total.covered += r.covered;
                if (r.mismatches()) {
                    ++sprite_bad_pictures;
                    if (sprite_bad_pictures <= 8) report_sprite_mismatch(*m, r, shown);
                }
                if (m->sprites->pack()) sprite_others_changed += check_composed_others(*m, *m->sprites);
            }
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
                // The special stage's own picture is its rectangle; the
                // widened one also draws the 16 px borders around it. Its
                // polygons are clipped there natively and not when widened,
                // and a clipped polygon's edge can land a pixel apart, so
                // there a frame may differ along polygon edges (measured: at
                // most 1.8% of the picture) but not in what is drawn.
                // The bonus stage and the results screen are drawn by the
                // same code with the screen's rectangle (issues #38, #39).
                const bool special = m->wide_special;
                const int16_t* rect = patches::PolyScene(m->poly_scene) == patches::PolyScene::Stage
                                          ? patches::kSpecialRect : patches::kFullRect;
                const int x0 = special ? rect[0] : 0;
                const int x1 = special ? rect[1] + 1 : nat->fb_width;
                const int y0 = special ? rect[2] : 0;
                const int y1 = special ? rect[3] + 1 : nat->fb_height;
                const int allowed = special ? (x1 - x0) * (y1 - y0) / 40 : 0;
                int differ = 0, dx = -1, dy = -1;
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x)
                        if (nat->framebuffer[y * kScreenWidth + x] != m->framebuffer[y * kScreenWidth + x + m->fb_extra]) {
                            if (!differ++) { dx = x; dy = y; }
                        }
                if (differ > allowed) {
                    std::printf("CENTRE MISMATCH at frame %llu, native pixel (%d,%d), %d pixels: widescreen rendering changed the 4:3 image\n",
                                (unsigned long long)m->frame_count, dx, dy, differ);
                    return 4;
                }
                centre_worst = std::max(centre_worst, differ);
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
            // With a ghost, the picture as the player sees it.
            const uint32_t* pic = ghost_player.active() && !ghost_picture.empty() ? ghost_picture.data() : m->framebuffer;
            std::vector<uint32_t> overlay;
            if (input_display_on) {
                // Bottom left of the picture, a pixel to a unit; every pixel
                // outside its box is checked to be the picture's own.
                overlay.assign(pic, pic + kScreenWidth * kScreenHeight);
                const int x0 = m->fb_extra + 8, y0 = m->fb_height - 8 - input_display::kHeight;
                input_display::draw(overlay.data(), kScreenWidth, m->fb_width, m->fb_height, x0, y0, 1, m->input.pad[0], 255);
                for (int y = 0; y < m->fb_height; ++y)
                    for (int x = 0; x < m->fb_width; ++x) {
                        const size_t k = size_t(y) * kScreenWidth + size_t(x);
                        if (overlay[k] == pic[k]) continue;
                        const bool inside = x >= x0 && x < x0 + input_display::kWidth && y >= y0 &&
                                            y < y0 + input_display::kHeight;
                        if (inside) ++input_display_changed; else ++input_display_outside;
                    }
                ++input_display_shots;
                pic = overlay.data();
            }
            write_png(name, pic, m->fb_width, m->fb_height, kScreenWidth);
            std::printf("wrote %s hash=%016llx\n", name,
                        (unsigned long long)image_hash(pic, m->fb_width, m->fb_height, kScreenWidth));
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
        if (state_hash_every && m->frame_count % state_hash_every == 0)
            std::printf("statehash %llu %016llx\n", (unsigned long long)m->frame_count,
                        (unsigned long long)savestate::state_hash(*m));
        if (auto it = save_states.find(m->frame_count); it != save_states.end()) {
            const std::vector<uint8_t> file = savestate::save(*m);
            for (const std::string& path : it->second) {
                if (!savestate::write_file(path, file)) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return 2; }
                std::printf("state: saved %s at frame %llu (%zu bytes, %zu raw)\n", path.c_str(),
                            (unsigned long long)m->frame_count, file.size(), savestate::raw_size(savestate::kPicture));
            }
        }
        if (rewind_interval && m->frame_count % uint64_t(rewind_interval) == 0) {
            const auto r0 = std::chrono::steady_clock::now();
            rewind.push(*m);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - r0).count();
            rewind_push_ms += ms;
            rewind_push_worst = std::max(rewind_push_worst, ms);
            ++rewind_pushes;
            std::vector<uint8_t> raw;
            savestate::serialize(*m, raw, 0);
            uint64_t h = 1469598103934665603ull;
            for (uint8_t b : raw) h = (h ^ b) * 1099511628211ull;
            rewind_hashes[m->frame_count] = h;
        }
        for (const Peek& p : peeks)
            if (m->frame_count % p.every == 0) {
                std::printf("peek %llu %04X ", (unsigned long long)m->frame_count, unsigned(p.addr));
                for (uint32_t i = 0; i < p.bytes; ++i) std::printf("%02X", m->wram[(p.addr + i) & 0xFFFF]);
                std::printf("\n");
            }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("ran %llu frames in %.2fs (%.1f fps)\n", (unsigned long long)frames, secs, frames / secs);
    if (m->wide_enabled)
        std::printf("widescreen: margins live for %llu frames in %d stretches\n",
                    (unsigned long long)wide_live_frames, wide_stretches);
    if (entry_frames)
        std::printf("widescreen: the way into the special stage wide from frame %llu for %llu frames\n",
                    (unsigned long long)entry_first, (unsigned long long)entry_frames);
    print_recomp_stats(*m);
    // Whether the game saved, and when: tests check that a finished level
    // reaches the cartridge save, and that a time attack run does not.
    std::printf("save RAM changed on %llu frames", (unsigned long long)sram_frames);
    if (sram_frames) std::printf(", the last at frame %llu", (unsigned long long)sram_last);
    std::printf("\n");
    if (hot_blocks) print_hot_blocks(*m, hot_blocks);
    if (profile) {

        static const char* names[PROF_COUNT] = {"68K", "Z80", "MSH2", "SSH2", "video", "audio/pwm"};
        double total = secs * 1e9;
        std::printf("profile (ms/frame):");
        for (int i = 0; i < PROF_COUNT; ++i)
            std::printf(" %s %.3f (%.0f%%)", names[i], m->stats.prof_ns[i] / 1e6 / double(frames), 100.0 * m->stats.prof_ns[i] / total);
        std::printf(" | total %.3f\n", total / 1e6 / double(frames));
        std::printf("worst frame %.3f ms (frame %llu), %llu frames over 8 ms\n", worst_ms,
                    (unsigned long long)worst_frame, (unsigned long long)slow_frames);
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
    if (compare_native && centre_worst) std::printf("centre check: special stage frames differ along polygon edges by at most %d pixels\n", centre_worst);
    if (!ghost_path.empty()) {
        std::printf("ghost: drawn on %llu of %llu frames, %llu pixels, %llu outside its box, %llu frames misplaced; "
                    "%.3f ms a frame on average, %.3f ms worst\n",
                    (unsigned long long)gt.drawn, (unsigned long long)gt.frames, (unsigned long long)gt.pixels,
                    (unsigned long long)gt.outside, (unsigned long long)gt.misplaced,
                    gt.frames ? gt.ms / double(gt.frames) : 0.0, gt.worst_ms);
        if (ghost_waiting) std::printf("ghost: never started (the live run's stage was not taken)\n");
    }
    if (speedrun_on) {
        const livesplit::Block& b = livesplit::block();
        std::printf("speedrun: %zu split(s), game time %s (%d frames), real time %s\n", run.splits().size(),
                    speedrun::format_game(run.igt_frames()).c_str(), run.igt_frames(),
                    speedrun::format_rta(run.rta_ms(double(m->frame_count) / 60.0)).c_str());
        std::printf("livesplit: block \"%s\" version %u size %u, work RAM %s, state %u, run %u, %u split(s), game time %u frames\n",
                    b.magic, b.version, b.size,
                    b.wram == uint64_t(reinterpret_cast<uintptr_t>(m->wram)) ? "found" : "WRONG", b.state, b.run_id,
                    b.splits, b.igt_frames);
    }
    if (input_display_on)
        std::printf("input display: drawn on %llu shots, %llu pixels inside its box, %llu outside\n",
                    (unsigned long long)input_display_shots, (unsigned long long)input_display_changed,
                    (unsigned long long)input_display_outside);
    if (!title_hold_events.empty())
        std::printf("title hold: the idle counter held on %llu frames\n", (unsigned long long)title_held_frames);
    if (ref) std::printf("lockstep: %llu frames bit-identical between interpreter and recompiled execution\n", (unsigned long long)lockstep_ok_frames);
    if (character_palettes)
        std::printf("characters: displayed palette worked out %llu times (%llu blocks left as they were), %d colour block(s) replaced at the end\n",
                    (unsigned long long)character_palettes->recomputed(), (unsigned long long)character_palettes->refused(),
                    character_palettes->substituted());
    if (m->sprites) {
        const SpriteRecorder& sr = *m->sprites;
        std::printf("sprites: %llu draws recorded (%llu not in the bank table, %llu unreadable), %llu flips\n",
                    (unsigned long long)sr.draws_recorded, (unsigned long long)sr.unknown_graphics,
                    (unsigned long long)sr.bad_graphics, (unsigned long long)sr.flips);
        if (sr.pack())
            std::printf("sprites: %llu pictures composed with replacements\n", (unsigned long long)sr.pictures_composed);
        if (sr.dumping) std::printf("sprites: %d graphics dumped\n", sprites_dumped);
    }
    if (sprites_check)
        std::printf("sprites check: %llu pictures, %llu bytes compared, %llu mismatches (wrong %llu, missing %llu, extra %llu) in %llu pictures, %llu bytes drawn over by polygons or fills\n",
                    (unsigned long long)sprite_pictures, (unsigned long long)sprite_total.compared,
                    (unsigned long long)sprite_total.mismatches(), (unsigned long long)sprite_total.wrong,
                    (unsigned long long)sprite_total.missing, (unsigned long long)sprite_total.extra,
                    (unsigned long long)sprite_bad_pictures, (unsigned long long)sprite_total.covered);
    if (sprites_check && m->sprites->pack())
        std::printf("sprites check: %llu bytes of polygons and fills shown changed by the replacements\n",
                    (unsigned long long)sprite_others_changed);
    if (!export_sheets_dir.empty()) {
        std::vector<std::string> messages;
        const int n = export_sprite_sheets(*m, export_sheets_dir, &messages);
        for (const auto& msg : messages) std::printf("sheets: %s\n", msg.c_str());
        std::printf("sheets: %d written to %s\n", n, export_sheets_dir.c_str());
    }
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
    if (rewind_interval && rewind_pushes) {
        // Every snapshot still held, newest first, must be the state it was
        // taken from.
        const size_t held = rewind.snapshots();
        const uint64_t oldest = rewind.oldest_frame(), newest = rewind.newest_frame();
        std::printf("rewind: %llu snapshots every %d frames, push %.3f ms average, %.3f ms worst (%.3f ms per frame)\n",
                    (unsigned long long)rewind_pushes, rewind_interval, rewind_push_ms / double(rewind_pushes),
                    rewind_push_worst, rewind_push_ms / double(frames - start_frame));
        std::printf("rewind: %zu held in %.1f MB of %zu MB, frames %llu..%llu (%.1f s)\n", held,
                    double(rewind.memory()) / 1048576.0, rewind_budget_mb, (unsigned long long)oldest,
                    (unsigned long long)newest, double(newest - oldest) / 60.0);
        size_t verified = 0, wrong = 0;
        double step_worst = 0;
        std::vector<uint8_t> raw;
        for (;;) {
            const auto r0 = std::chrono::steady_clock::now();
            const bool more = rewind.step_back(*m);
            step_worst = std::max(step_worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - r0).count());
            savestate::serialize(*m, raw, 0);
            uint64_t h = 1469598103934665603ull;
            for (uint8_t b : raw) h = (h ^ b) * 1099511628211ull;
            auto it = rewind_hashes.find(m->frame_count);
            if (it == rewind_hashes.end() || it->second != h) ++wrong; else ++verified;
            if (!more) break;
        }
        std::printf("rewind: stepped back through %zu snapshots, %zu restored exactly, %zu wrong, step %.3f ms worst\n",
                    verified + wrong, verified, wrong, step_worst);
        if (wrong || verified != held) return 7;
    }
    if (sprites_check && sprite_total.mismatches()) return 6;
    return hash_failures ? 4 : 0;
}
