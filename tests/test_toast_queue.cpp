// Notifications taking turns (platform/toast_queue.h): many awards earned at
// once must not stack up over the game. No window needed.
#include "platform/toast_queue.h"

#include <string>
#include <vector>

#include "test_framework.h"

using namespace chaotix;
using toast_queue::Toast;

namespace {

Toast award(const std::string& title, uint64_t now, int points = 10) {
    Toast t(title, "desc", now + 5000);
    t.points = points;
    t.icon = "ring";
    return t;
}

int shown_awards(const std::vector<Toast>& v) {
    int n = 0;
    for (const auto& t : v)
        if (t.award() && t.shown_ms) ++n;
    return n;
}

} // namespace

TEST(toast_queue, one_award_shows_for_its_full_time) {
    std::vector<Toast> v{award("A", 1000)};
    toast_queue::tick(v, 1000);
    CHECK_EQ(shown_awards(v), 1);
    CHECK_EQ(v[0].until_ms, uint64_t(6000));
    toast_queue::tick(v, 5999);
    CHECK_EQ(v.size(), size_t(1));
    toast_queue::tick(v, 6000);
    CHECK(v.empty());
}

TEST(toast_queue, many_awards_at_once_show_two_and_fold_the_rest) {
    std::vector<Toast> v;
    for (int i = 0; i < 9; ++i) v.push_back(award("A" + std::to_string(i), 1000, 5 + i));
    toast_queue::tick(v, 1000);
    // Two up, and the other seven are one banner waiting.
    CHECK_EQ(shown_awards(v), 2);
    CHECK_EQ(v.size(), size_t(3));
    CHECK_EQ(v[2].merged, 7);
    CHECK_STR(v[2].title, "7 MORE AWARDS");
    int pts = 0;
    for (int i = 2; i < 9; ++i) pts += 5 + i;
    CHECK_EQ(v[2].points, pts);
    CHECK(!v[2].shown_ms);
    // With something waiting, the two up make way after 2.5 s, not 5.
    CHECK_EQ(v[0].until_ms, uint64_t(1000 + toast_queue::kQueuedMs));
    toast_queue::tick(v, 1000 + toast_queue::kQueuedMs);
    CHECK_EQ(v.size(), size_t(1));
    CHECK(v[0].shown_ms != 0);
    CHECK_EQ(v[0].merged, 7);
}

TEST(toast_queue, never_more_than_two_awards_on_screen) {
    std::vector<Toast> v;
    uint64_t now = 1000;
    for (int step = 0; step < 200; ++step, now += 50) {
        if (step % 7 == 0) v.push_back(award("A" + std::to_string(step), now));
        toast_queue::tick(v, now);
        CHECK(shown_awards(v) <= toast_queue::kMaxAwards);
        // At most one banner waits: a single award or the folded one.
        int waiting = 0;
        for (const auto& t : v)
            if (!t.shown_ms) ++waiting;
        CHECK(waiting <= 1);
    }
}

TEST(toast_queue, a_late_award_folds_into_the_waiting_banner) {
    std::vector<Toast> v;
    for (int i = 0; i < 5; ++i) v.push_back(award("A" + std::to_string(i), 1000));
    toast_queue::tick(v, 1000);
    CHECK_EQ(v.back().merged, 3);
    v.push_back(award("late", 1100));
    v.push_back(award("later", 1100));
    toast_queue::tick(v, 1100);
    CHECK_EQ(v.size(), size_t(3));
    CHECK_EQ(v.back().merged, 5);
    CHECK_STR(v.back().title, "5 MORE AWARDS");
}

TEST(toast_queue, other_notifications_never_wait) {
    std::vector<Toast> v;
    for (int i = 0; i < 4; ++i) v.push_back(award("A" + std::to_string(i), 1000));
    v.emplace_back("STATE 1 SAVED", "", 1000 + 2500, "SAVE STATE");
    toast_queue::tick(v, 1000);
    bool state_up = false;
    for (const auto& t : v)
        if (t.caption == "SAVE STATE") state_up = t.shown_ms != 0 && t.until_ms == 3500;
    CHECK(state_up);
}
