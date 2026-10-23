// Netplay (issue #46) without the ROM or sockets: the rollback core on a toy
// game, the wire format, and whole sessions over the in-process network with
// latency, jitter and loss. The ROM tests in CMakeLists.txt (netplay_*) run
// the real game the same way, and over real UDP.
#include "net/loopback.h"
#include "net/netplay.h"
#include "net/protocol.h"
#include "net/rollback.h"
#include "net/udp_socket.h"
#include "test_framework.h"

#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

using namespace chaotix::net;

namespace {

// A game whose state depends on every pad of every frame, in order.
class Toy : public SessionGame {
public:
    uint64_t x = 1, y = 2;
    uint32_t frame = 0;
    uint64_t advances = 0, resims = 0;
    uint32_t poison_at = kNoFrame;  // flips a bit whenever this frame runs (re-runs too)
    std::string rom = "rom";

    void save(std::vector<uint8_t>& out) override {
        out.resize(20);
        std::memcpy(out.data(), &x, 8);
        std::memcpy(out.data() + 8, &y, 8);
        std::memcpy(out.data() + 16, &frame, 4);
    }
    void load(const std::vector<uint8_t>& in) override {
        std::memcpy(&x, in.data(), 8);
        std::memcpy(&y, in.data() + 8, 8);
        std::memcpy(&frame, in.data() + 16, 4);
    }
    void advance(const Input pads[2], bool resim) override {
        x = x * 6364136223846793005ull + pads[0] + 1;
        y = (y ^ pads[1]) * 1099511628211ull + (x >> 7);
        if (frame == poison_at) x ^= 1;
        ++frame;
        ++advances;
        if (resim) ++resims;
    }
    uint64_t fingerprint() {
        std::vector<uint8_t> b;
        save(b);
        return hash_bytes(b.data(), b.size());
    }
    Identity identity() const override {
        Identity id;
        id.game_version = "1.0";
        id.rom_sha1 = rom;
        id.data_sha1 = rom;
        id.must_match = "music=none\n";
        return id;
    }
    std::string settings = "s=1";
    std::string got_settings;
    std::string host_settings() const override { return settings; }
    std::vector<uint8_t> start_state() override {
        std::vector<uint8_t> b;
        save(b);
        b.resize(5000, 0xAB);  // several chunks
        return b;
    }
    bool use_host_settings(const std::string& s, std::string*) override {
        got_settings = s;
        return true;
    }
    bool load_start_state(const std::vector<uint8_t>& bytes, std::string* why) override {
        if (bytes.size() != 5000) {
            if (why) *why = "wrong size";
            return false;
        }
        load(bytes);
        return true;
    }
};

// The state the confirmed pads give, from the start state, at frame n.
uint64_t reference_hash(const Toy& start, const std::vector<std::array<Input, 2>>& log, uint32_t n) {
    Toy ref = start;
    ref.poison_at = kNoFrame;
    for (uint32_t f = 0; f < n; ++f) {
        const Input pads[2] = {log[f][0], log[f][1]};
        ref.advance(pads, false);
    }
    return ref.fingerprint();
}

uint32_t rnd(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s >> 8;
}

// Two cores joined by queues of "packets" (what Session sends), each one
// delivered `latency` ticks later unless lost.
struct Link {
    struct Packet {
        int due;
        uint32_t first, ack, frame;
        std::vector<Input> inputs;
        std::vector<std::pair<uint32_t, uint64_t>> hashes;
    };
    std::deque<Packet> q[2];  // to peer i
};

void send(Link& l, int to, const Rollback& from, int now, int latency) {
    Link::Packet p;
    p.due = now + latency;
    p.first = from.remote_ack();
    p.ack = from.remote_head();
    p.frame = from.frame();
    for (uint32_t f = p.first; f < from.local_head(); ++f) p.inputs.push_back(from.local_input(f));
    for (const auto& h : from.local_hashes()) p.hashes.push_back(h);
    l.q[to].push_back(std::move(p));
}

void deliver(Link& l, int to, Rollback& rb, int now) {
    while (!l.q[to].empty() && l.q[to].front().due <= now) {
        const Link::Packet& p = l.q[to].front();
        rb.add_remote_inputs(p.first, p.inputs.data(), p.inputs.size());
        rb.set_remote_ack(p.ack);
        rb.set_remote_frame(p.frame);
        for (const auto& h : p.hashes) rb.add_remote_hash(h.first, h.second);
        l.q[to].pop_front();
    }
}

struct Run {
    Toy start, a, b;
    std::unique_ptr<Rollback> ra, rb;
    Run(int delay_a, int delay_b, int window = 8, int hash_interval = 60) {
        a = start;
        b = start;
        RollbackConfig ca, cb;
        ca.local_player = 0;
        cb.local_player = 1;
        ca.input_delay = delay_a;
        cb.input_delay = delay_b;
        ca.max_rollback = cb.max_rollback = window;
        ca.hash_interval = cb.hash_interval = hash_interval;
        ca.record_confirmed = cb.record_confirmed = true;
        ra = std::make_unique<Rollback>(a, ca);
        rb = std::make_unique<Rollback>(b, cb);
    }
    // `ticks` ticks of both, with random pads (or `hold` for the second), the
    // packets `latency` ticks on the way, and some lost.
    void go(int ticks, int latency, int loss_pct, uint32_t seed, int hold = -1) {
        Link link;
        uint32_t s = seed;
        for (int t = 0; t < ticks; ++t) {
            deliver(link, 0, *ra, t);
            deliver(link, 1, *rb, t);
            const Input pa = Input(rnd(s) & 0xFF);
            const Input pb = hold >= 0 ? Input(hold) : Input(rnd(s) & 0xFF);
            ra->tick(pa);
            rb->tick(pb);
            if (int(rnd(s) % 100) >= loss_pct) send(link, 1, *ra, t, latency);
            if (int(rnd(s) % 100) >= loss_pct) send(link, 0, *rb, t, latency);
        }
        // Then let everything arrive.
        for (int t = ticks; t < ticks + 4 * latency + 40; ++t) {
            deliver(link, 0, *ra, t);
            deliver(link, 1, *rb, t);
            ra->tick(0);
            rb->tick(hold >= 0 ? Input(hold) : 0);
            send(link, 1, *ra, t, latency);
            send(link, 0, *rb, t, latency);
        }
    }
};

} // namespace

