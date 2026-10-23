// TIME ATTACK ghosts (issue #41, docs/GHOSTS.md): race a translucent copy of
// a run you made before.
//
// The game is deterministic: given the same starting state and the same pad
// on every frame, a machine does exactly what it did before (that is what
// lockstep validation checks). So a ghost is the run's inputs plus the state
// the machine was in when the game took the stage, and playing it back is a
// second Machine fed those inputs, one frame for each frame of the run being
// played. Starting from a state rather than from the stage request alone is
// what makes it exact: what the game was doing on the title screen before the
// request (its counters, its random numbers) carries into the level.
//
// What is drawn is only the ghost's characters: the 32X sprite recorder
// (runtime/sprite_record.h) lists the ghost machine's blitter calls, the
// characters are banks 0-15 (docs/SPRITES_32X.md), and those draws are
// rasterised host-side from their own bytes and the ghost's own palette. They
// are placed by world position: the ghost's screen position plus the ghost's
// camera, less the live camera. The live machine is never touched: the ghost
// is composed over a copy of its picture.
#pragma once
#include "frontend/time_attack.h"
#include "runtime/stage_select.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace chaotix {

class Machine;
struct Rom;

namespace ghost {

// OPTIONS: which ghost a run races. BEST is the fastest run of the stage
// kept (with any characters), LAST the run of it finished last.
enum class Mode { Off, Best, Last };
const char* mode_name(Mode m);              // "OFF", "BEST", "LAST"
Mode parse_mode(const std::string& s, Mode fallback);
// Off on phones and tablets, which may have no core to spare for a second
// machine (docs/GHOSTS.md has the numbers); BEST elsewhere.
Mode default_mode();

// The host's settings that change what the game does without being part of
// the emulated hardware's state: a replay that does not match them diverges.
struct Settings {
    int wide_extra = 0, wide_extra_bottom = 0;
    bool wide_enabled = false, wide_everywhere = false, wide_hud_edges = false;
    bool six_button[2] = {true, false};
    uint64_t music_replaced = 0;   // bit N: music id N is played by the host
    static Settings of(const Machine& m);
    void apply(Machine& m) const;
    bool operator==(const Settings& o) const;
    bool operator!=(const Settings& o) const { return !(*this == o); }
};

// From frame `frame` of the run on (0 is the first frame after the start).
struct SettingsChange {
    uint32_t frame = 0;
    Settings settings;
};

struct Ghost {
    // Who can play it: a ghost is refused by another ROM, another set of
    // ROM patches or character stats, or another version of the program.
    std::string game_version, rom_sha1, rom_data_sha1;
    stage_select::Request request;
    int time = 0;               // the run's time, level-clock frames
    int64_t recorded_unix = 0;  // when, 0 if unknown
    // A save state (runtime/save_state.h, without the picture) of the moment
    // the game took the stage, then each frame's pads from there: pad 1 in
    // the low word, pad 2 in the high.
    std::vector<uint8_t> start;
    std::vector<uint32_t> inputs;
    std::vector<SettingsChange> settings;   // the first at frame 0
};

// The file's bytes: a header, then the start state and the inputs (LZ4,
// runtime/lz.h). False, with the reason, for anything that is not a whole
// ghost of this format.
std::vector<uint8_t> encode(const Ghost& g);
bool decode(const std::vector<uint8_t>& bytes, Ghost& out, std::string* error);
// Whether this program, with this ROM, can play it.
bool compatible(const Ghost& g, const Machine& m, std::string* why);
// Whether `a` is a better run than `b`.
bool faster(const Ghost& a, const Ghost& b);

// <root>Ghosts/<first 8 of the ROM image's SHA-1>/, with the trailing slash.
// The image after patches, so patched games keep their own records.
std::string folder(const std::string& root, const Machine& m);
// <place>_<level>_<player>_<partner>.ghost: a stage's best with those two.
std::string best_name(const stage_select::Request& r);
// <place>_<level>_last.ghost: the run of that stage finished last.
std::string last_name(const stage_select::Request& r);

// The ghost to race on this stage, by the mode: false (and why) if there is
// none, or none this program can play. BEST looks at every file in `dir`
// named <place>_<level>_*.ghost except the stage's last. `path` is the file
// it came from.
bool choose(const std::string& dir, Mode mode, const stage_select::Request& r, const Machine& m, Ghost& out,
            std::string& path, std::string* why);
// Keeps a finished run: always as the stage's last, and as the best for its
// characters when it is faster than the one there (or that one is unusable).
struct Stored { bool last = false, best = false; size_t bytes = 0; };
Stored store(const std::string& dir, const Ghost& g, const Machine& m);
// A ghost that did not replay as recorded is put aside: renamed to
// <file>.desynced, so it is never chosen again but is still there to study.
bool discard(const std::string& path);

// Records a run as it is played. arm() when a run is asked for; then, around
// every frame of the machine, before_frame() (with the pads set) and
// after_frame(). The start is the end of the frame on which the game took
// the stage request; inputs are kept from the frame after.
class Recorder {
public:
    void arm(const stage_select::Request& r);
    void cancel() { armed_ = recording_ = false; ghost_ = Ghost{}; }
    bool recording() const { return recording_; }
    void before_frame(const Machine& m);
    void after_frame(const Machine& m);
    // The run is over with this time: the ghost, or false if nothing was
    // recorded. Disarms either way.
    bool finish(const Machine& m, int time, int64_t now_unix, Ghost& out);

private:
    bool armed_ = false, recording_ = false;
    Ghost ghost_;
    Settings last_;
};

// The ghost's characters as they are on its screen, ready to draw.
struct Frame {
    uint64_t index = 0;        // ghost frames run
    bool visible = false;      // the ghost is in its level and drew a character
    int cam_x = 0, cam_y = 0;  // the ghost's camera, as the picture was drawn
    // Their box on the ghost's screen, in pixels from the native picture's
    // top-left corner (widescreen margins are negative x), and its pixels
    // (ARGB; alpha 0 where no character is).
    int x = 0, y = 0, w = 0, h = 0;
    std::vector<uint32_t> px;
};
// The characters (32X banks 0-15) of the picture the machine shows. Needs a
// sprite recorder on the machine.
void capture(const Machine& gm, Frame& out);

// How the ghost is drawn: half see-through, tinted towards pale blue so that
// it reads as a ghost over a character of the same colours.
constexpr int kAlpha = 160;          // of 256
constexpr uint32_t kTint = 0xA0C8FF;
constexpr int kTintAmount = 128;     // of 256

struct Placed {
    int x = 0, y = 0, w = 0, h = 0;  // where, in picture pixels (clipped)
    int pixels = 0;                  // pixels drawn
};
// Draws `f` over a picture (XRGB, `stride` pixels a row) of width x height,
// whose native area starts at column `extra`, for a live camera at
// (cam_x, cam_y): the ghost's world position is its screen position plus its
// camera. Nothing is drawn off the picture.
Placed compose(uint32_t* picture, int stride, int width, int height, int extra, const Frame& f, int cam_x,
               int cam_y);

// The camera a picture was drawn with. Measured (chaotix_headless, a level
// in play): the picture a frame shows is the 32X sprite list of the end of
// the frame before, and its Mega Drive planes are scrolled by the camera of
// the end of that frame too (the scroll table at the end of a frame holds
// that frame's camera, and is read by the next frame's lines). So a picture
// goes with the camera one frame back: kept per frame, read kCameraLag back.
constexpr int kCameraLag = 1;
class CameraTrail {
public:
    void reset() { n_ = 0; }
    void push(int x, int y);
    // The camera kCameraLag frames back (or the oldest kept).
    void get(int& x, int& y) const;

private:
    int xs_[kCameraLag + 1] = {}, ys_[kCameraLag + 1] = {};
    int n_ = 0;
};

// What became of a ghost being played.
struct Status {
    uint64_t frames = 0;   // ghost frames run
    bool goal = false;     // reached the goal: `time` is its time
    int time = 0;
    bool ended = false;    // the run is over (or its inputs ran out)
    bool desynced = false; // it ended other than as recorded: not a faithful replay
};

// Plays a ghost on a machine of its own. The ghost machine runs on the
// reference interpreters: the recompiled code's dispatch state is
// process-wide and belongs to the machine being played. Threaded, it runs on
// a thread of its own, overlapped with the live machine; otherwise each
// advance() runs the frame there and then (tests, the headless runner).
class Player {
public:
    Player() = default;
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // `rom` must be the live machine's (patches and all). False (and why) if
    // the ghost's start state does not load.
    bool start(const Rom& rom, Ghost g, bool threaded, std::string* why);
    void stop();
    bool active() const { return started_; }
    const Ghost& ghost() const { return ghost_; }

