#include "recomp_dispatch.h"
#include "cpu/m68k/m68k_interp.h"
#include "cpu/sh2/sh2_interp.h"
#include "runtime/log.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>

#if CHAOTIX_HAVE_GENERATED
#include "recomp_decls.h"
#endif

namespace recomp {
int g_depth = 0;
bool g_idle_skip = true;
uint64_t g_idle_skipped[3] = {};
}

namespace chaotix {

namespace {

struct DispatchStats {
    uint64_t m68k_native = 0, m68k_interp = 0;
    uint64_t sh2_native[2] = {}, sh2_interp[2] = {};
    uint64_t sh2_validations = 0, sh2_invalid = 0;
    uint64_t m68k_trampolines = 0;
};
DispatchStats g_stats;
// Interpreter fallback histogram (entry PCs), used to direct coverage and
// static-analysis work. Keys: cpu << 32 | pc.
std::unordered_map<uint64_t, uint64_t> g_fallback;
Machine* g_m = nullptr;
bool g_installed = false;

// Outside the generated-code block: the interpreter reads SH-2 code through
// it too, so an interpreter-only build needs it as well.
const uint8_t* sh2_host_ptr(int cpu, uint32_t addr) {
    uint32_t area = addr >> 29;
    if (area <= 1) {
        uint32_t p = addr & 0x1FFFFFFF;
        if (p >= 0x06000000 && p < 0x08000000) return g_m->sdram + (p & 0x3FFFF);
        return nullptr;
    }
    if (area == 6) return g_m->onchip[cpu].cache_data + (addr & 0xFFF);
    return nullptr;
}

#if CHAOTIX_HAVE_GENERATED

// The block entry points, looked up on every dispatch. Open addressing in a
// power-of-two table with a multiplicative hash: std::unordered_map reduces
// its hash modulo a prime bucket count, a division per lookup, and 32-bit ARM
// (armeabi-v7a) has no divide instruction, so that is a library call each
// time (issue #54). Lookups find exactly what the map found.
template <class V>
class EntryTable {
public:
    void clear() {
        keys_.clear();
        vals_.clear();
        shift_ = 32;
        count_ = 0;
    }
    // Inserts or overwrites, as map[key] = v.
    void set(uint32_t key, V v) {
        if (key == kEmpty) return;  // not an address code can start at (odd)
        if ((count_ + 1) * 2 > keys_.size()) grow();
        uint32_t i = slot(key);
        const uint32_t mask = uint32_t(keys_.size() - 1);
        while (keys_[i] != kEmpty && keys_[i] != key) i = (i + 1) & mask;
        if (keys_[i] == kEmpty) ++count_;
        keys_[i] = key;
        vals_[i] = v;
    }
    const V* find(uint32_t key) const {
        if (keys_.empty() || key == kEmpty) return nullptr;
        const uint32_t mask = uint32_t(keys_.size() - 1);
        for (uint32_t i = slot(key);; i = (i + 1) & mask) {
            if (keys_[i] == key) return &vals_[i];
            if (keys_[i] == kEmpty) return nullptr;
        }
    }

private:
    static constexpr uint32_t kEmpty = 0xFFFFFFFFu;
    uint32_t slot(uint32_t key) const { return shift_ >= 32 ? 0 : (key * 0x9E3779B1u) >> shift_; }
    void grow() {
        std::vector<uint32_t> keys = std::move(keys_);
        std::vector<V> vals = std::move(vals_);
        const size_t cap = keys.empty() ? 64 : keys.size() * 2;
        keys_.assign(cap, kEmpty);
        vals_.assign(cap, V{});
        shift_ = 32;
        for (size_t c = cap; c > 1; c >>= 1) --shift_;
        count_ = 0;
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] != kEmpty) set(keys[i], vals[i]);
    }
    std::vector<uint32_t> keys_;
    std::vector<V> vals_;
    int shift_ = 32;
    size_t count_ = 0;
};

EntryTable<recomp::M68kFn> g_m68k_map;

struct Sh2FnState {
    recomp::Sh2Fn fn = nullptr;
    uint32_t range_begin = 0, range_count = 0;
    uint32_t sdram_epoch[2] = {0, 0}, cache_epoch[2] = {0, 0};
    bool valid[2] = {false, false};
};
std::vector<Sh2FnState> g_sh2_fns;
EntryTable<uint32_t> g_sh2_map;  // pc -> index in g_sh2_fns