TEST(netplay, hash_tells_states_apart) {
    std::vector<uint8_t> a(1000, 7), b = a;
    CHECK_EQ(hash_bytes(a.data(), a.size()), hash_bytes(b.data(), b.size()));
    b[999] ^= 1;
    CHECK(hash_bytes(a.data(), a.size()) != hash_bytes(b.data(), b.size()));
    b = a;
    b[3] ^= 0x80;
    CHECK(hash_bytes(a.data(), a.size()) != hash_bytes(b.data(), b.size()));
    CHECK(hash_bytes(a.data(), 999) != hash_bytes(a.data(), 1000));
}

TEST(netplay, no_latency_never_rolls_back) {
    // A packet sent after a tick is there before the next one: with a frame
    // of input delay every pad is known before its frame runs.
    Run r(1, 1);
    r.go(600, 0, 0, 5);
    CHECK_EQ(r.ra->stats().rollbacks, 0u);
    CHECK_EQ(r.rb->stats().rollbacks, 0u);
    CHECK_EQ(r.ra->stats().stalls, 0u);
    // Delivered the same tick: every frame known before it runs, so nothing
    // even needs a snapshot but the hashed ones.
    CHECK(r.ra->stats().saves <= 700 / 60 + 2);
    const uint32_t n = 600;
    CHECK(r.ra->confirmed() >= n && r.rb->confirmed() >= n);
    CHECK_EQ(r.ra->hash_at(n), r.rb->hash_at(n));
    CHECK_EQ(r.ra->hash_at(n), reference_hash(r.start, r.ra->confirmed_inputs(), n));
    CHECK(!r.ra->desynced() && !r.rb->desynced());
}

TEST(netplay, mispredictions_roll_back_to_the_same_result) {
    for (int latency : {1, 3, 6}) {
        Run r(0, 0);
        r.go(1500, latency, 0, 11 + uint32_t(latency));
        CHECK(r.ra->stats().rollbacks > 0);
        CHECK(r.rb->stats().rollbacks > 0);
        CHECK(r.ra->stats().max_rollback <= 8);
        const uint32_t n = 1500;
        CHECK(r.ra->confirmed() >= n && r.rb->confirmed() >= n);
        CHECK_EQ(r.ra->hash_at(n), r.rb->hash_at(n));
        // Both peers recorded the same final pads.
        CHECK(r.ra->confirmed_inputs() == r.rb->confirmed_inputs());
        CHECK_EQ(r.ra->hash_at(n), reference_hash(r.start, r.ra->confirmed_inputs(), n));
        CHECK(r.ra->stats().hashes_checked > 20);
        CHECK(!r.ra->desynced() && !r.rb->desynced());
    }
}

