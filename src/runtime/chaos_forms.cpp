// Chaos Forms. See chaos_forms.h.
#include "runtime/chaos_forms.h"
#include "runtime/patches.h"
#include "runtime/system.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace chaotix::chaos_forms {

namespace {

uint16_t rd16(const Machine& m, uint32_t off) {
    off &= 0xFFFF;
    return uint16_t(m.wram[off] << 8 | m.wram[(off + 1) & 0xFFFF]);
}
void wr16(Machine& m, uint32_t off, uint16_t v) {
    off &= 0xFFFF;
    m.wram[off] = uint8_t(v >> 8);
    m.wram[(off + 1) & 0xFFFF] = uint8_t(v);
}

constexpr uint32_t kGameMode = 0xDFDE, kZone = 0xDFF2, kLevel = 0xDFF4;
// The level is over: FFFDC5 is FF from the frame the goal sign is passed
// ($890882), and FFFDE8 is set there and by the end of a boss ($89105E); a new
// level clears it ($889368).
constexpr uint32_t kGoalPassed = 0xFDC5, kLevelCleared = 0xFDE8;
constexpr uint32_t kRings = 0xE008, kFrameTick = 0xE006;
// The level clock: 0 until the player has control (docs/ACHIEVEMENTS.md).
constexpr uint32_t kLevelClock = 0xE010;
constexpr uint32_t kPlayerChar = 0xE038, kPartnerChar = 0xE03A;  // id x 4
constexpr uint32_t kPlayerObject = 0xE034, kPartnerObject = 0xE036;
constexpr uint32_t kObjState = 0x07, kObjStatus = 0x3A;
constexpr uint32_t kStatInvincible = 0x32, kStatShoes = 0x34;
constexpr uint16_t kModeLevel = 0x18;
// Where a form is allowed: the attractions (zones 0-4), and WORLD ENTRANCE
// (zone 7) level 2, the final battle, but not its lobby (levels 0 and 1), nor
// the bonus stage and the ending's scenes after it (3-6).
constexpr uint32_t kLastAttraction = 4, kWorldEntrance = 7, kFinalBattle = 2;
constexpr uint8_t kStateJump = 0x04, kStateAir = 0x08;
constexpr uint32_t kZ80Paused = 0x1C10;
// Machine::chaos_drain counts the frames of the game running towards the next
// ring (0-59); its top bit says the jump button the transformation took is
// still held, so it is kept from the game until it is let go. Bit 6 says a
// monitor's invincibility or speed shoes were running when the form took them
// over, so their music is playing: the form then ends through the game's own
// end of the timers (kMonitorMusic). A monitor taken during a form adds to
// the form's timers (above kMark) and plays no music (quiet_monitors).
constexpr uint8_t kJumpTaken = 0x80, kMonitorMusic = 0x40, kDrainMask = 0x3F;
constexpr uint8_t kDrainFlags = kJumpTaken | kMonitorMusic;

// Where the characters' colours are in the 32X palette (docs/CHARACTERS.md):
// pixel p of the player is entry 0x12 + p, of the partner 0x24 + p, and the
// same colours again 0x80 further on with the priority bit flipped.
constexpr int kBase[2] = {0x12, 0x24};
constexpr int kPixels = 18;
// ROM: Espio's colours as the game loads them, and the 16 colour sets of five
// that cycle through his pixels 2-6.
constexpr uint32_t kEspioRom = 0x2292DC;
constexpr uint32_t kEspioCycle = 0x0E7BA;
constexpr int kEspioSets = 16, kEspioColours = 5;
constexpr int kEspioFramesPerSet = 4;

bool valid_ptr(uint16_t p) { return p >= 0x8000 && p <= 0xFFF0; }

// --- the transformation's sound (issue #146) --------------------------------
//
// The giant ring sound ($46) plays a falling arpeggio: its FM6 track ($E6F5
// in the sound driver's bank, ROM $50000; the FM5 track plays the same two
// frames later, through an F6 jump into it) repeats four notes going down,
// each repeat an octave lower (FB F4) and quieter (E6 06), so it sounds like
// an ending. The form's version (the owner's choice) plays the notes going
// up, each repeat a semitone higher and fading more gently. It is a copy of
// that track, made from the ROM's own bytes with those edits, in the unused
// end of the same bank ($C200, all FF), and a word after it that keeps the
// ROM's checksum as it was. Only the copy is new: the giant ring sound itself
// is unchanged, and nothing records the ROM as patched. The frame after a
// transformation, the driver has set up the sound's tracks but played none of
// them; the two that read that track are pointed at the same place in the
// copy.
constexpr uint32_t kSoundBank = 0x50000;  // ROM offset of the driver's bank $8D0000
constexpr uint16_t kRingTrack = 0xE6F5;
constexpr uint16_t kRingTrackLen = 19;
constexpr uint16_t kRingLoop = 4;  // where its loop (F7 00 06 $E6F9) goes back to
constexpr uint16_t kRiseTrack = 0xC200;
constexpr uint16_t kRiseCheck = kRiseTrack + 20;  // the word that keeps the checksum
constexpr uint16_t kRiseEnd = kRiseCheck + 2;
// The edits: the four notes (and their length), the transposition added each
// repeat (+1 semitone) and the volume taken off each repeat.
constexpr uint8_t kRiseNotes[5] = {0xCF, 0x04, 0xD2, 0xD5, 0xD8};
constexpr uint8_t kRiseStep = 0x01, kRiseFade = 0x02;
// The driver's three sound effect tracks in Z80 RAM ($30 bytes each: +0 bit 7
// playing, +3 the little-endian pointer into the bank).
constexpr uint32_t kSfxTracks = 0x1EB0, kSfxTrackSize = 0x30;
constexpr int kSfxTrackCount = 3;
// Frames after a transformation in which its sound is pointed at the copy.
constexpr uint16_t kRiseFrames = 8;

uint32_t bank_offset(uint16_t z80) { return kSoundBank + (uint32_t(z80) - 0x8000u); }

// The copy as it should be, from the ROM; false if the ROM does not hold the
// track where and as expected (another ROM, or a patch that moved it).
bool rise_track(const Machine& m, uint8_t* out) {
    const auto& d = m.rom.data;
    if (bank_offset(kRiseEnd) > d.size()) return false;
    const uint8_t* t = d.data() + bank_offset(kRingTrack);
    const uint16_t loop = uint16_t(t[16] | t[17] << 8);
    if (t[0] != 0xEF || t[2] != 0xE0 || t[4] < 0x81 || t[4] > 0xDF || t[5] >= 0x80 || t[9] != 0xFB ||
        t[11] != 0xE6 || t[13] != 0xF7 || loop != kRingTrack + kRingLoop || t[18] != 0xF2)
        return false;
    std::memcpy(out, t, kRingTrackLen);
    std::memcpy(out + kRingLoop, kRiseNotes, sizeof kRiseNotes);
    out[10] = kRiseStep;
    out[12] = kRiseFade;
    out[16] = uint8_t(kRiseTrack + kRingLoop);
    out[17] = uint8_t((kRiseTrack + kRingLoop) >> 8);
    return true;
}

// Puts the copy in place (once; it stays). False if it cannot be: the ROM is
// not as expected, or the place is not free in it.
bool build_rise(Machine& m) {
    uint8_t want[kRingTrackLen];
    if (!rise_track(m, want)) return false;
    uint8_t* at = m.rom.data.data() + bank_offset(kRiseTrack);
    if (std::memcmp(at, want, kRingTrackLen) == 0) return true;
    for (uint32_t i = 0; i < uint32_t(kRiseEnd - kRiseTrack); ++i)
        if (at[i] != 0xFF) return false;
    // The game checks the sum of the ROM's words when it starts: what the copy
    // changes in it, the word after it puts back.
    std::memcpy(at, want, kRingTrackLen);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < uint32_t(kRiseCheck - kRiseTrack); i += 2) sum += 0xFFFFu - uint32_t(at[i] << 8 | at[i + 1]);
    const uint16_t check = uint16_t(0xFFFFu + sum);
    at[kRiseCheck - kRiseTrack] = uint8_t(check >> 8);
    at[kRiseCheck - kRiseTrack + 1] = uint8_t(check);
    return true;
}