inline recomp::M68kFn lookup_m68k(const m68k::State* c) {
    // The 68000 PC is 32 bits wide; generated code is keyed by the exact
    // 24-bit address, so PCs with upper bits set (e.g. 0xFFFFxxxx RAM stubs
    // reached through sign-extended absolute-short jumps) are interpreted.
    if (c->pc >> 24) return nullptr;
    uint32_t key = c->pc;
    if (key >= 0x900000 && key < 0xA00000) key |= uint32_t((g_m->mars.bank & 3) + 1) << 24;
    const recomp::M68kFn* fn = g_m68k_map.find(key);
    return fn ? *fn : nullptr;
}


bool sh2_validate(Sh2FnState& s, int cpu) {
    if (s.range_count == 0) return true;
    uint32_t se = g_m->sdram_code_epoch, ce = g_m->cache_code_epoch[cpu];
    if (s.sdram_epoch[cpu] == se && s.cache_epoch[cpu] == ce) return s.valid[cpu];
    ++g_stats.sh2_validations;
    bool ok = true;
    for (uint32_t i = 0; i < s.range_count && ok; ++i) {
        const recomp::CodeRange& r = recomp::g_sh2_ranges[s.range_begin + i];
        const uint8_t* host = sh2_host_ptr(cpu, r.addr);
        if (!host) continue;
        // Against the ROM the code was made from, not a patched one (issue #35):
        // code a patch changed then runs in the interpreter.
        const std::vector<uint8_t>& rom = g_m->rom.unpatched();
        if (r.rom_off + r.len > rom.size() || std::memcmp(host, rom.data() + r.rom_off, r.len) != 0) ok = false;
    }
    if (!ok) ++g_stats.sh2_invalid;
    s.valid[cpu] = ok;
    s.sdram_epoch[cpu] = se;
    s.cache_epoch[cpu] = ce;
    return ok;
}

inline Sh2FnState* lookup_sh2(const sh2::State* c) {
    const uint32_t* index = g_sh2_map.find(c->pc);
    if (!index) return nullptr;
    Sh2FnState& s = g_sh2_fns[*index];
    return sh2_validate(s, c->id) ? &s : nullptr;
}

// The game patches `JMP abs.l` trampolines into work RAM at runtime (e.g. the
// V-int vector target and per-level routine hooks). Executing one here with
// the interpreter's exact semantics (12 cycles, then a block boundary) keeps
// native execution going instead of unwinding to the interpreter.
// Returns false if the block boundary check says to stop.
inline bool follow_ram_trampolines(m68k::State* c) {
    for (int guard = 0; guard < 4; ++guard) {
        if ((c->pc & 0xFFFFFF) < 0xE00000 || (c->pc & 1)) return true;
        if (m68k::rd16(c, c->pc) != 0x4EF9) return true;
        c->cycles -= 12;
        c->pc = m68k::rd32(c, c->pc + 2);
        ++g_stats.m68k_trampolines;
        if (!recomp::m68k_ok(c)) return false;
    }
    return true;
}

void m68k_exec(m68k::State* c) {
    recomp::M68kFn fn = lookup_m68k(c);
    if (!fn && (c->pc & 0xFFFFFF) >= 0xE00000) {
        const uint32_t pc0 = c->pc;
        if (!follow_ram_trampolines(c)) return;
        if (c->pc != pc0) {
            fn = lookup_m68k(c);
            if (!fn) return;  // a trampoline ran (one block); the caller's loop continues
        }
    }
    if (fn) {
        ++g_stats.m68k_native;
        recomp::g_depth = 0;
        fn(c);
    } else {
        ++g_stats.m68k_interp;
        ++g_fallback[c->pc];
        m68k::interp_block(c);
    }
}

void sh2_exec(sh2::State* c) {
    Sh2FnState* s = lookup_sh2(c);
    if (s) {
        ++g_stats.sh2_native[c->id];
        recomp::g_depth = 0;
        s->fn(c);
    } else {
        ++g_stats.sh2_interp[c->id];
        ++g_fallback[(uint64_t(1 + c->id) << 32) | c->pc];
        sh2::interp_block(c);
    }
}