TEST(netplay, packet_loss_is_covered_by_resending) {
    Run r(2, 1);
    r.go(2000, 4, 25, 99);
    const uint32_t n = 1980;
    CHECK(r.ra->confirmed() >= n && r.rb->confirmed() >= n);
    CHECK_EQ(r.ra->hash_at(n), r.rb->hash_at(n));
    CHECK_EQ(r.rb->hash_at(n), reference_hash(r.start, r.rb->confirmed_inputs(), n));
    CHECK(!r.ra->desynced());
}

TEST(netplay, a_held_pad_is_predicted_right) {
    // The second player holds one button throughout: repeating their last
    // pad is always right, so there is nothing to roll back on that side.
    Run r(0, 0);
    r.go(600, 5, 0, 3, 8);
    CHECK_EQ(r.ra->stats().rollbacks, 1u);  // only the first frames, predicted as no button
    CHECK(r.rb->stats().rollbacks > 0);     // the first player's pads change all the time
}

TEST(netplay, input_delay_shifts_the_local_pad) {
    for (int delay = 0; delay <= kMaxInputDelay; ++delay) {
        Toy g;
        RollbackConfig c;
        c.input_delay = delay;
        c.record_confirmed = true;
        Rollback rb(g, c);
        std::vector<Input> remote(64, 0);
        rb.add_remote_inputs(0, remote.data(), remote.size());
        for (int t = 0; t < 20; ++t) CHECK(rb.tick(Input(100 + t)));
        const auto& log = rb.confirmed_inputs();
        CHECK_EQ(log.size(), size_t(20));
        for (int f = 0; f < 20; ++f) {
            const Input want = f < delay ? 0 : Input(100 + f - delay);
            CHECK_EQ(log[size_t(f)][0], want);
        }
        CHECK_EQ(rb.local_head(), uint32_t(20 + delay));
    }
}

TEST(netplay, too_far_ahead_stalls_until_the_other_player_comes) {
    Toy g;
    RollbackConfig c;
    c.input_delay = 2;
    c.max_rollback = 8;
    Rollback rb(g, c);
    int ran = 0;
    for (int t = 0; t < 20; ++t) ran += rb.tick(1) ? 1 : 0;
    CHECK_EQ(ran, 8);
    CHECK_EQ(rb.frame(), 8u);
    CHECK_EQ(rb.stats().stalls, 12u);
    CHECK(rb.would_stall());
    // Their pads come, different from the prediction: one rollback of all
    // eight frames, and it may go on.
    std::vector<Input> remote(4, 64);
    rb.add_remote_inputs(0, remote.data(), remote.size());
    CHECK(!rb.would_stall());
    CHECK(rb.tick(1));
    CHECK_EQ(rb.stats().rollbacks, 1u);
    CHECK_EQ(rb.stats().last_rollback, 8);
    CHECK_EQ(rb.frame(), 9u);
    // 4 known + 8 predicted at most.
    for (int t = 0; t < 10; ++t) rb.tick(1);
    CHECK_EQ(rb.frame(), 12u);
}

TEST(netplay, a_divergence_is_caught_by_the_hashes) {
    Run r(1, 1, 8, 30);
    r.b.poison_at = 100;  // the second machine goes its own way at frame 100
    r.go(400, 3, 0, 21);
    CHECK(r.ra->desynced());
    CHECK(r.rb->desynced());
    CHECK_EQ(r.ra->desync_frame(), 120u);  // the first hash after it
    CHECK_EQ(r.rb->desync_frame(), 120u);
    CHECK(r.ra->hashed_snapshot(120) != nullptr);
    CHECK_EQ(r.ra->hash_at(90), r.rb->hash_at(90));  // the same before it
}

