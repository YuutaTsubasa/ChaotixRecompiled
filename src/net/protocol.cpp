#include "net/protocol.h"

#include <map>

namespace chaotix::net {

namespace {

const uint8_t kMagic[4] = {'C', 'X', 'N', 'P'};

struct Out {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { u8(uint8_t(v)); u8(uint8_t(v >> 8)); }
    void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
    void u64(uint64_t v) { u32(uint32_t(v)); u32(uint32_t(v >> 32)); }
    void str(const std::string& s) {
        const size_t n = s.size() < 0xFFFF ? s.size() : 0xFFFF;
        u16(uint16_t(n));
        b.insert(b.end(), s.begin(), s.begin() + long(n));
    }
};

struct In {
    const uint8_t* p;
    size_t n;
    bool ok = true;
    bool need(size_t k) {
        if (n < k) ok = false;
        return ok;
    }
    uint8_t u8() {
        if (!need(1)) return 0;
        --n;
        return *p++;
    }
    uint16_t u16() { const uint16_t lo = u8(); return uint16_t(lo | (u8() << 8)); }
    uint32_t u32() { const uint32_t lo = u16(); return lo | (uint32_t(u16()) << 16); }
    uint64_t u64() { const uint64_t lo = u32(); return lo | (uint64_t(u32()) << 32); }
    std::string str() {
        const size_t k = u16();
        if (!need(k)) return {};
        std::string s(reinterpret_cast<const char*>(p), k);
        p += k;
        n -= k;
        return s;
    }
};

void put_identity(Out& o, const Identity& id) {
    o.u16(id.protocol);
    o.str(id.game_version);
    o.str(id.rom_sha1);
    o.str(id.data_sha1);
    o.str(id.must_match);
}

void get_identity(In& in, Identity& id) {
    id.protocol = in.u16();
    id.game_version = in.str();
    id.rom_sha1 = in.str();
    id.data_sha1 = in.str();
    id.must_match = in.str();
}

std::map<std::string, std::string> lines(const std::string& s) {
    std::map<std::string, std::string> out;
    size_t at = 0;
    while (at < s.size()) {
        size_t end = s.find('\n', at);
        if (end == std::string::npos) end = s.size();
        const std::string line = s.substr(at, end - at);
        const size_t eq = line.find('=');
        if (eq != std::string::npos) out[line.substr(0, eq)] = line.substr(eq + 1);
        at = end + 1;
    }
    return out;
}

} // namespace

std::string mismatch(const Identity& mine, const Identity& theirs) {
    if (mine.protocol != theirs.protocol)
        return "the other player's netplay is a different version (protocol " + std::to_string(theirs.protocol) +
               ", this is " + std::to_string(mine.protocol) + "): use the same release";
    if (mine.game_version != theirs.game_version)
        return "the other player has version " + theirs.game_version + " (this is " + mine.game_version +
               "): use the same release";
    if (mine.rom_sha1 != theirs.rom_sha1) return "the other player's ROM is not the same as yours";
    if (mine.data_sha1 != theirs.data_sha1)
        return "the ROM patches or character stats (mods) are not the same on both sides";
    const auto a = lines(mine.must_match), b = lines(theirs.must_match);
    std::string out;
    auto note = [&](const std::string& key, const std::string& here, const std::string& there) {
        if (!out.empty()) out += "; ";
        out += key + " differs (yours: " + (here.empty() ? "none" : here) + ", theirs: " +
               (there.empty() ? "none" : there) + ")";
    };
    for (const auto& [k, v] : a) {
        auto it = b.find(k);
        const std::string other = it == b.end() ? "" : it->second;
        if (other != v) note(k, v, other);
    }
    for (const auto& [k, v] : b)
        if (!a.count(k)) note(k, "", v);
    return out;
}

std::vector<uint8_t> encode(const Message& m) {
    Out o;
    o.b.insert(o.b.end(), kMagic, kMagic + 4);
    o.u8(uint8_t(m.type));
    switch (m.type) {
    case Msg::Hello:
        put_identity(o, m.identity);
        break;
    case Msg::Welcome:
        put_identity(o, m.identity);
        o.u32(m.session);
        o.str(m.settings);
        o.u32(m.state_size);
        o.u32(m.chunks);
        break;
    case Msg::Reject:
        o.str(m.reason);
        break;
    case Msg::StateRequest:
        o.u32(m.session);
        o.u8(uint8_t(m.wanted.size() < 255 ? m.wanted.size() : 255));
        for (size_t i = 0; i < m.wanted.size() && i < 255; ++i) o.u32(m.wanted[i]);
        break;
    case Msg::StateChunk:
        o.u32(m.session);
        o.u32(m.index);
        o.u16(uint16_t(m.data.size()));
        o.b.insert(o.b.end(), m.data.begin(), m.data.end());
        break;
    case Msg::Ready:
    case Msg::Start:
        o.u32(m.session);
        break;
    case Msg::Input: {
        o.u32(m.session);
        o.u32(m.frame);
        o.u32(m.ack);
        o.u8(uint8_t(m.advantage));
        o.u32(m.send_ms);
        o.u32(m.echo_ms);
        o.u16(m.echo_hold);
        o.u32(m.first);
        const size_t n = m.inputs.size() < kMaxInputsPerMessage ? m.inputs.size() : kMaxInputsPerMessage;
        o.u8(uint8_t(n));
        for (size_t i = 0; i < n; ++i) o.u16(m.inputs[i]);
        const size_t h = m.hashes.size() < kMaxHashesPerMessage ? m.hashes.size() : kMaxHashesPerMessage;
        o.u8(uint8_t(h));
        for (size_t i = 0; i < h; ++i) {
            o.u32(m.hashes[i].first);
            o.u64(m.hashes[i].second);
        }
        break;
    }
    case Msg::Bye:
        o.u32(m.session);
        o.str(m.reason);
        break;
    case Msg::Desync:
        o.u32(m.session);
        o.u32(m.frame);
        break;
    }
    return std::move(o.b);
}

bool decode(const uint8_t* data, size_t n, Message& out) {
    if (n < 5 || data[0] != kMagic[0] || data[1] != kMagic[1] || data[2] != kMagic[2] || data[3] != kMagic[3])
        return false;
    In in{data + 5, n - 5};
    out = Message{};
    out.type = Msg(data[4]);
    switch (out.type) {
    case Msg::Hello:
        get_identity(in, out.identity);
        break;
    case Msg::Welcome:
        get_identity(in, out.identity);
        out.session = in.u32();
        out.settings = in.str();
        out.state_size = in.u32();
        out.chunks = in.u32();
        break;
    case Msg::Reject:
        out.reason = in.str();
        break;
    case Msg::StateRequest: {
        out.session = in.u32();
        const size_t k = in.u8();
        for (size_t i = 0; i < k && in.ok; ++i) out.wanted.push_back(in.u32());
        break;
    }
    case Msg::StateChunk: {
        out.session = in.u32();
        out.index = in.u32();
        const size_t k = in.u16();
        if (in.need(k)) out.data.assign(in.p, in.p + k);
        in.p += in.ok ? k : 0;
        in.n -= in.ok ? k : 0;
        break;
    }
    case Msg::Ready:
    case Msg::Start:
        out.session = in.u32();
        break;
    case Msg::Input: {
        out.session = in.u32();
        out.frame = in.u32();
        out.ack = in.u32();
        out.advantage = int8_t(in.u8());
        out.send_ms = in.u32();
        out.echo_ms = in.u32();
        out.echo_hold = in.u16();
        out.first = in.u32();
        const size_t k = in.u8();
        for (size_t i = 0; i < k && in.ok; ++i) out.inputs.push_back(in.u16());
        const size_t h = in.u8();
        for (size_t i = 0; i < h && in.ok; ++i) {
            const uint32_t f = in.u32();
            out.hashes.emplace_back(f, in.u64());
        }
        break;
    }
    case Msg::Bye:
        out.session = in.u32();
        out.reason = in.str();
        break;
    case Msg::Desync:
        out.session = in.u32();
        out.frame = in.u32();
        break;
    default:
        return false;
    }
    return in.ok;
}

} // namespace chaotix::net
