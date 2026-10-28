// Achievements: a small, self-contained unlock system in the spirit of
// RetroAchievements. Definitions live in a text file (assets/achievements.ini)
// and are evaluated against the emulated machine's work RAM once per frame;
// unlocks are stored in the user's save folder.
//
// This project ships its own definitions and does not talk to any service.
// The condition language is deliberately small:
//
//   [vars]
//   rings = word FFE008        ; byte | word | long, 68K work RAM address
//
//   [achievement First Ring]
//   description = Collect your first ring.
//   points = 5
//   when = mode == 0x38 and rings >= 1
//
// `when` is a list of comparisons joined by `and`. Each side is a variable, a
// number, or prev(variable) - the value the variable had on the previous
// frame, which is how "changed" conditions are written:
//
//   when = rings > prev(rings)
//
// An achievement about a whole attempt ("clear a level without losing a
// ring") adds `start` and `fail`, two more condition lists. `start` begins an
// attempt (again), `fail` spoils it, and `when` only counts while an attempt
// is going. Each frame `fail` is looked at first, then `start`, then `when`:
//
//   start = clock == 0
//   fail  = rings < prev(rings)
//   when  = goal == 0xFF and prev(goal) != 0xFF
//
// `icon` names the emblem the list and the notification draw for it (ring,
// chaos, goal, clock, place, team, boss, star); anything else is a star.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace chaotix {

class Machine;

namespace achievements {

enum class Width { Byte, Word, Long };

struct Var {
    Width width = Width::Word;
    uint32_t address = 0;
};

enum class Cmp { Eq, Ne, Lt, Le, Gt, Ge };

struct Operand {
    bool literal = false;
    bool previous = false;   // prev(var)
    uint32_t value = 0;      // literal value
    std::string var;         // variable name
};

struct Condition {
    Operand lhs, rhs;
    Cmp cmp = Cmp::Eq;
};

struct Achievement {
    std::string id;           // stable key used in the save file
    std::string title;
    std::string description;
    int points = 0;
    std::vector<Condition> when;  // all must hold
    // An attempt (see above): only with `start`. `armed` is whether one is
    // going; nothing is armed until `start` has held once.
    std::vector<Condition> start, fail;
    bool armed = false;
    std::string icon;             // emblem name, "" for the default
    bool unlocked = false;
    uint64_t unlocked_at = 0;     // unix time, 0 while locked
};

// Parses definitions. Returns false and fills `error` on a malformed file;
// the parsed achievements are still returned for whatever was valid.
bool parse(const std::string& text, std::map<std::string, Var>& vars,
           std::vector<Achievement>& out, std::string* error);

// Evaluates definitions against a memory snapshot each frame and reports
// unlocks. Keeping the state here (rather than in the Machine) means
// achievements never influence emulation.
class Tracker {
public:
    // Loads definitions; returns false if the file cannot be read or parsed.
    bool load_definitions(const std::string& path, std::string* error);
    // Conditions every achievement is additionally subject to, from the
    // definitions' [rules] require line. Exposed for tests.
    const std::vector<Condition>& require() const { return require_; }
    bool load_definitions_text(const std::string& text, std::string* error);
    // Restores/stores unlocks (ini: one "id = unix_time" line per unlock).
    bool load_progress(const std::string& path);
    // Locks everything again. The caller saves, or deletes, the file.
    void reset_progress();
    bool save_progress(const std::string& path) const;

    // Call once per emulated frame. `unlocked` is invoked for each newly
    // unlocked achievement.
    void update(const Machine& m, const std::function<void(const Achievement&)>& unlocked);
    // The game jumped in time (a save state loaded, a rewind step): the last
    // frame seen is not the one before the next, so prev() has nothing to
    // compare with until a frame has been seen again.
    // Any attempt going is dropped as well: what happened in between is not
    // known, so it has to start again.
    void forget_previous();

    const std::vector<Achievement>& list() const { return achievements_; }
    int unlocked_count() const;
    int points_earned() const;
    int points_total() const;
    // Testing hook: evaluate against raw memory instead of a Machine.
    void update_memory(const uint8_t* wram, size_t size, const std::function<void(const Achievement&)>& unlocked);

private:
    uint32_t read(const uint8_t* wram, size_t size, const Var& v) const;
    std::map<std::string, Var> vars_;
    std::map<std::string, uint32_t> previous_;   // last frame's values
    bool have_previous_ = false;
    std::vector<Achievement> achievements_;
    std::vector<Condition> require_;
};

} // namespace achievements
} // namespace chaotix
