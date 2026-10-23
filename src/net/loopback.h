// An in-process network for netplay tests (issue #46): two endpoints, and
// datagrams between them delayed by a latency plus random jitter, some lost.
// Time is whatever the owner says it is (set_now), so a test runs as fast as
// the machines do, and the same seed gives the same network every time.
#pragma once
#include "net/transport.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace chaotix::net {

class LoopbackNetwork {
public:
    struct Conditions {
        double latency_ms = 0;  // one way
        double jitter_ms = 0;   // each datagram's latency is latency +- up to this
        double loss = 0;        // 0..1: the share of datagrams lost
        bool reorder = true;    // jitter may reorder datagrams (as on the Internet)
    };
    explicit LoopbackNetwork(const Conditions& c, uint32_t seed = 1);

    void set_now(double ms) { now_ms_ = ms; }
    double now() const { return now_ms_; }
    // The transports of endpoint 0 and 1 (each made once; owned by the caller).
    std::unique_ptr<Transport> make(int side);
    static Endpoint endpoint(int side);

    uint64_t sent() const { return sent_; }
    uint64_t lost() const { return lost_; }

private:
    friend class LoopbackTransport;
    struct Packet {
        double due = 0;
        uint64_t seq = 0;
        int from = 0;
        std::vector<uint8_t> data;
    };
    void send(int from, int to, const uint8_t* data, size_t n);
    bool receive(int side, Endpoint& from, std::vector<uint8_t>& data);
    double random01();

    Conditions c_;
    uint64_t rng_;
    double now_ms_ = 0;
    std::vector<Packet> queue_[2];  // by destination
    double last_due_[2] = {0, 0};
    uint64_t seq_ = 0, sent_ = 0, lost_ = 0;
};

} // namespace chaotix::net
