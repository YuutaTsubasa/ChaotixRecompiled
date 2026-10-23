#include "net/netplay.h"

#include <algorithm>
#include <random>

namespace chaotix::net {

namespace {

constexpr uint32_t kHelloEvery = 250;     // ms between HELLOs while connecting
constexpr uint32_t kReadyEvery = 100;     // READY / START until the other side moves
constexpr uint32_t kKeepAliveEvery = 50;  // INPUT even when no frame is ticked
constexpr size_t kChunksInFlight = 128;   // start state chunks asked for and not yet come
constexpr size_t kChunksPerRequest = 64;
constexpr int kSyncWindow = 30;           // ticks a time sync decision is averaged over
constexpr double kFrameMs = 1000.0 / 60.0;

// The difference of two millisecond clocks that may wrap.
uint32_t since(uint32_t now, uint32_t then) { return now - then; }

} // namespace

const char* phase_name(Session::Phase p) {
    switch (p) {
    case Session::Phase::Idle: return "idle";
    case Session::Phase::Waiting: return "waiting";
    case Session::Phase::Connecting: return "connecting";
    case Session::Phase::Transferring: return "transferring";
    case Session::Phase::Running: return "running";
    case Session::Phase::Ended: return "ended";
    }
    return "?";
}

void Session::enter(Phase p, uint32_t now) {
    phase_ = p;
    phase_since_ = now;
}

void Session::host(std::unique_ptr<Transport> transport, SessionGame& game, const SessionOptions& o, uint32_t now) {
    *this = Session{};
    transport_ = std::move(transport);
    game_ = &game;
    opts_ = o;
    host_ = true;
    std::random_device rd;
    seed_ = rd() ^ (now * 2654435761u);
    enter(Phase::Waiting, now);
}

void Session::join(std::unique_ptr<Transport> transport, const Endpoint& host_at, SessionGame& game,
                   const SessionOptions& o, uint32_t now) {
    *this = Session{};
    transport_ = std::move(transport);
    game_ = &game;
    opts_ = o;
    host_ = false;
    peer_ = host_at;
    last_recv_ = now;
    enter(Phase::Connecting, now);
    Message hello;
    hello.type = Msg::Hello;
    hello.identity = game_->identity();
    send(hello);
    last_hello_ = now;
}

std::string Session::peer() const { return transport_ && peer_.valid() ? transport_->describe(peer_) : std::string(); }

float Session::transfer_progress() const {
    if (host_) return phase_ == Phase::Running ? 1.0f : 0.0f;
    if (!chunks_) return 0.0f;
    return float(got_count_) / float(chunks_);
}

void Session::send(const Message& m) {
    if (!transport_ || !peer_.valid()) return;
    const std::vector<uint8_t> bytes = encode(m);
    transport_->send(peer_, bytes.data(), bytes.size());
}

void Session::finish(End kind, const std::string& reason, uint32_t now) {
    if (phase_ == Phase::Ended) return;
    if (peer_.valid() && session_ && (kind == End::Left || kind == End::Desync || kind == End::Error)) {
        Message m;
        m.session = session_;
        if (kind == End::Desync) {
            m.type = Msg::Desync;
            m.frame = desync_frame_;
        } else {
            m.type = Msg::Bye;
            m.reason = reason;
        }
        for (int i = 0; i < 3; ++i) send(m);  // one of them gets there
    }
    end_ = kind;
    end_reason_ = reason;
    enter(Phase::Ended, now);
}

void Session::leave(uint32_t now) {
    if (phase_ == Phase::Idle || phase_ == Phase::Ended) return;
    finish(End::Left, "you left", now);
}

void Session::start_running(uint32_t now) {
    RollbackConfig rc;
    rc.local_player = host_ ? 0 : 1;
    rc.input_delay = opts_.input_delay;
    rc.max_rollback = opts_.max_rollback;
    rc.hash_interval = opts_.hash_interval;
    rc.record_confirmed = opts_.record_confirmed;
    rollback_ = std::make_unique<Rollback>(*game_, rc);
    last_recv_ = now;
    enter(Phase::Running, now);
}

void Session::request_chunks(uint32_t now) {
    const uint32_t rto = std::max<uint32_t>(150, uint32_t(rtt_ms_ * 2));
    size_t in_flight = 0;
    for (uint32_t i = 0; i < chunks_; ++i)
        if (!got_[i] && asked_at_[i] && since(now, asked_at_[i]) < rto) ++in_flight;
    Message req;
    req.type = Msg::StateRequest;
    req.session = session_;
    for (uint32_t i = 0; i < chunks_ && in_flight < kChunksInFlight; ++i) {
        if (got_[i] || (asked_at_[i] && since(now, asked_at_[i]) < rto)) continue;
        req.wanted.push_back(i);
        asked_at_[i] = now ? now : 1;
        ++in_flight;
        if (req.wanted.size() == kChunksPerRequest) {
            send(req);
            req.wanted.clear();
        }
    }
    if (!req.wanted.empty()) send(req);
}

void Session::send_input(uint32_t now) {
    if (!rollback_) return;
    Message m;
    m.type = Msg::Input;
    m.session = session_;
    m.frame = rollback_->frame();
    m.ack = rollback_->remote_head();
    const int rtt_frames = int(rtt_ms_ / kFrameMs + 0.5);
    m.advantage = int8_t(std::clamp(rollback_->frame_advantage(rtt_frames), -127, 127));
    m.send_ms = now ? now : 1;
    m.echo_ms = peer_send_ms_;
    m.echo_hold = uint16_t(std::min<uint32_t>(since(now, peer_send_at_), 0xFFFF));
    m.first = rollback_->remote_ack();
    for (uint32_t f = m.first; f < rollback_->local_head() && m.inputs.size() < kMaxInputsPerMessage; ++f)
        m.inputs.push_back(rollback_->local_input(f));
    const auto& hashes = rollback_->local_hashes();
    for (auto it = hashes.rbegin(); it != hashes.rend() && m.hashes.size() < kMaxHashesPerMessage; ++it)
        m.hashes.emplace_back(it->first, it->second);
    send(m);
    last_send_ = now;
}

void Session::handle(const Endpoint& from, const Message& m, uint32_t now) {
    if (host_) {
        if (m.type == Msg::Hello) {
            if ((phase_ == Phase::Transferring || phase_ == Phase::Running) && from == peer_) {
                if (phase_ == Phase::Transferring) {  // our WELCOME was lost
                    welcome_.session = session_;
                    send(welcome_);
                }
                return;
            }
            Message reply;
            if (phase_ != Phase::Waiting) {
                reply.type = Msg::Reject;
                reply.reason = "a game is already being played there";
            } else {
                const std::string why = mismatch(game_->identity(), m.identity);
                if (!why.empty()) {
                    reply.type = Msg::Reject;
                    reply.reason = why;
                    notice_ = "refused " + transport_->describe(from) + ": " + why;
                }
            }
            if (reply.type == Msg::Reject) {
                const std::vector<uint8_t> bytes = encode(reply);
                transport_->send(from, bytes.data(), bytes.size());
                return;
            }
            // Let in: the game holds still from here, at the state sent.
            peer_ = from;
            do {
                seed_ = seed_ * 1664525u + 1013904223u;
                session_ = seed_;
            } while (!session_);
            start_ = game_->start_state();
            welcome_ = Message{};
            welcome_.type = Msg::Welcome;
            welcome_.identity = game_->identity();
            welcome_.session = session_;
            welcome_.settings = game_->host_settings();
            welcome_.state_size = uint32_t(start_.size());
            welcome_.chunks = uint32_t((start_.size() + kChunkSize - 1) / kChunkSize);
            send(welcome_);
            last_recv_ = now;
            notice_ = transport_->describe(from) + " is joining";
            enter(Phase::Transferring, now);
            return;
        }
        if (from != peer_ || m.session != session_ || !session_) return;
        last_recv_ = now;
        switch (m.type) {
        case Msg::StateRequest:
            for (uint32_t i : m.wanted) {
                if (size_t(i) * kChunkSize >= start_.size()) continue;
                Message c;
                c.type = Msg::StateChunk;
                c.session = session_;
                c.index = i;
                const size_t at = size_t(i) * kChunkSize;
                const size_t n = std::min(kChunkSize, start_.size() - at);
                c.data.assign(start_.begin() + long(at), start_.begin() + long(at + n));
                send(c);
            }
            break;
        case Msg::Ready:
            if (phase_ == Phase::Transferring) start_running(now);
            if (phase_ == Phase::Running && !peer_started_) {
                Message s;
                s.type = Msg::Start;
                s.session = session_;
                send(s);
                last_start_ = now;
            }
            break;
        default:
            break;
        }
    } else {
        if (from != peer_) return;
        if (m.type == Msg::Reject) {
            if (phase_ == Phase::Connecting) finish(End::Rejected, "the host said no: " + m.reason, now);
            return;
        }
        if (m.type == Msg::Welcome) {
            if (phase_ != Phase::Connecting) return;
            last_recv_ = now;
            const std::string why = mismatch(game_->identity(), m.identity);
            session_ = m.session;
            if (!why.empty()) {
                finish(End::Error, why, now);
                end_ = End::Rejected;
                return;
            }
            std::string bad;
            if (!game_->use_host_settings(m.settings, &bad)) {
                finish(End::Error, "the host's settings cannot be used: " + bad, now);
                return;
            }
            state_size_ = m.state_size;
            chunks_ = m.chunks;
            if (!chunks_ || uint64_t(chunks_) * kChunkSize < state_size_ || state_size_ > (64u << 20)) {
                finish(End::Error, "the host sent a start state that makes no sense", now);
                return;
            }
            start_.assign(state_size_, 0);
            got_.assign(chunks_, 0);
            asked_at_.assign(chunks_, 0);
            got_count_ = 0;
            enter(Phase::Transferring, now);
            request_chunks(now);
            return;
        }
        if (m.session != session_ || !session_) return;
        last_recv_ = now;
        switch (m.type) {
        case Msg::StateChunk: {
            if (phase_ != Phase::Transferring || m.index >= chunks_ || got_[m.index]) break;
            const size_t at = size_t(m.index) * kChunkSize;
            const size_t want = std::min(kChunkSize, size_t(state_size_) - at);
            if (m.data.size() != want) break;
            std::copy(m.data.begin(), m.data.end(), start_.begin() + long(at));
            got_[m.index] = 1;
            ++got_count_;
            if (got_count_ == chunks_ && !loaded_) {
                std::string why;
                if (!game_->load_start_state(start_, &why)) {
                    finish(End::Error, "the host's game cannot be loaded here: " + why, now);
                    return;
                }
                loaded_ = true;
                Message r;
                r.type = Msg::Ready;
                r.session = session_;
                send(r);
                last_ready_ = now;
            } else if (got_count_ < chunks_) {
                request_chunks(now);
            }
            break;
        }
        case Msg::Start:
            if (phase_ == Phase::Transferring && loaded_) start_running(now);
            break;
        default:
            break;
        }
    }
    if (m.session != session_ || !session_) return;
    switch (m.type) {
    case Msg::Input:
        if (!host_ && phase_ == Phase::Transferring && loaded_) start_running(now);  // START was lost
        if (phase_ != Phase::Running || !rollback_) break;
        peer_started_ = true;
        rollback_->add_remote_inputs(m.first, m.inputs.data(), m.inputs.size());
        rollback_->set_remote_ack(m.ack);
        rollback_->set_remote_frame(m.frame);
        remote_advantage_ = m.advantage;
        for (const auto& h : m.hashes) rollback_->add_remote_hash(h.first, h.second);
        if (m.echo_ms) {
            // The time since our datagram left, less the time the other side
            // held its time stamp before answering.
            const double sample = double(since(now, m.echo_ms)) - double(m.echo_hold);
            if (sample >= 0 && sample < 10000) rtt_ms_ = rtt_ms_ <= 0 ? sample : rtt_ms_ * 0.875 + sample * 0.125;
        }
        if (!peer_send_ms_ || since(m.send_ms, peer_send_ms_) < 0x80000000u) {  // not an older one, reordered
            peer_send_ms_ = m.send_ms;
            peer_send_at_ = now;
        }
        break;
    case Msg::Bye:
        finish(End::OtherLeft, m.reason.empty() || m.reason == "you left" ? "the other player left" : "the other player left: " + m.reason, now);
        break;
    case Msg::Desync:
        desync_frame_ = m.frame;
        finish(End::Desync, "DESYNC: the other player's game differs from yours (frame " + std::to_string(m.frame) + ")", now);
        break;
    default:
        break;
    }
}

void Session::update(uint32_t now) {
    if (!transport_ || phase_ == Phase::Idle || phase_ == Phase::Ended) return;
    Endpoint from;
    std::vector<uint8_t> bytes;
    Message m;
    for (int i = 0; i < 4096 && transport_->receive(from, bytes); ++i) {
        if (!decode(bytes.data(), bytes.size(), m)) continue;
        handle(from, m, now);
        if (phase_ == Phase::Ended) return;
    }
    switch (phase_) {
    case Phase::Connecting:
        if (since(now, phase_since_) > opts_.connect_timeout_ms) {
            finish(End::Lost,
                   "no answer from " + peer() + ": is the host waiting, is the address right, and is its port forwarded?",
                   now);
            return;
        }
        if (since(now, last_hello_) >= kHelloEvery) {
            Message hello;
            hello.type = Msg::Hello;
            hello.identity = game_->identity();
            send(hello);
            last_hello_ = now;
        }
        break;
    case Phase::Transferring:
        if (since(now, last_recv_) > opts_.timeout_ms) {
            if (host_) {
                // The player who was joining went away: wait for another.
                notice_ = peer() + " stopped answering while joining";
                peer_ = Endpoint{};
                session_ = 0;
                start_.clear();
                enter(Phase::Waiting, now);
            } else {
                finish(End::Lost, "the host stopped answering while sending the game", now);
            }
            return;
        }
        if (!host_) {
            if (!loaded_) request_chunks(now);
            else if (since(now, last_ready_) >= kReadyEvery) {
                Message r;
                r.type = Msg::Ready;
                r.session = session_;
                send(r);
                last_ready_ = now;
            }
        }
        break;
    case Phase::Running:
        if (rollback_ && rollback_->desynced()) {
            desync_frame_ = rollback_->desync_frame();
            finish(End::Desync, "DESYNC at frame " + std::to_string(desync_frame_) + ": the two games are no longer the same", now);
            return;
        }
        if (since(now, last_recv_) > opts_.timeout_ms) {
            finish(End::Lost, "the connection to the other player was lost", now);
            return;
        }
        if (host_ && !peer_started_ && since(now, last_start_) >= kReadyEvery) {
            Message s;
            s.type = Msg::Start;
            s.session = session_;
            send(s);
            last_start_ = now;
        }
        if (since(now, last_send_) >= kKeepAliveEvery) send_input(now);
        break;
    default:
        break;
    }
}

Session::Tick Session::tick(Input local, uint32_t now) {
    if (phase_ != Phase::Running || !rollback_) return Tick::NotRunning;
    if (rollback_->desynced()) {
        update(now);
        return Tick::NotRunning;
    }
    if (opts_.time_sync && rollback_->remote_frame() != kNoFrame) {
        // GGPO's rule: each side knows how far ahead it is of the other and
        // the other says how far ahead it is; the one ahead by more waits
        // half the difference, a frame at a time.
        const int rtt_frames = int(rtt_ms_ / kFrameMs + 0.5);
        sum_local_adv_ += rollback_->frame_advantage(rtt_frames);
        sum_remote_adv_ += remote_advantage_;
        if (++sync_ticks_ >= kSyncWindow) {
            const double local_adv = double(sum_local_adv_) / sync_ticks_;
            const double remote_adv = double(sum_remote_adv_) / sync_ticks_;
            const int wait = int((local_adv - remote_adv) / 2.0);
            if (wait >= 1 && skip_ == 0) skip_ = std::min(wait, 3);
            sync_ticks_ = 0;
            sum_local_adv_ = sum_remote_adv_ = 0;
        }
    }
    if (skip_ > 0) {
        --skip_;
        ++waits_;
        send_input(now);
        return Tick::Waiting;
    }
    const bool ran = rollback_->tick(local);
    send_input(now);
    if (rollback_->desynced()) {
        desync_frame_ = rollback_->desync_frame();
        finish(End::Desync, "DESYNC at frame " + std::to_string(desync_frame_) + ": the two games are no longer the same", now);
        return Tick::NotRunning;
    }
    return ran ? Tick::Advanced : Tick::Stalled;
}

} // namespace chaotix::net
