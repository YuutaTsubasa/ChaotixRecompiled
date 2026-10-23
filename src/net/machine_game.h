// The game a netplay session runs (issue #46): a Machine behind the
// interfaces of net/rollback.h and net/netplay.h.
//
// A snapshot is the raw save state without the picture (runtime/save_state.h)
// followed by the cartridge SRAM: a save state leaves the SRAM out, but a
// frame run on a prediction may write it (a level finished), and a rollback
// must take that back too. The start state the host sends is a save state
// file with the SRAM in its host bytes, so the client plays the host's
// saves; the client's own are kept aside and put back afterwards.
//
// The host settings that change what the game does without being part of
// the hardware's state are fixed for the session: the client takes the
// host's (widescreen margins, the 6-button pad, lobby shortcuts) and both
// apply them before every frame; the replacement music must already be the
// same on both (it is in the identity's must-match lines).
#pragma once
#include "net/netplay.h"

#include <functional>
#include <string>
#include <vector>

namespace chaotix {

class Machine;

namespace net {

// The host settings a session fixes (see above).
struct NetSettings {
    int wide_extra = 0, wide_extra_bottom = 0;
    bool wide_enabled = false, wide_everywhere = false, wide_hud_edges = false;
    bool six_button[2] = {true, false};
    int lobby_partner = -1, lobby_time = -1;
    static NetSettings of(const Machine& m);
    void apply(Machine& m) const;
    std::string encode() const;
    bool decode(const std::string& s, std::string* why);
    bool operator==(const NetSettings& o) const { return encode() == o.encode(); }
};

class MachineGame : public SessionGame {
public:
    explicit MachineGame(Machine& m) : m_(m) {}
    Machine& machine() { return m_; }
    const NetSettings& settings() const { return settings_; }
    void set_settings(const NetSettings& s) { settings_ = s; }

    // Around every frame the session runs (resimulating: a re-run after a
    // rollback). The app keeps the sound of new frames only, and its music
    // follows them.
    std::function<void(bool resimulating)> before_frame, after_frame;

    // Game
    void save(std::vector<uint8_t>& out) override;
    void load(const std::vector<uint8_t>& in) override;
    void advance(const Input pads[2], bool resimulating) override;
    // Leaves out the sound's output filter, which follows each player's own
    // volume settings (savestate::output_only_ranges).
    uint64_t hash(const std::vector<uint8_t>& snapshot) override;
    // SessionGame
    Identity identity() const override;
    std::string host_settings() const override { return settings_.encode(); }
    std::vector<uint8_t> start_state() override;
    bool use_host_settings(const std::string& settings, std::string* why) override;
    bool load_start_state(const std::vector<uint8_t>& bytes, std::string* why) override;

    // The client's own SRAM, kept when the host's was loaded over it.
    bool has_own_sram() const { return !own_sram_.empty(); }
    void restore_own_sram();

    // Writes a snapshot (save()'s bytes) as a loadable save state file, for
    // studying a desync. False if it cannot.
    bool write_snapshot_as_state(const std::vector<uint8_t>& snapshot, const std::string& path);

private:
    Machine& m_;
    NetSettings settings_;
    std::vector<uint8_t> own_sram_;
    std::vector<uint8_t> scratch_;
};

} // namespace net
} // namespace chaotix
