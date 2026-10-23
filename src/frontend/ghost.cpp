// See ghost.h.
#include "frontend/ghost.h"

#include "runtime/log.h"
#include "runtime/lz.h"
#include "runtime/patches.h"
#include "runtime/save_state.h"
#include "runtime/sprite_record.h"
#include "runtime/system.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace chaotix::ghost {

namespace {

namespace fs = std::filesystem;

// ---- the file -------------------------------------------------------------
//
// Little-endian, fixed-size header:
//   "CHXGHOST", format, header size,
//   program version (16), ROM SHA-1 (48), ROM image SHA-1 after patches (48),
//   place, level, at-time, player, partner (u16 each), two players (u8), 0,
//   time (i32), recorded (i64, Unix seconds),
//   frames (u32: inputs), settings changes (u32), start state size (u32),
//   packed inputs size (u32), CRC-32 of everything after the header (u32).
// Then the settings changes (24 bytes each), the start state (a save state
// file without its picture) and the inputs (u32 each, LZ4).
constexpr char kMagic[8] = {'C', 'H', 'X', 'G', 'H', 'O', 'S', 'T'};
constexpr uint32_t kFormat = 1;
constexpr size_t kVersionLen = 16, kShaLen = 48;
constexpr size_t kHeaderSize = 8 + 4 + 4 + kVersionLen + 2 * kShaLen + 5 * 2 + 2 + 4 + 8 + 5 * 4;
constexpr size_t kSettingSize = 24;
// More frames than any level allows (its own limit is ten minutes), with
// room for the results screen: a bigger count is a damaged file.
constexpr uint32_t kMaxFrames = 60 * 60 * 15;

void put16(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 2; ++i) o.push_back(uint8_t(v >> (8 * i))); }
void put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i))); }
void put64(std::vector<uint8_t>& o, uint64_t v) { for (int i = 0; i < 8; ++i) o.push_back(uint8_t(v >> (8 * i))); }
void put_text(std::vector<uint8_t>& o, const std::string& s, size_t len) {
    for (size_t i = 0; i < len; ++i) o.push_back(i < s.size() && i + 1 < len ? uint8_t(s[i]) : 0);
}
uint32_t get16(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }
uint32_t get32(const uint8_t* p) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t(p[i]) << (8 * i); return v; }
uint64_t get64(const uint8_t* p) { uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= uint64_t(p[i]) << (8 * i); return v; }
std::string get_text(const uint8_t* p, size_t len) {
    size_t n = 0;
    while (n < len && p[n]) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

uint32_t crc32(const uint8_t* d, size_t n) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t crc = ~0u;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

const std::string& data_sha1(const Machine& m) { return m.rom.patched_sha1.empty() ? m.rom.sha1 : m.rom.patched_sha1; }

bool read_bytes(const std::string& path, std::vector<uint8_t>& out) { return savestate::read_file(path, out); }

bool load_ghost(const std::string& path, Ghost& g, std::string* why) {
    std::vector<uint8_t> bytes;
    if (!read_bytes(path, bytes)) return fail(why, "no ghost");
    return decode(bytes, g, why);
}

uint32_t rgb555(uint16_t c) {
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    return r << 16 | g << 8 | b;
}

// The characters' banks (docs/SPRITES_32X.md): 0-7 the eight characters,
// 8-15 their second sets.
constexpr int kCharacterBanks = 16;

} // namespace

// ---- options ----------------------------------------------------------------

const char* mode_name(Mode m) {
    switch (m) {
    case Mode::Best: return "BEST";
    case Mode::Last: return "LAST";
    default: return "OFF";
    }
}

Mode parse_mode(const std::string& s, Mode fallback) {
    std::string u;
    for (char c : s) u.push_back(char(std::toupper(static_cast<unsigned char>(c))));
    if (u == "OFF" || u == "FALSE" || u == "0") return Mode::Off;
    if (u == "BEST" || u == "ON" || u == "TRUE" || u == "1") return Mode::Best;
    if (u == "LAST") return Mode::Last;
    return fallback;
}

Mode default_mode() {
#if defined(__ANDROID__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    return Mode::Off;
#else
    return Mode::Best;
#endif
}

// ---- settings ---------------------------------------------------------------

