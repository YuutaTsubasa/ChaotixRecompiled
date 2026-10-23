// chaotix_headless's netplay modes (issue #46, docs/NETPLAY.md): the game
// played by two peers through the real netplay session, with scripted
// (fuzzed) pads, and checked.
//
//   --netplay-loopback LATENCY:JITTER:LOSS   both peers in this process, over
//       the simulated network (milliseconds one way, +- milliseconds, percent
//       lost); simulated time, so it runs as fast as the machines go.
//   --netplay-host PORT / --netplay-join ADDRESS   one peer, over real UDP, in
//       real time; run one of each.
//
// Either way the run ends once frame --frames (rounded up to the hash
// interval) is confirmed on both sides, and passes only if both peers' state
// hashes at that frame are equal and equal to a single machine's, started
// from the same state and fed the confirmed pads of both players.
#include "frontend/netplay_headless.h"

#include "audio/volumes.h"
#include "game/recomp_dispatch.h"
#include "input/input.h"
#include "net/loopback.h"
#include "net/machine_game.h"
#include "net/netplay.h"
#include "net/udp_socket.h"
#include "runtime/save_state.h"
#include "runtime/stage_select.h"
#include "runtime/system.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace chaotix {

namespace {

using net::Input;

struct Options {
    std::string rom;
    uint32_t frames = 1200;      // session frames to confirm
    uint64_t start = 0;          // frames the host runs alone first
    bool stage_set = false;
    uint64_t stage_frame = 0;
    stage_select::Request stage;
    bool loopback = false;
    double latency = 0, jitter = 0, loss = 0;
    int host_port = -1;
    std::string join;
    int delay = net::kDefaultInputDelay, delay2 = -1;
    int window = net::kDefaultMaxRollback;
    int hash_every = net::kDefaultHashInterval;
    uint32_t fuzz = 1;
    uint32_t net_seed = 1;
    int64_t inject_desync = -1;  // the client's machine is changed at this session frame
    int client_volume = -1;      // the client's music level, percent (-1: as the host's)
    std::string dump_dir = ".";
    int wide = 0;
    double timeout_s = 120;
    bool interp = false;
};

std::vector<std::string> split(const std::string& s, char c) {
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) {
        const size_t n = s.find(c, at);
        out.push_back(s.substr(at, n == std::string::npos ? std::string::npos : n - at));
        if (n == std::string::npos) break;
        at = n + 1;
    }
    return out;
}

// Pseudo-random play for one player (as chaotix_headless --fuzz: held
// directions, tapped buttons, never Start or Mode), one pad per call.
class FuzzPad {
public:
    explicit FuzzPad(uint32_t seed) : s_(seed * 2654435761u + 12345u) {}
    Input next() {
        static const uint16_t dirs[] = {PAD_RIGHT, PAD_RIGHT, PAD_RIGHT, PAD_LEFT, PAD_DOWN, PAD_UP, 0, PAD_RIGHT | PAD_DOWN};
        static const uint16_t acts[] = {PAD_A, PAD_B, PAD_C, PAD_C, 0, 0, PAD_C, PAD_B, 0};
        if (dir_left_ <= 0) {
            dir_ = dirs[rnd() % 8];
            dir_left_ = int(20 + rnd() % 90);
        }
        if (act_left_ <= 0 && gap_left_ <= 0) {
            act_ = acts[rnd() % 9];
            act_left_ = int(1 + rnd() % 25);
            gap_left_ = int(10 + rnd() % 30);
        }
        Input pad = Input(dir_ | (act_left_ > 0 ? act_ : 0));
        --dir_left_;
        if (act_left_ > 0) --act_left_;
        else --gap_left_;
        return pad;
    }

private:
    uint32_t rnd() {
        s_ = s_ * 1664525u + 1013904223u;
        return s_ >> 8;
    }
    uint32_t s_;
    uint16_t dir_ = 0, act_ = 0;
    int dir_left_ = 0, act_left_ = 0, gap_left_ = 0;
};

// A peer's game, keeping the start state it sent or got (for the reference).
class Game : public net::MachineGame {
public:
    using MachineGame::MachineGame;
    std::vector<uint8_t> start;
    std::vector<uint8_t> start_state() override {
        start = MachineGame::start_state();
        return start;
    }
    bool load_start_state(const std::vector<uint8_t>& bytes, std::string* why) override {
        start = bytes;
        return MachineGame::load_start_state(bytes, why);
    }
};

