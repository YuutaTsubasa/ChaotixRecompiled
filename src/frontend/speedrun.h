// Speedrun timing (issue #47): a run's real time and game time, and its
// splits, watched from outside the game like a TIME ATTACK run
// (frontend/time_attack.h). Nothing here writes to the machine.
//
// Definitions (docs/SPEEDRUN.md has the long form):
//
//   A run      a story-mode playthrough. It starts on the frame the game
//              leaves DATA LOAD (game mode 0x60) for the game itself -- START
//              on a save slot, a new game or a loaded one -- or when the
//              player starts it by hand (the START RUN key or menu entry). It
//              is reset by hand, or by the game going back to its title
//              screen (mode 0x08) or its boot (mode 0x00).
//   RTA        real time from the start of the run, measured on the host's
//              clock (what the caller passes as `now`), so it runs on through
//              pauses, menus, loading and fast-forward, as a stopwatch would.
//   Level time what the level's own clock shows on its HUD and results
//              screen (stage_select.h's level clock, less the HUD's lag), in
//              frames at 60 Hz.
//   IGT        game time: the sum of the level times of the levels finished
//              in the run, plus the running clock of the level being played.
//              A level left without being finished (its clock ran out) adds
//              nothing, as it adds nothing to the game's own play time.
//   Split      a finished level: the frame its clock stops at the goal (the
//              results screen's time is settled then). A level the game
//              counts as finished without that (its progress byte at
//              FFDFFE + place moves on) splits there instead, and leaving
//              INTRODUCTION for the lobby splits too (it has no clock). One
//              split per visit to a level.
//
// Personal best: when a run ends (it is reset, by hand or by the game) with
// more splits than the best kept, or as many in less time, it becomes the
// best, kept per ROM in <user data>/Splits/. The splits of the run being made
// are compared with the best's, split by split.
#pragma once
#include "frontend/time_attack.h"

#include <cstdint>
#include <string>
#include <vector>

namespace chaotix {

class Machine;

namespace speedrun {

// Game modes, as $FFDFDE holds them (runtime/patches.h).
inline constexpr unsigned kModeBoot = 0x00;
inline constexpr unsigned kModeTitle = 0x08;
inline constexpr unsigned kModeLevel = 0x18;
inline constexpr unsigned kModeDataLoad = 0x60;
// FFDFFE + place: the level of each attraction (place 0-4) the game is up
// to; a finished level moves it on ($890F86).
inline constexpr uint32_t kProgress = 0xDFFE;
inline constexpr int kIntroduction = 6;
inline constexpr int kLobby = 7;

enum class Compare { Real, Game };

struct Split {
    int place = 0, level = 0;
    std::string name;           // "BOTANIC BASE 1"
    uint64_t frame = 0;         // the emulated frame it was taken on
    int64_t rta_ms = 0;         // real time of the run at the split
    int level_frames = 0;       // the level's own time (0: it keeps no clock)
    int igt_frames = 0;         // game time of the run at the split
};

// The best run kept, by its splits.
struct Best {
    std::vector<Split> splits;
    bool empty() const { return splits.empty(); }
};

// What happened on a frame, for the caller to act on.
enum Event : unsigned {
    kNone = 0,
    kStarted = 1,
    kSplit = 2,
    kReset = 4,
};

class Tracker {
public:
    // Idle: no run. Running. Stopped: TIME ATTACK's run is over and its
    // time stays on screen until the next start or a reset.
    enum class State { Idle, Running, Stopped };

    // The game's place names (stage_select::place_names), for the splits.
    void set_place_names(std::vector<std::string> names) { place_names_ = std::move(names); }

    // Call once per emulated frame, after it ran, with the host's clock in
    // seconds. Returns the Events of this frame.
    unsigned update(const Machine& m, double now);

    // By hand: starts a run now (a running one starts again), or ends it.
    // A run started by hand for TIME ATTACK (time_attack = true) keeps no
    // best, and the game's title screen does not reset it.
    // Both return the Events they caused (a run that was going ends with
    // kReset, so its splits can be offered as a best).
    unsigned start(double now, bool time_attack = false);
    unsigned reset();
    // TIME ATTACK's run is over: its time stays on screen, the run stops.
    void stop(double now);

    State state() const { return state_; }
    bool running() const { return state_ == State::Running; }
    bool time_attack() const { return time_attack_; }
    // Real time of the run, live; frozen once stopped.
    int64_t rta_ms(double now) const;
    // Game time of the run, live.
    int igt_frames() const;
    // The level being played: its name and its clock, while it has one.
    bool in_level() const { return level_.running && !split_this_visit_; }
    std::string level_name() const;
    int level_frames() const;
    const std::vector<Split>& splits() const { return splits_; }
    // How many runs have been started, so an observer can tell a new one.
    uint32_t run_id() const { return run_id_; }
    // A cheat was on during the run: it cannot be a best.
    void mark_cheated() { if (running()) cheated_ = true; }

    // The run that ended last (on kReset), for the caller to offer as a best.
    const std::vector<Split>& ended_splits() const { return ended_; }
    bool ended_counts() const { return ended_counts_; }

    std::string name_of(int place, int level) const;

private:
    void begin_level(int place, int level, const Machine& m);
    void take_split(int place, int level, const std::string& name, int level_frames);
    unsigned end_run();

    State state_ = State::Idle;
    bool time_attack_ = false;
    bool cheated_ = false;
    double start_ = 0;
    double stopped_at_ = 0;
    bool intro_done_ = false;
    uint32_t run_id_ = 0;
    std::vector<Split> splits_;
    std::vector<Split> ended_;
    bool ended_counts_ = false;
    int igt_done_ = 0;          // finished levels' times
    // The level being watched.
    time_attack::Run level_;
    int place_ = -1, level_no_ = -1;
    bool split_this_visit_ = false;
    int progress_at_entry_ = 0;
    unsigned prev_mode_ = 0xFFFF;
    int prev_place_ = -1;
    std::vector<std::string> place_names_;
    double last_now_ = 0;
    uint64_t frame_ = 0;
};

// Whether `run` should replace `best`: more splits, or as many in less time
// by the comparison chosen. A run without splits never does.
bool better(const std::vector<Split>& run, const Best& best, Compare by);
// The time a split is compared by, in milliseconds.
int64_t compare_ms(const Split& s, Compare by);
// The run's split `i` against the best's split `i`, in milliseconds
// (negative: ahead). False when the best has no such split.
bool delta_ms(const std::vector<Split>& run, size_t i, const Best& best, Compare by, int64_t& out);

// Files: <root>Splits/<first 8 hex digits of the ROM data's SHA-1>.splits.
std::string best_path(const std::string& root, const std::string& rom_sha1);
bool save_best(const std::string& path, const Best& best);
bool load_best(const std::string& path, Best& best);

// 1:02:03.45 / 2:03.45 / 3.45 -- real time.
std::string format_rta(int64_t ms);
// The game's own way: 1'02"34 (level and game time, in frames).
std::string format_game(int frames);
// +1.23 / -0.50 / +1:02.30.
std::string format_delta(int64_t ms);
inline int64_t frames_to_ms(int frames) { return int64_t(frames) * 1000 / 60; }

enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };
const char* corner_name(Corner c);
Corner parse_corner(const std::string& v, Corner fallback);

} // namespace speedrun
} // namespace chaotix