Settings Settings::of(const Machine& m) {
    Settings s;
    s.wide_extra = m.wide_extra;
    s.wide_extra_bottom = m.wide_extra_bottom;
    s.wide_enabled = m.wide_enabled;
    s.wide_everywhere = m.wide_everywhere;
    s.wide_hud_edges = m.wide_hud_edges;
    s.six_button[0] = m.input.six_button[0];
    s.six_button[1] = m.input.six_button[1];
    for (size_t i = 0; i < m.music_replaced.size() && i < 64; ++i)
        if (m.music_replaced[i]) s.music_replaced |= uint64_t(1) << i;
    return s;
}

void Settings::apply(Machine& m) const {
    m.wide_extra = wide_extra;
    m.wide_extra_bottom = wide_extra_bottom;
    m.wide_enabled = wide_enabled;
    m.wide_everywhere = wide_everywhere;
    m.wide_hud_edges = wide_hud_edges;
    m.input.six_button[0] = six_button[0];
    m.input.six_button[1] = six_button[1];
    for (size_t i = 0; i < m.music_replaced.size() && i < 64; ++i) m.music_replaced[i] = (music_replaced >> i) & 1;
    // A time attack run has no lobby shortcuts (runtime/lobby.h).
    m.lobby_partner = m.lobby_time = -1;
}

bool Settings::operator==(const Settings& o) const {
    return wide_extra == o.wide_extra && wide_extra_bottom == o.wide_extra_bottom && wide_enabled == o.wide_enabled &&
           wide_everywhere == o.wide_everywhere && wide_hud_edges == o.wide_hud_edges &&
           six_button[0] == o.six_button[0] && six_button[1] == o.six_button[1] && music_replaced == o.music_replaced;
}

// ---- the file -----------------------------------------------------------