#endif // CHAOTIX_HAVE_GENERATED

} // namespace

} // namespace chaotix

namespace recomp {

int m68k_call_dynamic(m68k::State* c) {
#if CHAOTIX_HAVE_GENERATED
    M68kFn fn = chaotix::lookup_m68k(c);
    if (!fn && (c->pc & 0xFFFFFF) >= 0xE00000) {
        if (!chaotix::follow_ram_trampolines(c)) return 0;
        fn = chaotix::lookup_m68k(c);
    }
    if (!fn || g_depth >= kMaxDepth) return 1;
    ++g_depth;
    int r = fn(c);
    --g_depth;
    return r;
#else
    (void)c;
    return 1;
#endif
}

static const uint32_t g_no_epoch = 0;
const uint32_t* g_sdram_code_epoch = &g_no_epoch;

uint8_t sh2_code_byte(const sh2::State* c, uint32_t addr) {
    const uint8_t* p = chaotix::sh2_host_ptr(c->id, addr);
    return p ? *p : 0;
}

uint16_t sh2_code16(const sh2::State* c, uint32_t addr) {
    const uint8_t* p = chaotix::sh2_host_ptr(c->id, addr);
    return p ? uint16_t(p[0] << 8 | p[1]) : 0;
}

uint32_t sh2_code32(const sh2::State* c, uint32_t addr) {
    const uint8_t* p = chaotix::sh2_host_ptr(c->id, addr);
    return p ? uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3] : 0;
}

bool sh2_callee_valid(const sh2::State* c, uint32_t* cache) {
#if CHAOTIX_HAVE_GENERATED
    if (!*cache) {
        const uint32_t* index = chaotix::g_sh2_map.find(c->pc);
        if (!index) return false;
        *cache = *index + 1;
    }
    return chaotix::sh2_validate(chaotix::g_sh2_fns[*cache - 1], c->id);
#else
    (void)c; (void)cache;
    return false;
#endif
}

int32_t sh2_poll_repeats_slow(const sh2::State* c, uint32_t a, int size, int32_t at, int32_t cost) {
    const int32_t last = chaotix::g_m->sh2_read_stable_to(c->id, a, size, at);
    if (last == INT32_MIN) return INT32_MAX;
    const int64_t span = int64_t(at) - last;  // 32-bit division below (armeabi-v7a)
    return span > INT32_MAX ? INT32_MAX : int32_t(span) / cost;
}

int sh2_call_dynamic(sh2::State* c) {
#if CHAOTIX_HAVE_GENERATED
    chaotix::Sh2FnState* s = chaotix::lookup_sh2(c);
    if (!s || g_depth >= kMaxDepth) return 1;
    ++g_depth;
    int r = s->fn(c);
    --g_depth;
    return r;
#else
    (void)c;
    return 1;
#endif
}

} // namespace recomp

