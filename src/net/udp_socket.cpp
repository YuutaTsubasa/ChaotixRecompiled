#include "net/udp_socket.h"

#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mutex>
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace chaotix::net {

namespace {

#ifdef _WIN32
bool startup() {
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        WSADATA data;
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    });
    return ok;
}
void close_fd(intptr_t fd) { closesocket(SOCKET(fd)); }
std::string last_error() { return "error " + std::to_string(WSAGetLastError()); }
#else
bool startup() { return true; }
void close_fd(intptr_t fd) { ::close(int(fd)); }
std::string last_error() { return std::strerror(errno); }
#endif

void to_endpoint(const sockaddr* sa, size_t len, Endpoint& out) {
    out = Endpoint{};
    len = len < out.bytes.size() ? len : out.bytes.size();
    std::memcpy(out.bytes.data(), sa, len);
    out.size = uint8_t(len);
}

int family_of(const Endpoint& e) {
    sockaddr_storage ss{};
    std::memcpy(&ss, e.bytes.data(), e.size);
    return ss.ss_family;
}

} // namespace

UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::is_open() const { return fd_ != -1; }

void UdpSocket::close() {
    if (fd_ != -1) close_fd(fd_);
    fd_ = -1;
}

bool UdpSocket::open(uint16_t port, std::string* error, const Endpoint* like) {
    close();
    if (!startup()) {
        if (error) *error = "the network is not available";
        return false;
    }
    const int want = like ? family_of(*like) : AF_INET6;
    for (int family : {want, AF_INET}) {
        if (family != AF_INET && family != AF_INET6) continue;
#ifdef _WIN32
        SOCKET s = socket(family, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) continue;
        const intptr_t fd = intptr_t(s);
#else
        const int s = socket(family, SOCK_DGRAM, IPPROTO_UDP);
        if (s < 0) continue;
        const intptr_t fd = s;
#endif
        if (family == AF_INET6) {
            int off = 0;  // IPv4 too, as mapped addresses
            setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&off), sizeof off);
        }
        sockaddr_storage ss{};
        socklen_t len = 0;
        if (family == AF_INET6) {
            auto* a = reinterpret_cast<sockaddr_in6*>(&ss);
            a->sin6_family = AF_INET6;
            a->sin6_port = htons(port);
            a->sin6_addr = in6addr_any;
            len = sizeof(sockaddr_in6);
        } else {
            auto* a = reinterpret_cast<sockaddr_in*>(&ss);
            a->sin_family = AF_INET;
            a->sin_port = htons(port);
            a->sin_addr.s_addr = htonl(INADDR_ANY);
            len = sizeof(sockaddr_in);
        }
        if (bind(s, reinterpret_cast<sockaddr*>(&ss), len) != 0) {
            if (error) *error = "cannot use port " + std::to_string(port) + " (" + last_error() + ")";
            close_fd(fd);
            if (like) return false;
            continue;
        }
#ifdef _WIN32
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        // Windows reports an ICMP "port unreachable" from an earlier send as
        // an error on the next receive; a datagram socket has no use for it.
        DWORD flag = FALSE, got = 0;
        const DWORD kUdpConnReset = 0x9800000C;  // SIO_UDP_CONNRESET
        WSAIoctl(s, kUdpConnReset, &flag, sizeof flag, nullptr, 0, &got, nullptr, nullptr);
#else
        fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
        int buf = 1 << 20;
        setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buf), sizeof buf);
        setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buf), sizeof buf);
        fd_ = fd;
        family_ = family;
        return true;
    }
    if (error && error->empty()) *error = "cannot open a UDP socket (" + last_error() + ")";
    return false;
}

uint16_t UdpSocket::local_port() const {
    if (fd_ == -1) return 0;
    sockaddr_storage ss{};
    socklen_t len = sizeof ss;
    if (getsockname(decltype(socket(0, 0, 0))(fd_), reinterpret_cast<sockaddr*>(&ss), &len) != 0) return 0;
    if (ss.ss_family == AF_INET6) return ntohs(reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port);
    return ntohs(reinterpret_cast<sockaddr_in*>(&ss)->sin_port);
}

