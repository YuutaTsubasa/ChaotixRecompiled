// See speedrun.h.
#include "frontend/speedrun.h"
#include "runtime/stage_select.h"
#include "runtime/system.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace chaotix::speedrun {

namespace {

unsigned rd16(const Machine& m, uint32_t off) {
    return unsigned(m.wram[off] << 8 | m.wram[off + 1]);
}

// The level time the HUD shows for a level clock reading.
int shown(int clock) { return std::max(0, clock - stage_select::kHudLag); }

} // namespace

std::string Tracker::name_of(int place, int level) const {
    std::string name;
    if (place >= 0 && size_t(place) < place_names_.size()) name = place_names_[size_t(place)];
    else if (place >= 0 && place < stage_select::kPlaceCount) name = stage_select::kPlaces[place].name;
    else name = "PLACE " + std::to_string(place);
    if (place == kIntroduction) return name;   // one scene as far as a run goes
    return name + " " + std::to_string(level);
}

std::string Tracker::level_name() const {
    return place_ >= 0 ? name_of(place_, level_no_) : std::string();
}

int Tracker::level_frames() const {
    return level_.running && level_.have_start ? shown(level_.time) : 0;
}

int64_t Tracker::rta_ms(double now) const {
    switch (state_) {
    case State::Idle: return 0;
    case State::Running: return std::max<int64_t>(0, int64_t((now - start_) * 1000.0));
    case State::Stopped: return std::max<int64_t>(0, int64_t((stopped_at_ - start_) * 1000.0));
    }
    return 0;
}

int Tracker::igt_frames() const {
    return igt_done_ + (in_level() ? level_frames() : 0);
}

unsigned Tracker::start(double now, bool time_attack) {
    const unsigned ended = state_ != State::Idle ? end_run() : 0u;
    state_ = State::Running;
    time_attack_ = time_attack;
    cheated_ = false;
    start_ = now;
    last_now_ = now;
    ++run_id_;
    splits_.clear();
    igt_done_ = 0;
    intro_done_ = false;
    level_.cancel();
    place_ = level_no_ = -1;
    split_this_visit_ = false;
    return ended | kStarted;
}

unsigned Tracker::end_run() {
    ended_ = splits_;
    ended_counts_ = !time_attack_ && !cheated_ && !splits_.empty();
    state_ = State::Idle;
    splits_.clear();
    igt_done_ = 0;
    level_.cancel();
    place_ = level_no_ = -1;
    split_this_visit_ = false;
    time_attack_ = false;
    return kReset;
}

unsigned Tracker::reset() {
    return state_ != State::Idle ? end_run() : 0u;
}

void Tracker::stop(double now) {
    if (state_ != State::Running) return;
    // The level being played when it stops keeps its time, as far as it got.
    if (in_level()) igt_done_ += level_frames();
    split_this_visit_ = true;
    level_.cancel();
    stopped_at_ = now;
    state_ = State::Stopped;
}

void Tracker::begin_level(int place, int level, const Machine& m) {
    stage_select::Request r;
    r.place = uint16_t(place);
    r.level = uint16_t(level);
    level_.begin(r);
    place_ = place;
    level_no_ = level;
    split_this_visit_ = false;
    progress_at_entry_ = place >= 0 && place < 5 ? m.wram[kProgress + uint32_t(place)] : 0;
}

void Tracker::take_split(int place, int level, const std::string& name, int level_frames) {
    Split s;
    s.place = place;
    s.level = level;
    s.name = name;
    s.frame = frame_;
    s.rta_ms = rta_ms(last_now_);
    s.level_frames = level_frames;
    igt_done_ += level_frames;
    s.igt_frames = igt_done_;
    splits_.push_back(s);
    split_this_visit_ = true;
}

unsigned Tracker::update(const Machine& m, double now) {
    last_now_ = now;
    frame_ = m.frame_count;
    const unsigned mode = rd16(m, 0xDFDE);
    const int place = int(rd16(m, 0xDFF2));
    const int level = int(rd16(m, 0xDFF4));
    unsigned events = 0;

    // The game leaving DATA LOAD for the game itself starts a run; going
    // back to its title screen (or its boot) ends one.
    if (prev_mode_ == kModeDataLoad && mode != kModeDataLoad && mode != kModeTitle && mode != kModeBoot) {
        events |= start(now, false);
    } else if (state_ == State::Running && !time_attack_ && prev_mode_ != 0xFFFF && mode != prev_mode_ &&
               (mode == kModeTitle || mode == kModeBoot)) {
        events |= end_run();
    }

    if (state_ == State::Running) {
        // The level being played.
        if (level_.running) {
            const bool left = level_.update(m);
            if (!split_this_visit_) {
                if (level_.finished && level_.settling <= 0 && level_.timed()) {
                    // Its clock has stopped at the goal: the time on the
                    // results screen.
                    take_split(place_, level_no_, name_of(place_, level_no_), shown(level_.time));
                    events |= kSplit;
                } else if (place_ >= 0 && place_ < 5 &&
                           m.wram[kProgress + uint32_t(place_)] != progress_at_entry_ &&
                           m.wram[kProgress + uint32_t(place_)] == uint8_t(level_no_ + 1)) {
                    // The game counted it as finished without a stop at the
                    // goal that could be seen.
                    take_split(place_, level_no_, name_of(place_, level_no_),
                               level_.timed() ? shown(level_.time) : 0);
                    events |= kSplit;
                }
            }
            if (left) {
                level_.cancel();
                place_ = level_no_ = -1;
            }
        }
        // INTRODUCTION has no clock: its end is when the game hands over to
        // the lobby, having set every attraction to its first level ($890F28).
        if (!intro_done_ && prev_place_ == kIntroduction && place == kLobby && m.wram[kProgress] == 1 &&
            m.wram[kProgress + 1] == 1 && m.wram[kProgress + 2] == 1 && m.wram[kProgress + 3] == 1) {
            intro_done_ = true;
            take_split(kIntroduction, 0, name_of(kIntroduction, 0), 0);
            events |= kSplit;
        }
        // A level starting. The lobby's room before the catapult sets the
        // place while the level is still 0, so a level is 1 or more.
        if (!level_.running && mode == kModeLevel && place >= 0 && place < kLobby && level >= 1 &&
            !m.stage_pending) {
            begin_level(place, level, m);
            level_.update(m);
        }
    }

    prev_mode_ = mode;
    prev_place_ = place;
    return events;
}