namespace chaotix {

RecompStatus install_recompiled_code(Machine& m, bool enable) {
    RecompStatus st;
    g_m = &m;
    g_stats = DispatchStats{};
    for (uint64_t& n : recomp::g_idle_skipped) n = 0;
    g_fallback.clear();
#if CHAOTIX_HAVE_GENERATED
    if (!enable) {
        st.description = "reference interpreters (recompiled code disabled by request)";
        return st;
    }
    if (m.rom.sha1 != recomp::g_rom_sha1) {
        LOGW("recomp", "generated code was built from a different ROM (%s); using interpreters", recomp::g_rom_sha1);
        st.description = "reference interpreters (generated code does not match this ROM)";
        return st;
    }
    g_m68k_map.clear();
    for (size_t i = 0; i < recomp::g_m68k_entry_count; ++i) g_m68k_map.set(recomp::g_m68k_entries[i].key, recomp::g_m68k_entries[i].fn);
    g_sh2_fns.clear();
    g_sh2_map.clear();
    std::map<recomp::Sh2Fn, uint32_t> index;
    m.sdram_code_ranges.clear();
    m.cache_code_ranges.clear();
    for (size_t i = 0; i < recomp::g_sh2_entry_count; ++i) {
        const recomp::Sh2Entry& e = recomp::g_sh2_entries[i];
        auto it = index.find(e.fn);
        if (it == index.end()) {
            Sh2FnState s;
            s.fn = e.fn;
            s.range_begin = e.range_begin;
            s.range_count = e.range_count;
            it = index.emplace(e.fn, uint32_t(g_sh2_fns.size())).first;
            g_sh2_fns.push_back(s);
        }
        g_sh2_map.set(e.pc, it->second);
    }
    for (size_t i = 0; i < recomp::g_sh2_range_count; ++i) {
        const recomp::CodeRange& r = recomp::g_sh2_ranges[i];
        uint32_t area = r.addr >> 29;
        if (area <= 1 && ((r.addr & 0x1FFFFFFF) >> 24) == 0x06) m.sdram_code_ranges.push_back({r.addr & 0x3FFFF, (r.addr & 0x3FFFF) + r.len});
        else if (area == 6) m.cache_code_ranges.push_back({r.addr & 0xFFF, (r.addr & 0xFFF) + r.len});
    }
    m.remap_sh2();
    // A ROM patch over code that runs straight from ROM (or literals folded
    // from it) would be ignored by the generated code: that CPU interprets.
    bool patched_code[2] = {false, false};
    for (const auto& [off, len] : m.rom.patched)
        for (size_t i = 0; i < recomp::g_rom_code_count; ++i) {
            const recomp::RomCode& rc = recomp::g_rom_code[i];
            if (rc.len && off < rc.rom_off + rc.len && rc.rom_off < off + len) patched_code[rc.cpu & 1] = true;
        }
    if (!patched_code[0]) m.exec.m68k_block = m68k_exec;
    recomp::g_sdram_code_epoch = &m.sdram_code_epoch;
    if (!patched_code[1]) m.exec.sh2_block = sh2_exec;
    st.active = true;
    g_installed = true;
    char buf[320];
    std::snprintf(buf, sizeof buf, "static recompilation (%zu 68K entry points, %zu SH-2 entry points, %zu SH-2 functions); interpreter fallback for uncovered code%s%s",
                  recomp::g_m68k_entry_count, recomp::g_sh2_entry_count, g_sh2_fns.size(),
                  patched_code[0] ? "; 68K interpreted (a ROM patch changes its code)" : "",
                  patched_code[1] ? "; SH-2 interpreted (a ROM patch changes its code)" : "");
    st.description = buf;
#else
    (void)enable;
    st.description = "reference interpreters (no generated code in this build)";
#endif
    return st;
}

Machine* bind_recompiled_code(Machine& m) {
    Machine* before = g_m;
    g_m = &m;
#if CHAOTIX_HAVE_GENERATED
    recomp::g_sdram_code_epoch = &m.sdram_code_epoch;
    // A machine's code epochs start at 1, so 0 matches none: every function
    // is checked again against this machine's memory before it next runs.
    for (Sh2FnState& s : g_sh2_fns) {
        s.sdram_epoch[0] = s.sdram_epoch[1] = 0;
        s.cache_epoch[0] = s.cache_epoch[1] = 0;
        s.valid[0] = s.valid[1] = false;
    }
#endif
    return before;
}

DispatchSnapshot dispatch_snapshot() {
    DispatchSnapshot s;
    auto pct = [](uint64_t a, uint64_t b) { return (a + b) ? 100.0 * double(a) / double(a + b) : 0.0; };
    s.recompiled = g_installed;
    s.m68k_native_pct = pct(g_stats.m68k_native, g_stats.m68k_interp);
    s.sh2_native_pct[0] = pct(g_stats.sh2_native[0], g_stats.sh2_interp[0]);
    s.sh2_native_pct[1] = pct(g_stats.sh2_native[1], g_stats.sh2_interp[1]);
    return s;
}

void set_idle_skip(bool on) { recomp::g_idle_skip = on; }

void print_recomp_stats(const Machine& m) {
    std::printf("cycles: 68K %llu, MSH2 %llu, SSH2 %llu\n", (unsigned long long)m.stats.m68k_cycles,
                (unsigned long long)m.stats.msh2_cycles, (unsigned long long)m.stats.ssh2_cycles);
    auto pct = [](uint64_t a, uint64_t b) { return (a + b) ? 100.0 * double(a) / double(a + b) : 0.0; };
    std::printf("dispatch: 68K native %llu / interp %llu (%.1f%% native); MSH2 %.1f%% native; SSH2 %.1f%% native; SH2 validations %llu (invalid %llu)\n",
                (unsigned long long)g_stats.m68k_native, (unsigned long long)g_stats.m68k_interp,
                pct(g_stats.m68k_native, g_stats.m68k_interp), pct(g_stats.sh2_native[0], g_stats.sh2_interp[0]),
                pct(g_stats.sh2_native[1], g_stats.sh2_interp[1]), (unsigned long long)g_stats.sh2_validations,
                (unsigned long long)g_stats.sh2_invalid);
    if (g_installed) {
        const uint64_t* sk = recomp::g_idle_skipped;
        auto share = [](uint64_t a, uint64_t b) { return b ? 100.0 * double(a) / double(b) : 0.0; };
        std::printf("wait loops fast-forwarded: 68K %.1f%%, MSH2 %.1f%%, SSH2 %.1f%% of cycles%s\n",
                    share(sk[0], m.stats.m68k_cycles), share(sk[1], m.stats.msh2_cycles), share(sk[2], m.stats.ssh2_cycles),
                    recomp::g_idle_skip ? "" : " (off)");
    }
    if (!g_fallback.empty()) {
        std::vector<std::pair<uint64_t, uint64_t>> v(g_fallback.begin(), g_fallback.end());
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        std::printf("interpreter fallback (%zu distinct entry PCs), top:", v.size());
        for (size_t i = 0; i < v.size() && i < 16; ++i)
            std::printf(" %s%08X:%llu", (v[i].first >> 32) ? ((v[i].first >> 32) == 1 ? "M:" : "S:") : "",
                        uint32_t(v[i].first), (unsigned long long)v[i].second);
        std::printf("\n");
    }
}

void CoverageRecorder::attach(Machine& m) {
    m_ = &m;
    m.on_block = [this](int cpu, uint32_t pc) {
        uint32_t bank = 0;
        if (cpu == 0 && pc >= 0x900000 && pc < 0xA00000) bank = m_->mars.bank & 3;
        entries_.insert({cpu, pc | (uint64_t(bank) << 32)});
    };
}

// Finds where the code bytes at a RAM address come from in the ROM.
static int64_t find_rom_source(const Machine& m, const uint8_t* bytes, size_t n) {
    const auto& rom = m.rom.data;
    auto it = std::search(rom.begin(), rom.begin() + long(m.rom.file_size), bytes, bytes + n);
    if (it == rom.begin() + long(m.rom.file_size)) return -1;
    return int64_t(it - rom.begin());
}

bool CoverageRecorder::save(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "# chaotix coverage v2\n");
    std::fprintf(f, "# rom_sha1 %s\n", m_ ? m_->rom.sha1.c_str() : "?");
    std::fprintf(f, "# columns: cpu(0=68k,1=msh2,2=ssh2) pc bank rom_source(-1 = statically mapped)\n");
    for (const auto& e : entries_) {
        int cpu = e.first;
        uint32_t pc = uint32_t(e.second);
        uint32_t bank = uint32_t(e.second >> 32);
        int64_t src = -1;
        if (m_) {
            bool ram = cpu == 0 ? pc >= 0xE00000 : ((pc >> 24) == 0x06 || (pc >> 24) == 0x26 || (pc >> 29) == 6);
            if (ram) {
                uint8_t buf[32];
                for (int i = 0; i < 32; ++i) {
                    if (cpu == 0) buf[i] = uint8_t(const_cast<Machine*>(m_)->m68k_read8(pc + uint32_t(i)));
                    else buf[i] = uint8_t(const_cast<Machine*>(m_)->sh2_read(cpu - 1, pc + uint32_t(i), 1));
                }
                src = find_rom_source(*m_, buf, sizeof buf);
            }
        }
        std::fprintf(f, "%d %08X %u %lld\n", cpu, pc, bank, (long long)src);
    }
    std::fclose(f);
    return true;
}

} // namespace chaotix
