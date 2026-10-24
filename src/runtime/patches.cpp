// Runtime game patches (see patches.h).
#include "runtime/patches.h"
#include "runtime/lobby.h"
#include "runtime/system.h"
#include "runtime/tether.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace chaotix::patches {

namespace {

inline void set_low_word(uint32_t& r, uint32_t v) { r = (r & 0xFFFF0000u) | (v & 0xFFFFu); }
inline void set_low_byte(uint32_t& r, uint32_t v) { r = (r & 0xFFFFFF00u) | (v & 0xFFu); }

inline uint16_t wram16(const Machine& m, uint32_t a) {
    return uint16_t(m.wram[a] << 8 | m.wram[a + 1]);
}

// Issue #36: more copies of a backdrop object's two pieces in the margins
// (see kBackdropMaps). Runs at kPiecesDone, after the last piece is pushed.
void extend_backdrop(Machine& m, m68k::State* c) {
    if (!m.wide_active || m.wide_special) return;
    const uint32_t map = m68k::rd32(c, c->a[0] + 0x10) & 0xFFFFFF;
    if (std::find(std::begin(kBackdropMaps), std::end(kBackdropMaps), map) == std::end(kBackdropMaps)) return;
    if ((c->a[3] & 0xFFFFFF) != map + 8) return;  // both pieces were just pushed
    const uint16_t p0 = uint16_t(m68k::rd16(c, map)), w0 = uint16_t(m68k::rd16(c, map + 2));
    const uint16_t p1 = uint16_t(m68k::rd16(c, map + 4)), w1 = uint16_t(m68k::rd16(c, map + 6));
    const int h = int8_t(p1);
    if ((p0 >> 8) != (p1 >> 8) || int8_t(p0) != -h || h <= 0 || (w0 >> 8) != (w1 >> 8) || (w0 & 0xFF) != 0 ||
        (w1 & 0xFF) != 0x80)
        return;
    // The list holds the right piece at a5 and the left one 12 bytes above;
    // X is the command's word at +4.
    uint32_t a5 = c->a[5];
    const int right = int16_t(m68k::rd16(c, a5 + 4));
    const int left = int16_t(m68k::rd16(c, a5 + 12 + 4));
    const int period = 2 * h;
    if (right - left != period) return;
    uint16_t cmd[6];
    for (uint32_t i = 0; i < 6; ++i) cmd[i] = uint16_t(m68k::rd16(c, a5 + 2 * i));
    const int ce = (std::clamp(m.wide_extra, 0, kMaxWideExtra) + 7) & ~7;  // the clip rectangle's margin
    auto push = [&](int x) {
        if (((a5 - 12) & 0xFFFF) < kCommandListLimit) return false;
        a5 -= 12;
        cmd[2] = uint16_t(x);
        for (uint32_t i = 0; i < 6; ++i) m68k::wr16(c, a5 + 2 * i, cmd[i]);
        return true;
    };
    for (int x = left - period; x + h > -ce; x -= period)
        if (!push(x)) break;
    for (int x = right + period; x - h < kNativeWidth + ce; x += period)
        if (!push(x)) break;
    c->a[5] = a5;
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
    case kPiecesDone:
        extend_backdrop(*m, c);
        return;
    case kModeDispatch:
        // Issue #43: a partner called back in SOLO goes away again next scene.
        tether::on_scene_change(*m);
        // The game is between scenes: a stage the host asked for can start.
        if (m->stage_pending) {
            m->stage_pending = false;
            stage_select::apply(*m, m->stage_request);
            return;
        }
        // The music player's machine (frontend/jukebox.h) goes to a scene of
        // the game's own: the mode the dispatcher is about to read, and the
        // sub-mode its handler starts from. Not before the boot scene (mode
        // 0) is over: it is what starts the sound driver and the interrupts.
        if (m->mode_request >= 0 && wram16(*m, kGameMode) != 0) {
            m->wram[kGameMode] = uint8_t(m->mode_request >> 8);
            m->wram[kGameMode + 1] = uint8_t(m->mode_request);
            m->wram[kGameMode + 2] = uint8_t(m->submode_request >> 8);
            m->wram[kGameMode + 3] = uint8_t(m->submode_request);
            m->mode_request = -1;
            return;
        }
        // Issue #44: the partner and time of day the player chose, put in
        // place before the lobby or an attraction loads (runtime/lobby.h).
        lobby::apply(*m);
        return;
    // Issue #43: the ring tether options (runtime/tether.h).
    case tether::kCharsUpdate: tether::on_chars_update(*m); return;
    case tether::kForce: tether::on_force(*m, c); return;
    case tether::kPartnerSide: tether::on_partner_side(*m, c); return;
    case tether::kClampBefore: tether::on_clamp_before(*m, c); return;
    case tether::kClampAfter: tether::on_clamp_after(*m, c); return;
    case tether::kBothObjects[0].pc: case tether::kBothObjects[1].pc: case tether::kBothObjects[2].pc:
    case tether::kBothObjects[3].pc: case tether::kBothObjects[4].pc: case tether::kBothObjects[5].pc:
        tether::on_both_object(*m, c, pc);
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

// The rectangle the widened scene had before (the stage's, or the screen's).
const int16_t* native_rect(PolyScene k) { return k == PolyScene::Stage ? kSpecialRect : kFullRect; }

// Our widened rectangle for a scene of this kind is still in place: the game
// has not set another.
bool special_rect_is_ours(const Machine& m, PolyScene k) {
    const int16_t* r = native_rect(k);
    return m.special_rect_overridden && PolyScene(m.poly_scene) == k && special_rect(m, 0) == 0 &&
           special_rect(m, 1) >= 319 && special_rect(m, 2) == r[2] && special_rect(m, 3) == r[3];
}

bool rect_is(const Machine& m, const int16_t* r) {
    for (int i = 0; i < 4; ++i)
        if (special_rect(m, i) != r[i]) return false;
    return true;
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
        }
    }
}

} // namespace