struct Peer {
    const char* name = "";
    std::unique_ptr<Machine> m;
    std::unique_ptr<Game> game;
    net::Session session;
    FuzzPad pad{1};
    uint64_t start_frame_count = 0;
    double worst_tick_ms = 0, total_tick_ms = 0;
    uint64_t ticks = 0;
    double worst_rollback_ms = 0;
    int worst_rollback_frames = 0;
};

bool make_machine(const Options& o, std::unique_ptr<Machine>& m, bool host) {
    m = std::make_unique<Machine>();
    std::string err;
    if (!m->load_rom(o.rom, &err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return false;
    }
    m->reset();
    install_recompiled_code(*m, !o.interp);
    if (host) {
        m->wide_enabled = o.wide > 0;
        m->wide_extra = o.wide;
    }
    return true;
}

// The host's game before the session: to the title screen, or into a stage
// the way TIME ATTACK asks for one (with the same taps of Start).
void run_alone(const Options& o, Machine& m) {
    int kick = -1;
    for (uint64_t f = 0; f < o.start; ++f) {
        uint16_t btn = 0;
        if (o.stage_set && f == o.stage_frame) {
            m.stage_request = o.stage;
            m.stage_pending = true;
            kick = 0;
        }
        if (kick >= 0) {
            if (!m.stage_pending) kick = -1;
            else {
                if (kick % 24 < 6) btn |= PAD_START;
                ++kick;
            }
        }
        m.input.pad[0] = btn;
        m.input.pad[1] = 0;
        m.run_frame();
        m.audio_out.clear();
    }
}

void prepare(const Options& o, Peer& p, bool host) {
    p.game = std::make_unique<Game>(*p.m);
    Machine* mp = p.m.get();
    Peer* pp = &p;
    p.game->after_frame = [mp](bool) {
        mp->audio_out.clear();
        mp->music_events.clear();
    };
    if (!host && o.client_volume >= 0) {
        // The second player listens at other music and effects levels: the
        // games must not differ for it (the levels shape only the sound).
        const float v = float(o.client_volume) / 100.0f;
        p.game->before_frame = [mp, v](bool) { audio::set_music_and_effect_levels(*mp, v, v * 0.5f); };
    }
    if (!host && o.inject_desync >= 0) {
        // The client's machine goes its own way at that frame, on every run
        // of it (a rollback re-runs it too): a byte of work RAM changed.
        p.game->before_frame = [mp, pp, &o](bool) {
            if (mp->frame_count == pp->start_frame_count + uint64_t(o.inject_desync)) mp->wram[0x1234] ^= 0x5A;
        };
    }
}

net::SessionOptions session_options(const Options& o, bool host) {
    net::SessionOptions so;
    so.input_delay = host || o.delay2 < 0 ? o.delay : o.delay2;
    so.max_rollback = o.window;
    so.hash_interval = o.hash_every;
    so.record_confirmed = true;
    return so;
}

// One tick of a peer, timed.
void tick(Peer& p, uint32_t now) {
    if (p.session.phase() != net::Session::Phase::Running) return;
    if (p.ticks == 0) p.start_frame_count = p.m->frame_count;
    const uint64_t rb_before = p.session.rollback()->stats().resimulated;
    const auto t0 = std::chrono::steady_clock::now();
    p.session.tick(p.pad.next(), now);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    p.worst_tick_ms = std::max(p.worst_tick_ms, ms);
    p.total_tick_ms += ms;
    ++p.ticks;
    const int rolled = int(p.session.rollback()->stats().resimulated - rb_before);
    if (rolled > 0 && ms > p.worst_rollback_ms) {
        p.worst_rollback_ms = ms;
        p.worst_rollback_frames = rolled;
    }
}

void report(const Peer& p) {
    const net::Rollback* rb = p.session.rollback();
    if (!rb) return;
    const net::RollbackStats& s = rb->stats();
    std::printf("netplay: %s: %llu frames, %llu rollbacks (%llu frames re-run, longest %d), %llu stalls, %llu waits, "
                "%llu hashes checked, ping %d ms, tick %.2f ms mean, %.2f worst (a rollback of %d frames: %.2f ms)\n",
                p.name, (unsigned long long)s.frames, (unsigned long long)s.rollbacks,
                (unsigned long long)s.resimulated, s.max_rollback, (unsigned long long)s.stalls,
                (unsigned long long)p.session.waits(), (unsigned long long)s.hashes_checked, p.session.ping_ms(),
                p.ticks ? p.total_tick_ms / double(p.ticks) : 0.0, p.worst_tick_ms, p.worst_rollback_frames,
                p.worst_rollback_ms);
}

// The single-machine run: the start state, then the confirmed pads. The
// snapshot hash before frame n.
bool reference(const Options& o, const Game& from, const std::vector<std::array<Input, 2>>& log, uint32_t n,
               uint64_t& out) {
    if (log.size() < n) return false;
    std::unique_ptr<Machine> m;
    if (!make_machine(o, m, false)) return false;
    bind_recompiled_code(*m);
    Game g(*m);
    g.set_settings(from.settings());
    std::string why;
    if (!g.load_start_state(from.start, &why)) {
        std::fprintf(stderr, "reference: %s\n", why.c_str());
        return false;
    }
    g.after_frame = [&m](bool) {
        m->audio_out.clear();
        m->music_events.clear();
    };
    for (uint32_t f = 0; f < n; ++f) {
        const Input pads[2] = {log[f][0], log[f][1]};
        g.advance(pads, false);
    }
    std::vector<uint8_t> snap;
    g.save(snap);
    out = g.hash(snap);
    return true;
}

void dump_desync(const Options& o, Peer& p) {
    const uint32_t f = p.session.desync_frame();
    const net::Rollback* rb = p.session.rollback();
    const std::vector<uint8_t>* snap = rb ? rb->hashed_snapshot(f) : nullptr;
    std::string path;
    if (snap) {
        path = o.dump_dir + "/netplay_desync_" + p.name + "_frame" + std::to_string(f) + ".state";
        if (!p.game->write_snapshot_as_state(*snap, path)) path = "(could not write it)";
    }
    std::printf("netplay: %s: %s%s%s\n", p.name, p.session.end_reason().c_str(), path.empty() ? "" : "; state at that frame: ",
                path.c_str());
}

uint32_t target_frame(const Options& o) {
    const uint32_t h = uint32_t(std::max(o.hash_every, 1));
    return (o.frames + h - 1) / h * h;
}

int run_loopback(const Options& o) {
    Peer host, client;
    host.name = "player1";
    client.name = "player2";
    host.pad = FuzzPad(o.fuzz);
    client.pad = FuzzPad(o.fuzz * 7919u + 1u);
    if (!make_machine(o, host.m, true) || !make_machine(o, client.m, false)) return 1;
    bind_recompiled_code(*host.m);
    run_alone(o, *host.m);
    prepare(o, host, true);
    prepare(o, client, false);
    net::LoopbackNetwork::Conditions c;
    c.latency_ms = o.latency;
    c.jitter_ms = o.jitter;
    c.loss = o.loss / 100.0;
    net::LoopbackNetwork network(c, o.net_seed);
    double now = 0;
    host.session.host(network.make(0), *host.game, session_options(o, true), 0);
    client.session.join(network.make(1), net::LoopbackNetwork::endpoint(0), *client.game, session_options(o, false), 0);
    const uint32_t n = target_frame(o);
    double next_h = 0, next_c = 1000.0 / 120.0;  // half a frame out of step
    Machine* bound = host.m.get();
    const double limit = o.timeout_s * 1000.0 * 20;  // simulated time
    const auto wall0 = std::chrono::steady_clock::now();
    auto done = [&](const Peer& p) {
        const net::Rollback* rb = p.session.rollback();
        return rb && rb->confirmed() > n && rb->hash_at(n) != 0;
    };
    while (now < limit) {
        network.set_now(now);
        const uint32_t ms = uint32_t(now);
        host.session.update(ms);
        client.session.update(ms);
        if (now >= next_h) {
            if (bound != host.m.get()) bound = host.m.get(), bind_recompiled_code(*bound);
            tick(host, ms);
            next_h += 1000.0 / 60.0;
        }
        if (now >= next_c) {
            if (bound != client.m.get()) bound = client.m.get(), bind_recompiled_code(*bound);
            tick(client, ms);
            next_c += 1000.0 / 60.0;
        }
        if (host.session.phase() == net::Session::Phase::Ended || client.session.phase() == net::Session::Phase::Ended)
            break;
        if (done(host) && done(client)) break;
        now += 1.0;
    }
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
    std::printf("netplay: loopback %.0f ms +- %.0f ms, %.0f%% lost (%llu of %llu datagrams), input delay %d/%d, "
                "%.1f s simulated in %.1f s, start state %zu bytes\n",
                o.latency, o.jitter, o.loss, (unsigned long long)network.lost(), (unsigned long long)network.sent(),
                session_options(o, true).input_delay, session_options(o, false).input_delay, now / 1000.0, wall,
                host.session.start_state_bytes());
    report(host);
    report(client);
    const bool desync = host.session.end() == net::Session::End::Desync || client.session.end() == net::Session::End::Desync;
    if (desync) {
        // Each side noticed (or was told); both dump what they have.
        network.set_now(now + 1000);
        host.session.update(uint32_t(now + 1000));
        client.session.update(uint32_t(now + 1000));
        dump_desync(o, host);
        dump_desync(o, client);
        if (o.inject_desync >= 0) {
            const bool both = host.session.end() == net::Session::End::Desync &&
                              client.session.end() == net::Session::End::Desync;
            std::printf("netplay: injected divergence at frame %lld detected by %s\n", (long long)o.inject_desync,
                        both ? "both players" : "one player only");
            return both ? 0 : 1;
        }
        return 1;
    }
    if (o.inject_desync >= 0) {
        std::printf("netplay: injected divergence at frame %lld NOT detected\n", (long long)o.inject_desync);
        return 1;
    }
    if (!done(host) || !done(client)) {
        std::printf("netplay: did not reach frame %u (%s / %s: %s%s)\n", n, net::phase_name(host.session.phase()),
                    net::phase_name(client.session.phase()), host.session.end_reason().c_str(),
                    client.session.end_reason().c_str());
        return 1;
    }
    const uint64_t a = host.session.rollback()->hash_at(n), b = client.session.rollback()->hash_at(n);
    const bool same_log = host.session.rollback()->confirmed_inputs().size() >= n &&
                          client.session.rollback()->confirmed_inputs().size() >= n &&
                          std::equal(host.session.rollback()->confirmed_inputs().begin(),
                                     host.session.rollback()->confirmed_inputs().begin() + n,
                                     client.session.rollback()->confirmed_inputs().begin());
    uint64_t r = 0;
    const bool have_ref = reference(o, *host.game, host.session.rollback()->confirmed_inputs(), n, r);
    const bool ok = a == b && have_ref && a == r && same_log;
    std::printf("netplay: frame %u: player 1 %016llx, player 2 %016llx, reference %016llx: %s%s\n", n,
                (unsigned long long)a, (unsigned long long)b, (unsigned long long)r, ok ? "identical" : "DIFFERENT",
                same_log ? "" : " (the confirmed pads differ)");
    return ok ? 0 : 1;
}

int run_udp(const Options& o) {
    const bool host_side = o.host_port >= 0;
    Peer p;
    p.name = host_side ? "player1" : "player2";
    p.pad = FuzzPad(host_side ? o.fuzz : o.fuzz * 7919u + 1u);
    if (!make_machine(o, p.m, host_side)) return 1;
    if (host_side) run_alone(o, *p.m);
    prepare(o, p, host_side);
    auto socket = std::make_unique<net::UdpSocket>();
    std::string why;
    net::Endpoint host_at;
    if (!host_side && !net::UdpSocket::resolve(o.join, net::kDefaultPort, host_at, &why)) {
        std::fprintf(stderr, "netplay: %s\n", why.c_str());
        return 1;
    }
    if (!socket->open(host_side ? uint16_t(o.host_port) : 0, &why, host_side ? nullptr : &host_at)) {
        std::fprintf(stderr, "netplay: %s\n", why.c_str());
        return 1;
    }
    const std::string doing = host_side ? std::string("hosting") : "joining " + o.join + " from";
    std::printf("netplay: %s on UDP port %u\n", doing.c_str(), unsigned(socket->local_port()));
    std::fflush(stdout);
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    auto now_ms = [&] { return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count()) + 1; };
    if (host_side) p.session.host(std::move(socket), *p.game, session_options(o, true), now_ms());
    else p.session.join(std::move(socket), host_at, *p.game, session_options(o, false), now_ms());
    const uint32_t n = target_frame(o);
    auto next = clock::now();
    const auto period = std::chrono::microseconds(16667);
    int linger = -1;  // ticks left after this side has all it needs
    while (std::chrono::duration<double>(clock::now() - t0).count() < o.timeout_s) {
        p.session.update(now_ms());
        if (p.session.phase() == net::Session::Phase::Ended) break;
        if (clock::now() >= next) {
            tick(p, now_ms());
            next += period;
            if (clock::now() > next + period * 4) next = clock::now();  // fell far behind: do not race
            const net::Rollback* rb = p.session.rollback();
            if (linger < 0 && rb && rb->confirmed() > n && rb->hash_at(n) && rb->remote_hash_at(n) &&
                rb->remote_ack() > n)
                linger = 60;  // a second more, so the other side gets all it needs too
            if (linger > 0 && --linger == 0) {
                p.session.leave(now_ms());
                break;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    report(p);
    if (p.session.end() == net::Session::End::Desync) {
        dump_desync(o, p);
        return 1;
    }
    const net::Rollback* rb = p.session.rollback();
    if (!rb || !rb->hash_at(n)) {
        std::printf("netplay: did not reach frame %u (%s: %s)\n", n, net::phase_name(p.session.phase()),
                    p.session.end_reason().c_str());
        return 1;
    }
    uint64_t r = 0;
    const bool have_ref = reference(o, *p.game, rb->confirmed_inputs(), n, r);
    const uint64_t mine = rb->hash_at(n), theirs = rb->remote_hash_at(n);
    const bool ok = have_ref && mine == r && mine == theirs;
    std::printf("netplay: frame %u: %s %016llx, the other player %016llx, reference %016llx: %s\n", n, p.name,
                (unsigned long long)mine, (unsigned long long)theirs, (unsigned long long)r, ok ? "identical" : "DIFFERENT");
    return ok ? 0 : 1;
}

} // namespace

bool is_netplay_headless(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (!std::strncmp(argv[i], "--netplay", 9)) return true;
    return false;
}

int netplay_headless_main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--rom") o.rom = next();
        else if (a == "--frames") o.frames = uint32_t(std::strtoul(next().c_str(), nullptr, 10));
        else if (a == "--netplay-start") o.start = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--stage") {
            const auto parts = split(next(), ':');
            if (parts.size() < 7) { std::fprintf(stderr, "bad --stage\n"); return 2; }
            o.stage_frame = std::strtoull(parts[0].c_str(), nullptr, 10);
            o.stage.place = uint16_t(std::atoi(parts[1].c_str()));
            o.stage.level = uint16_t(std::atoi(parts[2].c_str()));
            o.stage.attime = uint16_t(std::atoi(parts[3].c_str()));
            o.stage.player = uint16_t(std::atoi(parts[4].c_str()));
            o.stage.combi = uint16_t(std::atoi(parts[5].c_str()));
            o.stage.two_players = std::atoi(parts[6].c_str()) != 0;
            o.stage_set = true;
        } else if (a == "--netplay-loopback") {
            const auto parts = split(next(), ':');
            o.loopback = true;
            o.latency = parts.size() > 0 ? std::atof(parts[0].c_str()) : 0;
            o.jitter = parts.size() > 1 ? std::atof(parts[1].c_str()) : 0;
            o.loss = parts.size() > 2 ? std::atof(parts[2].c_str()) : 0;
        } else if (a == "--netplay-host") o.host_port = std::atoi(next().c_str());
        else if (a == "--netplay-join") o.join = next();
        else if (a == "--netplay-delay") o.delay = std::atoi(next().c_str());
        else if (a == "--netplay-delay2") o.delay2 = std::atoi(next().c_str());
        else if (a == "--netplay-window") o.window = std::atoi(next().c_str());
        else if (a == "--netplay-hash-every") o.hash_every = std::atoi(next().c_str());
        else if (a == "--netplay-fuzz") o.fuzz = uint32_t(std::strtoul(next().c_str(), nullptr, 10));
        else if (a == "--netplay-seed") o.net_seed = uint32_t(std::strtoul(next().c_str(), nullptr, 10));
        else if (a == "--netplay-inject-desync") o.inject_desync = std::atoll(next().c_str());
        else if (a == "--netplay-dump") o.dump_dir = next();
        else if (a == "--netplay-client-volume") o.client_volume = std::atoi(next().c_str());
        else if (a == "--netplay-timeout") o.timeout_s = std::atof(next().c_str());
        else if (a == "--wide") o.wide = std::atoi(next().c_str());
        else if (a == "--interp") o.interp = true;
        else {
            std::fprintf(stderr, "unknown argument for a netplay run: %s\n", a.c_str());
            return 2;
        }
    }
    if (o.rom.empty()) { std::fprintf(stderr, "netplay: --rom is needed\n"); return 2; }
    if (o.stage_set && o.start <= o.stage_frame) o.start = o.stage_frame + 900;
    if (o.loopback) return run_loopback(o);
    if (o.host_port >= 0 || !o.join.empty()) return run_udp(o);
    std::fprintf(stderr, "netplay: --netplay-loopback, --netplay-host or --netplay-join\n");
    return 2;
}

} // namespace chaotix
