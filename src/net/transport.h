// Netplay transports (issue #46): how datagrams get to the other player.
//
// The session (net/netplay.h) only sends and receives datagrams to and from
// endpoints; what carries them is behind this interface: UDP sockets
// (net/udp_socket.h) for real play, an in-process network with simulated
// latency, jitter and loss (net/loopback.h) for tests. A relay or NAT
// hole-punching transport would be another implementation.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chaotix::net {

// An opaque address: a sockaddr for UDP, an id for the loopback network.
struct Endpoint {
    std::array<uint8_t, 28> bytes{};
    uint8_t size = 0;
    bool valid() const { return size != 0; }
    bool operator==(const Endpoint& o) const { return size == o.size && bytes == o.bytes; }
    bool operator!=(const Endpoint& o) const { return !(*this == o); }
};

class Transport {
public:
    virtual ~Transport() = default;
    // Fire and forget: a datagram may be lost, duplicated or reordered.
    virtual void send(const Endpoint& to, const uint8_t* data, size_t n) = 0;
    // The next datagram waiting, without blocking; false if none.
    virtual bool receive(Endpoint& from, std::vector<uint8_t>& data) = 0;
    // For messages: "192.168.1.20:7845".
    virtual std::string describe(const Endpoint& e) const = 0;
};

// Datagrams are kept under the usual Internet MTU (no fragmentation).
constexpr size_t kMaxDatagram = 1200;

} // namespace chaotix::net