// Where in the copy a track that reads the ring sound's FM6 track at `p` goes
// on, or 0: inside that track, or on an F6 jump into it (FM5's).
uint16_t rise_place(const Machine& m, uint16_t p) {
    if (p >= kRingTrack && p < kRingTrack + kRingTrackLen) return uint16_t(kRiseTrack + (p - kRingTrack));
    if (p < 0x8000 || p > 0xFFFC) return 0;
    const uint8_t* t = m.rom.data.data() + bank_offset(p);
    const uint16_t to = uint16_t(t[1] | t[2] << 8);
    if (t[0] == 0xF6 && to >= kRingTrack && to < kRingTrack + kRingTrackLen) return uint16_t(kRiseTrack + (to - kRingTrack));
    return 0;
}

uint16_t sfx_pointer(const Machine& m, int k, bool* playing) {
    const uint32_t r = kSfxTracks + uint32_t(k) * kSfxTrackSize;
    *playing = (m.zram[r] & 0x80) != 0;
    return uint16_t(m.zram[r + 3] | m.zram[r + 4] << 8);
}

// A sound effect track reads the copy.
bool reads_rise(const Machine& m) {
    for (int k = 0; k < kSfxTrackCount; ++k) {
        bool playing = false;
        const uint16_t p = sfx_pointer(m, k, &playing);
        if (playing && p >= kRiseTrack && p < kRiseTrack + kRingTrackLen) return true;
    }
    return false;
}