// Puts the bonus stage's polygon centre at X 160 + shift (kBonusCentreSdram).
static void set_bonus_centre(Machine& m, int shift) {
    const uint16_t mov = uint16_t(0xEA00 | uint8_t(kBonusCentre + shift));
    const uint16_t w = sdram16(m, kBonusCentreSdram);
    if ((w & 0xFF00) != 0xEA00 || sdram16(m, kBonusCentreSdram + 2) != kBonusCentreNext || w == mov) return;
    m.sdram[kBonusCentreSdram] = uint8_t(mov >> 8);
    m.sdram[kBonusCentreSdram + 1] = uint8_t(mov);
}

// Puts the HUD's base X at kHudX - shift (see kHudBaseX).
void set_hud_shift(Machine& m, int shift) {
    const uint16_t mov = uint16_t(0xE100 | uint8_t(int8_t(kHudX - shift)));
    for (const HudBase& h : kHudBaseX) {
        const uint16_t w = sdram16(m, h.sdram);
        if ((w & 0xFF00) != 0xE100 || sdram16(m, h.sdram + 2) != h.next || w == mov) continue;
        m.sdram[h.sdram] = uint8_t(mov >> 8);
        m.sdram[h.sdram + 1] = uint8_t(mov);
    }
}

bool special_stage_active(const Machine& m) {
    if (!special_projection_loaded(m)) return false;
    return special_rect_is_ours(m, PolyScene::Stage) || rect_is(m, kSpecialRect);
}

namespace {

// The results screen's vertex X, widened by a margin of ce (see PolyScene).
int16_t results_x(int16_t x, int ce) {
    if (x <= 0) return x;
    if (x >= 319) return int16_t(x + 2 * ce);
    return int16_t(x + ce);
}

// Whether SDRAM holds the results screen's polygon list as the ROM's image
// has it, its vertices widened by ce (0: unchanged). With write (the
// machine's SDRAM), puts the vertices there instead.
bool results_table(const Machine& m, int ce, uint8_t* write = nullptr) {
    const uint8_t* rom = m.rom.data.data() + kSdramImageRom;
    const uint8_t* ram = m.sdram;
    auto rd16 = [](const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); };
    uint32_t o = kResultsTableSdram;
    while (o + 4 <= kResultsTableEnd) {
        const uint32_t head = uint32_t(rd16(rom + o)) << 16 | rd16(rom + o + 2);
        if (!write && std::memcmp(ram + o, rom + o, 4) != 0) return false;
        o += 4;
        if (!head) return true;
        for (uint32_t n = head & 0xFF; n; --n, o += 4) {
            if (o + 4 > kResultsTableEnd) return false;
            const uint16_t x = uint16_t(results_x(int16_t(rd16(rom + o)), ce));
            if (write) {
                write[o] = uint8_t(x >> 8);
                write[o + 1] = uint8_t(x);
            } else if (rd16(ram + o) != x || std::memcmp(ram + o + 2, rom + o + 2, 2) != 0) {
                return false;
            }
        }
    }
    return false;
}

