#include "net/rollback.h"

#include <algorithm>
#include <cstring>

namespace chaotix::net {

uint64_t hash_bytes(const uint8_t* data, size_t n) {
    // FNV-1a's prime over 64-bit words, with a final avalanche: about ten
    // times faster than byte-wise FNV over a megabyte, and as good at telling
    // two states apart.
    uint64_t h = 1469598103934665603ull ^ uint64_t(n);
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w = 0;
        for (int k = 0; k < 8; ++k) w |= uint64_t(data[i + size_t(k)]) << (8 * k);
        h = (h ^ w) * 1099511628211ull;
        h ^= h >> 29;
    }
    for (; i < n; ++i) h = (h ^ data[i]) * 1099511628211ull;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    return h;
}

Rollback::Rollback(Game& game, const RollbackConfig& cfg) : game_(game), cfg_(cfg) {
    cfg_.input_delay = std::clamp(cfg_.input_delay, 0, kMaxInputDelay);
    cfg_.max_rollback = std::max(cfg_.max_rollback, 1);
    cfg_.hash_interval = std::max(cfg_.hash_interval, 0);
    cfg_.local_player = cfg_.local_player ? 1 : 0;
    local_.assign(size_t(cfg_.input_delay), 0);
    // The state before every frame that may be rolled back to: the frames
    // run on a prediction (at most max_rollback) and the one being run.
    slots_.resize(size_t(cfg_.max_rollback) + 2);
}

Input Rollback::predict(uint32_t f) const {
    if (f < remote_.size()) return remote_[f];
    return remote_.empty() ? Input(0) : remote_.back();
}

void Rollback::simulate(uint32_t f, bool resim) {
    Input pads[2];
    const Input remote = predict(f);
    pads[cfg_.local_player] = local_[f];
    pads[1 - cfg_.local_player] = remote;
    if (used_.size() <= f) used_.resize(size_t(f) + 1);
    used_[f] = remote;
    // Only a frame run on a prediction can turn out wrong and be rolled back
    // to; a frame whose state is hashed is kept until the hash is made.
    const bool hashed = cfg_.hash_interval > 0 && f % uint32_t(cfg_.hash_interval) == 0;
    if (f >= remote_.size() || hashed) {
        Slot& s = slot_for(f);
        game_.save(s.data);
        s.frame = f;
        ++stats_.saves;
    }
    game_.advance(pads, resim);
}

void Rollback::correct() {
    if (first_wrong_ == kNoFrame) return;
    if (first_wrong_ < current_) {
        Slot& s = slot_for(first_wrong_);
        // Always there (see the constructor); the check keeps a logic error
        // from loading another frame's state.
        if (s.frame == first_wrong_) {
            game_.load(s.data);
            const uint32_t from = first_wrong_;
            first_wrong_ = kNoFrame;
            for (uint32_t f = from; f < current_; ++f) simulate(f, true);
            const int n = int(current_ - from);
            ++stats_.rollbacks;
            stats_.resimulated += uint64_t(n);
            stats_.last_rollback = n;
            stats_.max_rollback = std::max(stats_.max_rollback, n);
        }
    }
    first_wrong_ = kNoFrame;
}

void Rollback::after_correct() {
    const uint32_t done = confirmed();
    if (cfg_.record_confirmed) {
        while (log_.size() < done) {
            const uint32_t f = uint32_t(log_.size());
            std::array<Input, 2> pads{};
            pads[size_t(cfg_.local_player)] = local_[f];
            pads[size_t(1 - cfg_.local_player)] = remote_[f];
            log_.push_back(pads);
        }
    }
    if (cfg_.hash_interval <= 0) return;
    // The state before frame h is final once every frame before it has been
    // run with both pads known.
    while (next_hash_ < current_ && next_hash_ <= done) {
        const Slot& s = slot_for(next_hash_);
        if (s.frame == next_hash_) {
            local_hashes_[next_hash_] = hash_bytes(s.data.data(), s.data.size());
            hashed_.emplace_back(next_hash_, s.data);
            while (hashed_.size() > 3) hashed_.pop_front();
            compare(next_hash_);
        }
        next_hash_ += uint32_t(cfg_.hash_interval);
    }
    const uint32_t keep = uint32_t(cfg_.hash_interval) * 32;
    if (next_hash_ > keep) {
        const uint32_t oldest = next_hash_ - keep;
        local_hashes_.erase(local_hashes_.begin(), local_hashes_.lower_bound(oldest));
        remote_hashes_.erase(remote_hashes_.begin(), remote_hashes_.lower_bound(oldest));
    }
}

void Rollback::compare(uint32_t f) {
    auto l = local_hashes_.find(f);
    auto r = remote_hashes_.find(f);
    if (l == local_hashes_.end() || r == remote_hashes_.end()) return;
    ++stats_.hashes_checked;
    if (l->second != r->second && desync_frame_ == kNoFrame) desync_frame_ = f;
}

bool Rollback::would_stall() const { return current_ >= remote_.size() + uint32_t(cfg_.max_rollback); }

bool Rollback::tick(Input local) {
    if (desynced()) return false;
    correct();
    after_correct();
    if (would_stall()) {
        ++stats_.stalls;
        return false;
    }
    local_.push_back(local);
    simulate(current_, false);
    ++current_;
    ++stats_.frames;
    after_correct();
    return true;
}

void Rollback::add_remote_inputs(uint32_t first, const Input* inputs, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const uint32_t f = first + uint32_t(i);
        if (f < remote_.size()) continue;
        if (f > remote_.size()) break;  // a gap: sent again later
        remote_.push_back(inputs[i]);
        if (f < current_ && used_[f] != inputs[i] && f < first_wrong_) first_wrong_ = f;
    }
}

void Rollback::set_remote_ack(uint32_t frame) {
    frame = std::min(frame, uint32_t(local_.size()));
    if (frame > remote_ack_) remote_ack_ = frame;
}

int Rollback::frame_advantage(int rtt_frames) const {
    if (remote_frame_ == kNoFrame) return 0;
    return int(int64_t(current_) - int64_t(remote_frame_) - rtt_frames / 2);
}

void Rollback::add_remote_hash(uint32_t frame, uint64_t hash) {
    if (cfg_.hash_interval > 0 && next_hash_ > uint32_t(cfg_.hash_interval) * 32 &&
        frame < next_hash_ - uint32_t(cfg_.hash_interval) * 32)
        return;  // long since checked
    auto it = remote_hashes_.find(frame);
    if (it != remote_hashes_.end() && it->second == hash) return;  // sent again: already compared
    remote_hashes_[frame] = hash;
    compare(frame);
}

const std::vector<uint8_t>* Rollback::hashed_snapshot(uint32_t frame) const {
    for (const auto& h : hashed_)
        if (h.first == frame) return &h.second;
    return nullptr;
}

uint64_t Rollback::hash_at(uint32_t frame) const {
    auto it = local_hashes_.find(frame);
    return it == local_hashes_.end() ? 0 : it->second;
}

uint64_t Rollback::remote_hash_at(uint32_t frame) const {
    auto it = remote_hashes_.find(frame);
    return it == remote_hashes_.end() ? 0 : it->second;
}

uint32_t Rollback::confirmed() const {
    return std::min({uint32_t(remote_.size()), current_, first_wrong_});
}

} // namespace chaotix::net