    // The live run went one frame on: so does the ghost.
    void advance();
    // The newest picture of the ghost. Threaded, waits up to `wait_ms` for
    // the ghost to catch up with the live run first (it falls behind only on
    // a machine with no time to spare, and is then drawn a frame late).
    bool latest(Frame& out, double wait_ms = 0);
    Status status() const;
    // Frames asked for and not yet run: how far a threaded ghost is behind.
    uint64_t behind() const;
    // Time spent running ghost frames, for the frame timing overlay.
    double last_frame_ms() const { return last_ms_.load(); }

private:
    void run_one();
    void worker();

    Ghost ghost_;
    std::unique_ptr<Machine> m_;
    time_attack::Run run_;
    Frame held_;   // captured at the end of the last frame, shown in the next
    size_t next_setting_ = 0;
    bool started_ = false, threaded_ = false;

    mutable std::mutex mu_;
    std::condition_variable cv_;      // the worker: more frames to run, or quit
    std::condition_variable done_cv_; // the caller: a frame finished
    uint64_t target_ = 0;             // frames asked for
    bool quit_ = false;
    Frame frame_;                     // the newest, under mu_
    Status status_;                   // under mu_
    std::atomic<double> last_ms_{0};
    std::thread thread_;
};

} // namespace ghost
} // namespace chaotix
