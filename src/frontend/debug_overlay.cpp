#include "debug_overlay.h"
#include "game/recomp_dispatch.h"
#include "runtime/system.h"
#include <cstdarg>
#include <cstdio>

namespace chaotix {

namespace {
std::string fmt(const char* f, ...) {
    char b[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}
} // namespace

std::vector<std::string> build_debug_overlay(const Machine& m, const FrameTiming& t, int page, const std::string& video_line,
                                             const std::vector<uint32_t>& watches) {
    std::vector<std::string> out;
    const DispatchSnapshot d = dispatch_snapshot();
    out.push_back(fmt("render %.1f fps  sim %.2f fps%s  frame %llu  display %uHz", t.render_fps, t.sim_fps,
                      t.fast_forward ? " (FF)" : "", (unsigned long long)m.frame_count, t.refresh_hz));
    out.push_back(fmt("emu %.2f ms/frame  present %.2f ms", t.emu_ms, t.present_ms));
    out.push_back(fmt("code: %s  native 68K %.1f%%  MSH2 %.1f%%  SSH2 %.1f%%", d.recompiled ? "recompiled" : "interpreter",
                      d.m68k_native_pct, d.sh2_native_pct[0], d.sh2_native_pct[1]));
    out.push_back(video_line);
    switch (page) {
    case 0:
        out.push_back(fmt("68K  PC %06X SR %04X SP %08X", m.m68k.pc, m68k::get_sr(&m.m68k), m.m68k.a[7]));
        out.push_back(fmt("MSH2 PC %08X SR %03X  SSH2 PC %08X SR %03X", m.sh2[0].pc, m.sh2[0].sr, m.sh2[1].pc, m.sh2[1].sr));
        out.push_back(fmt("32X mode %X FS %d  VDP r1 %02X r12 %02X  IRQ 68K %d M %d S %d", m.mars.bitmap_mode, m.mars.fb_display,
                          m.vdp.reg[1], m.vdp.reg[12], m.m68k.irq_level, m.sh2[0].irq_level, m.sh2[1].irq_level));
        out.push_back(fmt("COMM %04X %04X %04X %04X %04X %04X %04X %04X", m.mars.comm[0], m.mars.comm[1], m.mars.comm[2],
                          m.mars.comm[3], m.mars.comm[4], m.mars.comm[5], m.mars.comm[6], m.mars.comm[7]));
        break;
    case 1: {
        const auto& c = m.m68k;
        out.push_back(fmt("68K PC %06X SR %04X USP/SSP %08X", c.pc, m68k::get_sr(&c), c.other_sp));
        for (int i = 0; i < 8; i += 4)
            out.push_back(fmt("D%d %08X %08X %08X %08X", i, c.d[i], c.d[i + 1], c.d[i + 2], c.d[i + 3]));
        for (int i = 0; i < 8; i += 4)
            out.push_back(fmt("A%d %08X %08X %08X %08X", i, c.a[i], c.a[i + 1], c.a[i + 2], c.a[i + 3]));
        break;
    }
    case 2:
        for (int s = 0; s < 2; ++s) {
            const auto& c = m.sh2[s];
            out.push_back(fmt("%s PC %08X SR %03X PR %08X GBR %08X VBR %08X", s ? "SSH2" : "MSH2", c.pc, c.sr, c.pr, c.gbr, c.vbr));
            out.push_back(fmt(" MAC %08X:%08X", c.mach, c.macl));
            for (int i = 0; i < 16; i += 4)
                out.push_back(fmt(" R%-2d %08X %08X %08X %08X", i, c.r[i], c.r[i + 1], c.r[i + 2], c.r[i + 3]));
        }
        break;
    case 3:
        out.push_back("memory watches (68K address space):");
        for (uint32_t a : watches) {
            Machine& mm = const_cast<Machine&>(m);
            out.push_back(fmt(" %06X: %04X %04X %04X %04X", a, mm.m68k_read16(a), mm.m68k_read16(a + 2), mm.m68k_read16(a + 4),
                              mm.m68k_read16(a + 6)));
        }
        break;
    default: break;
    }
    out.push_back("F1 overlay  F2 aspect  F3 filter  F4 scaling  F5 page  F11 fullscreen  Tab fast-forward  F12 screenshot");
    return out;
}

} // namespace chaotix