bool results_code_loaded(const Machine& m) {
    return std::memcmp(m.sdram + kResultsCodeSdram, m.rom.data.data() + kSdramImageRom + kResultsCodeSdram,
                       kResultsTableSdram - kResultsCodeSdram) == 0;
}

// Plane A's colour (palette line and index) at plane pixel (x, y).
uint8_t plane_a_pixel(const Vdp& v, int x, int y) {
    const uint32_t base = uint32_t(v.reg[2] & 0x38) << 10;
    const uint32_t cell = uint32_t((y >> 3) & 31) * 64 + uint32_t((x >> 3) & 63);
    const uint16_t e = uint16_t(v.vram[(base + 2 * cell) & 0xFFFF] << 8 | v.vram[(base + 2 * cell + 1) & 0xFFFF]);
    int tx = x & 7, ty = y & 7;
    if (e & 0x0800) tx = 7 - tx;
    if (e & 0x1000) ty = 7 - ty;
    const uint8_t b = v.vram[(((e & 0x7FFu) << 5) + uint32_t(ty) * 4 + uint32_t(tx >> 1)) & 0xFFFF];
    const uint8_t c = (tx & 1) ? (b & 15) : (b >> 4);
    return c ? uint8_t(((e >> 9) & 0x30) | c) : 0;
}

// Whether plane A is drawn unscrolled on every line and in every column of
// the active screen, whatever the scroll modes (register 11).
bool plane_a_unscrolled(const Vdp& v) {
    const uint32_t hs = uint32_t(v.reg[13] & 0x3F) << 10;
    const int lines = (v.reg[11] & 3) >= 2 ? kActiveLines : 1;
    for (int y = 0; y < lines; ++y) {
        const uint32_t a = hs + uint32_t((v.reg[11] & 3) == 2 ? (y & ~7) : y) * 4;
        if (((v.vram[a & 0xFFFF] << 8) | v.vram[(a + 1) & 0xFFFF]) & 0x3FF) return false;
    }
    const int columns = (v.reg[11] & 4) ? 20 : 1;
    for (int c = 0; c < columns; ++c)
        if (v.vsram[2 * c] & 0x3FF) return false;
    return true;
}

// Distance of screen pixel (x, y) from the screen's centre, in quarter pixels
// (as Vdp::render_line measures it).
int radius4(int x, int y) {
    const double dx = double(2 * x + 1 - kNativeWidth), dy = double(2 * y + 1 - kActiveLines);
    return int(2.0 * std::sqrt(dx * dx + dy * dy));
}

} // namespace

bool results_screen_active(const Machine& m) {
    // It follows the stage, and only the stage: the stage's intro has the
    // same code and rectangle loaded for a few frames.
    const PolyScene k = PolyScene(m.poly_scene);
    if (k != PolyScene::Stage && k != PolyScene::Results) return false;
    if (!special_projection_loaded(m) || !results_code_loaded(m)) return false;
    if (!special_rect_is_ours(m, PolyScene::Results) && !rect_is(m, kFullRect)) return false;
    return results_table(m, m.results_table_patched ? m.special_shift : 0);
}

static_assert(sizeof(Machine::radial_lut) >= size_t(kRadialLen), "Machine::radial_lut holds kRadialLen entries");

