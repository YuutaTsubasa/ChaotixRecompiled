// A netplay session (issue #46, docs/NETPLAY.md): the handshake, the start
// state's transfer, and then the rollback core (net/rollback.h) fed over the
// network, with ping, time sync, desync detection and disconnects.
//
// Platform-independent: datagrams go through a Transport (net/transport.h),
// time is given by the caller in milliseconds, and the game is a SessionGame.
// The host is player 1, the client player 2. Call update() often (every pass
// of the main loop: it reads the network), and tick() once per frame period
// while the session is running.
#pragma once
#include "net/protocol.h"
#include "net/rollback.h"
#include "net/transport.h"

#include <memory>
#include <string>
#include <vector>

namespace chaotix::net {

// What a session needs of the game besides the rollback core's Game.
class SessionGame : public Game {
public:
    virtual Identity identity() const = 0;
    // The host: the settings the client takes over for the session, and the
    // state both start from (as bytes; asked for once, when a player is let
    // in: the host's game holds still from then until the session starts).
    virtual std::string host_settings() const = 0;
    virtual std::vector<uint8_t> start_state() = 0;
    // The client: false, with the reason, if they cannot be used.
    virtual bool use_host_settings(const std::string& settings, std::string* why) = 0;
    virtual bool load_start_state(const std::vector<uint8_t>& bytes, std::string* why) = 0;
};

struct SessionOptions {
    int input_delay = kDefaultInputDelay;
    int max_rollback = kDefaultMaxRollback;
    int hash_interval = kDefaultHashInterval;
    uint32_t timeout_ms = 5000;           // silence that ends a running session
    uint32_t connect_timeout_ms = 15000;  // a host that never answers
    bool record_confirmed = false;        // keep the final pads of every frame (tests)
    bool time_sync = true;                // the side ahead waits a frame now and then
};

class Session {
public:
    enum class Phase { Idle, Waiting, Connecting, Transferring, Running, Ended };
    enum class End { None, Left, OtherLeft, Lost, Rejected, Desync, Error };
    enum class Tick { NotRunning, Advanced, Stalled, Waiting };

    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = default;
    Session& operator=(Session&&) = default;

    // Player 1: waits for a player to join through `transport`.
    void host(std::unique_ptr<Transport> transport, SessionGame& game, const SessionOptions& o, uint32_t now_ms);
    // Player 2: joins the host at `host_at`.
    void join(std::unique_ptr<Transport> transport, const Endpoint& host_at, SessionGame& game,
              const SessionOptions& o, uint32_t now_ms);

    // Reads the network and keeps the handshake and the link going.
    void update(uint32_t now_ms);
    // One frame period while Running: runs a frame (or more, after a
    // rollback) through the game, or waits.
    Tick tick(Input local, uint32_t now_ms);
    // Says goodbye to the other player and ends the session.
    void leave(uint32_t now_ms);

    Phase phase() const { return phase_; }
    bool is_host() const { return host_; }
    // Whether the game is the session's: from the moment a player is let in
    // (the host) or the host is asked (the client) until the end.
    bool owns_game() const { return phase_ == Phase::Transferring || phase_ == Phase::Running || (!host_ && phase_ == Phase::Connecting); }
    End end() const { return end_; }
    const std::string& end_reason() const { return end_reason_; }
    // The last thing worth telling a waiting host (a player refused, ...).
    const std::string& notice() const { return notice_; }
    std::string peer() const;
    // 0..1 while the start state is on its way.
    float transfer_progress() const;
    size_t start_state_bytes() const { return host_ ? start_.size() : size_t(state_size_); }
    // Round trip, smoothed; 0 until measured.
    int ping_ms() const { return int(rtt_ms_ + 0.5); }
    uint64_t waits() const { return waits_; }
    uint32_t desync_frame() const { return desync_frame_; }
    // The core, while (and after) running; null before.
    const Rollback* rollback() const { return rollback_.get(); }
    Rollback* rollback() { return rollback_.get(); }
    int local_player() const { return host_ ? 0 : 1; }

private:
    void handle(const Endpoint& from, const Message& m, uint32_t now);
    void send(const Message& m);
    void send_input(uint32_t now);
    void start_running(uint32_t now);
    void request_chunks(uint32_t now);
    void finish(End kind, const std::string& reason, uint32_t now);
    void enter(Phase p, uint32_t now);

    std::unique_ptr<Transport> transport_;
    SessionGame* game_ = nullptr;
    SessionOptions opts_;
    Phase phase_ = Phase::Idle;
    bool host_ = false;
    Endpoint peer_;
    uint32_t session_ = 0;
    uint32_t phase_since_ = 0, last_recv_ = 0, last_send_ = 0, last_hello_ = 0, last_ready_ = 0, last_start_ = 0;
    End end_ = End::None;
    std::string end_reason_, notice_;
    // The start state: the host's whole; the client's chunks as they come.
    std::vector<uint8_t> start_;
    Message welcome_;
    uint32_t state_size_ = 0, chunks_ = 0, got_count_ = 0;
    std::vector<uint8_t> got_;
    std::vector<uint32_t> asked_at_;
    bool loaded_ = false, peer_started_ = false;
    std::unique_ptr<Rollback> rollback_;
    double rtt_ms_ = 0;
    uint32_t peer_send_ms_ = 0, peer_send_at_ = 0;
    int remote_advantage_ = 0;
    // Time sync: the advantages summed over a window of ticks.
    int sync_ticks_ = 0, skip_ = 0;
    int64_t sum_local_adv_ = 0, sum_remote_adv_ = 0;
    uint64_t waits_ = 0;
    uint32_t desync_frame_ = kNoFrame;
    uint32_t seed_ = 0;
};

const char* phase_name(Session::Phase p);

} // namespace chaotix::net