void rise(Machine& m) {
    if (!build_rise(m)) return;
    for (int k = 0; k < kSfxTrackCount; ++k) {
        bool playing = false;
        const uint16_t to = rise_place(m, sfx_pointer(m, k, &playing));
        if (!playing || !to) continue;
        const uint32_t r = kSfxTracks + uint32_t(k) * kSfxTrackSize;
        m.zram[r + 3] = uint8_t(to);
        m.zram[r + 4] = uint8_t(to >> 8);
    }
}

// The status object of the player (0) or the partner (1), 0 if none.
uint16_t status_of(const Machine& m, int who) {
    const uint16_t obj = rd16(m, who ? kPartnerObject : kPlayerObject);
    if (!valid_ptr(obj)) return 0;
    const uint16_t st = rd16(m, obj + kObjStatus);
    return valid_ptr(st) ? st : 0;
}

bool in_level(const Machine& m) {
    if (rd16(m, kGameMode) != kModeLevel) return false;
    const uint16_t zone = rd16(m, kZone);
    if (zone <= kLastAttraction) return true;
    return zone == kWorldEntrance && rd16(m, kLevel) == kFinalBattle;
}

// The goal sign has been touched or the boss beaten: the level is ending.
bool level_over(const Machine& m) {
    return m.wram[kGoalPassed] == 0xFF || rd16(m, kLevelCleared) != 0;
}

// Puts one timer in a form's range, or keeps it there: the game counts it
// down a frame at a time (so the stars circle), and a whole turn is added
// before it gets near the mark.
void keep(Machine& m, uint32_t timer) {
    const uint16_t v = rd16(m, timer);
    // A monitor's timer (under the mark, not run out): its music is on.
    if (v != 0 && v < kMark) m.chaos_drain |= kMonitorMusic;
    if (v < kMark) wr16(m, timer, kHold);
    else if (v < kRefill) wr16(m, timer, uint16_t(v + kTurn));
}
void hold(Machine& m, uint16_t st) {
    keep(m, st + kStatInvincible);
    keep(m, st + kStatShoes);
}
// Ends the forms of both characters (`qs` 0 without a partner). With no
// monitor's music on, the timers are cleared and the music stays as it is.
// With one (Codex review, issue #146), they are left at 1: the game counts
// them to 0 the next frame and asks for the level's music again ($88E962),
// as when a monitor runs out.
void release(Machine& m, uint16_t ps, uint16_t qs) {
    const uint16_t end = (m.chaos_drain & kMonitorMusic) ? 1 : 0;
    for (uint16_t st : {ps, qs}) {
        if (!st) continue;
        if (rd16(m, st + kStatInvincible) >= kMark) wr16(m, st + kStatInvincible, end);
        if (rd16(m, st + kStatShoes) >= kMark) wr16(m, st + kStatShoes, end);
    }
    m.chaos_drain &= uint8_t(~kMonitorMusic);
}

// --- colours ----------------------------------------------------------------

struct Rgb { float r, g, b; };
struct Hsv { float h, s, v; };

Rgb unpack(uint16_t c) { return {float(c & 31) / 31.f, float((c >> 5) & 31) / 31.f, float((c >> 10) & 31) / 31.f}; }
uint16_t pack(Rgb c, uint16_t keep) {
    auto q = [](float x) { return unsigned(std::clamp(std::lround(x * 31.f), 0l, 31l)); };
    return uint16_t((keep & 0x8000) | (q(c.b) << 10) | (q(c.g) << 5) | q(c.r));
}

Hsv to_hsv(Rgb c) {
    const float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), d = mx - mn;
    float h = 0;
    if (d > 0) {
        if (mx == c.r) h = std::fmod((c.g - c.b) / d, 6.f);
        else if (mx == c.g) h = (c.b - c.r) / d + 2.f;
        else h = (c.r - c.g) / d + 4.f;
        h *= 60.f;
        if (h < 0) h += 360.f;
    }
    return {h, mx > 0 ? d / mx : 0.f, mx};
}