bool bonus_tunnel(const Machine& m, uint8_t* lut) {
    const Vdp& v = m.vdp;
    // A 64x32-cell plane.
    if (v.reg[16] != kLevelPlaneSize) return false;
    // A quarter of a picture, mirrored into the other three: a cheap first
    // look at the corners' cells before the whole picture is measured.
    const uint32_t base = uint32_t(v.reg[2] & 0x38) << 10;
    auto cell = [&](int x, int y) {
        const uint32_t a = (base + 2 * uint32_t(y * 64 + x)) & 0xFFFF;
        return uint16_t(v.vram[a] << 8 | v.vram[a + 1]);
    };
    const uint16_t corner = cell(0, 0);
    if (!(corner & 0x7FF) || cell(39, 0) != (corner ^ 0x0800) || cell(0, 27) != (corner ^ 0x1000) ||
        cell(39, 27) != (corner ^ 0x1800))
        return false;
    // Not scrolled at all. In play the stage is called from inside a level
    // and keeps the level's scroll modes, line scroll in most zones (issue
    // #52), so every line's and every column's scroll is looked at, as
    // Vdp::render_line reads them.
    if (!plane_a_unscrolled(v)) return false;
    // Each radius's colour, by vote over the top left quarter of the picture
    // (it is drawn mirrored). Whole rings stop short of the corners (r 195).
    constexpr int kInPicture = 4 * 190;
    static_assert(kInPicture < kRadialLen, "");
    thread_local uint16_t votes[kInPicture][64];
    std::memset(votes, 0, sizeof votes);
    for (int y = 0; y < kActiveLines / 2; ++y)
        for (int x = 0; x < kNativeWidth / 2; ++x) {
            const int r4 = radius4(x, y);
            if (r4 < kInPicture) ++votes[r4][plane_a_pixel(v, x, y)];
        }
    for (int r4 = 0; r4 < kInPicture; ++r4) {
        int best = 0;
        for (int c = 1; c < 64; ++c)
            if (votes[r4][c] > votes[r4][best]) best = c;
        lut[r4] = uint8_t(best);
    }
    // From r = 80 out: runs of colours 1, 2 .. K of palette line 0, every
    // run in a ring as wide.
    constexpr int kFrom = 4 * 80;
    int pos = kFrom;
    while (pos < kInPicture && lut[pos] != 1) ++pos;
    int k = 0, last_c = 0, last_w4 = 0, last_end = -1;
    for (int run = pos; run < kInPicture;) {
        int end = run;
        while (end < kInPicture && lut[end] == lut[run]) ++end;
        if (end >= kInPicture) break;  // cut short where the picture ends
        const int c = lut[run];
        if (c == 1 && last_c) {
            if (k && k != last_c) return false;
            k = last_c;
        } else if (c < 1 || c > 15 || (last_c && c != last_c + 1)) {
            return false;
        }
        last_c = c;
        last_w4 = end - run;
        last_end = end;
        run = end;
    }
    if (last_end < 0 || k < 4 || last_w4 < 4) return false;
    // The rings must make the whole picture, not just the diagonal.
    int checked = 0, matching = 0;
    for (int y = 1; y < kActiveLines; y += 3)
        for (int x = 1; x < kNativeWidth; x += 3) {
            const int r4 = radius4(x, y);
            if (r4 < kFrom || r4 + 1 >= last_end) continue;
            const int c = plane_a_pixel(v, x, y);
            ++checked;
            if (c == lut[r4] || c == lut[r4 - 1] || c == lut[r4 + 1]) ++matching;
        }
    if (checked < 2000 || matching * 100 < checked * 97) return false;
    // Further out they go on as drawn: one index per w px, w one more for
    // every ring.
    int ring = (last_w4 + 2) / 4, c = last_c;
    for (pos = last_end; pos < kRadialLen;) {
        c = c % k + 1;
        if (c == 1) ++ring;
        for (int i = 0; i < 4 * ring && pos < kRadialLen; ++i) lut[pos++] = uint8_t(c);
    }
    return true;
}

void sh2_hook(Machine& m, sh2::State& c, uint32_t pc) {
    if (pc != kSh2BlitScaled || c.id != 0 || c.pr != kBonusItemReturn) return;
    if (!m.wide_special || PolyScene(m.poly_scene) != PolyScene::Bonus) return;
    // As far right as the centre the handler was given.
    const uint16_t mov = sdram16(m, kBonusCentreSdram);
    if ((mov & 0xFF00) != 0xEA00 || sdram16(m, kBonusCentreSdram + 2) != kBonusCentreNext) return;
    c.r[1] -= uint32_t(int(uint8_t(mov)) - kBonusCentre);
}

bool is_widened_clear(const Machine& m, uint32_t word_addr, uint32_t words, uint16_t data) {
    if (!m.wide_special || data != 0) return false;
    switch (PolyScene(m.poly_scene)) {
    case PolyScene::None:  // set by hand (tests): the stage, as it always was
    case PolyScene::Stage: return (word_addr & 0xFF) == kSpecialClearStart && words == kSpecialClearLen + 1u;
    case PolyScene::Bonus: return (word_addr & 0xFF) == 0 && words == kBonusClearLen + 1u;
    default: return false;
    }
}

uint64_t scene_key(const Machine& m) {
    return (uint64_t(wram16(m, kGameMode)) << 32) | (uint64_t(wram16(m, kSceneZone)) << 16) |
           wram16(m, kSceneLevel);
}

bool wide_zone(const Machine& m) { return m.wide_everywhere || wram16(m, kSceneZone) != kWorldEntrance; }