TEST(netplay, messages_survive_the_wire) {
    Message m;
    m.type = Msg::Input;
    m.session = 0xDEADBEEF;
    m.frame = 1234;
    m.ack = 1200;
    m.advantage = -3;
    m.send_ms = 99;
    m.echo_ms = 77;
    m.first = 1201;
    m.inputs = {1, 2, 0xFFFF};
    m.hashes = {{60, 0x0123456789ABCDEFull}, {120, 5}};
    const std::vector<uint8_t> b = encode(m);
    CHECK(b.size() <= kMaxDatagram);
    Message d;
    CHECK(decode(b.data(), b.size(), d));
    CHECK(d.type == Msg::Input);
    CHECK_EQ(d.session, 0xDEADBEEFu);
    CHECK_EQ(d.frame, 1234u);
    CHECK_EQ(d.ack, 1200u);
    CHECK_EQ(int(d.advantage), -3);
    CHECK_EQ(d.first, 1201u);
    CHECK(d.inputs == m.inputs);
    CHECK(d.hashes == m.hashes);
    // Cut short or not ours: refused.
    CHECK(!decode(b.data(), b.size() - 1, d));
    std::vector<uint8_t> bad = b;
    bad[0] = 'X';
    CHECK(!decode(bad.data(), bad.size(), d));

    Message w;
    w.type = Msg::Welcome;
    w.identity.game_version = "0.11.0";
    w.identity.rom_sha1 = "abc";
    w.identity.data_sha1 = "def";
    w.identity.must_match = "x=1\n";
    w.session = 7;
    w.settings = "wide=53";
    w.state_size = 300000;
    w.chunks = 293;
    const std::vector<uint8_t> wb = encode(w);
    CHECK(decode(wb.data(), wb.size(), d));
    CHECK(d.identity.game_version == "0.11.0" && d.identity.must_match == "x=1\n");
    CHECK(d.settings == "wide=53" && d.state_size == 300000 && d.chunks == 293);

    Message c;
    c.type = Msg::StateChunk;
    c.session = 7;
    c.index = 5;
    c.data.assign(kChunkSize, 0x5A);
    const std::vector<uint8_t> cb = encode(c);
    CHECK(cb.size() <= kMaxDatagram);
    CHECK(decode(cb.data(), cb.size(), d));
    CHECK(d.data == c.data && d.index == 5);
}

TEST(netplay, mismatches_are_explained) {
    Identity a, b;
    a.game_version = b.game_version = "0.11.0";
    a.rom_sha1 = b.rom_sha1 = "r";
    a.data_sha1 = b.data_sha1 = "r";
    a.must_match = b.must_match = "replaced music tracks=\n";
    CHECK(mismatch(a, b).empty());
    b.game_version = "0.10.0";
    CHECK(mismatch(a, b).find("version 0.10.0") != std::string::npos);
    b = a;
    b.rom_sha1 = "q";
    CHECK(mismatch(a, b).find("ROM") != std::string::npos);
    b = a;
    b.data_sha1 = "q";
    CHECK(mismatch(a, b).find("patches") != std::string::npos);
    b = a;
    b.must_match = "replaced music tracks=05\n";
    CHECK(mismatch(a, b).find("replaced music tracks differs (yours: none, theirs: 05)") != std::string::npos);
    b = a;
    b.protocol = 99;
    CHECK(mismatch(a, b).find("protocol 99") != std::string::npos);
}

TEST(netplay, addresses_resolve) {
    Endpoint e;
    std::string why;
    UdpSocket s;
    CHECK(UdpSocket::resolve("127.0.0.1:7000", kDefaultPort, e, &why));
    CHECK(s.describe(e) == "127.0.0.1:7000");
    CHECK(UdpSocket::resolve("127.0.0.1", kDefaultPort, e, &why));
    CHECK(s.describe(e) == "127.0.0.1:7845");
    CHECK(UdpSocket::resolve("[::1]:9000", kDefaultPort, e, &why));
    CHECK(s.describe(e) == "[::1]:9000");
    CHECK(!UdpSocket::resolve("127.0.0.1:99999", kDefaultPort, e, &why));
    CHECK(why.find("port") != std::string::npos);
    CHECK(!UdpSocket::resolve("", kDefaultPort, e, &why));
}

namespace {

// Two sessions over the in-process network, ticking at 60 Hz of simulated
// time (the client's ticks half a frame out of step with the host's).
struct Pair {
    LoopbackNetwork net;
    Toy hg, cg;
    Session host, client;
    double now = 0;
    Pair(const LoopbackNetwork::Conditions& c, uint32_t seed) : net(c, seed) {
        hg.x = 42;  // the host's game is where the client's must get to
        hg.y = 4242;
        hg.frame = 0;
    }
    uint32_t ms() const { return uint32_t(now); }
    void start(const SessionOptions& o) {
        host.host(net.make(0), hg, o, ms());
        client.join(net.make(1), LoopbackNetwork::endpoint(0), cg, o, ms());
    }
    // Runs until both have confirmed `frames` (or the time is up).
    void run(uint32_t frames, uint32_t seed, double limit_ms = 600000) {
        uint32_t s = seed;
        double next_h = now, next_c = now + 8.3;
        while (now < limit_ms) {
            net.set_now(now);
            host.update(ms());
            client.update(ms());
            if (now >= next_h) {
                host.tick(Input(rnd(s) & 0x7F), ms());
                next_h += 1000.0 / 60.0;
            }
            if (now >= next_c) {
                client.tick(Input(rnd(s) & 0x7F), ms());
                next_c += 1000.0 / 60.0;
            }
            const Rollback* a = host.rollback();
            const Rollback* b = client.rollback();
            if (a && b && a->confirmed() > frames && b->confirmed() > frames && a->hash_at(frames) &&
                b->hash_at(frames))
                break;
            if (host.phase() == Session::Phase::Ended || client.phase() == Session::Phase::Ended) break;
            now += 1.0;
        }
    }
};

} // namespace