Rgb from_hsv(Hsv c) {
    const float ch = c.v * c.s, hp = std::fmod(c.h, 360.f) / 60.f;
    const float x = ch * (1.f - std::fabs(std::fmod(hp, 2.f) - 1.f));
    float r = 0, g = 0, b = 0;
    if (hp < 1) { r = ch; g = x; }
    else if (hp < 2) { r = x; g = ch; }
    else if (hp < 3) { g = ch; b = x; }
    else if (hp < 4) { g = x; b = ch; }
    else if (hp < 5) { r = x; b = ch; }
    else { r = ch; b = x; }
    const float m = c.v - ch;
    return {r + m, g + m, b + m};
}

Rgb mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

// One character's Chaos colours: the pixels whose colour changes (bit p-1 is
// pixel p), the hue they take, and the least saturation they are given. A
// pixel keeps its own brightness, so the shading of the sprite is kept and a
// fade (which darkens CRAM itself) carries on through the colour.
struct Look {
    uint32_t pixels;
    float hue;
    float min_sat;
};
constexpr uint32_t px(std::initializer_list<int> l) {
    uint32_t m = 0;
    for (int p : l) m |= 1u << (p - 1);
    return m;
}
// Hues: lime green 100 (Chaos Knuckles), green 125, gold 46 (Vector, Charmy, Heavy).
const Look kLooks[8] = {
    /* 0 Mighty   */ {px({2, 3, 4, 5, 13, 14, 15}), 125.f, 0.85f},  // the red back
    /* 1 (unused) */ {0, 0.f, 0.f},
    /* 2 Knuckles */ {px({5, 6, 7, 8}), 100.f, 0.80f},              // the red body
    /* 3 Charmy   */ {px({10, 11, 12, 13}), 46.f, 0.90f},           // the yellow stripes (and his cheek and ring): gold
    /* 4 Vector   */ {px({9, 10, 11, 12, 16, 17}), 46.f, 0.90f},    // the green parts
    /* 5 Bomb     */ {px({6, 7, 8, 9}), 100.f, 0.85f},              // the red body
    /* 6 Heavy    */ {px({3, 4, 5, 6, 7}), 46.f, 0.85f},            // the silver body
    /* 7 Espio    */ {0, 0.f, 0.f},                                 // the rainbow, below
};

// `p` is the pulse, 0 (mid tone) to 1 (brightest).
uint16_t recolour(uint16_t c, const Look& look, float p) {
    const Hsv o = to_hsv(unpack(c));
    const float s = std::max(o.s, look.min_sat);
    const Rgb mid = from_hsv({look.hue, s, o.v * 0.78f});
    const Rgb bright = mix(from_hsv({look.hue, s, std::min(1.f, o.v * 1.2f)}), {1.f, 1.f, 1.f}, 0.40f * o.v);
    return pack(mix(mid, bright, p), c);
}

float pulse(uint64_t frame) {
    constexpr float kTwoPi = 6.2831853f;
    return 0.5f - 0.5f * std::cos(kTwoPi * float(frame % kFramesPerRing) / float(kFramesPerRing));
}

float brightness(uint16_t c) { const Rgb x = unpack(c); return std::max({x.r, x.g, x.b}); }

void apply_block(Machine& m, int who, int id, float p) {
    id &= 7;
    const int base = kBase[who];
    if (id == 7) {
        // Espio: his body pixels 2-6 run through the game's own rainbow sets.
        const int set = int((m.frame_count / kEspioFramesPerSet) % kEspioSets);
        for (int i = 0; i < kEspioColours; ++i) {
            const uint16_t rainbow = m.rom.read16(kEspioCycle + uint32_t((set * kEspioColours + i) * 2));
            const int pixel = 2 + i;
            const uint16_t own = m.rom.read16(kEspioRom + uint32_t((pixel - 1) * 2));
            for (int copy = 0; copy < 2; ++copy) {
                const int e = base + pixel + copy * 0x80;
                // Follows a fade: the entry's brightness against the ROM's.
                const float have = brightness(m.chaos_pal[e]), want = brightness(own);
                const float k = want > 0.f ? std::min(1.f, have / want) : 1.f;
                const Rgb r = unpack(rainbow);
                m.chaos_pal[e] = pack({r.r * k, r.g * k, r.b * k}, m.chaos_pal[e]);
            }
        }
        return;
    }
    const Look& look = kLooks[id];
    for (int pixel = 1; pixel <= kPixels; ++pixel) {
        if (!(look.pixels & (1u << (pixel - 1)))) continue;
        for (int copy = 0; copy < 2; ++copy) {
            const int e = base + pixel + copy * 0x80;
            m.chaos_pal[e] = recolour(m.chaos_pal[e], look, p);
        }
    }
}

} // namespace

