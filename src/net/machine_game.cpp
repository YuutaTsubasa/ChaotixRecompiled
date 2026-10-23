#include "net/machine_game.h"

#include "runtime/save_state.h"
#include "runtime/system.h"

#include <cstdio>
#include <cstring>
#include <map>

namespace chaotix::net {

namespace {

constexpr size_t kSram = sizeof(Machine::sram);

std::map<std::string, std::string> fields(const std::string& s) {
    std::map<std::string, std::string> out;
    size_t at = 0;
    while (at < s.size()) {
        size_t end = s.find(';', at);
        if (end == std::string::npos) end = s.size();
        const std::string kv = s.substr(at, end - at);
        const size_t eq = kv.find('=');
        if (eq != std::string::npos) out[kv.substr(0, eq)] = kv.substr(eq + 1);
        at = end + 1;
    }
    return out;
}

} // namespace

NetSettings NetSettings::of(const Machine& m) {
    NetSettings s;
    s.wide_extra = m.wide_extra;
    s.wide_extra_bottom = m.wide_extra_bottom;
    s.wide_enabled = m.wide_enabled;
    s.wide_everywhere = m.wide_everywhere;
    s.wide_hud_edges = m.wide_hud_edges;
    s.six_button[0] = m.input.six_button[0];
    s.six_button[1] = m.input.six_button[1];
    s.lobby_partner = m.lobby_partner;
    s.lobby_time = m.lobby_time;
    return s;
}

void NetSettings::apply(Machine& m) const {
    m.wide_extra = wide_extra;
    m.wide_extra_bottom = wide_extra_bottom;
    m.wide_enabled = wide_enabled;
    m.wide_everywhere = wide_everywhere;
    m.wide_hud_edges = wide_hud_edges;
    m.input.six_button[0] = six_button[0];
    m.input.six_button[1] = six_button[1];
    m.lobby_partner = lobby_partner;
    m.lobby_time = lobby_time;
}

std::string NetSettings::encode() const {
    char buf[256];
    std::snprintf(buf, sizeof buf, "wide=%d;wide_bottom=%d;wide_on=%d;wide_everywhere=%d;hud_edges=%d;six=%d%d;lobby=%d,%d",
                  wide_extra, wide_extra_bottom, int(wide_enabled), int(wide_everywhere), int(wide_hud_edges),
                  int(six_button[0]), int(six_button[1]), lobby_partner, lobby_time);
    return buf;
}

bool NetSettings::decode(const std::string& s, std::string* why) {
    const auto f = fields(s);
    for (const char* k : {"wide", "wide_bottom", "wide_on", "wide_everywhere", "hud_edges", "six", "lobby"})
        if (!f.count(k)) {
            if (why) *why = std::string("no ") + k + " in the host's settings";
            return false;
        }
    NetSettings n;
    n.wide_extra = std::atoi(f.at("wide").c_str());
    n.wide_extra_bottom = std::atoi(f.at("wide_bottom").c_str());
    n.wide_enabled = f.at("wide_on") == "1";
    n.wide_everywhere = f.at("wide_everywhere") == "1";
    n.wide_hud_edges = f.at("hud_edges") == "1";
    const std::string& six = f.at("six");
    n.six_button[0] = six.size() > 0 && six[0] == '1';
    n.six_button[1] = six.size() > 1 && six[1] == '1';
    if (std::sscanf(f.at("lobby").c_str(), "%d,%d", &n.lobby_partner, &n.lobby_time) != 2) {
        if (why) *why = "the host's lobby setting is unreadable";
        return false;
    }
    if (n.wide_extra < 0 || n.wide_extra > 256 || n.wide_extra_bottom < 0 || n.wide_extra_bottom > 64) {
        if (why) *why = "the host's widescreen margins are out of range";
        return false;
    }
    *this = n;
    return true;
}

void MachineGame::save(std::vector<uint8_t>& out) {
    savestate::serialize(m_, out, 0);
    const size_t n = out.size();
    out.resize(n + kSram);
    std::memcpy(out.data() + n, m_.sram, kSram);
}

void MachineGame::load(const std::vector<uint8_t>& in) {
    if (in.size() < kSram) return;
    const size_t n = in.size() - kSram;
    if (!savestate::deserialize(m_, in.data(), n, 0, nullptr)) return;
    std::memcpy(m_.sram, in.data() + n, kSram);
}

void MachineGame::advance(const Input pads[2], bool resimulating) {
    settings_.apply(m_);
    m_.input.pad[0] = pads[0];
    m_.input.pad[1] = pads[1];
    // The sound chips' output filters are in the state, and only move while
    // sound is made: both sides make it, whether or not they play it.
    m_.audio_enabled = true;
    if (before_frame) before_frame(resimulating);
    m_.run_frame();
    if (after_frame) after_frame(resimulating);
}

Identity MachineGame::identity() const {
    Identity id;
    id.game_version = savestate::game_version();
    id.rom_sha1 = m_.rom.sha1;
    id.data_sha1 = m_.rom.patched_sha1.empty() ? m_.rom.sha1 : m_.rom.patched_sha1;
    // The replacement music changes what the game does (the patch stops its
    // own music for a replaced track): the same tracks on both sides.
    std::string music;
    for (size_t i = 0; i < m_.music_replaced.size(); ++i) {
        if (!m_.music_replaced[i]) continue;
        char b[8];
        std::snprintf(b, sizeof b, "%s%02X", music.empty() ? "" : " ", unsigned(i));
        music += b;
    }
    id.must_match = "replaced music tracks=" + music + "\n";
    return id;
}

std::vector<uint8_t> MachineGame::start_state() {
    settings_ = NetSettings::of(m_);
    const std::vector<uint8_t> sram(m_.sram, m_.sram + kSram);
    return savestate::save(m_, sram, 0, savestate::kPicture);
}

bool MachineGame::use_host_settings(const std::string& settings, std::string* why) {
    return settings_.decode(settings, why);
}

bool MachineGame::load_start_state(const std::vector<uint8_t>& bytes, std::string* why) {
    std::vector<uint8_t> host;
    std::vector<uint8_t> own(m_.sram, m_.sram + kSram);
    if (!savestate::load(m_, bytes, why, &host)) return false;
    if (host.size() != kSram) {
        if (why) *why = "the host's save data is missing";
        return false;
    }
    if (own_sram_.empty()) own_sram_ = std::move(own);
    std::memcpy(m_.sram, host.data(), kSram);
    settings_.apply(m_);
    return true;
}

void MachineGame::restore_own_sram() {
    if (own_sram_.size() != kSram) return;
    std::memcpy(m_.sram, own_sram_.data(), kSram);
    own_sram_.clear();
}

bool MachineGame::write_snapshot_as_state(const std::vector<uint8_t>& snapshot, const std::string& path) {
    if (snapshot.size() < kSram) return false;
    // Through the machine itself, which is put back as it was afterwards.
    std::vector<uint8_t> now;
    save(now);
    const std::vector<uint8_t> picture(reinterpret_cast<const uint8_t*>(m_.framebuffer),
                                       reinterpret_cast<const uint8_t*>(m_.framebuffer) + sizeof m_.framebuffer);
    load(snapshot);
    const std::vector<uint8_t> sram(m_.sram, m_.sram + kSram);
    const std::vector<uint8_t> file = savestate::save(m_, sram, 0, 0);
    load(now);
    std::memcpy(m_.framebuffer, picture.data(), picture.size());
    return savestate::write_file(path, file);
}

uint64_t snapshot_hash(const std::vector<uint8_t>& snapshot) { return hash_bytes(snapshot.data(), snapshot.size()); }

} // namespace chaotix::net
