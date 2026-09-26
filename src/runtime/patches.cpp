// Runtime game patches (see patches.h).
#include "runtime/patches.h"
#include "runtime/system.h"
#include <algorithm>

namespace chaotix::patches {

namespace {

inline void set_low_word(uint32_t& r, uint32_t v) { r = (r & 0xFFFF0000u) | (v & 0xFFFFu); }

void m68k_hook(m68k::State* c, uint32_t pc) {
    Machine* m = static_cast<Machine*>(c->hook_user);
    switch (pc) {
    case kFullRedrawA:
    case kFullRedrawB:
        // A full redraw re-establishes the ring, so a new shift is safe here.
        m->plane_shift = plane_shift_for(std::clamp(m->wide_extra, 0, kMaxWideExtra));
        return;
    case kPlaneUpdateA:
    case kPlaneUpdateB:
        m->level_seen_frame = m->frame_count;
        return;
    case kModeDispatch:
        // The game is between scenes: a stage the host asked for can start.
        if (m->stage_pending) {
            m->stage_pending = false;
            stage_select::apply(*m, m->stage_request);
        }
        return;
    case kRingCullLo:
    case kRingCullHi:
    case kRingCullDone:
    case kRingCullExit: {
        const int e = m->wide_active ? std::clamp(m->wide_extra, 0, kMaxWideExtra) : 0;
        int want = 0;
        if (pc == kRingCullLo) want = e;
        else if (pc == kRingCullHi) want = -e;
        set_low_word(c->d[2], c->d[2] + uint32_t(want - m->cull_shift));
        m->cull_shift = want;
        return;
    }
    default: break;
    }
    if (pc == kCamClampMax || pc == kCamClampMin) {
        const int e = std::clamp(m->wide_extra, 0, kMaxWideExtra);
        if (!e) return;
        const int right = int16_t(m68k::rd16(c, c->a[1] + 8));
        const int left = int16_t(m68k::rd16(c, c->a[1] + 0xA));
        int lo = left + e, hi = right - e;
        if (lo > hi) lo = hi = (left + right) / 2;  // room narrower than the view: centre it
        set_low_word(c->d[1], uint32_t(pc == kCamClampMax ? hi : lo));
        return;
    }
    if (pc == kCamClampBottom) {
        // The extra rows are below the screen, so only the bottom bound moves.
        const int e = std::clamp(m->wide_extra_bottom, 0, kMaxWideExtraBottom);
        if (!e) return;
        const int bottom = int16_t(m68k::rd16(c, c->a[1] + 0xC));
        const int top = int16_t(m68k::rd16(c, c->a[1] + 0xE));
        set_low_word(c->d[1], uint32_t(std::max(bottom - e, top)));
        return;
    }
    const int w = m->plane_shift;
    if (!w || m->vdp.reg[16] != kLevelPlaneSize) return;
    switch (pc) {
    case kFillSplitX: {
        // Same transform as the plane.x the current fill was given.
        int x = int(int16_t(c->d[2])) - w;
        set_low_word(c->d[2], uint32_t(m->plane_fill_clamped ? std::max(x, 0) : x));
        return;
    }
    case kRowsA_X: case kRowUpA_X: case kRowDownA_X: case kColLeftA_X: case kColRightA_X: {
        // Unmasked world X: keep it inside the level (the native ring never
        // starts left of 0 either).
        m->plane_fill_clamped = true;
        int x = int(int16_t(c->d[0])) - w;
        set_low_word(c->d[0], uint32_t(std::max(x, 0)));
        return;
    }
    case kRowsB_X: case kRowUpB_X: case kRowDownB_X: case kColLeftB_X: case kColRightB_X:
        // X is masked to the plane's repeat width by the fill routine.
        m->plane_fill_clamped = false;
        set_low_word(c->d[0], c->d[0] - uint32_t(w));
        return;
    default: return;
    }
}

void write_clip(Machine& m, int16_t left, int16_t right) {
    uint8_t* p = m.sdram + kClipRectSdram;
    p[0] = uint8_t(uint16_t(left) >> 8);
    p[1] = uint8_t(left);
    p[2] = uint8_t(uint16_t(right) >> 8);
    p[3] = uint8_t(right);
}

} // namespace

bool wide_scene_active(const Machine& m) {
    return m.level_seen_frame != ~0ull && m.level_seen_frame + 8 >= m.frame_count;
}

int camera_x(const Machine& m) { return int(int16_t((m.wram[kPlaneAStruct] << 8) | m.wram[kPlaneAStruct + 1])); }
int camera_y(const Machine& m) { return int(int16_t((m.wram[(kPlaneAStruct + 0x10) & 0xFFFF] << 8) | m.wram[(kPlaneAStruct + 0x11) & 0xFFFF])); }

MarginCut margin_cut(const Machine& m, int extra) {
    // Only the part of the left margin left of the level's origin is blacked
    // out: there is no layout there, so the ring holds unrelated tiles. The
    // camera clamp keeps the margins inside the level's camera range while it
    // can; where a room is narrower than the view the margins show the tiles
    // the engine streamed anyway, which continue the room's scenery.
    MarginCut c;
    c.left = std::clamp(extra - camera_x(m), 0, extra);
    return c;
}

void begin_frame(Machine& m) {
    const int e = std::clamp(m.wide_extra, 0, kMaxWideExtra);
    const int eb = std::clamp(m.wide_extra_bottom, 0, kMaxWideExtraBottom);
    // plane_shift is latched when a level loads: turning widescreen on in the
    // middle of a level shows bars until the next load. The bottom rows need
    // no patch, so they only wait for a level scene.
    m.wide_active = (e > 0 || eb > 0) && (e == 0 || m.plane_shift > 0) &&
                    m.vdp.reg[16] == kLevelPlaneSize && wide_scene_active(m);
    if (m.wide_active) {
        // The blitters write 16-bit words at clip-aligned addresses: an odd
        // bound makes the SH-2 raise an address error (it crashed at E = 53).
        // Draw a multiple of 8 columns; the shadow holds up to kMaxWideExtra.
        const int ce = (e + 7) & ~7;
        write_clip(m, int16_t(-ce), int16_t(320 + ce));
        m.clip_overridden = true;
    } else if (m.clip_overridden) {
        // Restore the native rectangle from the SDRAM image in the ROM.
        const uint8_t* r = m.rom.data.data() + kClipRectRom;
        write_clip(m, int16_t((r[0] << 8) | r[1]), int16_t((r[2] << 8) | r[3]));
        m.clip_overridden = false;
    }
}

void install(Machine& m) {
    m.m68k.hook = m68k_hook;
    m.m68k.hook_user = &m;
    m.m68k.hook_pcs = kM68kHooks;
    m.m68k.hook_count = uint32_t(sizeof kM68kHooks / sizeof kM68kHooks[0]);
    m.plane_shift = 0;
    m.level_seen_frame = ~0ull;
    m.wide_active = false;
    m.clip_overridden = false;
    m.cull_shift = 0;
    m.plane_fill_clamped = true;
}

} // namespace chaotix::patches
