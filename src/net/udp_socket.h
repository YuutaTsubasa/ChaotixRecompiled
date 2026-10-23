// A non-blocking UDP socket (issue #46): the netplay transport for real play.
// Plain BSD sockets, or Winsock on Windows; all of the platform code for the
// network is in udp_socket.cpp.
#pragma once
#include "net/transport.h"

#include <cstdint>
#include <string>

namespace chaotix::net {

constexpr uint16_t kDefaultPort = 7845;

class UdpSocket : public Transport {
public:
    UdpSocket() = default;
    ~UdpSocket() override;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Binds to `port` (0: any free one). `like` picks the address family: a
    // host listens on IPv6 and IPv4 both (where the system allows it), a
    // client uses the family of the address it joins.
    bool open(uint16_t port, std::string* error, const Endpoint* like = nullptr);
    void close();
    bool is_open() const;
    uint16_t local_port() const;

    void send(const Endpoint& to, const uint8_t* data, size_t n) override;
    bool receive(Endpoint& from, std::vector<uint8_t>& data) override;
    std::string describe(const Endpoint& e) const override;

    // "203.0.113.5:7845", "[2001:db8::1]:7845", "my-pc.tailnet.ts.net" (the
    // default port), "192.168.1.20". False, with the reason, if it does not
    // resolve.
    static bool resolve(const std::string& text, uint16_t default_port, Endpoint& out, std::string* error);

private:
    intptr_t fd_ = -1;
    int family_ = 0;
};

} // namespace chaotix::net