std::vector<uint8_t> encode(const Ghost& g) {
    std::vector<uint8_t> payload;
    for (const SettingsChange& c : g.settings) {
        const Settings& s = c.settings;
        put32(payload, c.frame);
        put32(payload, uint32_t(s.wide_extra));
        put32(payload, uint32_t(s.wide_extra_bottom));
        put32(payload, uint32_t(s.wide_enabled) | uint32_t(s.wide_everywhere) << 1 | uint32_t(s.wide_hud_edges) << 2 |
                           uint32_t(s.six_button[0]) << 3 | uint32_t(s.six_button[1]) << 4);
        put64(payload, s.music_replaced);
    }
    payload.insert(payload.end(), g.start.begin(), g.start.end());
    std::vector<uint8_t> raw, packed;
    raw.reserve(g.inputs.size() * 4);
    for (uint32_t v : g.inputs) put32(raw, v);
    lz::compress(raw.data(), raw.size(), packed);
    payload.insert(payload.end(), packed.begin(), packed.end());

    std::vector<uint8_t> out;
    out.reserve(kHeaderSize + payload.size());
    out.insert(out.end(), kMagic, kMagic + 8);
    put32(out, kFormat);
    put32(out, uint32_t(kHeaderSize));
    put_text(out, g.game_version, kVersionLen);
    put_text(out, g.rom_sha1, kShaLen);
    put_text(out, g.rom_data_sha1, kShaLen);
    put16(out, g.request.place);
    put16(out, g.request.level);
    put16(out, g.request.attime);
    put16(out, g.request.player);
    put16(out, g.request.combi);
    out.push_back(g.request.two_players ? 1 : 0);
    out.push_back(0);
    put32(out, uint32_t(g.time));
    put64(out, uint64_t(g.recorded_unix));
    put32(out, uint32_t(g.inputs.size()));
    put32(out, uint32_t(g.settings.size()));
    put32(out, uint32_t(g.start.size()));
    put32(out, uint32_t(packed.size()));
    put32(out, crc32(payload.data(), payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

bool decode(const std::vector<uint8_t>& bytes, Ghost& out, std::string* error) {
    if (bytes.size() < 16 || std::memcmp(bytes.data(), kMagic, 8) != 0) return fail(error, "not a ghost");
    const uint8_t* p = bytes.data() + 8;
    const uint32_t format = get32(p);
    if (format != kFormat)
        return fail(error, "ghost format " + std::to_string(format) + " (this version reads format " +
                               std::to_string(kFormat) + ")");
    if (get32(p + 4) != kHeaderSize || bytes.size() < kHeaderSize) return fail(error, "the ghost's header is damaged");
    p += 8;
    Ghost g;
    g.game_version = get_text(p, kVersionLen); p += kVersionLen;
    g.rom_sha1 = get_text(p, kShaLen); p += kShaLen;
    g.rom_data_sha1 = get_text(p, kShaLen); p += kShaLen;
    g.request.place = uint16_t(get16(p));
    g.request.level = uint16_t(get16(p + 2));
    g.request.attime = uint16_t(get16(p + 4));
    g.request.player = uint16_t(get16(p + 6));
    g.request.combi = uint16_t(get16(p + 8));
    g.request.two_players = p[10] != 0;
    p += 12;
    g.time = int(int32_t(get32(p))); p += 4;
    g.recorded_unix = int64_t(get64(p)); p += 8;
    const uint32_t frames = get32(p), changes = get32(p + 4), start = get32(p + 8), packed = get32(p + 12),
                   crc = get32(p + 16);
    if (frames > kMaxFrames || changes > frames + 1 || !stage_select::valid(g.request))
        return fail(error, "the ghost's header is damaged");
    const uint64_t payload = uint64_t(changes) * kSettingSize + start + packed;
    if (kHeaderSize + payload != bytes.size()) return fail(error, "the ghost is truncated or damaged");
    const uint8_t* q = bytes.data() + kHeaderSize;
    if (crc32(q, size_t(payload)) != crc) return fail(error, "the ghost is damaged");
    for (uint32_t i = 0; i < changes; ++i, q += kSettingSize) {
        SettingsChange c;
        c.frame = get32(q);
        c.settings.wide_extra = int(int32_t(get32(q + 4)));
        c.settings.wide_extra_bottom = int(int32_t(get32(q + 8)));
        const uint32_t f = get32(q + 12);
        c.settings.wide_enabled = f & 1;
        c.settings.wide_everywhere = (f >> 1) & 1;
        c.settings.wide_hud_edges = (f >> 2) & 1;
        c.settings.six_button[0] = (f >> 3) & 1;
        c.settings.six_button[1] = (f >> 4) & 1;
        c.settings.music_replaced = get64(q + 16);
        if ((i == 0 && c.frame != 0) || (i > 0 && c.frame <= g.settings.back().frame) || c.frame >= frames + 1)
            return fail(error, "the ghost is damaged");
        g.settings.push_back(c);
    }
    if (g.settings.empty()) return fail(error, "the ghost is damaged");
    g.start.assign(q, q + start);
    q += start;
    std::vector<uint8_t> raw(size_t(frames) * 4);
    if (!lz::decompress(q, packed, raw.data(), raw.size())) return fail(error, "the ghost is damaged");
    g.inputs.resize(frames);
    for (uint32_t i = 0; i < frames; ++i) g.inputs[i] = get32(raw.data() + 4 * size_t(i));
    out = std::move(g);
    return true;
}

bool compatible(const Ghost& g, const Machine& m, std::string* why) {
    if (g.rom_sha1 != m.rom.sha1) return fail(why, "this ghost is from another ROM");
    if (g.rom_data_sha1 != data_sha1(m)) return fail(why, "this ghost was made with other ROM patches or character stats");
    if (g.game_version != savestate::game_version())
        return fail(why, "this ghost is from version " + g.game_version + " (this is " + savestate::game_version() + ")");
    savestate::Info info;
    if (!savestate::read_info(g.start, info, why)) return false;
    return true;
}

bool faster(const Ghost& a, const Ghost& b) { return a.time > 0 && (b.time <= 0 || a.time < b.time); }

std::string folder(const std::string& root, const Machine& m) {
    return root + "Ghosts/" + data_sha1(m).substr(0, 8) + "/";
}

std::string best_name(const stage_select::Request& r) {
    return std::to_string(r.place) + "_" + std::to_string(r.level) + "_" + std::to_string(r.player) + "_" +
           std::to_string(r.combi) + ".ghost";
}

std::string last_name(const stage_select::Request& r) {
    return std::to_string(r.place) + "_" + std::to_string(r.level) + "_last.ghost";
}

bool choose(const std::string& dir, Mode mode, const stage_select::Request& r, const Machine& m, Ghost& out,
            std::string& path, std::string* why) {
    if (mode == Mode::Off) return fail(why, "ghosts are off");
    auto usable = [&](const std::string& p, Ghost& g, std::string* reason) {
        if (!load_ghost(p, g, reason)) return false;
        if (!compatible(g, m, reason)) return false;
        if (g.request.place != r.place || g.request.level != r.level) return fail(reason, "another stage's ghost");
        return true;
    };
    if (mode == Mode::Last) {
        path = dir + last_name(r);
        return usable(path, out, why);
    }
    // The fastest of the stage's, whoever ran it: every file named for the
    // stage but its last run, so a ghost copied in from elsewhere under any
    // name counts too.
    const std::string prefix = std::to_string(r.place) + "_" + std::to_string(r.level) + "_";
    bool found = false;
    std::string reason = "no ghost for this stage yet";
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().u8string();
        if (name.rfind(prefix, 0) != 0 || name == last_name(r) || name.size() < 6 ||
            name.compare(name.size() - 6, 6, ".ghost") != 0)
            continue;
        Ghost g;
        std::string w;
        if (!usable(dir + name, g, &w)) { reason = w; continue; }
        if (!found || faster(g, out)) {
            out = std::move(g);
            path = dir + name;
            found = true;
        }
    }
    if (!found) return fail(why, reason);
    return true;
}

Stored store(const std::string& dir, const Ghost& g, const Machine& m) {
    Stored s;
    std::error_code ec;
    fs::create_directories(fs::u8path(dir), ec);
    const std::vector<uint8_t> bytes = encode(g);
    s.bytes = bytes.size();
    s.last = savestate::write_file(dir + last_name(g.request), bytes);
    const std::string best = dir + best_name(g.request);
    Ghost had;
    if (!load_ghost(best, had, nullptr) || !compatible(had, m, nullptr) || faster(g, had))
        s.best = savestate::write_file(best, bytes);
    return s;
}

bool discard(const std::string& path) {
    std::error_code ec;
    const fs::path from = fs::u8path(path), to = fs::u8path(path + ".desynced");
    fs::remove(to, ec);
    fs::rename(from, to, ec);
    return !ec;
}

// ---- recording ------------------------------------------------------------

void Recorder::arm(const stage_select::Request& r) {
    cancel();
    armed_ = true;
    ghost_.request = r;
}

void Recorder::before_frame(const Machine& m) {
    if (!recording_) return;
    if (ghost_.inputs.size() >= kMaxFrames) return;   // a run left running for ever
    const Settings s = Settings::of(m);
    if (ghost_.settings.empty() || s != last_) {
        ghost_.settings.push_back({uint32_t(ghost_.inputs.size()), s});
        last_ = s;
    }
    ghost_.inputs.push_back(uint32_t(m.input.pad[0]) | uint32_t(m.input.pad[1]) << 16);
}

void Recorder::after_frame(const Machine& m) {
    if (!armed_ || recording_ || m.stage_pending) return;
    // The frame the game took the stage on has just ended: this is where the
    // ghost machine will start.
    ghost_.start = savestate::save(m, {}, 0, 0);
    recording_ = true;
}

bool Recorder::finish(const Machine& m, int time, int64_t now_unix, Ghost& out) {
    const bool ok = recording_ && !ghost_.inputs.empty() && ghost_.inputs.size() < kMaxFrames;
    if (ok) {
        ghost_.game_version = savestate::game_version();
        ghost_.rom_sha1 = m.rom.sha1;
        ghost_.rom_data_sha1 = data_sha1(m);
        ghost_.time = time;
        ghost_.recorded_unix = now_unix;
        out = std::move(ghost_);
    }
    cancel();
    return ok;
}

// ---- the picture ------------------------------------------------------------

namespace {

struct Dot { int16_t x, y; uint8_t v; };

void collect(void* user, uint32_t off, uint8_t v) {
    // Frame buffer offsets are 0x200 + 0x200 * line + column; columns left of
    // the native picture (widescreen margins, at most kMaxWideExtra) wrap to
    // the end of the line before.
    const int rel = int(off & 0x1FFFF) - int(patches::kFbLineBase);
    if (rel < -int(patches::kFbLineStride)) return;
    int y = rel >= 0 ? rel / int(patches::kFbLineStride) : -1;
    int x = rel - y * int(patches::kFbLineStride);
    if (x >= kNativeWidth + patches::kMaxWideExtra + 56) {
        x -= int(patches::kFbLineStride);
        ++y;
    }
    static_cast<std::vector<Dot>*>(user)->push_back({int16_t(x), int16_t(y), v});
}

} // namespace

void capture(const Machine& gm, Frame& out) {
    out.visible = false;
    out.w = out.h = 0;
    out.px.clear();
    if (!gm.sprites) return;
    const SpriteList& list = gm.sprites->displayed(gm);
    std::vector<Dot> dots;
    for (const SpriteDraw& d0 : list.draws) {
        if (d0.bank < 0 || d0.bank >= kCharacterBanks) continue;
        SpriteDraw d = d0;
        // The ghost's own screen edges clip nothing: the live view may be
        // wider than the ghost's was.
        d.clip.left = int16_t(-patches::kMaxWideExtra);
        d.clip.right = int16_t(kNativeWidth + patches::kMaxWideExtra);
        d.clip.top = 0;
        d.clip.bottom = (kScreenHeight - 1) << 8;
        rasterise_sprite(d, list.bytes(d), d.size, collect, &dots,
                         d.kind == SpriteKind::Warped ? list.pool.data() + d.warp : nullptr);
    }
    if (dots.empty()) return;
    int x0 = dots[0].x, x1 = dots[0].x, y0 = dots[0].y, y1 = dots[0].y;
    for (const Dot& d : dots) {
        x0 = std::min<int>(x0, d.x);
        x1 = std::max<int>(x1, d.x);
        y0 = std::min<int>(y0, d.y);
        y1 = std::max<int>(y1, d.y);
    }
    out.x = x0;
    out.y = y0;
    out.w = x1 - x0 + 1;
    out.h = y1 - y0 + 1;
    out.px.assign(size_t(out.w) * size_t(out.h), 0);
    for (const Dot& d : dots)
        out.px[size_t(d.y - y0) * size_t(out.w) + size_t(d.x - x0)] = 0xFF000000u | rgb555(gm.mars.pal[d.v]);
    out.visible = true;
}

Placed compose(uint32_t* picture, int stride, int width, int height, int extra, const Frame& f, int cam_x,
               int cam_y) {
    Placed r;
    if (!f.visible || f.px.empty()) return r;
    // Ghost world position = its screen position + its camera; on the live
    // screen, less the live camera.
    const int ox = f.x + (f.cam_x - cam_x) + extra, oy = f.y + (f.cam_y - cam_y);
    const int x0 = std::max(0, ox), y0 = std::max(0, oy);
    const int x1 = std::min(width, ox + f.w), y1 = std::min(height, oy + f.h);
    if (x0 >= x1 || y0 >= y1) return r;   // off screen
    const int tr = int(kTint >> 16 & 0xFF), tg = int(kTint >> 8 & 0xFF), tb = int(kTint & 0xFF);
    int minx = x1, miny = y1, maxx = -1, maxy = -1;
    for (int y = y0; y < y1; ++y) {
        const uint32_t* src = f.px.data() + size_t(y - oy) * size_t(f.w);
        uint32_t* dst = picture + size_t(y) * size_t(stride);
        for (int x = x0; x < x1; ++x) {
            const uint32_t g = src[x - ox];
            if (!(g >> 24)) continue;
            int gr = int(g >> 16 & 0xFF), gg = int(g >> 8 & 0xFF), gb = int(g & 0xFF);
            gr += (tr - gr) * kTintAmount / 256;
            gg += (tg - gg) * kTintAmount / 256;
            gb += (tb - gb) * kTintAmount / 256;
            const uint32_t p = dst[x];
            const int pr = int(p >> 16 & 0xFF), pg = int(p >> 8 & 0xFF), pb = int(p & 0xFF);
            const int nr = (pr * (256 - kAlpha) + gr * kAlpha) >> 8;
            const int ng = (pg * (256 - kAlpha) + gg * kAlpha) >> 8;
            const int nb = (pb * (256 - kAlpha) + gb * kAlpha) >> 8;
            dst[x] = (p & 0xFF000000u) | uint32_t(nr) << 16 | uint32_t(ng) << 8 | uint32_t(nb);
            ++r.pixels;
            minx = std::min(minx, x);
            maxx = std::max(maxx, x);
            miny = std::min(miny, y);
            maxy = std::max(maxy, y);
        }
    }
    if (r.pixels) {
        r.x = minx;
        r.y = miny;
        r.w = maxx - minx + 1;
        r.h = maxy - miny + 1;
    }
    return r;
}

void CameraTrail::push(int x, int y) {
    // Oldest first; at most kCameraLag + 1 kept.
    if (n_ == kCameraLag + 1) {
        for (int i = 1; i < n_; ++i) { xs_[i - 1] = xs_[i]; ys_[i - 1] = ys_[i]; }
        --n_;
    }
    xs_[n_] = x;
    ys_[n_] = y;
    ++n_;
}

void CameraTrail::get(int& x, int& y) const {
    if (!n_) { x = y = 0; return; }
    x = xs_[0];
    y = ys_[0];
}

// ---- playing ----------------------------------------------------------------

Player::~Player() { stop(); }

bool Player::start(const Rom& rom, Ghost g, bool threaded, std::string* why) {
    stop();
    auto m = std::make_unique<Machine>();
    m->load_rom(Rom(rom));
    m->reset();
    m->sprites = std::make_shared<SpriteRecorder>();
    if (!savestate::load(*m, g.start, why)) return false;
    if (g.settings.empty()) return fail(why, "the ghost has no settings");
    ghost_ = std::move(g);
    m_ = std::move(m);
    next_setting_ = 0;
    run_.begin(ghost_.request);
    // The live run's tracker saw the frame the stage was taken on; so does
    // this one, or the two would calibrate the clock a frame apart.
    run_.update(*m_);
    held_ = Frame{};
    {
        std::lock_guard<std::mutex> lock(mu_);
        target_ = 0;
        quit_ = false;
        status_ = Status{};
        frame_ = Frame{};
    }
    started_ = true;
    threaded_ = threaded;
    if (threaded_) thread_ = std::thread([this] { worker(); });
    return true;
}

void Player::stop() {
    if (thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            quit_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }
    started_ = false;
    threaded_ = false;
    m_.reset();
}

void Player::advance() {
    if (!started_) return;
    if (!threaded_) {
        run_one();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mu_);
        ++target_;
    }
    cv_.notify_one();
}

void Player::worker() {
    std::unique_lock<std::mutex> lock(mu_);
    for (;;) {
        cv_.wait(lock, [this] { return quit_ || status_.frames < target_; });
        if (quit_) return;
        lock.unlock();
        run_one();
        lock.lock();
    }
}

void Player::run_one() {
    Status st;
    {
        std::lock_guard<std::mutex> lock(mu_);
        st = status_;
    }
    if (st.ended) {
        std::lock_guard<std::mutex> lock(mu_);
        ++status_.frames;   // nothing more to run; the caller's count goes on
        frame_.visible = false;
        done_cv_.notify_all();
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const uint64_t i = st.frames;
    Frame f;
    if (i >= ghost_.inputs.size()) {
        // The recorded run ended on its last input. Still going: it is not
        // the run that was recorded.
        st.ended = true;
        st.desynced = true;
    } else {
        while (next_setting_ < ghost_.settings.size() && ghost_.settings[next_setting_].frame <= i)
            ghost_.settings[next_setting_++].settings.apply(*m_);
        const uint32_t pads = ghost_.inputs[size_t(i)];
        m_->input.pad[0] = uint16_t(pads);
        m_->input.pad[1] = uint16_t(pads >> 16);
        m_->run_frame();
        m_->audio_out.clear();
        m_->music_events.clear();
        const bool over = run_.update(*m_);
        if (run_.finished && run_.settling <= 0 && !st.goal) {
            st.goal = true;
            st.time = run_.time;
        }
        if (over) {
            st.ended = true;
            // A faithful replay ends where the run did, on its last frame,
            // with its time.
            st.desynced = run_.time != ghost_.time || i + 1 != ghost_.inputs.size();
            if (!st.goal) st.time = run_.time;
        }
        // What the machine shows in a frame is the 32X picture listed at the
        // end of the frame before, drawn with that frame's camera (see
        // CameraTrail): so the live run's next picture gets this frame's,
        // and this one gets the one held from the frame before.
        f = std::move(held_);
        held_ = Frame{};
        // Only while the clock runs: the title card before it draws the
        // characters' portraits from their banks, and the results screen
        // after the goal draws their icons.
        if (!st.ended && run_.have_start && !run_.finished) {
            capture(*m_, held_);
            held_.cam_x = patches::camera_x(*m_);
            held_.cam_y = patches::camera_y(*m_);
        }
    }
    f.index = i + 1;
    ++st.frames;
    last_ms_.store(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    {
        std::lock_guard<std::mutex> lock(mu_);
        // A desync is told as soon as it is known; so is the end.
        status_ = st;
        frame_ = std::move(f);
    }
    done_cv_.notify_all();
}

bool Player::latest(Frame& out, double wait_ms) {
    if (!started_) return false;
    std::unique_lock<std::mutex> lock(mu_);
    if (threaded_ && wait_ms > 0)
        done_cv_.wait_for(lock, std::chrono::duration<double, std::milli>(wait_ms),
                          [this] { return status_.frames >= target_; });
    out = frame_;
    return true;
}

Status Player::status() const {
    std::lock_guard<std::mutex> lock(mu_);
    return status_;
}

uint64_t Player::behind() const {
    std::lock_guard<std::mutex> lock(mu_);
    return threaded_ && target_ > status_.frames ? target_ - status_.frames : 0;
}

} // namespace chaotix::ghost