bool wide_scene_active(const Machine& m) {
    if (!wide_zone(m)) return false;
    // INTRODUCTION's first encounter parks Eggman at screen X=-49 while the
    // script continues, and stages Metal Sonic outside the native view too.
    // $8A6CB8 sets C21C bit2 when that sequence starts; level initialization
    // clears it. Other levels reuse these camera flags, so keep this scoped.
    if (!m.wide_everywhere && wram16(m, kSceneZone) == 6 && wram16(m, kSceneLevel) == 0 && (m.wram[0xC21C] & 0x04))
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
    // In play the bonus stage is called from inside a level ($892970) and
    // hands back to it: the game mode, zone and level stay the level's, the
    // level engine is merely stopped, and its plane size is the level's. So it
    // is looked for first, by its own picture, which no level has.
    const bool bonus = m.wide_enabled && e > 0 && special_projection_loaded(m) &&
                       (special_rect_is_ours(m, PolyScene::Bonus) || rect_is(m, kFullRect)) &&
                       bonus_tunnel(m, m.radial_lut);
    const bool level = !bonus && (e > 0 || eb > 0) && (e == 0 || m.plane_shift > 0) &&
                       m.vdp.reg[16] == kLevelPlaneSize && wide_scene_active(m);
    // The scenes drawn by the special stage's polygon code (PolyScene): the
    // stage itself (only side margins: its rectangle ends at 219), the
    // results screen after it, and the bonus stage.
    PolyScene want = PolyScene::None;
    if (bonus) want = PolyScene::Bonus;
    else if (!level && m.wide_enabled && e > 0) {
        if (special_stage_active(m)) want = PolyScene::Stage;
        else if (results_screen_active(m)) want = PolyScene::Results;
    }
    // Same multiple of 8 as the level clip, which also keeps the shift even,
    // so a moved fill stays word-aligned.
    const int ce = (e + 7) & ~7;
    const PolyScene had = PolyScene(m.poly_scene);
    if (had != PolyScene::None && (want != had || m.special_shift != ce)) {
        // Put the scene's own rectangle, centre and polygons back, unless
        // something has already replaced them (the next scene sets its own).
        if (special_rect_is_ours(m, had)) write_special_rect(m, native_rect(had)[0], native_rect(had)[1]);
        set_special_projection(m, 0);
        set_bonus_centre(m, 0);
        if (m.results_table_patched && results_table(m, m.special_shift)) results_table(m, 0, m.sdram);
        if (m.bonus_clip_overridden) {
            const uint8_t* p = m.sdram + kClipRectSdram;
            if (int16_t(p[0] << 8 | p[1]) == -m.special_shift && int16_t(p[2] << 8 | p[3]) == 320 + m.special_shift)
                write_clip(m, 0, 320, uint32_t(p[8]) << 24 | uint32_t(p[9]) << 16 | uint32_t(p[10]) << 8 | p[11]);
        }
        m.results_table_patched = false;
        m.bonus_clip_overridden = false;
        m.special_shift = 0;
        m.special_rect_overridden = false;
        m.poly_scene = uint8_t(PolyScene::None);
    }
    m.wide_special = want != PolyScene::None;
    m.wide_active = level || m.wide_special;
    m.radial_len = want == PolyScene::Bonus ? kRadialLen : 0;
    if (m.wide_special) {
        m.poly_scene = uint8_t(want);
        write_special_rect(m, 0, int16_t(319 + 2 * ce));
        // Where each scene's polygons get their X: the stage's projections,
        // the bonus stage's centre, the results screen's list.
        if (want == PolyScene::Stage) set_special_projection(m, ce);
        if (want == PolyScene::Bonus) set_bonus_centre(m, ce);
        m.special_shift = ce;
        m.special_rect_overridden = true;
        if (want == PolyScene::Results && !m.results_table_patched) {
            results_table(m, ce, m.sdram);
            m.results_table_patched = true;
        }
        if (want == PolyScene::Bonus) {
            // The items are blitted where they are: their clip reaches into
            // the margins, as in a level, and what lands there goes to the
            // shadow.
            const uint8_t* p = m.sdram + kClipRectSdram;
            write_clip(m, int16_t(-ce), int16_t(320 + ce),
                       uint32_t(p[8]) << 24 | uint32_t(p[9]) << 16 | uint32_t(p[10]) << 8 | p[11]);
            m.bonus_clip_overridden = true;
        }
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
    // The HUD keeps to the left edge of what the margin shows of the level.
    set_hud_shift(m, level && m.wide_hud_edges && e ? e - margin_cut(m, e).left : 0);
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
    m.poly_scene = 0;
    m.results_table_patched = false;
    m.bonus_clip_overridden = false;
    m.radial_len = 0;
    m.cull_shift = 0;
    m.plane_fill_clamped = true;
    m.tether_called = false;
}

} // namespace chaotix::patches