void UdpSocket::send(const Endpoint& to, const uint8_t* data, size_t n) {
    if (fd_ == -1 || !to.valid()) return;
    sockaddr_storage ss{};
    std::memcpy(&ss, to.bytes.data(), to.size);
    socklen_t len = to.size;
    if (family_ == AF_INET6 && ss.ss_family == AF_INET) {
        // An IPv4 address through the dual-stack socket: as a mapped one.
        const sockaddr_in v4 = *reinterpret_cast<sockaddr_in*>(&ss);
        sockaddr_in6 v6{};
        v6.sin6_family = AF_INET6;
        v6.sin6_port = v4.sin_port;
        uint8_t* b = reinterpret_cast<uint8_t*>(&v6.sin6_addr);
        b[10] = b[11] = 0xFF;
        std::memcpy(b + 12, &v4.sin_addr, 4);
        std::memset(&ss, 0, sizeof ss);
        std::memcpy(&ss, &v6, sizeof v6);
        len = sizeof v6;
    }
    sendto(decltype(socket(0, 0, 0))(fd_), reinterpret_cast<const char*>(data), int(n), 0,
           reinterpret_cast<sockaddr*>(&ss), len);
}

bool UdpSocket::receive(Endpoint& from, std::vector<uint8_t>& data) {
    if (fd_ == -1) return false;
    uint8_t buf[2048];
    for (;;) {
        sockaddr_storage ss{};
        socklen_t len = sizeof ss;
        const auto n = recvfrom(decltype(socket(0, 0, 0))(fd_), reinterpret_cast<char*>(buf), int(sizeof buf), 0,
                                reinterpret_cast<sockaddr*>(&ss), &len);
        if (n < 0) {
#ifdef _WIN32
            const int e = WSAGetLastError();
            if (e == WSAECONNRESET || e == WSAEMSGSIZE) continue;  // a stale ICMP report, an oversized datagram
#endif
            return false;
        }
        to_endpoint(reinterpret_cast<sockaddr*>(&ss), size_t(len), from);
        data.assign(buf, buf + n);
        return true;
    }
}

std::string UdpSocket::describe(const Endpoint& e) const {
    if (!e.valid()) return "?";
    sockaddr_storage ss{};
    std::memcpy(&ss, e.bytes.data(), e.size);
    char host[INET6_ADDRSTRLEN + 1] = {};
    uint16_t port = 0;
    if (ss.ss_family == AF_INET6) {
        auto* a = reinterpret_cast<sockaddr_in6*>(&ss);
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&a->sin6_addr);
        static const uint8_t kMapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
        port = ntohs(a->sin6_port);
        if (std::memcmp(b, kMapped, 12) == 0) {
            inet_ntop(AF_INET, const_cast<uint8_t*>(b + 12), host, sizeof host);
            return std::string(host) + ":" + std::to_string(port);
        }
        inet_ntop(AF_INET6, &a->sin6_addr, host, sizeof host);
        return "[" + std::string(host) + "]:" + std::to_string(port);
    }
    auto* a = reinterpret_cast<sockaddr_in*>(&ss);
    inet_ntop(AF_INET, &a->sin_addr, host, sizeof host);
    port = ntohs(a->sin_port);
    return std::string(host) + ":" + std::to_string(port);
}

bool UdpSocket::resolve(const std::string& text_in, uint16_t default_port, Endpoint& out, std::string* error) {
    if (!startup()) {
        if (error) *error = "the network is not available";
        return false;
    }
    std::string text = text_in;
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.erase(text.begin());
    std::string host = text, port = std::to_string(default_port);
    if (!text.empty() && text.front() == '[') {
        const size_t close = text.find(']');
        if (close == std::string::npos) {
            if (error) *error = "the address has a [ without its ]";
            return false;
        }
        host = text.substr(1, close - 1);
        if (close + 1 < text.size() && text[close + 1] == ':') port = text.substr(close + 2);
    } else if (text.find(':') != std::string::npos && text.find(':') == text.rfind(':')) {
        host = text.substr(0, text.find(':'));
        port = text.substr(text.find(':') + 1);
    }
    if (host.empty()) {
        if (error) *error = "no address given";
        return false;
    }
    char* end = nullptr;
    const long p = std::strtol(port.c_str(), &end, 10);
    if (port.empty() || *end || p <= 0 || p > 65535) {
        if (error) *error = "the port must be a number from 1 to 65535";
        return false;
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(p).c_str(), &hints, &res) != 0 || !res) {
        if (error) *error = "cannot find the address " + host;
        return false;
    }
    // IPv4 first when there is a choice: home routers forward IPv4 ports.
    const addrinfo* pick = res;
    for (const addrinfo* a = res; a; a = a->ai_next)
        if (a->ai_family == AF_INET) { pick = a; break; }
    to_endpoint(pick->ai_addr, size_t(pick->ai_addrlen), out);
    freeaddrinfo(res);
    return true;
}

} // namespace chaotix::net
