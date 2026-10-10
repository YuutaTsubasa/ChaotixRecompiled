// Runtime game patches (see patches.h).
#include "runtime/patches.h"
#include "runtime/system.h"
#include <algorithm>

namespace chaotix::patches {

namespace {

inline void set_low_word(uint32_t& r, uint32_t v) { r = (r & 0xFFFF0000u) | (v & 0xFFFFu); }
inline void set_low_byte(uint32_t& r, uint32_t v) { r = (r & 0xFFFFFF00u) | (v & 0xFFu); }

inline uint16_t wram16(const Machine& m, uint32_t a) {
    return uint16_t(m.wram[a] << 8 | m.wram[a + 1]);
}

void m68k_hook(m68k::State* c, uint32_t pc) {
    Machine* m = static_cast<Machine*>(c->hook_user);
    switch (pc) {
    case kFullRedrawA:
    case kFullRedrawB:
        // A full redraw re-establishes the ring, so a new shift is safe here.
        // The shift does not depend on how wide the margins are (the ring
        // always covers cam - 96 .. cam + 416), so it is latched from the
        // setting rather than from the current margin: the margins follow the
        // window, and a window resized during a level would otherwise leave
        // that level in 4:3 until the next load.
        // WORLD ENTRANCE is left native (see wide_zone).
        m->plane_shift = plane_shift_for(m->wide_enabled && wide_zone(*m) ? kMaxWideExtra : 0);
        // Also how a scene change is told apart from a level that has merely
        // stopped (see wide_scene_active): a new scene redraws the planes.
        m->full_redraw_frame = m->frame_count;
        return;
    case kPlaneUpdateA:
    case kPlaneUpdateB:
        m->level_seen_frame = m->frame_count;
        m->level_scene = scene_key(*m);
        return;
    case kSoundRequest: {
        // Music the host replaces with a file of its own: the driver is told
        // to stop its music instead, so its sound effects still play. The
        // request itself goes to the host either way (audio/music_mods.h).
        const uint8_t id = uint8_t(c->d[0]);
        if (id != 0 && (id < kFirstSoundEffect || id == kSoundLowerMusic || id == kSoundStopMusic)) {
            // Only the newest matter; a machine nobody drains stays small.
            if (m->music_events.size() >= 64) m->music_events.erase(m->music_events.begin());
            m->music_events.push_back(id);
            if (id < kFirstSoundEffect && m->music_replaced[id]) set_low_byte(c->d[0], kSoundStopMusic);
        }
        return;
    }
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
        const int e = m->wide_active && !m->wide_special ? std::clamp(m->wide_extra, 0, kMaxWideExtra) : 0;
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
        if (!e || !wide_zone(*m)) return;
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
        if (!e || !wide_zone(*m)) return;
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

void write_clip(Machine& m, int16_t left, int16_t right, uint32_t bottom) {
    uint8_t* p = m.sdram + kClipRectSdram;
    p[0] = uint8_t(uint16_t(left) >> 8);
    p[1] = uint8_t(left);
    p[2] = uint8_t(uint16_t(right) >> 8);
    p[3] = uint8_t(right);
    p[8] = uint8_t(bottom >> 24);
    p[9] = uint8_t(bottom >> 16);
    p[10] = uint8_t(bottom >> 8);
    p[11] = uint8_t(bottom);
}

int16_t special_rect(const Machine& m, int i) {
    const uint8_t* p = m.sdram + kSpecialRectSdram + 2 * i;
    return int16_t((p[0] << 8) | p[1]);
}

void write_special_rect(Machine& m, int16_t left, int16_t right) {
    uint8_t* p = m.sdram + kSpecialRectSdram;
    p[0] = uint8_t(uint16_t(left) >> 8);
    p[1] = uint8_t(left);
    p[2] = uint8_t(uint16_t(right) >> 8);
    p[3] = uint8_t(right);
}

// Our widened rectangle is still in place: the stage has not set another.
bool special_rect_is_ours(const Machine& m) {
    return m.special_rect_overridden && special_rect(m, 0) == 0 && special_rect(m, 1) >= 319 &&
           special_rect(m, 2) == kSpecialRect[2] && special_rect(m, 3) == kSpecialRect[3];
}

uint16_t sdram16(const Machine& m, uint32_t off) { return uint16_t((m.sdram[off] << 8) | m.sdram[off + 1]); }

// ADD #imm,Rn with any immediate.
bool is_add_to(uint16_t w, uint8_t reg) { return (w & 0xFF00) == (0x7000 | uint16_t(reg) << 8); }

// The stage's code is loaded, possibly with our centre in it.
bool special_projection_loaded(const Machine& m) {
    for (int i = 0; i < 8; ++i) {
        const uint16_t w = sdram16(m, kSpecialProjectSdram + 2 * uint32_t(i));
        const bool centre = i == 1 || i == 2;
        if (centre ? !is_add_to(w, 4) : w != kSpecialProjectCode[i]) return false;
    }
    return true;
}

// Puts the screen centre at 160 + shift in every projection.
void set_special_projection(Machine& m, int shift) {
    if (!special_projection_loaded(m)) return;
    for (const CentreAdd& c : kSpecialCentreAdds) {
        const uint16_t add = uint16_t(0x7000 | uint16_t(c.reg) << 8 | (80 + shift / 2));
        for (uint32_t i = 0; i < 2; ++i) {
            const uint32_t off = c.sdram + 2 * i;
            const uint16_t w = sdram16(m, off);
            if (w == add || !is_add_to(w, c.reg)) continue;
            m.sdram[off] = uint8_t(add >> 8);
            m.sdram[off + 1] = uint8_t(add);
            ++m.sdram_code_epoch;
        }
    }
}

} // namespace

bool special_stage_active(const Machine& m) {
    if (wram16(m, kGameMode) != kModeSpecialStage || !special_projection_loaded(m)) return false;
    if (special_rect_is_ours(m)) return true;
    for (int i = 0; i < 4; ++i)
        if (special_rect(m, i) != kSpecialRect[i]) return false;
    return true;
}

uint64_t scene_key(const Machine& m) {
    return (uint64_t(wram16(m, kGameMode)) << 32) | (uint64_t(wram16(m, kSceneZone)) << 16) |
           wram16(m, kSceneLevel);
}

bool wide_zone(const Machine& m) { return wram16(m, kSceneZone) != kWorldEntrance; }

bool wide_scene_active(const Machine& m) {
    if (!wide_zone(m)) return false;
    // INTRODUCTION's first encounter parks Eggman at screen X=-49 while the
    // script continues, and stages Metal Sonic outside the native view too.
    // $8A6CB8 sets C21C bit2 when that sequence starts; level initialization
    // clears it. Other levels reuse these camera flags, so keep this scoped.
    if (wram16(m, kSceneZone) == 6 && wram16(m, kSceneLevel) == 0 && (m.wram[0xC21C] & 0x04))
        return false;
    if (m.level_seen_frame == ~0ull) return false;
    if (m.level_seen_frame + 8 >= m.frame_count) return true;  // the level is running
    // The level engine has stopped, but it also stops while the game is
    // paused, and the game holds the level on screen then -- dropping the
    // margins here is what made the picture snap to 4:3 and back every time
    // someone paused. Nothing moves while the engine is stopped, so the ring
    // still holds the level's tiles. Keep them until the planes are claimed by
    // something else: begin_frame has already cleared level_seen_frame if the
    // scene changed, so all that is left to rule out is a redraw within the
    // same scene -- a level restarting after a death.
    return m.full_redraw_frame == ~0ull || m.full_redraw_frame <= m.level_seen_frame;
}

int camera_x(const Machine& m) { return int(int16_t((m.wram[kPlaneAStruct] << 8) | m.wram[kPlaneAStruct + 1])); }
CameraRange camera_range(const Machine& m) {
    CameraRange r;
    r.left = int16_t(wram16(m, (kPlaneAStruct + 0xA) & 0xFFFF));
    r.right = int16_t(wram16(m, (kPlaneAStruct + 0x8) & 0xFFFF));
    return r;
}

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
    // A different scene is running: the level whose tiles the ring holds is
    // over, whatever the plane geometry still says. (A pause keeps the key.)
    if (m.level_seen_frame != ~0ull && scene_key(m) != m.level_scene) m.level_seen_frame = ~0ull;
    const int e = std::clamp(m.wide_extra, 0, kMaxWideExtra);
    const int eb = std::clamp(m.wide_extra_bottom, 0, kMaxWideExtraBottom);
    // plane_shift is latched when a level loads, from the setting rather than
    // from the margin width, so resizing the window mid-level widens the view
    // at once; only turning widescreen off and on again waits for the next
    // load. The bottom rows need no patch, so they only wait for a level
    // scene.
    const bool level = (e > 0 || eb > 0) && (e == 0 || m.plane_shift > 0) &&
                       m.vdp.reg[16] == kLevelPlaneSize && wide_scene_active(m);
    // Only side margins in the special stage: its rectangle ends at 219.
    m.wide_special = !level && m.wide_enabled && e > 0 && special_stage_active(m);
    m.wide_active = level || m.wide_special;
    if (m.wide_special) {
        // Same multiple of 8 as the level clip, which also keeps the shift
        // even, so a moved fill stays word-aligned.
        const int ce = (e + 7) & ~7;
        write_special_rect(m, 0, int16_t(319 + 2 * ce));
        set_special_projection(m, ce);
        m.special_shift = ce;
        m.special_rect_overridden = true;
    } else if (m.special_rect_overridden) {
        // Put the stage's own rectangle and centre back, unless something
        // has already replaced them (the next scene sets its own).
        if (special_rect_is_ours(m)) write_special_rect(m, kSpecialRect[0], kSpecialRect[1]);
        set_special_projection(m, 0);
        m.special_shift = 0;
        m.special_rect_overridden = false;
    }
    if (level) {
        // The blitters write 16-bit words at clip-aligned addresses: an odd
        // bound makes the SH-2 raise an address error (it crashed at E = 53).
        // Draw a multiple of 8 columns; the shadow holds up to kMaxWideExtra.
        const int ce = (e + 7) & ~7;
        // The SH-2 blitters compare vertical bounds in 24.8 fixed point.
        write_clip(m, int16_t(-ce), int16_t(320 + ce),
                   uint32_t(kActiveLines - 1 + eb) << 8);
        m.clip_overridden = true;
    } else if (m.clip_overridden) {
        // Restore the native rectangle from the SDRAM image in the ROM.
        const uint8_t* r = m.rom.data.data() + kClipRectRom;
        const uint32_t bottom = (uint32_t(r[8]) << 24) | (uint32_t(r[9]) << 16) |
                                (uint32_t(r[10]) << 8) | r[11];
        write_clip(m, int16_t((r[0] << 8) | r[1]), int16_t((r[2] << 8) | r[3]), bottom);
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
    m.full_redraw_frame = ~0ull;
    m.level_scene = 0;
    m.wide_active = false;
    m.clip_overridden = false;
    m.wide_special = false;
    m.special_rect_overridden = false;
    m.special_shift = 0;
    m.cull_shift = 0;
    m.plane_fill_clamped = true;
}

} // namespace chaotix::patches
