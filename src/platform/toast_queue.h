// Notifications (unlocks, time attack results, save states) and how they
// queue. Earning many awards at once (loading a save, a level clear that
// earns several) used to put every banner on the screen at once, stacked up
// the left side over the game. Now at most kMaxAwards award banners show at a
// time; the others wait their turn, each shown for less time while others
// wait, and when more than one is still waiting beyond the free places they
// fold into one "N MORE AWARDS" banner. Other notifications (TIME ATTACK,
// NETPLAY, states) answer something the player just did, so they never wait.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace chaotix::toast_queue {

struct Toast {
    Toast(std::string t, std::string d, uint64_t until, std::string c = "AWARD GET!")
        : title(std::move(t)), description(std::move(d)), until_ms(until), caption(std::move(c)) {}
    std::string title, description;
    // When it goes, in SDL_GetTicks() milliseconds. Given by whoever adds it
    // as "now + how long"; while it waits its turn, that "how long" is kept.
    uint64_t until_ms = 0;
    std::string caption = "AWARD GET!";
    int points = 0;          // an award's points: drawn with its emblem when > 0
    std::string icon;        // the award's emblem (achievements.h)
    uint64_t shown_ms = 0;   // when it was first drawn, for sliding in; 0 while waiting
    uint64_t queued_ms = 0;  // when the queue first saw it
    uint64_t duration_ms = 0;
    int merged = 0;          // awards folded into this one ("N MORE AWARDS")
    bool award() const { return points > 0; }
};

inline constexpr int kMaxAwards = 2;
// How long an award banner stays while others wait for its place.
inline constexpr uint64_t kQueuedMs = 2500;

// No "+" in front: the game's font has none.
inline std::string merged_title(int n) { return std::to_string(n) + " MORE AWARDS"; }

// Once per drawn frame, before drawing: drops the banners whose time is up,
// folds and starts the waiting ones. Afterwards the banners to draw are the
// ones with shown_ms set, in order.
inline void tick(std::vector<Toast>& toasts, uint64_t now) {
    for (auto& t : toasts) {
        if (t.queued_ms) continue;
        t.queued_ms = now ? now : 1;
        t.duration_ms = t.until_ms > now ? t.until_ms - now : 0;
    }
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(),
                                [&](const Toast& t) { return t.shown_ms && now >= t.until_ms; }),
                 toasts.end());

    int showing = 0;
    for (const auto& t : toasts)
        if (t.award() && t.shown_ms) ++showing;
    const int free = std::max(0, kMaxAwards - showing);

    // The waiting awards past the free places: one waits as it is, two or
    // more become one banner.
    std::vector<size_t> waiting;
    for (size_t i = 0; i < toasts.size(); ++i)
        if (toasts[i].award() && !toasts[i].shown_ms) waiting.push_back(i);
    if (waiting.size() > size_t(free) + 1) {
        const size_t first = waiting[size_t(free)];
        Toast sum = toasts[first];
        if (!sum.merged) {
            sum = Toast("", "See them all on the AWARDS page.", 0, "AWARDS GET!");
            sum.icon = "star";
            sum.queued_ms = toasts[first].queued_ms;
            sum.duration_ms = std::max<uint64_t>(toasts[first].duration_ms, kQueuedMs);
            sum.points = 0;
            sum.merged = 0;
        }
        for (size_t k = size_t(free); k < waiting.size(); ++k) {
            const Toast& t = toasts[waiting[k]];
            if (t.merged) {
                if (waiting[k] != first) { sum.merged += t.merged; sum.points += t.points; }
            } else {
                sum.merged += 1;
                sum.points += t.points;
            }
        }
        sum.title = merged_title(sum.merged);
        toasts[first] = sum;
        for (size_t k = waiting.size(); k-- > size_t(free) + 1;) toasts.erase(toasts.begin() + long(waiting[k]));
    }

    // Start what has a place: every other notification at once, awards while
    // there is room.
    int starting = 0;
    for (auto& t : toasts) {
        if (t.shown_ms) continue;
        if (t.award()) {
            if (showing + starting >= kMaxAwards) continue;
            ++starting;
        }
        t.shown_ms = now ? now : 1;
        t.until_ms = now + t.duration_ms;
    }
    // Awards still waiting: the ones up make way sooner (but not abruptly).
    bool queue = false;
    for (const auto& t : toasts)
        if (t.award() && !t.shown_ms) queue = true;
    if (!queue) return;
    for (auto& t : toasts)
        if (t.award() && t.shown_ms) t.until_ms = std::min(t.until_ms, std::max(t.shown_ms + kQueuedMs, now + 250));
}

} // namespace chaotix::toast_queue