bool active(const Machine& m, int who) {
    const uint16_t st = status_of(m, who);
    return st && rd16(m, st + kStatInvincible) >= kMark;
}

bool quiet_monitors(const Machine& m) {
    return m.chaos_forms && in_level(m) && (active(m, 0) || active(m, 1));
}

bool quiet_request(const Machine& m, uint8_t id, uint32_t ret) {
    if (id != kInvincibleMusic && id != kQuietSound) return false;
    return (ret & 0xFFFFFF) == kInvincibleMonitorReturn && quiet_monitors(m);
}

void begin_frame(Machine& m) {
    const bool jump = (m.input.pad[0] & PAD_C) != 0;
    const bool pressed = jump && !m.chaos_jump_prev;
    m.chaos_jump_prev = jump;
    if (m.chaos_drain & kJumpTaken) {
        // Held since the transformation: to the game it must not look like a
        // new press (Knuckles' glide, Charmy's flight) when only the first
        // frame of it was taken.
        if (jump) m.input.pad[0] &= uint16_t(~PAD_C);
        else m.chaos_drain &= uint8_t(~kJumpTaken);
    }
    // A save state taken while the form's sound plays, loaded where the copy
    // was never made: it is made again.
    if (reads_rise(m)) build_rise(m);
    if (!in_level(m)) return;

    const uint16_t ps = status_of(m, 0), qs = status_of(m, 1);
    if (!ps) return;
    if (!m.chaos_forms) {
        // Turned off (OPTIONS > EXTRAS > MODS, or a TIME ATTACK run) in the
        // middle of a form: it ends at once, or it would run on for minutes
        // with awards and records no longer paused. A monitor's timers are
        // under kMark and are left alone.
        release(m, ps, qs);
        return;
    }
    int rings = int16_t(rd16(m, kRings));

    if (level_over(m)) {
        // The goal or the boss: the form ends here, as a monitor's would not
        // (nothing is asked of the sound driver), and none starts.
        release(m, ps, qs);
        return;
    }

    if (rd16(m, ps + kStatInvincible) >= kMark) {
        // On: one ring a second of the game running.
        const uint16_t tick = rd16(m, kFrameTick);
        if (tick != m.chaos_last_tick) {
            m.chaos_last_tick = tick;
            if (m.zram[kZ80Paused] == 0) {
                const uint8_t taken = m.chaos_drain & kDrainFlags;
                const uint8_t frames = uint8_t((m.chaos_drain & kDrainMask) + 1);
                if (frames >= kFramesPerRing) {
                    m.chaos_drain = taken;
                    --rings;
                    wr16(m, kRings, uint16_t(int16_t(rings)));
                } else {
                    m.chaos_drain = uint8_t(taken | frames);
                }
            }
        }
        if (rings <= 0) {
            release(m, ps, qs);
            return;
        }
        // Just transformed (the game has counted the timer down from kHold a
        // few times): its sound is the rising one.
        const uint16_t t = rd16(m, ps + kStatInvincible);
        if (t <= kHold && t > kHold - kRiseFrames && !m.chaos_sound) rise(m);
        hold(m, ps);
        if (qs) hold(m, qs);
        return;
    }

    // Off: the transformation, a jump press in the air.
    if (!pressed || rd16(m, kChaosRings) < kAllRings || rings < kRingsNeeded) return;
    if (rd16(m, kLevelClock) == 0) return;  // the level is not entered yet
    const uint16_t obj = rd16(m, kPlayerObject);
    const uint8_t state = m.wram[(obj + kObjState) & 0xFFFF];
    if (state != kStateJump && state != kStateAir) return;
    m.chaos_drain = kJumpTaken;
    m.chaos_last_tick = rd16(m, kFrameTick);
    hold(m, ps);
    if (qs) hold(m, qs);
    m.input.pad[0] &= uint16_t(~PAD_C);  // the press was the transformation's
    // A sound, so that the transformation is heard (issue #146).
    patches::request_sound(m, m.chaos_sound ? m.chaos_sound : kTransformSound);
}

const uint16_t* display(Machine& m, const uint16_t* pal) {
    if (!m.chaos_forms) return pal;
    const bool on[2] = {active(m, 0), active(m, 1)};
    if (!on[0] && !on[1]) return pal;
    std::memcpy(m.chaos_pal, pal, sizeof m.chaos_pal);
    const float p = pulse(m.frame_count);
    if (on[0]) apply_block(m, 0, rd16(m, kPlayerChar) / 4, p);
    if (on[1]) apply_block(m, 1, rd16(m, kPartnerChar) / 4, p);
    return m.chaos_pal;
}

} // namespace chaotix::chaos_forms