bool better(const std::vector<Split>& run, const Best& best, Compare by) {
    if (run.empty()) return false;
    if (run.size() != best.splits.size()) return run.size() > best.splits.size();
    return compare_ms(run.back(), by) < compare_ms(best.splits.back(), by);
}

int64_t compare_ms(const Split& s, Compare by) {
    return by == Compare::Game ? frames_to_ms(s.igt_frames) : s.rta_ms;
}

bool delta_ms(const std::vector<Split>& run, size_t i, const Best& best, Compare by, int64_t& out) {
    if (i >= run.size() || i >= best.splits.size()) return false;
    out = compare_ms(run[i], by) - compare_ms(best.splits[i], by);
    return true;
}

std::string best_path(const std::string& root, const std::string& rom_sha1) {
    return root + "Splits/" + rom_sha1.substr(0, 8) + ".splits";
}

bool save_best(const std::string& path, const Best& best) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << "chaotix-splits 1\n";
    f << "# split PLACE LEVEL RTA_MS IGT_FRAMES LEVEL_FRAMES FRAME NAME\n";
    for (const Split& s : best.splits)
        f << "split " << s.place << ' ' << s.level << ' ' << s.rta_ms << ' ' << s.igt_frames << ' '
          << s.level_frames << ' ' << s.frame << ' ' << s.name << '\n';
    return bool(f);
}

bool load_best(const std::string& path, Best& best) {
    best.splits.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string line;
    if (!std::getline(f, line) || line.rfind("chaotix-splits 1", 0) != 0) return false;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("split ", 0) != 0) continue;
        std::istringstream in(line.substr(6));
        Split s;
        long long rta = 0;
        unsigned long long frame = 0;
        if (!(in >> s.place >> s.level >> rta >> s.igt_frames >> s.level_frames >> frame)) {
            best.splits.clear();
            return false;
        }
        s.rta_ms = rta;
        s.frame = frame;
        std::getline(in >> std::ws, s.name);
        best.splits.push_back(s);
    }
    return true;
}

std::string format_rta(int64_t ms) {
    if (ms < 0) ms = 0;
    const int64_t cs = ms / 10;
    const int c = int(cs % 100), s = int(cs / 100 % 60), m = int(cs / 6000 % 60), h = int(cs / 360000);
    char out[40];
    if (h) std::snprintf(out, sizeof out, "%d:%02d:%02d.%02d", h, m, s, c);
    else std::snprintf(out, sizeof out, "%d:%02d.%02d", m, s, c);
    return out;
}

std::string format_game(int frames) {
    if (frames < 0) frames = 0;
    const int64_t cs = int64_t(frames) * 100 / 60;
    char out[40];
    std::snprintf(out, sizeof out, "%d'%02d\"%02d", int(cs / 6000), int(cs / 100 % 60), int(cs % 100));
    return out;
}

std::string format_delta(int64_t ms) {
    const char sign = ms < 0 ? '-' : '+';
    const int64_t cs = (ms < 0 ? -ms : ms) / 10;
    char out[40];
    if (cs >= 6000)
        std::snprintf(out, sizeof out, "%c%d:%02d.%02d", sign, int(cs / 6000), int(cs / 100 % 60), int(cs % 100));
    else
        std::snprintf(out, sizeof out, "%c%d.%02d", sign, int(cs / 100), int(cs % 100));
    return out;
}

const char* corner_name(Corner c) {
    switch (c) {
    case Corner::TopLeft: return "TopLeft";
    case Corner::TopRight: return "TopRight";
    case Corner::BottomLeft: return "BottomLeft";
    case Corner::BottomRight: return "BottomRight";
    }
    return "TopRight";
}

Corner parse_corner(const std::string& v, Corner fallback) {
    std::string k;
    for (char ch : v)
        if (ch != ' ' && ch != '_' && ch != '-') k.push_back(char(std::tolower(static_cast<unsigned char>(ch))));
    if (k == "topleft") return Corner::TopLeft;
    if (k == "topright") return Corner::TopRight;
    if (k == "bottomleft") return Corner::BottomLeft;
    if (k == "bottomright") return Corner::BottomRight;
    return fallback;
}

} // namespace chaotix::speedrun