TEST(netplay, sessions_agree_over_a_bad_network) {
    LoopbackNetwork::Conditions c;
    c.latency_ms = 70;
    c.jitter_ms = 30;
    c.loss = 0.10;
    Pair p(c, 17);
    Toy start = p.hg;
    SessionOptions o;
    o.record_confirmed = true;
    p.start(o);
    p.run(1800, 5);
    CHECK(p.host.phase() == Session::Phase::Running);
    CHECK(p.client.phase() == Session::Phase::Running);
    CHECK(p.cg.got_settings == "s=1");
    const Rollback* a = p.host.rollback();
    const Rollback* b = p.client.rollback();
    CHECK(a && b);
    if (!a || !b) return;
    CHECK_EQ(a->hash_at(1800), b->hash_at(1800));
    CHECK_EQ(a->hash_at(1800), reference_hash(start, a->confirmed_inputs(), 1800));
    CHECK(a->stats().rollbacks > 0);
    CHECK(a->stats().hashes_checked > 10 && b->stats().hashes_checked > 10);
    CHECK(p.host.ping_ms() > 100 && p.host.ping_ms() < 220);
    CHECK(p.net.lost() > 0);
    // Then the client leaves: the host hears of it.
    p.client.leave(p.ms());
    for (int i = 0; i < 400 && p.host.phase() != Session::Phase::Ended; ++i) {
        p.now += 1;
        p.net.set_now(p.now);
        p.host.update(p.ms());
    }
    CHECK(p.host.phase() == Session::Phase::Ended);
    CHECK(p.host.end() == Session::End::OtherLeft);
}

TEST(netplay, a_different_rom_is_refused_with_the_reason) {
    LoopbackNetwork::Conditions c;
    c.latency_ms = 10;
    Pair p(c, 3);
    p.cg.rom = "another";
    p.start(SessionOptions{});
    p.run(10, 1, 2000);
    CHECK(p.client.phase() == Session::Phase::Ended);
    CHECK(p.client.end() == Session::End::Rejected);
    CHECK(p.client.end_reason().find("ROM") != std::string::npos);
    // The host goes on waiting, and says who it turned away.
    CHECK(p.host.phase() == Session::Phase::Waiting);
    CHECK(p.host.notice().find("ROM") != std::string::npos);
}

TEST(netplay, a_silent_peer_times_out) {
    LoopbackNetwork::Conditions c;
    c.latency_ms = 20;
    Pair p(c, 9);
    SessionOptions o;
    o.timeout_ms = 1000;
    p.start(o);
    p.run(120, 2, 10000);
    CHECK(p.client.phase() == Session::Phase::Running);
    // The host vanishes (no more updates or ticks).
    const double until = p.now + 3000;
    while (p.now < until && p.client.phase() == Session::Phase::Running) {
        p.now += 1;
        p.net.set_now(p.now);
        p.client.update(p.ms());
        if (int(p.now) % 16 == 0) p.client.tick(0, p.ms());
    }
    CHECK(p.client.phase() == Session::Phase::Ended);
    CHECK(p.client.end() == Session::End::Lost);
}

TEST(netplay, nobody_at_the_address) {
    LoopbackNetwork::Conditions c;
    LoopbackNetwork net(c, 1);
    Toy g;
    Session s;
    SessionOptions o;
    o.connect_timeout_ms = 2000;
    auto t = net.make(1);
    s.join(std::move(t), LoopbackNetwork::endpoint(0), g, o, 0);
    for (uint32_t ms = 0; ms < 3000 && s.phase() != Session::Phase::Ended; ms += 10) {
        net.set_now(ms);
        s.update(ms);
    }
    CHECK(s.phase() == Session::Phase::Ended);
    CHECK(s.end() == Session::End::Lost);
    CHECK(s.end_reason().find("no answer") != std::string::npos);
}
