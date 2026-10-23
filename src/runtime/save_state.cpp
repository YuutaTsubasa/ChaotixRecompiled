#include "runtime/save_state.h"

#include "runtime/lz.h"
#include "runtime/sprite_record.h"
#include "runtime/system.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <type_traits>

#ifndef CHAOTIX_VERSION_STRING
#define CHAOTIX_VERSION_STRING "unknown"
#endif

namespace chaotix {

// The machine's private time-slice bookkeeping (Machine befriends this).
// Between frames no slice is running, but it is state all the same.
struct MachineAccess {
    template <class A> static void slices(A& a, Machine& m) {
        for (SliceInfo* s : {&m.m68k_slice_, &m.sh2_slice_[0], &m.sh2_slice_[1]}) {
            a(s->running);
            a(s->base);
            a(s->budget);
        }
    }
};

namespace savestate {

namespace {

static_assert(sizeof(int) == 4 && sizeof(float) == 4, "the raw layout assumes 32-bit int and float");

// Archives: one transfer function (below) lists every field once, and these
// count, write or read them, so save and load cannot disagree on the order.
// Arrays of numbers go as one block of bytes; bool is one byte, 0 or 1.
template <class T> constexpr bool is_block() {
    using E = std::remove_all_extents_t<T>;
    return std::is_array_v<T> && std::is_arithmetic_v<E> && !std::is_same_v<E, bool>;
}

struct Counter {
    size_t n = 0;
    template <class T> void operator()(const T& v) {
        if constexpr (is_block<T>()) n += sizeof v;
        else if constexpr (std::is_array_v<T>) { for (const auto& e : v) (*this)(e); }
        else if constexpr (std::is_same_v<T, bool>) n += 1;
        else { static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>); n += sizeof v; }
    }
};

struct Writer {
    uint8_t* p;
    template <class T> void operator()(const T& v) {
        if constexpr (is_block<T>()) { std::memcpy(p, &v, sizeof v); p += sizeof v; }
        else if constexpr (std::is_array_v<T>) { for (const auto& e : v) (*this)(e); }
        else if constexpr (std::is_same_v<T, bool>) *p++ = v ? 1 : 0;
        else { std::memcpy(p, &v, sizeof v); p += sizeof v; }
    }
};

struct Reader {
    const uint8_t* p;
    template <class T> void operator()(T& v) {
        if constexpr (is_block<T>()) { std::memcpy(&v, p, sizeof v); p += sizeof v; }
        else if constexpr (std::is_array_v<T>) { for (auto& e : v) (*this)(e); }
        else if constexpr (std::is_same_v<T, bool>) v = *p++ != 0;
        else { std::memcpy(&v, p, sizeof v); p += sizeof v; }
    }
};

// Every field of the state, in the order of the format. A field added to the
// machine that the emulation depends on belongs here (and kFormatVersion
// goes up); see save_state.h for what is left out on purpose.
template <class A> void transfer(A& a, Machine& m, unsigned flags) {
    // Output picture.
    a(m.fb_width); a(m.fb_height); a(m.fb_extra); a(m.fb_extra_bottom);
    if (flags & kPicture) a(m.framebuffer);
    // Widescreen settings and the patches' latched state (runtime/patches.h).
    a(m.wide_extra); a(m.wide_extra_bottom); a(m.wide_enabled); a(m.wide_everywhere); a(m.wide_hud_edges);
    a(m.plane_shift); a(m.level_seen_frame); a(m.full_redraw_frame); a(m.level_scene); a(m.wide_active);
    a(m.clip_overridden); a(m.wide_special); a(m.special_rect_overridden); a(m.poly_scene);
    a(m.results_table_patched); a(m.bonus_clip_overridden); a(m.radial_lut); a(m.radial_len);
    a(m.special_shift); a(m.cull_shift); a(m.plane_fill_clamped);
    a(m.stage_request.place); a(m.stage_request.level); a(m.stage_request.attime);
    a(m.stage_request.player); a(m.stage_request.combi); a(m.stage_request.two_players);
    a(m.stage_pending);
    a(m.fb_margin);
    // Memory.
    a(m.wram); a(m.sdram); a(m.zram); a(m.bios_stub);
    // Mega Drive VDP.
    Vdp& v = m.vdp;
    a(v.vram); a(v.cram); a(v.vsram); a(v.reg); a(v.hidden_frame_tile); a(v.radial_len); a(v.radial_cx2);
    a(v.radial_cy2); a(v.shown_extra); a(v.shown_extra_bottom); a(v.cmd_pending); a(v.cmd_first); a(v.addr);
    a(v.code); a(v.fill_pending); a(v.vint_pending); a(v.hint_pending); a(v.hint_counter); a(v.in_vblank);
    a(v.odd_frame); a(v.sprite_overflow); a(v.sprite_collision); a(v.dma_stall_cycles);
    // 32X system registers, VDP and frame buffers.
    Mars& s = m.mars;
    a(s.adapter_ctrl); a(s.int_ctrl); a(s.bank); a(s.dreq_ctrl); a(s.dreq_src); a(s.dreq_dst); a(s.dreq_len);
    a(s.fifo); a(s.fifo_count); a(s.fifo_rd); a(s.dreq_words_left); a(s.sega_tv); a(s.comm); a(s.hint_vector);
    a(s.sh_int_mask); a(s.hcount); a(s.hcount_counter); a(s.irq_pending); a(s.bitmap_mode); a(s.screen_shift);
    a(s.fill_len); a(s.fill_addr); a(s.fill_data); a(s.fb_display); a(s.fb_select_req); a(s.pal); a(s.fb);
    // PWM (not its sample log, which nothing reads).
    Pwm& w = m.pwm;
    a(w.ctrl); a(w.cycle); a(w.fifo_l); a(w.fifo_r); a(w.count_l); a(w.count_r); a(w.timer); a(w.accum);
    a(w.out_l); a(w.out_r);
    // SH-2 on-chip peripherals.
    for (Sh2OnChip& o : m.onchip) {
        a(o.ipra); a(o.iprb); a(o.vcra); a(o.vcrb); a(o.vcrc); a(o.vcrd); a(o.vcrwdt); a(o.icr);
        a(o.dvsr); a(o.dvdnt); a(o.dvcr); a(o.vcrdiv); a(o.dvdnth); a(o.dvdntl);
        for (auto& c : o.dma) { a(c.sar); a(c.dar); a(c.tcr); a(c.chcr); a(c.vcr); }
        a(o.dmaor); a(o.tier); a(o.ftcsr); a(o.frt_tcr); a(o.tocr); a(o.frc); a(o.ocra); a(o.ocrb); a(o.ficr);
        a(o.frt_temp); a(o.frt_accum); a(o.wtcsr); a(o.wtcnt); a(o.rstcsr); a(o.wdt_accum); a(o.ccr); a(o.sbycr);
        a(o.bsc); a(o.cache_data);
    }
    // CPUs: registers only; the bus maps and hooks are the machine's own.
    m68k::State& c = m.m68k;
    a(c.d); a(c.a); a(c.pc); a(c.other_sp); a(c.s); a(c.t); a(c.imask); a(c.x); a(c.n); a(c.z); a(c.v); a(c.c);
    a(c.stopped); a(c.irq_level); a(c.cycles); a(c.clock);
    for (sh2::State& h : m.sh2) {
        a(h.r); a(h.pc); a(h.pr); a(h.gbr); a(h.vbr); a(h.mach); a(h.macl); a(h.sr); a(h.cycles);
        a(h.sleeping); a(h.irq_level); a(h.irq_vector); a(h.id);
    }
    z80::State& z = m.z80;
    a(z.a); a(z.f); a(z.b); a(z.c); a(z.d); a(z.e); a(z.h); a(z.l);
    a(z.a2); a(z.f2); a(z.b2); a(z.c2); a(z.d2); a(z.e2); a(z.h2); a(z.l2);
    a(z.ix); a(z.iy); a(z.sp); a(z.pc); a(z.i); a(z.r); a(z.iff1); a(z.iff2); a(z.im); a(z.halted);
    a(z.ei_pending); a(z.irq_line); a(z.cycles);
    // Sound.
    m.ym.state_io(a);
    m.psg.state_io(a);
    a(m.z80_busreq); a(m.z80_reset); a(m.z80_bank);
    a(m.audio_pos); a(m.psg_frac); a(m.dc_x); a(m.dc_y);
    // I/O and the cartridge's SRAM mapping register (not the SRAM itself).
    a(m.io_ctrl); a(m.io_data); a(m.sram_ctrl); a(m.pad_th_count); a(m.pad_th_time);
    // Timing.
    a(m.mclk); a(m.m68k_clock); a(m.sh2_clock); a(m.z80_clock); a(m.line); a(m.line_start_mclk);
    a(m.frame_count); a(m.boot_hle_done);
    MachineAccess::slices(a, m);
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

// The header, little-endian, fixed size.
constexpr char kMagic[8] = {'C', 'H', 'X', 'S', 'T', 'A', 'T', 'E'};
constexpr size_t kVersionLen = 16, kShaLen = 48;
constexpr size_t kHeaderSize = 8 + 4 + 4 + kVersionLen + kShaLen + kShaLen + 4 + 4 + 4 + 4 + 4 + 8 + 8;

void put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i))); }
void put64(std::vector<uint8_t>& o, uint64_t v) { for (int i = 0; i < 8; ++i) o.push_back(uint8_t(v >> (8 * i))); }
void put_text(std::vector<uint8_t>& o, const std::string& s, size_t len) {
    for (size_t i = 0; i < len; ++i) o.push_back(i < s.size() && i + 1 < len ? uint8_t(s[i]) : 0);
}
uint32_t get32(const uint8_t* p) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t(p[i]) << (8 * i); return v; }
uint64_t get64(const uint8_t* p) { uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= uint64_t(p[i]) << (8 * i); return v; }
std::string get_text(const uint8_t* p, size_t len) {
    size_t n = 0;
    while (n < len && p[n]) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

const std::string& data_sha1(const Machine& m) { return m.rom.patched_sha1.empty() ? m.rom.sha1 : m.rom.patched_sha1; }

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

} // namespace

