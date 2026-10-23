#include "net/loopback.h"

#include <algorithm>
#include <string>

namespace chaotix::net {

class LoopbackTransport : public Transport {
public:
    LoopbackTransport(LoopbackNetwork& net, int side) : net_(net), side_(side) {}
    void send(const Endpoint& to, const uint8_t* data, size_t n) override {
        if (to == LoopbackNetwork::endpoint(1 - side_)) net_.send(side_, 1 - side_, data, n);
    }
    bool receive(Endpoint& from, std::vector<uint8_t>& data) override { return net_.receive(side_, from, data); }
    std::string describe(const Endpoint& e) const override {
        return e.valid() ? "loopback:" + std::to_string(int(e.bytes[0])) : "?";
    }

private:
    LoopbackNetwork& net_;
    int side_;
};

LoopbackNetwork::LoopbackNetwork(const Conditions& c, uint32_t seed) : c_(c), rng_(seed * 2654435761ull + 1) {}

Endpoint LoopbackNetwork::endpoint(int side) {
    Endpoint e;
    e.bytes[0] = uint8_t(side + 1);
    e.size = 1;
    return e;
}

std::unique_ptr<Transport> LoopbackNetwork::make(int side) { return std::make_unique<LoopbackTransport>(*this, side); }

double LoopbackNetwork::random01() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 7;
    rng_ ^= rng_ << 17;
    return double(rng_ >> 11) / double(1ull << 53);
}

void LoopbackNetwork::send(int from, int to, const uint8_t* data, size_t n) {
    ++sent_;
    if (n > kMaxDatagram || random01() < c_.loss) {
        ++lost_;
        return;
    }
    double due = now_ms_ + c_.latency_ms + (random01() * 2.0 - 1.0) * c_.jitter_ms;
    due = std::max(due, now_ms_);
    if (!c_.reorder) due = std::max(due, last_due_[to]);
    last_due_[to] = due;
    queue_[to].push_back({due, seq_++, from, std::vector<uint8_t>(data, data + n)});
}

bool LoopbackNetwork::receive(int side, Endpoint& from, std::vector<uint8_t>& data) {
    auto& q = queue_[side];
    auto best = q.end();
    for (auto it = q.begin(); it != q.end(); ++it)
        if (it->due <= now_ms_ && (best == q.end() || it->due < best->due ||
                                   (it->due == best->due && it->seq < best->seq)))
            best = it;
    if (best == q.end()) return false;
    from = endpoint(best->from);
    data = std::move(best->data);
    q.erase(best);
    return true;
}

} // namespace chaotix::net
