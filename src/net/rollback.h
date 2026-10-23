// Rollback netcode (issue #46, docs/NETPLAY.md): the core, with no sockets
// and no machine in it, so that it can be tested on its own.
//
// Both players run the whole game. Each frame needs both pads; the local one
// is known (input_delay frames ahead of the frame it is for), the remote one
// arrives later over the network. Until it does, it is predicted: the remote
// player is assumed to be holding what they held on the last frame known. The
// game runs on with the prediction, and a snapshot is kept of the state
// before every frame that used one. When the remote pad for frame F arrives
// and differs from what was predicted, the state before F is loaded and the
// frames from F to the present are run again with what is now known: a
// rollback. The game is deterministic, so once every pad is known both
// players' machines are exactly the same (checked: every hash_interval
// frames each side hashes the state before a frame whose inputs are all
// known, and the hashes are exchanged and compared).
//
// At most max_rollback frames are ever run on a prediction; with the remote
// player that far behind, a tick waits instead (a stall) until their input
// comes.
//
// Frame numbers count from the session's start (0 = the state both players
// started from). The local player's first input_delay frames are neutral (no
// button), on both sides, so a player's stream simply begins with that many
// zeros; the two players need not use the same delay.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace chaotix::net {

using Input = uint16_t;  // a pad: PadButton bits (input/input.h)

constexpr int kMaxInputDelay = 4;
constexpr int kDefaultInputDelay = 2;
constexpr int kDefaultMaxRollback = 8;
constexpr int kDefaultHashInterval = 60;
constexpr uint32_t kNoFrame = 0xFFFFFFFFu;

// What the core needs of the game. Snapshots are opaque bytes; the core only
// keeps them, gives them back, and hashes them.
class Game {
public:
    virtual ~Game() = default;
    // The whole state, as it is now (between frames).
    virtual void save(std::vector<uint8_t>& out) = 0;
    // Back to a state save() made.
    virtual void load(const std::vector<uint8_t>& in) = 0;
    // Runs one frame with these pads (player 1, player 2). `resimulating`:
    // the frame is being run again after a rollback (its picture and sound
    // were already shown once; the caller may drop its sound).
    virtual void advance(const Input pads[2], bool resimulating) = 0;
};

// A fast 64-bit hash of a snapshot (words at a time; the same on every
// platform).
uint64_t hash_bytes(const uint8_t* data, size_t n);

struct RollbackConfig {
    int local_player = 0;                 // 0 = player 1 (the host), 1 = player 2
    int input_delay = kDefaultInputDelay; // 0..kMaxInputDelay
    int max_rollback = kDefaultMaxRollback;
    int hash_interval = kDefaultHashInterval;  // 0: no hashes
    bool record_confirmed = false;        // keep every frame's final pads (tests)
};

struct RollbackStats {
    uint64_t frames = 0;           // frames advanced (new frames, not counting re-runs)
    uint64_t rollbacks = 0;        // times a misprediction was corrected
    uint64_t resimulated = 0;      // frames run again
    int last_rollback = 0;         // frames re-run by the most recent rollback
    int max_rollback = 0;          // the most ever re-run at once
    uint64_t stalls = 0;           // ticks that waited for the remote player
    uint64_t saves = 0;            // snapshots taken
    uint64_t hashes_checked = 0;   // hashes compared with the remote player's
};

class Rollback {
public:
    Rollback(Game& game, const RollbackConfig& cfg);

    const RollbackConfig& config() const { return cfg_; }
    // The next frame to run (= frames run so far).
    uint32_t frame() const { return current_; }

    // One frame period with the local pad. Corrects any misprediction first,
    // then runs the next frame unless that would put more than max_rollback
    // frames on a prediction, in which case it stalls: nothing runs and the
    // pad is not used (false). True when a frame was run.
    bool tick(Input local);
    // Whether the next tick would stall (the remote player is too far behind).
    bool would_stall() const;

    // ---- the network side ----
    // The remote player's pads for frames [first, first + n). Frames already
    // known are ignored; a gap (a frame after one not yet received) ends the
    // run, as the sender sends again from what was acknowledged.
    void add_remote_inputs(uint32_t first, const Input* inputs, size_t n);
    // The remote player has the local pads of every frame before `frame`.
    void set_remote_ack(uint32_t frame);
    uint32_t remote_ack() const { return remote_ack_; }
    // The remote pads known: every frame before this.
    uint32_t remote_head() const { return uint32_t(remote_.size()); }
    // The local pads known: every frame before this.
    uint32_t local_head() const { return uint32_t(local_.size()); }
    Input local_input(uint32_t frame) const { return local_[frame]; }
    // The frame the remote player said they were on, for time sync.
    void set_remote_frame(uint32_t frame) { if (frame > remote_frame_ || remote_frame_ == kNoFrame) remote_frame_ = frame; }
    uint32_t remote_frame() const { return remote_frame_; }
    // How many frames ahead of the remote player this side is, given the
    // round trip in frames (positive: ahead, and should wait now and then).
    int frame_advantage(int rtt_frames) const;

    // ---- desync detection ----
    // The local hashes made so far, newest last (frame, hash).
    const std::map<uint32_t, uint64_t>& local_hashes() const { return local_hashes_; }
    void add_remote_hash(uint32_t frame, uint64_t hash);
    bool desynced() const { return desync_frame_ != kNoFrame; }
    uint32_t desync_frame() const { return desync_frame_; }
    // The local snapshot a hash was made of, while it is kept (the last few):
    // what to write out when the players disagree. Null if gone.
    const std::vector<uint8_t>* hashed_snapshot(uint32_t frame) const;
    // The local hash of the state before `frame`; 0 if not made (yet).
    uint64_t hash_at(uint32_t frame) const;

    // ---- what is final ----
    // Every frame before this has had both pads known and was run with them:
    // the state before it is the same on both machines.
    uint32_t confirmed() const;
    // With record_confirmed: the final pads of every confirmed frame.
    const std::vector<std::array<Input, 2>>& confirmed_inputs() const { return log_; }

    const RollbackStats& stats() const { return stats_; }

private:
    Input predict(uint32_t f) const;
    void simulate(uint32_t f, bool resim);
    void correct();
    void after_correct();
    void compare(uint32_t f);
    struct Slot {
        uint32_t frame = kNoFrame;
        std::vector<uint8_t> data;
    };
    Slot& slot_for(uint32_t f) { return slots_[f % slots_.size()]; }

    Game& game_;
    RollbackConfig cfg_;
    uint32_t current_ = 0;
    std::vector<Input> local_, remote_, used_;  // used_: the remote pad each run frame used
    uint32_t first_wrong_ = kNoFrame;
    uint32_t remote_ack_ = 0;
    uint32_t remote_frame_ = kNoFrame;
    std::vector<Slot> slots_;
    uint32_t next_hash_ = 0;
    std::map<uint32_t, uint64_t> local_hashes_, remote_hashes_;
    std::deque<std::pair<uint32_t, std::vector<uint8_t>>> hashed_;
    uint32_t desync_frame_ = kNoFrame;
    std::vector<std::array<Input, 2>> log_;
    RollbackStats stats_;
};

} // namespace chaotix::net