const char* game_version() { return CHAOTIX_VERSION_STRING; }

size_t raw_size(unsigned flags) {
    // The same for every machine: counted once, on a machine made for it.
    static const std::array<size_t, 2> sizes = [] {
        auto probe = std::make_unique<Machine>();
        Counter plain, picture;
        transfer(plain, *probe, 0);
        transfer(picture, *probe, kPicture);
        return std::array<size_t, 2>{plain.n, picture.n};
    }();
    return sizes[(flags & kPicture) ? 1 : 0];
}

void serialize(const Machine& m, std::vector<uint8_t>& out, unsigned flags) {
    out.resize(raw_size(flags));
    Writer w{out.data()};
    // The writer only reads; transfer takes one function for both ways.
    transfer(w, const_cast<Machine&>(m), flags);
}

bool deserialize(Machine& m, const uint8_t* data, size_t n, unsigned flags, std::string* error) {
    if (n != raw_size(flags)) return fail(error, "the state is the wrong size for this format");
    Reader r{data};
    transfer(r, m, flags);
    // The recompiled code caches whether SDRAM code still matches the ROM by
    // these counters; the memory just changed under them, so move them on to
    // values no check has been made at.
    ++m.sdram_code_epoch;
    ++m.cache_code_epoch[0];
    ++m.cache_code_epoch[1];
    m.stats.frame = m.frame_count;
    m.vdp.radial_lut = m.radial_lut;
    // The music requests drained after each frame belong to the old timeline.
    m.music_events.clear();
    // The buses follow the mapping registers just restored.
    m.remap_m68k();
    m.remap_sh2();
    // The recorded sprite lists described the frame buffers just replaced.
    if (m.sprites) m.sprites->reset();
    return true;
}

