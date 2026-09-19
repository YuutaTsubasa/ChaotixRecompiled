#include "rom.h"
#include <cstdio>
#include <cstring>

namespace chaotix {

namespace {

struct Sha1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint8_t buf[64];
    size_t buflen = 0;
    uint64_t total = 0;

    static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
    void block(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    void update(const uint8_t* p, size_t n) {
        total += n;
        while (n) {
            size_t take = 64 - buflen;
            if (take > n) take = n;
            std::memcpy(buf + buflen, p, take);
            buflen += take; p += take; n -= take;
            if (buflen == 64) { block(buf); buflen = 0; }
        }
    }
    std::string finish() {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (buflen != 56) update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = uint8_t(bits >> (56 - 8 * i));
        update(len, 8);
        char out[41];
        for (int i = 0; i < 5; ++i) snprintf(out + i * 8, 9, "%08x", h[i]);
        return out;
    }
};

struct KnownRom {
    const char* sha1;
    RomVersion version;
};

// Knuckles' Chaotix (Japan, USA) — the only retail release.
const KnownRom kKnown[] = {
    {"0c2fff7bc79ed26507c08ac47464c3af19f7ced7", RomVersion::ChaotixJUE},
};

} // namespace

std::string sha1_hex(const uint8_t* data, size_t n) {
    Sha1 s;
    s.update(data, n);
    return s.finish();
}

const char* rom_version_name(RomVersion v) {
    switch (v) {
    case RomVersion::ChaotixJUE: return "Knuckles' Chaotix (Japan, USA)";
    default: return "unknown";
    }
}

bool Rom::load(const std::string& path, std::string* error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (error) *error = "cannot open ROM: " + path; return false; }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 8 * 1024 * 1024) { std::fclose(f); if (error) *error = "unexpected ROM size"; return false; }
    std::vector<uint8_t> bytes(static_cast<size_t>(sz));
    size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (got != bytes.size()) { if (error) *error = "short read"; return false; }
    return load_memory(bytes.data(), bytes.size(), error);
}

bool Rom::load_memory(const uint8_t* bytes, size_t n, std::string* error) {
    if (n < 0x800) { if (error) *error = "ROM too small"; return false; }
    file_size = n;
    sha1 = sha1_hex(bytes, n);
    version = RomVersion::Unknown;
    for (const auto& k : kKnown)
        if (sha1 == k.sha1) version = k.version;
    size_t cap = 0x80000;
    while (cap < n) cap <<= 1;
    if (cap < 0x400000) cap = 0x400000;
    data.assign(cap, 0xFF);
    std::memcpy(data.data(), bytes, n);
    // Mirror smaller images across the 4 MiB window.
    for (size_t off = n; off < 0x400000 && (n & (n - 1)) == 0; off += n)
        std::memcpy(data.data() + off, bytes, n);

    std::memcpy(domestic_name, bytes + 0x120, 48);
    header_checksum = read16(0x18E);
    std::memcpy(mars.module_name, bytes + 0x3C0, 16);
    mars.version = read32(0x3D0);
    mars.source = read32(0x3D4);
    mars.dest = read32(0x3D8);
    mars.size = read32(0x3DC);
    mars.master_entry = read32(0x3E0);
    mars.slave_entry = read32(0x3E4);
    mars.master_vbr = read32(0x3E8);
    mars.slave_vbr = read32(0x3EC);
    if (std::memcmp(bytes + 0x100, "SEGA 32X", 8) != 0) {
        if (error) *error = "not a 32X ROM (missing 'SEGA 32X' header)";
        return false;
    }
    return true;
}

uint16_t Rom::compute_checksum() const {
    uint16_t sum = 0;
    for (size_t i = 0x200; i + 1 < file_size; i += 2) sum = uint16_t(sum + read16(uint32_t(i)));
    return sum;
}

} // namespace chaotix