std::vector<uint8_t> save(const Machine& m, const std::vector<uint8_t>& host, int64_t saved_unix, unsigned flags) {
    flags &= kPicture;
    std::vector<uint8_t> raw, packed;
    serialize(m, raw, flags);
    lz::compress(raw.data(), raw.size(), packed);
    std::vector<uint8_t> out;
    out.reserve(kHeaderSize + packed.size() + host.size());
    out.insert(out.end(), kMagic, kMagic + 8);
    put32(out, kFormatVersion);
    put32(out, uint32_t(kHeaderSize));
    put_text(out, game_version(), kVersionLen);
    put_text(out, m.rom.sha1, kShaLen);
    put_text(out, data_sha1(m), kShaLen);
    put32(out, flags);
    put32(out, uint32_t(raw.size()));
    put32(out, uint32_t(packed.size()));
    put32(out, crc32(raw.data(), raw.size()));
    put32(out, uint32_t(host.size()));
    put64(out, m.frame_count);
    put64(out, uint64_t(saved_unix));
    out.insert(out.end(), packed.begin(), packed.end());
    out.insert(out.end(), host.begin(), host.end());
    return out;
}

bool read_info(const std::vector<uint8_t>& file, Info& info, std::string* error) {
    if (file.size() < 16 || std::memcmp(file.data(), kMagic, 8) != 0) return fail(error, "not a save state");
    const uint8_t* p = file.data() + 8;
    info.format = get32(p);
    if (info.format != kFormatVersion)
        return fail(error, "save state format " + std::to_string(info.format) + " (this version reads format " +
                               std::to_string(kFormatVersion) + ")");
    if (get32(p + 4) != kHeaderSize || file.size() < kHeaderSize) return fail(error, "the save state's header is damaged");
    p += 8;
    info.game_version = get_text(p, kVersionLen); p += kVersionLen;
    info.rom_sha1 = get_text(p, kShaLen); p += kShaLen;
    info.rom_data_sha1 = get_text(p, kShaLen); p += kShaLen;
    info.flags = get32(p); p += 4;
    info.raw_size = get32(p); p += 4;
    info.compressed_size = get32(p); p += 4;
    p += 4;  // CRC
    const size_t host = get32(p); p += 4;
    info.frame = get64(p); p += 8;
    info.saved_unix = int64_t(get64(p));
    if (kHeaderSize + info.compressed_size + host != file.size()) return fail(error, "the save state is truncated or damaged");
    return true;
}

bool load(Machine& m, const std::vector<uint8_t>& file, std::string* error, std::vector<uint8_t>* host, Info* out_info) {
    Info info;
    if (!read_info(file, info, error)) return false;
    if (info.rom_sha1 != m.rom.sha1) return fail(error, "this save state is from another ROM");
    if (info.rom_data_sha1 != data_sha1(m))
        return fail(error, "this save state was made with other ROM patches or character stats");
    if (info.game_version != game_version())
        return fail(error, "this save state is from version " + info.game_version + " (this is " + game_version() + ")");
    if (info.raw_size != raw_size(info.flags)) return fail(error, "the save state's size does not match its format");
    std::vector<uint8_t> raw(info.raw_size);
    const uint8_t* packed = file.data() + kHeaderSize;
    if (!lz::decompress(packed, info.compressed_size, raw.data(), raw.size()) ||
        crc32(raw.data(), raw.size()) != get32(file.data() + kHeaderSize - 24))
        return fail(error, "the save state is damaged");
    if (!deserialize(m, raw.data(), raw.size(), info.flags, error)) return false;
    if (host) host->assign(packed + info.compressed_size, file.data() + file.size());
    if (out_info) *out_info = info;
    return true;
}

bool write_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    // Written beside and then renamed over, so a crash mid-write leaves the
    // old state rather than half of a new one.
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (std::fclose(f) != 0 || !ok) { std::remove(tmp.c_str()); return false; }
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

bool read_file(const std::string& path, std::vector<uint8_t>& bytes) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    bytes.clear();
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    return true;
}

uint64_t state_hash(const Machine& m) {
    std::vector<uint8_t> raw;
    serialize(m, raw, kPicture);
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : raw) h = (h ^ b) * 1099511628211ull;
    return h;
}

} // namespace savestate
} // namespace chaotix
