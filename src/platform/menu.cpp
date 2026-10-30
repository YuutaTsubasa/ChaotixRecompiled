#include "platform/menu.h"
#include "platform/award_art.h"
#include "runtime/tether.h"
#include "runtime/lobby.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

#include "frontend/backdrop.h"
#include "frontend/config.h"
#include "frontend/ghost.h"
#include "frontend/level_select.h"
#include "frontend/speedrun.h"
#include "game/achievements.h"
#include "game/cheats.h"

namespace chaotix {

namespace {

// The hint lines along the foot of the pages ("Up/Down choose ..."), in the
// pixel face like the rest of the menu rather than a smooth one among the
// pixel art (issue #96).
constexpr ui::Font kHintFont = ui::Font::PixelSmall;

// Menus of this era shout, so the labels are upper case and the selected one
// carries a marker rather than a highlight bar.
const char* kAspectNames[] = {"AUTO", "4:3", "16:9", "16:10", "21:9", "CUSTOM"};
// In ScaleMode's order (renderer/viewport.h: Integer, Fit, Stretch).
const char* kScaleNames[] = {"INTEGER", "FIT", "STRETCH"};
const char* kWindowNames[] = {"WINDOW", "BORDERLESS", "FULLSCREEN"};
const char* kTouchNames[] = {"AUTO", "ON", "OFF"};

std::string on_off(bool v) { return v ? "ON" : "OFF"; }

// Steps a value through a list, wrapping at both ends.
template <typename T>
void cycle(T& value, int count, int step) {
    int i = int(value) + step;
    while (i < 0) i += count;
    value = T(i % count);
}

} // namespace

bool Menu::is_row_page(Page p) { return p == Page::Main; }

bool Menu::is_list_page(Page p) {
    return p == Page::Front || p == Page::Options || p == Page::Controls || p == Page::Keys ||
           p == Page::States || p == Page::StateKeys ||
           p == Page::TimeAttack || p == Page::Cheats || p == Page::Video || p == Page::Sound ||
           p == Page::Input || p == Page::Extras || p == Page::Lobby || p == Page::Mods || p == Page::ModInfo ||
           p == Page::Music || p == Page::Speedrun || p == Page::Netplay ||
           p == Page::Tether || p == Page::LevelSelect;
}

// Where BACK and Escape go from a page below another one.
Menu::Page Menu::parent_of(Page p) {
    switch (p) {
    case Page::Video: case Page::Sound: case Page::Input: case Page::Extras: return Page::Options;
    case Page::Cheats: case Page::Lobby: return Page::Extras;
    case Page::Tether: return Page::Options;
    case Page::Speedrun: return Page::Extras;
    case Page::Mods: return Page::Extras;
    case Page::ModInfo: return Page::Mods;
    case Page::Keys: case Page::StateKeys: return Page::Controls;
    default: return Page::Closed;
    }
}

void Menu::set_page(Page p) {
    if (p != Page::Mods && p != Page::ModInfo) leave_mods();
    if (is_row_page(p) || p == Page::Front) return_to_ = p;
    const Page from = page_;
    page_ = p;
    if (transitions_ && p != from) page_opened_ms_ = std::max<uint64_t>(1, SDL_GetTicks());
    selected_ = 0;
    scroll_ = 0;
    anim_ = 0;
    awaiting_.clear();
    editing_ = nullptr;
    confirm_reset_ = false;
    info_lines_.clear();
    if (p == Page::Front) build_front();
    else if (p == Page::Main) build_main();
    else if (p == Page::Options) build_options();
    else if (p == Page::Video) build_video();
    else if (p == Page::Sound) build_sound();
    else if (p == Page::Input) build_input();
    else if (p == Page::Extras) build_extras();
    else if (p == Page::Controls) build_controls();
    else if (p == Page::Keys) build_keys();
    else if (p == Page::States) build_states();
    else if (p == Page::StateKeys) build_state_keys();
    else if (p == Page::TimeAttack) build_time_attack();
    else if (p == Page::LevelSelect) build_level_select();
    else if (p == Page::Cheats) build_cheats();
    else if (p == Page::Lobby) build_lobby();
    else if (p == Page::Tether) build_tether();
    else if (p == Page::Speedrun) build_speedrun();
    else if (p == Page::Netplay) build_netplay();
    else if (p == Page::Mods) {
        build_mods();
        // Back from a pack's page: on that pack again.
        if (from == Page::ModInfo && mod_index_ < items_.size()) selected_ = int(mod_index_);
    } else if (p == Page::ModInfo) build_mod_info();
    else if (p == Page::Music) {
        // Back to where it was opened from: the front end or SOUND.
        if (from != Page::Music) music_back_ = from == Page::Front ? Page::Front : Page::Sound;
        build_music();
    } else items_.clear();
}

void Menu::close() {
    if (editing_) end_edit(true);
    leave_mods();
    page_ = Page::Closed;
}

void Menu::leave_mods() {
    if (!mods_dirty_) return;
    mods_dirty_ = false;
    if (hooks_.reload_mods) hooks_.reload_mods();
}

void Menu::open_front() {
    revealed_ = false;  // before PUSH START, only the hint until a press
    set_page(Page::Front);
}

void Menu::toggle() {
    if (open()) close(); else set_page(Page::Main);
}

void Menu::show_achievements() { set_page(Page::Achievements); }

bool Menu::show_page(const std::string& name) {
    if (name == "front") { revealed_ = true; set_page(Page::Front); }  // asked for: shown at once
    else if (name == "timeattack") set_page(Page::TimeAttack);
    else if (name == "levelselect") set_page(Page::LevelSelect);
    else if (name == "cheats") set_page(Page::Cheats);
    else if (name == "lobby") set_page(Page::Lobby);
    else if (name == "gameplay") set_page(Page::Tether);
    else if (name == "speedrun") set_page(Page::Speedrun);
    else if (name == "netplay") set_page(Page::Netplay);
    else if (name == "controls") set_page(Page::Controls);
    else if (name == "keys1" || name == "keys2" || name == "pad1" || name == "pad2") {
        keys_pad_ = name.rfind("pad", 0) == 0;
        keys_player_ = name.back() == '1' ? 0 : 1;
        set_page(Page::Keys);
    }
    else if (name == "main") set_page(Page::Main);
    else if (name == "states") set_page(Page::States);
    else if (name == "statekeys") set_page(Page::StateKeys);
    else if (name == "options") set_page(Page::Options);
    else if (name == "video") set_page(Page::Video);
    else if (name == "sound") set_page(Page::Sound);
    else if (name == "input") set_page(Page::Input);
    else if (name == "extras") set_page(Page::Extras);
    else if (name == "mods") set_page(Page::Mods);
    else if (name == "music") set_page(Page::Music);
    else if (name == "awards") set_page(Page::Achievements);
    else return false;
    return true;
}

std::string Menu::page_name() const {
    switch (page_) {
    case Page::Front: return "front";
    case Page::Main: return "main";
    case Page::Options: return "options";
    case Page::Video: return "video";
    case Page::Sound: return "sound";
    case Page::Input: return "input";
    case Page::Extras: return "extras";
    case Page::Controls: return "controls";
    case Page::Keys: return "keys";
    case Page::TimeAttack: return "timeattack";
    case Page::LevelSelect: return "levelselect";
    case Page::Achievements: return "awards";
    case Page::Cheats: return "cheats";
    case Page::States: return "states";
    case Page::StateKeys: return "statekeys";
    case Page::Lobby: return "lobby";
    case Page::Tether: return "gameplay";
    case Page::Speedrun: return "speedrun";
    case Page::Netplay: return "netplay";
    case Page::Mods: return "mods";
    case Page::ModInfo: return "modinfo";
    case Page::Music: return "music";
    default: return "";
    }
}

std::string Menu::selected_label() const {
    if (selected_ < 0 || selected_ >= int(items_.size())) return "";
    return items_[size_t(selected_)].label;
}

std::string Menu::selected_value() const {
    if (selected_ < 0 || selected_ >= int(items_.size()) || !items_[size_t(selected_)].value) return "";
    return items_[size_t(selected_)].value();
}

std::string Menu::selected_note() const {
    if (selected_ < 0 || selected_ >= int(items_.size())) return "";
    return items_[size_t(selected_)].note;
}

int Menu::zoom() const { return open() && cfg_ ? cfg_->menu_zoom() : 0; }

std::vector<std::pair<int, SDL_FRect>> Menu::drawn_rows() const {
    std::vector<std::pair<int, SDL_FRect>> out;
    for (const Hit& h : hits_)
        if (h.index >= 0) out.push_back({h.index, h.r});
    return out;
}

// The front end: what pressing Start on the title screen reaches. START GAME
// hands over to the game's own menus, so nothing the original offers is lost.
void Menu::build_front() {
    items_.clear();
    items_.push_back({"START GAME", {}, [this](int) {
        close();
        if (hooks_.start_game) hooks_.start_game();
    }});
    items_.push_back({"TIME ATTACK", {}, [this](int) { set_page(Page::TimeAttack); }});
    items_.push_back({"LEVEL SELECT", {}, [this](int) { set_page(Page::LevelSelect); }});
    items_.push_back({"OPTIONS", {}, [this](int) { set_page(Page::Options); }});
    items_.push_back({"CONTROLS", {}, [this](int) { set_page(Page::Controls); }});
    items_.push_back({"MUSIC PLAYER", {}, [this](int) { set_page(Page::Music); }});
    items_.push_back({"AWARDS", {}, [this](int) { set_page(Page::Achievements); }});
    // Issue #46: online two-player, hosted or joined from here. Near the end,
    // so the entries above keep their places.
    items_.push_back({"NETPLAY", {}, [this](int) { set_page(Page::Netplay); }});
    items_.push_back({"QUIT", {}, [this](int) { if (hooks_.quit) hooks_.quit(); }});
}

// The pause menu: the game has been handed control, so this can give it back
// or take it away again.
void Menu::build_main() {
    items_.clear();
    items_.push_back({"RESUME", {}, [this](int) { close(); }});
    // Save states and rewind (issue #40): a page of their own, so that the
    // row stays short enough for a phone.
    items_.push_back({"STATES", {}, [this](int) { set_page(Page::States); }});
    items_.push_back({"OPTIONS", {}, [this](int) { set_page(Page::Options); }});
    items_.push_back({"CONTROLS", {}, [this](int) { set_page(Page::Controls); }});
    items_.push_back({"AWARDS", {}, [this](int) { set_page(Page::Achievements); }});
    // Netplay (issue #46): to see how it goes, or to leave, in the middle of
    // a game.
    items_.push_back({"NETPLAY", {}, [this](int) { set_page(Page::Netplay); }});
    items_.push_back({"BACK TO TITLE", {}, [this](int) {
        close();
        if (hooks_.back_to_title) hooks_.back_to_title();
    }});
    items_.push_back({"QUIT", {}, [this](int) { if (hooks_.quit) hooks_.quit(); }});
}

// The buttons of a Mega Drive pad, in the order they sit on it rather than
// the order a map happens to store them in.
// The buttons of a Mega Drive pad and what each one does in this game, so the
// binding pages say more than "A". Measured rather than assumed: B is the one
// the game itself answers with a HOLD! bubble, C is the one that jumps, A
// takes ten rings off the counter the moment it is pressed, and X/Y/Z/Mode
// leave the picture and the game's state untouched anywhere they were tried
// (see ARCHITECTURE.md 8.4).
struct PadButton { const char* name; const char* does; };
const PadButton kPadButtons[] = {
    {"up", "LOOK UP"},        {"down", "CROUCH"},
    {"left", "MOVE LEFT"},    {"right", "MOVE RIGHT"},
    {"a", "PULL PARTNER IN - 10 RINGS"},
    {"b", "HOLD PARTNER STILL"},
    {"c", "JUMP"},
    {"x", "NOT USED BY THIS GAME"},
    {"y", "NOT USED BY THIS GAME"},
    {"z", "NOT USED BY THIS GAME"},
    {"start", "PAUSE"},
    {"mode", "NOT USED BY THIS GAME"},
};

// The buttons the emulated 6-button pad leaves alone, so opening the menu
// never costs the game one. SDL's own names, with what they are called on the
// controllers people actually have.
struct MenuButtonChoice { const char* name; const char* label; };
const MenuButtonChoice kMenuButtons[] = {
    {"leftstick", "LEFT STICK CLICK"},
    {"rightstick", "RIGHT STICK CLICK"},
    {"guide", "GUIDE / HOME / PS"},
    {"touchpad", "TOUCHPAD CLICK"},
    {"misc1", "SHARE / CAPTURE"},
};
constexpr int kMenuButtonCount = int(sizeof kMenuButtons / sizeof kMenuButtons[0]);

// Fast-forward on a pad (issue #45): the same spare buttons, and the triggers,
// which the emulated pad does not use either (SDL calls them axes; the
// frontend reads them as pressed past half way).
const MenuButtonChoice kFastForwardButtons[] = {
    {"righttrigger", "RIGHT TRIGGER"},
    {"lefttrigger", "LEFT TRIGGER"},
    {"rightstick", "RIGHT STICK CLICK"},
    {"leftstick", "LEFT STICK CLICK"},
    {"guide", "GUIDE / HOME / PS"},
    {"touchpad", "TOUCHPAD CLICK"},
    {"misc1", "SHARE / CAPTURE"},
    {"none", "NONE"},
};
constexpr int kFastForwardButtonCount = int(sizeof kFastForwardButtons / sizeof kFastForwardButtons[0]);
// What awaiting_ holds while a host key (fast-forward, save states, rewind)
// is being captured; the key page's captures are Mega Drive button names,
// which these are not.
const char kFastForwardCapture[] = "fast forward";
const char kSaveStateCapture[] = "save state";
const char kLoadStateCapture[] = "load state";
const char kSlotCapture[] = "next slot";
const char kRewindCapture[] = "rewind";
const char kRunStartCapture[] = "start run";
const char kRunResetCapture[] = "reset run";

// The gamepad buttons the host's own functions may use: the menu button's
// choices, the triggers, and NONE.
const MenuButtonChoice* pad_choice(const std::string& name) {
    for (const MenuButtonChoice& b : kFastForwardButtons)
        if (name == b.name) return &b;
    return nullptr;
}

std::string upper(std::string v) {
    for (char& c : v) c = char(std::toupper((unsigned char)c));
    return v;
}

std::string device_label(const Menu::Hooks& hooks, const std::string& dev) {
    if (dev == "auto") return "AUTO";
    if (dev == "keyboard") return "KEYBOARD";
    if (dev == "none") return "NONE";
    if (dev.rfind("pad", 0) == 0) {
        // "PAD 2" says nothing about which controller that is, so show the
        // name the system reports and fall back to the number.
        const int i = std::atoi(dev.c_str() + 3);
        if (hooks.pad_name) {
            const std::string name = hooks.pad_name(i - 1);
            if (!name.empty()) return upper(name);
        }
        return "PAD " + dev.substr(3);
    }
    return upper(dev);
}

std::vector<std::string> Menu::device_choices() const {
    std::vector<std::string> out{"auto", "keyboard"};
    const int pads = hooks_.pad_count ? hooks_.pad_count() : 0;
    for (int i = 1; i <= pads; ++i) out.push_back("pad" + std::to_string(i));
    out.push_back("none");
    return out;
}

void Menu::build_controls() {
    items_.clear();
    Config* c = cfg_;
    if (!c) return;
    for (int p = 0; p < 2; ++p) {
        items_.push_back({std::to_string(p + 1) + "P DEVICE",
                          [this, c, p] { return device_label(hooks_, c->device[p]); },
                          [this, c, p](int step) {
                              if (!step) return;
                              const std::vector<std::string> all = device_choices();
                              int i = 0;
                              for (size_t k = 0; k < all.size(); ++k)
                                  if (all[k] == c->device[p]) i = int(k);
                              i = (i + step + int(all.size())) % int(all.size());
                              c->device[p] = all[size_t(i)];
                          }});
    }
    for (int p = 0; p < 2; ++p) {
        items_.push_back({std::to_string(p + 1) + "P KEYBOARD", {},
                          [this, p](int) { keys_player_ = p; keys_pad_ = false; set_page(Page::Keys); }});
        items_.push_back({std::to_string(p + 1) + "P GAMEPAD", {},
                          [this, p](int) { keys_player_ = p; keys_pad_ = true; set_page(Page::Keys); }});
    }
    // Escape opens this menu on a keyboard; on a pad it is whichever of the
    // buttons the game does not use is chosen here.
    items_.push_back({"MENU BUTTON",
                      [c] {
                          for (const MenuButtonChoice& b : kMenuButtons)
                              if (c->menu_button == b.name) return std::string(b.label);
                          return upper(c->menu_button);
                      },
                      [c](int step) {
                          if (!step) return;
                          int i = 0;
                          for (int k = 0; k < kMenuButtonCount; ++k)
                              if (c->menu_button == kMenuButtons[k].name) i = k;
                          // Not the fast-forward button: one button, one job.
                          do i = (i + step + kMenuButtonCount) % kMenuButtonCount;
                          while (c->ff_button == kMenuButtons[i].name || c->rewind_button == kMenuButtons[i].name ||
                                 c->state_save_button == kMenuButtons[i].name ||
                                 c->state_load_button == kMenuButtons[i].name);
                          c->menu_button = kMenuButtons[i].name;
                      }});
    // Fast-forward (issue #45): Tab and the right trigger to begin with.
    // How it behaves (held or toggled, how fast) is under OPTIONS > INPUT.
    items_.push_back({"FAST FORWARD KEY",
                      [this, c] {
                          if (awaiting_ == kFastForwardCapture) return std::string("PRESS A KEY");
                          return c->ff_key.empty() ? std::string("-") : upper(c->ff_key);
                      },
                      [this](int step) { if (step == 0) awaiting_ = kFastForwardCapture; },
                      {}, /*read_only=*/false, /*pressed=*/true});
    items_.push_back({"FAST FORWARD PAD",
                      [c] {
                          for (const MenuButtonChoice& b : kFastForwardButtons)
                              if (c->ff_button == b.name) return std::string(b.label);
                          return upper(c->ff_button);
                      },
                      [c](int step) {
                          if (!step) return;
                          int i = 0;
                          for (int k = 0; k < kFastForwardButtonCount; ++k)
                              if (c->ff_button == kFastForwardButtons[k].name) i = k;
                          do i = (i + step + kFastForwardButtonCount) % kFastForwardButtonCount;
                          while (c->menu_button == kFastForwardButtons[i].name ||
                                 (std::string(kFastForwardButtons[i].name) != "none" &&
                                  (c->rewind_button == kFastForwardButtons[i].name ||
                                   c->state_save_button == kFastForwardButtons[i].name ||
                                   c->state_load_button == kFastForwardButtons[i].name)));
                          c->ff_button = kFastForwardButtons[i].name;
                      }});
    // Issue #40: the keys and buttons for save states and rewind, on a page of
    // their own (this one is as long as a phone's screen allows).
    items_.push_back({"STATES AND REWIND", {}, [this](int) { set_page(Page::StateKeys); }});
    items_.push_back({"BACK", {}, [this](int) { const Page back = return_to_; set_page(back); }});
}

std::string* Menu::capture_target(const std::string& what) const {
    if (!cfg_) return nullptr;
    if (what == kFastForwardCapture) return &cfg_->ff_key;
    if (what == kSaveStateCapture) return &cfg_->state_save_key;
    if (what == kLoadStateCapture) return &cfg_->state_load_key;
    if (what == kSlotCapture) return &cfg_->state_slot_key;
    if (what == kRewindCapture) return &cfg_->rewind_key;
    if (what == kRunStartCapture) return &cfg_->sr_start_key;
    if (what == kRunResetCapture) return &cfg_->sr_reset_key;
    return nullptr;
}

// Save states (issue #40), from the pause menu: the slot quick save and quick
// load use, and rewind on or off. Saving or loading closes the menu: the
// player wants the game, and the message says how it went.
void Menu::build_states() {
    items_.clear();
    Config* c = cfg_;
    if (!c) return;
    auto locked = [this] { return hooks_.states_locked ? hooks_.states_locked() : std::string(); };
    auto slot_note = [this, c, locked] {
        const std::string why = locked();
        if (!why.empty()) return why;
        return hooks_.state_info ? hooks_.state_info(c->state_slot) : std::string();
    };
    items_.push_back({"SAVE STATE", {}, [this, c](int step) {
        if (step != 0) return;
        close();
        if (hooks_.save_state) hooks_.save_state(c->state_slot);
    }});
    items_.push_back({"LOAD STATE", {}, [this, c](int step) {
        if (step != 0) return;
        close();
        if (hooks_.load_state) hooks_.load_state(c->state_slot);
    }});
    items_.push_back({"SLOT",
                      [c] { return std::to_string(c->state_slot); },
                      [this, c, slot_note](int s) {
                          if (!s) return;
                          c->state_slot = (c->state_slot - 1 + s + kStateSlots) % kStateSlots + 1;
                          // The note says what the slot holds, so it follows.
                          items_[size_t(selected_)].note = slot_note();
                      },
                      slot_note()});
    items_.push_back({"REWIND",
                      [c] { return on_off(c->rewind); },
                      [c](int s) { if (s) c->rewind = !c->rewind; },
                      locked().empty() ? std::string("hold the rewind key or button") : locked()});
    items_.push_back({"BACK", {}, [this](int) { set_page(return_to_); }});
}

// Rewind on or off, and the keys and buttons for save states and rewind
// (CONTROLS > STATES AND REWIND). Keys are captured like the fast-forward
// key; buttons step through the ones the game leaves alone, never one another
// host function holds. Ten rows, as many as a phone's screen holds.
void Menu::build_state_keys() {
    items_.clear();
    Config* c = cfg_;
    if (!c) return;
    items_.push_back({"REWIND",
                      [c] { return on_off(c->rewind); },
                      [c](int s) { if (s) c->rewind = !c->rewind; },
                      "keep the last minute to step back through"});
    items_.push_back({"TOUCH REWIND",
                      [c] { return on_off(c->touch_rewind); },
                      [c](int s) { if (s) c->touch_rewind = !c->touch_rewind; },
                      "an on-screen button for it"});
    auto key_row = [this](const char* label, const char* capture, std::string Config::*field) {
        Config* cc = cfg_;
        items_.push_back({label,
                          [this, cc, capture, field] {
                              if (awaiting_ == capture) return std::string("PRESS A KEY");
                              const std::string& v = cc->*field;
                              return v.empty() ? std::string("-") : upper(v);
                          },
                          [this, capture](int step) { if (step == 0) awaiting_ = capture; },
                          {}, /*read_only=*/false, /*pressed=*/true});
    };
    key_row("SAVE STATE KEY", kSaveStateCapture, &Config::state_save_key);
    key_row("LOAD STATE KEY", kLoadStateCapture, &Config::state_load_key);
    key_row("NEXT SLOT KEY", kSlotCapture, &Config::state_slot_key);
    key_row("REWIND KEY", kRewindCapture, &Config::rewind_key);
    auto pad_row = [this](const char* label, std::string Config::*field) {
        Config* cc = cfg_;
        items_.push_back({label,
                          [cc, field] {
                              const MenuButtonChoice* b = pad_choice(cc->*field);
                              return b ? std::string(b->label) : upper(cc->*field);
                          },
                          [cc, field](int step) {
                              if (!step) return;
                              int i = 0;
                              for (int k = 0; k < kFastForwardButtonCount; ++k)
                                  if (cc->*field == kFastForwardButtons[k].name) i = k;
                              // Not a button the menu, fast-forward or the other two hold.
                              auto taken = [cc, field](const std::string& name) {
                                  if (name == "none") return false;
                                  if (name == cc->menu_button || name == cc->ff_button) return true;
                                  for (std::string Config::*other : {&Config::state_save_button, &Config::state_load_button,
                                                                     &Config::rewind_button})
                                      if (other != field && name == cc->*other) return true;
                                  return false;
                              };
                              for (int tries = 0; tries < kFastForwardButtonCount; ++tries) {
                                  i = (i + step + kFastForwardButtonCount) % kFastForwardButtonCount;
                                  if (!taken(kFastForwardButtons[i].name)) break;
                              }
                              cc->*field = kFastForwardButtons[i].name;
                          }});
    };
    pad_row("SAVE STATE PAD", &Config::state_save_button);
    pad_row("LOAD STATE PAD", &Config::state_load_button);
    pad_row("REWIND PAD", &Config::rewind_button);
    items_.push_back({"BACK", {}, [this](int) { set_page(Page::Controls); }});
}

void Menu::add_character_rows(int* player, int* combi, bool* no_partner) {
    using namespace stage_select;
    // The names are the ROM's, so a ROM patch that renames a character shows
    // here. The two character rows skip the gap in the game's table, which
    // loads broken art, for as long as it has only the game's placeholder
    // for a name.
    std::vector<std::string> names = characters_;
    if (names.size() != size_t(kCharacterCount)) names.assign(std::begin(kCharacters), std::end(kCharacters));
    auto name_of = [names](int who) {
        return who >= 0 && who < kCharacterCount ? names[size_t(who)] : std::string();
    };
    auto step_character = [names](int& who, int s) {
        for (int i = 0; i < kCharacterCount; ++i) {
            who = (who + s + kCharacterCount) % kCharacterCount;
            if (!is_placeholder_name(names[size_t(who)])) return;
        }
    };
    sc_.player = player;
    sc_.player_row = int(items_.size());
    items_.push_back({"PLAYER",
                      [player, name_of] { return name_of(*player); },
                      [player, step_character](int s) { if (s) step_character(*player, s); }});
    sc_.combi = combi;
    sc_.no_partner = no_partner;
    sc_.combi_row = int(items_.size());
    if (!no_partner) {
        items_.push_back({"COMBI",
                          [combi, name_of] { return name_of(*combi); },
                          [combi, step_character](int s) { if (s) step_character(*combi, s); }});
        return;
    }
    // LEVEL SELECT (issue #55): NONE after the last character, where it
    // applies. The character picked before NONE stays in *combi: the request
    // still names it, and stepping on from NONE starts from either end.
    items_.push_back({"COMBI",
                      [this, combi, name_of] { return showcase_no_partner() ? std::string("NONE") : name_of(*combi); },
                      [this, combi, no_partner, step_character](int s) {
                          if (!s) return;
                          const bool offered = sc_.place && sc_.two_players &&
                                               level_select::partner_none_offered(*sc_.place, *sc_.two_players);
                          if (offered && *no_partner) {
                              *no_partner = false;
                              *combi = s > 0 ? kCharacterCount - 1 : 0;
                              step_character(*combi, s > 0 ? 1 : -1);
                              return;
                          }
                          *no_partner = false;
                          const int before = *combi;
                          step_character(*combi, s);
                          // Past the last character (or back past the first):
                          // NONE, in between.
                          const bool wrapped = s > 0 ? *combi <= before : *combi >= before;
                          if (offered && wrapped) {
                              *combi = before;
                              *no_partner = true;
                          }
                      },
                      "1 player, the attractions: NONE plays alone (SOLO)"});
}

void Menu::add_players_row(bool* two_players) {
    sc_.two_players = two_players;
    sc_.players_row = int(items_.size());
    items_.push_back({"PLAYERS",
                      [two_players] { return std::string(*two_players ? "2 PLAYERS" : "1 PLAYER"); },
                      [two_players](int s) { if (s) *two_players = !*two_players; }});
}

stage_select::Request Menu::showcase_request() const {
    stage_select::Request r;
    if (!sc_.place) return r;
    r.place = uint16_t(*sc_.place);
    r.level = uint16_t(*sc_.level);
    r.attime = uint16_t(*sc_.time);
    r.player = uint16_t(*sc_.player);
    r.combi = uint16_t(*sc_.combi);
    r.two_players = *sc_.two_players;
    return r;
}

bool Menu::showcase_no_partner() const {
    return sc_.no_partner && *sc_.no_partner && !sc_.timed && sc_.place && sc_.two_players &&
           level_select::partner_none_offered(*sc_.place, *sc_.two_players);
}

std::vector<int> Menu::showcase_levels() const {
    std::vector<int> out;
    if (!sc_.place) return out;
    for (int l = 0; l < 8; ++l)
        if (sc_.timed ? stage_select::has_level(*sc_.place, l) : level_select::has_level(*sc_.place, l))
            out.push_back(l);
    return out;
}

std::string Menu::showcase_level_label(int level) const {
    if (!sc_.place) return "";
    if (sc_.timed) return std::to_string(level);
    // The hub and the bonus stage have one level each, which the list page
    // showed as "-"; a chip of its own says there is the one.
    const std::string label = level_select::level_label(*sc_.place, level);
    return label == "-" ? "1" : label;
}

// LEVEL SELECT (issue #51, frontend/level_select.h): TIME ATTACK's choices
// without its clock, over more places -- the hub, the special stages and the
// bonus stage as well. Kept apart from TIME ATTACK's in the settings file.
void Menu::build_level_select() {
    items_.clear();
    sc_ = Showcase{};
    Config* c = cfg_;
    if (!c) return;
    if (!level_select::offered(c->ls_place)) c->ls_place = level_select::kPlaces[0];
    if (!level_select::has_level(c->ls_place, c->ls_level)) c->ls_level = level_select::first_level(c->ls_place);
    sc_.timed = false;
    sc_.place = &c->ls_place;
    sc_.level = &c->ls_level;
    sc_.time = &c->ls_time;

    sc_.place_row = int(items_.size());
    items_.push_back({"PLACE",
                      [c] { return std::string(stage_select::kPlaces[c->ls_place].name); },
                      [c](int s) {
                          if (!s) return;
                          c->ls_place = level_select::step_place(c->ls_place, s);
                          if (!level_select::has_level(c->ls_place, c->ls_level))
                              c->ls_level = level_select::first_level(c->ls_place);
                      }});
    sc_.level_row = int(items_.size());
    items_.push_back({"LEVEL",
                      [c] { return level_select::level_label(c->ls_place, c->ls_level); },
                      [c](int s) { if (s) c->ls_level = level_select::step_level(c->ls_place, c->ls_level, s); }});
    sc_.time_row = int(items_.size());
    items_.push_back({"AT-TIME",
                      [c] { return std::string(stage_select::kTimes[(c->ls_time >> 1) & 3]); },
                      [c](int s) { if (s) c->ls_time = ((c->ls_time / 2 + s + 4) % 4) * 2; }});
    add_character_rows(&c->ls_player, &c->ls_combi, &c->ls_no_partner);
    add_players_row(&c->ls_two_players);
    sc_.start_row = int(items_.size());
    items_.push_back({"START", {}, [this](int) {
        const stage_select::Request r = showcase_request();
        if (!level_select::valid(r)) return;
        const bool solo = showcase_no_partner();
        close();
        if (hooks_.start_level) hooks_.start_level(r, solo);
    }, "practice: no clock, records or saving"});
    sc_.back_row = int(items_.size());
    items_.push_back({"BACK", {}, [this](int) { const Page back = return_to_; set_page(back); }});
}

// TIME ATTACK: the fields the game's own stage select offers, so the choices
// are the game's rather than an invention of ours. Everything is kept in the
// settings file, so the same run can be set up again in one go.
void Menu::build_time_attack() {
    using namespace stage_select;
    items_.clear();
    sc_ = Showcase{};
    Config* c = cfg_;
    if (!c) return;
    sc_.timed = true;
    sc_.place = &c->ta_place;
    sc_.level = &c->ta_level;
    sc_.time = &c->ta_time;
    sc_.ghost = &c->ta_ghost;

    // A place only has certain levels, so changing the place pulls the level
    // to one it actually has.
    auto fix_level = [c] {
        if (!has_level(c->ta_place, c->ta_level)) c->ta_level = first_level(c->ta_place);
    };
    fix_level();

    sc_.place_row = int(items_.size());
    items_.push_back({"PLACE",
                      [c] { return std::string(kPlaces[c->ta_place].name); },
                      [c, fix_level](int s) {
                          if (!s) return;
                          c->ta_place = (c->ta_place + s + kPlayablePlaces) % kPlayablePlaces;
                          fix_level();
                      }});
    sc_.level_row = int(items_.size());
    items_.push_back({"LEVEL",
                      [c] { return std::to_string(c->ta_level); },
                      [c](int s) {
                          if (!s) return;
                          // Step to the next level this place has, wrapping.
                          for (int i = 0; i < 8; ++i) {
                              c->ta_level = (c->ta_level + s + 8) % 8;
                              if (has_level(c->ta_place, c->ta_level)) return;
                          }
                      }});
    sc_.time_row = int(items_.size());
    items_.push_back({"AT-TIME",
                      [c] { return std::string(kTimes[(c->ta_time >> 1) & 3]); },
                      [c](int s) { if (s) c->ta_time = ((c->ta_time / 2 + s + 4) % 4) * 2; }});
    add_character_rows(&c->ta_player, &c->ta_combi);
    // Not a setting: the record for whatever is selected above, so choosing a
    // stage shows what there is to beat. Left and right do nothing, and up
    // and down step over it.
    Item best{"BEST TIME",
              [c] {
                  const int b = best_time(*c, c->ta_place, c->ta_level);
                  return b ? format_time(b) : std::string("--");
              },
              [](int) {}};
    best.read_only = true;
    sc_.best_row = int(items_.size());
    items_.push_back(std::move(best));
    // Issue #41: a translucent copy of a run kept before races this one.
    // It is a second game running alongside, which a phone may not have the
    // time for, so there it starts off and says so.
    sc_.ghost_row = int(items_.size());
    items_.push_back({"GHOST",
                      [c] { return std::string(ghost::mode_name(ghost::Mode(c->ta_ghost))); },
                      [c](int s) { if (s) c->ta_ghost = (c->ta_ghost + s + 3) % 3; },
                      ghost::default_mode() == ghost::Mode::Off
                          ? "a second game: may slow a phone"
                          : "race a past run of the stage"});
    add_players_row(&c->ta_two_players);
    sc_.start_row = int(items_.size());
    items_.push_back({"START", {}, [this](int) {
        const Request r = showcase_request();
        if (!valid(r)) return;
        close();
        if (hooks_.start_stage) hooks_.start_stage(r);
    }});
    sc_.back_row = int(items_.size());
    items_.push_back({"BACK", {}, [this](int) { const Page back = return_to_; set_page(back); }});
}

void Menu::build_keys() {
    items_.clear();
    Config* c = cfg_;
    if (!c) return;
    const int p = keys_player_;
    const bool pad = keys_pad_;
    for (const PadButton& pb : kPadButtons) {
        const std::string button = pb.name;
        items_.push_back({upper(button),
                          [this, c, p, pad, button] {
                              if (awaiting_ == button)
                                  return std::string(pad ? "PRESS A BUTTON" : "PRESS A KEY");
                              const auto& binds = pad ? (p == 0 ? c->pads : c->pads2)
                                                      : (p == 0 ? c->keys : c->keys2);
                              auto it = binds.find(button);
                              return it == binds.end() ? std::string("-") : upper(it->second);
                          },
                          [this, button](int step) {
                              // Enter starts the capture; left/right do nothing
                              // here, there is no list to step through.
                              if (step == 0) awaiting_ = button;
                          },
                          pb.does, /*read_only=*/false, /*pressed=*/true});
    }
    items_.push_back({"BACK", {}, [this](int) { set_page(Page::Controls); }});
}

void Menu::build_options() {
    items_.clear();
    items_.push_back({"VIDEO", {}, [this](int) { set_page(Page::Video); }});
    items_.push_back({"SOUND", {}, [this](int) { set_page(Page::Sound); }});
    items_.push_back({"INPUT", {}, [this](int) { set_page(Page::Input); }});
    items_.push_back({"EXTRAS", {}, [this](int) { set_page(Page::Extras); }});
    // Issue #43: how the game plays. A category of its own, as EXTRAS is as
    // long as a phone's screen allows.
    items_.push_back({"GAMEPLAY", {}, [this](int) { set_page(Page::Tether); }});
    items_.push_back({"BACK", {}, [this](int) { const Page back = return_to_; set_page(back); }});
}

void Menu::build_video() {
    items_.clear();
    Config* c = cfg_;
    if (!c) return;
    auto video = [this] { if (hooks_.apply_video) hooks_.apply_video(); };

    items_.push_back({"WIDESCREEN",
                      [c] { return on_off(c->widescreen); },
                      [c](int s) { if (s) c->widescreen = !c->widescreen; }});
    // No WIDE LOBBY any more: WORLD ENTRANCE is widescreen as it is (issue
    // #61), and the INTRODUCTION Eggman scene stays 4:3 on purpose.
    items_.push_back({"WIDE HUD",
                      [c] { return on_off(c->wide_hud); },
                      [c](int s) { if (s) c->wide_hud = !c->wide_hud; },
                      "score, time and rings at the screen edge"});
    items_.push_back({"ASPECT RATIO",
                      [c] { return std::string(kAspectNames[int(c->viewport.aspect)]); },
                      [c](int s) { if (s) cycle(c->viewport.aspect, 5, s); }});
    items_.push_back({"SCALING",
                      [c] { return std::string(kScaleNames[int(c->viewport.scale)]); },
                      [c](int s) { if (s) cycle(c->viewport.scale, 3, s); }});
    items_.push_back({"SMOOTHING",
                      [c] { return on_off(c->linear_filter); },
                      [c, video](int s) { if (s) { c->linear_filter = !c->linear_filter; video(); } }});
    items_.push_back({"SCREEN",
                      [c] { return std::string(kWindowNames[int(c->window_mode)]); },
                      [c, video](int s) { if (s) { cycle(c->window_mode, 3, s); video(); } }});
    items_.push_back({"V-SYNC",
                      [c] { return on_off(c->vsync); },
                      [c, video](int s) { if (s) { c->vsync = !c->vsync; video(); } }});
    // Issue #54: frame rates and emulation time in a corner, to see how fast
    // the game runs (on a phone especially).
    items_.push_back({"SHOW FPS",
                      [c] { return on_off(c->show_fps); },
                      [c](int s) { if (s) c->show_fps = !c->show_fps; },
                      "frame rate and emulation time in a corner"});
    // Issue #49: this menu's rows, letters and touch targets. AUTO is LARGE
    // on a phone or tablet and says so. It changes as soon as it is stepped,
    // so the row being stepped grows under the finger.
    items_.push_back({"MENU SIZE",
                      [c] {
                          static const char* names[] = {"NORMAL", "LARGE", "EXTRA LARGE"};
                          const std::string zoom = names[std::clamp(c->menu_zoom(), 0, ui::kMenuSizes - 1)];
                          return c->menu_size < 0 ? "AUTO (" + zoom + ")" : zoom;
                      },
                      [c](int s) {
                          if (!s) return;
                          // -1 AUTO, then 0 .. kMenuSizes-1.
                          int at = c->menu_size + 1;
                          cycle(at, ui::kMenuSizes + 1, s);
                          c->menu_size = at - 1;
                      }});
    items_.push_back({"BACK", {}, [this](int) { set_page(Page::Options); }});
}

void Menu::build_sound() {
    items_.clear();
    Config* c = cfg_;
    if (!c) return;
    items_.push_back({"SOUND",
                      [c] { return on_off(c->audio); },
                      [c](int s) { if (s) c->audio = !c->audio; }});
    items_.push_back({"VOLUME",
                      [c] { return std::to_string(c->volume); },
                      [c](int s) { c->volume = std::clamp(c->volume + s * 5, 0, 100); }});
    items_.push_back({"MUSIC",
                      [c] { return std::to_string(c->music_volume); },
                      [c](int s) { c->music_volume = std::clamp(c->music_volume + s * 5, 0, 100); }});
    items_.push_back({"EFFECTS",
                      [c] { return std::to_string(c->effects_volume); },
                      [c](int s) { c->effects_volume = std::clamp(c->effects_volume + s * 5, 0, 100); }});
    // Issue #48: the game's music, from the pause menu this way (the game is
    // held while it plays) as well as from the front end.
    items_.push_back({"MUSIC PLAYER", {}, [this](int) { set_page(Page::Music); }, "the game's songs"});
    items_.push_back({"BACK", {}, [this](int) { set_page(Page::Options); }});
}

void Menu::build_input() {
    items_.clear();
    Config* c = cfg_;
    if (!c) return;
    items_.push_back({"TOUCH PAD",
                      [c] { return std::string(kTouchNames[int(c->touch)]); },
                      [c](int s) { if (s) cycle(c->touch, 3, s); }});
    items_.push_back({"TOUCH LAYOUT",
                      [c] { return std::string(c->touch_sonic_cd ? "SONIC CD" : "CLASSIC"); },
                      [c](int s) { if (s) c->touch_sonic_cd = !c->touch_sonic_cd; },
                      "SONIC CD: one big jump button, as in its mobile version"});
    items_.push_back({"TOUCH OPACITY",
                      [c] { return std::to_string(c->touch_opacity) + "%"; },
                      [c](int s) { c->touch_opacity = std::clamp(c->touch_opacity + s * 5, 0, 100); },
                      "the on-screen controls at rest"});
    items_.push_back({"PRESSED OPACITY",
                      [c] { return std::to_string(c->touch_pressed_opacity) + "%"; },
                      [c](int s) { c->touch_pressed_opacity = std::clamp(c->touch_pressed_opacity + s * 5, 0, 100); },
                      "and while held"});
    items_.push_back({"TOUCH STICK",
                      [c] { return std::string(c->touch_stick ? "JOYSTICK" : "D-PAD"); },
                      [c](int s) { if (s) c->touch_stick = !c->touch_stick; },
                      "the on-screen directions"});
    // Issue #45. The keys for it are on the CONTROLS page.
    items_.push_back({"TOUCH FAST FORWARD",
                      [c] { return on_off(c->touch_ff); },
                      [c](int s) { if (s) c->touch_ff = !c->touch_ff; },
                      "an on-screen button for it"});
    items_.push_back({"FAST FORWARD",
                      [c] { return std::string(c->ff_mode == FastForwardMode::Toggle ? "TOGGLE" : "HOLD"); },
                      [c](int s) { if (s) cycle(c->ff_mode, 2, s); },
                      "not in TIME ATTACK"});
    items_.push_back({"FAST FORWARD SPEED",
                      [c] {
                          return std::string(c->ff_speed == FastForwardSpeed::X2   ? "2X"
                                             : c->ff_speed == FastForwardSpeed::X4 ? "4X"
                                                                                   : "MAX");
                      },
                      [c](int s) { if (s) cycle(c->ff_speed, 3, s); }});
    items_.push_back({"6-BUTTON PAD",
                      [c] { return on_off(c->six_button); },
                      [c](int s) { if (s) c->six_button = !c->six_button; }});
    items_.push_back({"BACK", {}, [this](int) { set_page(Page::Options); }});
}

void Menu::build_extras() {
    items_.clear();
    Config* c = cfg_;
    if (!c) return;
    items_.push_back({"CHEATS", [c] { return c->cheats.empty() && c->cheat_codes.empty() ? std::string("OFF") : std::string("ON"); },
                      [this](int step) { if (step == 0) set_page(Page::Cheats); }});
    items_.back().pressed = true;
    // Issue #44: a page of its own, as CHEATS is, so this one stays short.
    items_.push_back({"LOBBY SHORTCUTS", [c] { return on_off(c->lobby_shortcuts); },
                      [this](int step) { if (step == 0) set_page(Page::Lobby); },
                      "pick your partner and the time of day"});
    items_.back().pressed = true;
    // Issue #47: the timer, splits and input display, on a page of their own.
    items_.push_back({"SPEEDRUN", [c] { return on_off(c->sr_timer || c->sr_splits || c->sr_input); },
                      [this](int step) { if (step == 0) set_page(Page::Speedrun); },
                      "timer, splits and input display"});
    items_.back().pressed = true;
    // Issue #42: the mod packs, on a page of their own (with the Android
    // MODS FOLDER row, which used to be here).
    items_.push_back({"MODS",
                      [this] {
                          const std::vector<mod_packs::Pack>* packs = hooks_.mod_packs ? hooks_.mod_packs() : nullptr;
                          int on = 0;
                          if (packs)
                              for (const auto& p : *packs) on += p.enabled ? 1 : 0;
                          return on ? std::to_string(on) + " ON" : std::string("NONE");
                      },
                      [this](int step) { if (step == 0) set_page(Page::Mods); }});
    items_.back().pressed = true;
    // Issue #8: this menu's own look. Both are made from the ROM, so they are
    // the game's rather than ours; PIXEL and PLAIN are the earlier look. Here
    // rather than under VIDEO, which is as long as a phone's screen allows.
    items_.push_back({"MENU FONT",
                      [c] { return std::string(c->menu_game_font ? "GAME" : "PIXEL"); },
                      [c](int s) { if (s) c->menu_game_font = !c->menu_game_font; },
                      "the game's own letters"});
    // Issue #37: CYCLE shows the zones in turn, a zone's name only that one.
    items_.push_back({"MENU BACKGROUND",
                      [c] {
                          if (!c->menu_backdrop_on()) return std::string("PLAIN");
                          const int zone = c->menu_backdrop_zone();
                          return zone < 0 ? std::string("CYCLE") : std::string(backdrop_specs()[size_t(zone)].name);
                      },
                      [c](int s) {
                          if (!s) return;
                          std::vector<std::string> values{"Cycle"};
                          for (int i : backdrop_menu_order()) values.push_back(backdrop_specs()[size_t(i)].zone);
                          values.push_back("Plain");
                          int at = 0;
                          const std::string now = parse_menu_background(c->menu_background);
                          for (size_t i = 0; i < values.size(); ++i)
                              if (values[i] == now) at = int(i);
                          cycle(at, int(values.size()), s);
                          c->menu_background = values[size_t(at)];
                      },
                      "the game's zones behind it"});
    items_.push_back({"DUMP GRAPHICS",
                      [c] { return on_off(c->dump_graphics); },
                      [c](int s) { if (s) c->dump_graphics = !c->dump_graphics; },
                      "save the graphics drawn to Mods/Textures/Dump"});
    if (hooks_.export_sprites)
        items_.push_back({"EXPORT SPRITES", {}, [this](int step) { if (step == 0) hooks_.export_sprites(); },
                          "every frame of the characters, to Mods/Textures/Sheets"});
    items_.push_back({"AWARDS",
                      [c] { return on_off(c->achievements); },
                      [c](int s) { if (s) c->achievements = !c->achievements; }});
    Item reset{"RESET AWARDS",
               [this] { return confirm_reset_ ? std::string("SURE? PRESS AGAIN")
                                              : std::string("..."); },
               [this](int step) {
                   // A value row, so that the confirmation can be shown
                   // in place; left and right must not trip it.
                   if (step != 0) return;
                   if (!confirm_reset_) { confirm_reset_ = true; return; }
                   confirm_reset_ = false;
                   if (hooks_.reset_awards) hooks_.reset_awards();
               }};
    reset.pressed = true;
    items_.push_back(std::move(reset));
    items_.push_back({"BACK", {}, [this](int) { set_page(Page::Options); }});
}

// Issue #20: the built-in cheats, and whether the config adds codes of its
// own. Any of them on stops awards and time attack records.
void Menu::build_cheats() {
    items_.clear();
    Config* c = cfg_;
    if (!c) return;
    for (const auto& cheat : cheats::builtin()) {
        const std::string key = cheat.key;
        items_.push_back({cheat.name,
                          [c, key] { return on_off(c->cheats.count(key) != 0); },
                          [c, key](int s) {
                              if (!s) return;
                              if (c->cheats.count(key)) c->cheats.erase(key); else c->cheats.insert(key);
                          },
                          cheat.note});
    }
    Item own{"CONFIG CODES", [c] {
                 std::vector<cheats::Write> w;
                 return c->cheat_codes.empty() ? std::string("NONE")
                        : cheats::parse_codes(c->cheat_codes, w) ? std::string("ON") : std::string("INVALID");
             },
             {}, "Action Replay codes in Config/chaotix.ini, [Cheats] Codes"};
    own.read_only = true;
    items_.push_back(std::move(own));
    Item note{"NO AWARDS OR RECORDS WHILE ON", {}, {}};
    note.read_only = true;
    items_.push_back(std::move(note));
    items_.push_back({"BACK", {}, [this](int) { set_page(Page::Extras); }});
}

// Issue #44 (runtime/lobby.h): in story mode, the partner and the time of
// day, put in place each time the game starts WORLD ENTRANCE or one of the
// five attractions. CATCHER and GAME leave the choice to the game. Reachable
// from the pause menu too, so it can be changed while walking the lobby.
void Menu::build_lobby() {
    using namespace stage_select;
    items_.clear();
    Config* c = cfg_;
    if (!c) return;
    std::vector<std::string> names = characters_;
    if (names.size() != size_t(kCharacterCount)) names.assign(std::begin(kCharacters), std::end(kCharacters));
    items_.push_back({"SHORTCUTS", [c] { return on_off(c->lobby_shortcuts); },
                      [c](int s) { if (s) c->lobby_shortcuts = !c->lobby_shortcuts; },
                      "story mode, not TIME ATTACK"});
    // CATCHER (-1), the characters, then NONE (lobby::kPartnerNone, issue
    // #55: the attractions played SOLO). The gap in the game's table (slot 1)
    // is skipped while it has only the placeholder for a name, as TIME
    // ATTACK does.
    items_.push_back({"PARTNER",
                      [c, names] {
                          if (c->lobby_partner == lobby::kPartnerNone) return std::string("NONE");
                          return c->lobby_partner < 0 || c->lobby_partner >= kCharacterCount
                                     ? std::string("CATCHER")
                                     : names[size_t(c->lobby_partner)];
                      },
                      [c, names](int s) {
                          if (!s) return;
                          // As positions: 0 CATCHER, 1 .. kCharacterCount the
                          // characters, kCharacterCount + 1 NONE.
                          const int count = kCharacterCount + 2;
                          int at = c->lobby_partner == lobby::kPartnerNone ? count - 1
                                   : c->lobby_partner < 0 || c->lobby_partner >= kCharacterCount
                                       ? 0
                                       : c->lobby_partner + 1;
                          for (int i = 0; i < count; ++i) {
                              at = (at + (s > 0 ? 1 : -1) + count) % count;
                              if (at == 0 || at == count - 1 || !is_placeholder_name(names[size_t(at - 1)])) break;
                          }
                          c->lobby_partner = at == 0 ? -1 : at == count - 1 ? lobby::kPartnerNone : at - 1;
                      },
                      "NONE: alone in the attractions (SOLO)"});
    items_.push_back({"TIME OF DAY",
                      [c] {
                          return c->lobby_time < 0 || c->lobby_time >= kTimeCount ? std::string("GAME")
                                                                                 : std::string(kTimes[c->lobby_time]);
                      },
                      [c](int s) {
                          if (!s) return;
                          int t = c->lobby_time + 1 + s;  // -1..3 as 0..4
                          t = (t + kTimeCount + 1) % (kTimeCount + 1);
                          c->lobby_time = t - 1;
                      },
                      "GAME: it moves on every second level"});
    items_.push_back({"BACK", {}, [this](int) { set_page(Page::Extras); }});
}

// OPTIONS > GAMEPLAY. Issue #43 (runtime/tether.h, docs/RING_TETHER.md): the
// Combi Ring's strength and what the partner does, in the five attractions'
// levels. Off in TIME ATTACK. Reachable from the pause menu too, so it can be
// tried out in a level; a change applies from the next frame.
void Menu::build_tether() {
    items_.clear();
    Config* c = cfg_;
    if (!c) return;
    auto name = [](const char* const* names, int count, int v) {
        return std::string(names[std::clamp(v, 0, count - 1)]);
    };
    items_.push_back({"TETHER",
                      [c, name] { return name(tether::kStrengthNames, tether::kStrengthCount, c->tether_strength); },
                      [c](int s) { if (s) c->tether_strength = (c->tether_strength + s + tether::kStrengthCount) % tether::kStrengthCount; },
                      "how hard the Combi Ring pulls"});
    items_.push_back({"PARTNER",
                      [c, name] { return name(tether::kPartnerNames, tether::kPartnerCount, c->tether_partner); },
                      [c](int s) { if (s) c->tether_partner = (c->tether_partner + s + tether::kPartnerCount) % tether::kPartnerCount; },
                      "1 player. SOLO: hold CALL to bring back"});
    items_.push_back({"BACK", {}, [this](int) { set_page(Page::Options); }});
}

// Issue #47 (frontend/speedrun.h, docs/SPEEDRUN.md): the overlays, where they
// go, and starting or resetting a run by hand -- the only way on a phone,
// which has no keys for it. Reachable from the pause menu too.
void Menu::build_speedrun() {
    items_.clear();
    Config* c = cfg_;
    if (!c) return;
    auto toggle = [this](const char* label, bool Config::*field, const char* note) {
        Config* cc = cfg_;
        items_.push_back({label, [cc, field] { return on_off(cc->*field); },
                          [cc, field](int s) { if (s) cc->*field = !(cc->*field); }, note});
    };
    toggle("TIMER", &Config::sr_timer, "real time and game time of the run");
    toggle("SPLITS", &Config::sr_splits, "each level finished, against your best");
    items_.push_back({"COMPARE", [c] { return std::string(c->sr_compare_game ? "GAME TIME" : "REAL TIME"); },
                      [c](int s) { if (s) c->sr_compare_game = !c->sr_compare_game; },
                      "what the splits and the best go by"});
    toggle("INPUT DISPLAY", &Config::sr_input, "the pads' buttons");
    auto corner_row = [this](const char* label, std::string Config::*field, speedrun::Corner fallback) {
        Config* cc = cfg_;
        static const char* kNames[] = {"TOP LEFT", "TOP RIGHT", "BOTTOM LEFT", "BOTTOM RIGHT"};
        items_.push_back({label,
                          [cc, field, fallback] { return std::string(kNames[int(speedrun::parse_corner(cc->*field, fallback))]); },
                          [cc, field, fallback](int s) {
                              if (!s) return;
                              speedrun::Corner k = speedrun::parse_corner(cc->*field, fallback);
                              cycle(k, 4, s);
                              cc->*field = speedrun::corner_name(k);
                          }});
    };
    corner_row("TIMER CORNER", &Config::sr_timer_corner, speedrun::Corner::TopRight);
    corner_row("INPUT CORNER", &Config::sr_input_corner, speedrun::Corner::BottomLeft);
    items_.push_back({"OPACITY", [c] { return std::to_string(c->sr_opacity) + "%"; },
                      [c](int s) { if (s) c->sr_opacity = std::clamp(c->sr_opacity + 5 * s, 10, 100); }});
    items_.push_back({"START RUN", {}, [this](int step) {
        if (step != 0) return;
        close();
        if (hooks_.speedrun_start) hooks_.speedrun_start();
    }, "or START on DATA LOAD starts one"});
    items_.push_back({"RESET RUN", {}, [this](int step) {
        if (step != 0) return;
        close();
        if (hooks_.speedrun_reset) hooks_.speedrun_reset();
    }, "or going back to the title"});
    auto key_row = [this](const char* label, const char* capture, std::string Config::*field) {
        Config* cc = cfg_;
        items_.push_back({label,
                          [this, cc, capture, field] {
                              if (awaiting_ == capture) return std::string("PRESS A KEY");
                              const std::string& v = cc->*field;
                              return v.empty() ? std::string("-") : upper(v);
                          },
                          [this, capture](int step) { if (step == 0) awaiting_ = capture; },
                          {}, /*read_only=*/false, /*pressed=*/true});
    };
    key_row("START RUN KEY", kRunStartCapture, &Config::sr_start_key);
    key_row("RESET RUN KEY", kRunResetCapture, &Config::sr_reset_key);
    items_.push_back({"BACK", {}, [this](int) { set_page(Page::Extras); }});
}

// Issue #46 (net/netplay.h, docs/NETPLAY.md): host or join a game, the
// address and port, the input delay, how it is going, and leaving.
void Menu::build_netplay() {
    items_.clear();
    Config* c = cfg_;
    if (!c) return;
    items_.push_back({"HOST A GAME", {}, [this](int step) {
        if (step == 0 && hooks_.netplay_host) hooks_.netplay_host();
    }, "you are player 1; your friend joins you"});
    items_.push_back({"JOIN A GAME", {}, [this](int step) {
        if (step == 0 && hooks_.netplay_join) hooks_.netplay_join();
    }, "you are player 2, at the address below"});
    items_.push_back({"ADDRESS",
                      [this, c] {
                          if (editing_ == &c->netplay_address) return upper(c->netplay_address) + "_";
                          return c->netplay_address.empty() ? std::string("TYPE ONE") : upper(c->netplay_address);
                      },
                      [this, c](int step) {
                          if (step == 0)
                              begin_edit(&c->netplay_address, "0123456789.:abcdefghijklmnopqrstuvwxyz-[]", 63, {});
                      },
                      "the host's address:port", false, /*pressed=*/true});
    items_.push_back({"PORT",
                      [this, c] {
                          if (editing_ == &port_text_) return port_text_ + "_";
                          return std::to_string(c->netplay_port);
                      },
                      [this, c](int step) {
                          if (step == 0) {
                              port_text_ = std::to_string(c->netplay_port);
                              begin_edit(&port_text_, "0123456789", 5, [this, c] {
                                  const int p = std::atoi(port_text_.c_str());
                                  if (p > 0 && p < 65536) c->netplay_port = p;
                              });
                          } else {
                              c->netplay_port = std::clamp(c->netplay_port + step, 1, 65535);
                          }
                      },
                      "the UDP port a host waits on", false, /*pressed=*/true});
    items_.push_back({"INPUT DELAY",
                      [c] { return std::to_string(c->netplay_delay) + (c->netplay_delay == 1 ? " FRAME" : " FRAMES"); },
                      [c](int s) { if (s) c->netplay_delay = std::clamp(c->netplay_delay + s, 0, 4); },
                      "more: fewer corrections, later controls"});
    items_.push_back({"STATUS", [this] { return hooks_.netplay_status ? hooks_.netplay_status() : std::string("OFF"); },
                      [](int) {}, "", /*read_only=*/true});
    items_.push_back({"DISCONNECT", {}, [this](int step) {
        if (step == 0 && hooks_.netplay_leave) hooks_.netplay_leave();
    }});
    items_.push_back({"BACK", {}, [this](int) { back_out(); }});
}

void Menu::begin_edit(std::string* target, const char* allowed, size_t max_len, std::function<void()> done) {
    editing_ = target;
    edit_before_ = *target;
    edit_allowed_ = allowed;
    edit_max_ = max_len;
    edit_done_ = std::move(done);
}

void Menu::end_edit(bool keep) {
    if (!editing_) return;
    std::string* target = editing_;
    editing_ = nullptr;
    if (!keep) {
        *target = edit_before_;
        return;
    }
    if (edit_done_) edit_done_();
}

bool Menu::on_text(const char* text) {
    if (!editing_ || !text) return false;
    for (const char* p = text; *p; ++p) {
        char ch = *p;
        if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
        if (!std::strchr(edit_allowed_, ch) || editing_->size() >= edit_max_) continue;
        editing_->push_back(ch);
    }
    return true;
}

bool Menu::edit_key(int key) {
    if (!editing_) return true;
    std::string& t = *editing_;
    const std::string allowed = edit_allowed_;
    switch (key) {
    case 0: end_edit(true); break;
    case 1: end_edit(false); break;
    case 2: if (!t.empty()) t.pop_back(); break;
    case 3: case 4: {
        // The last character steps through the ones allowed, as on a
        // console's name entry: all a pad has to type with.
        if (t.empty() || allowed.empty()) {
            if (!allowed.empty() && t.size() < edit_max_) t.push_back(allowed[0]);
            break;
        }
        size_t i = allowed.find(t.back());
        if (i == std::string::npos) i = 0;
        i = (i + (key == 3 ? 1 : allowed.size() - 1)) % allowed.size();
        t.back() = allowed[i];
        break;
    }
    case 5:
        if (!allowed.empty() && t.size() < edit_max_) t.push_back(t.empty() ? allowed[0] : t.back());
        break;
    }
    return true;
}

void Menu::refresh() {
    const int keep = selected_, top = scroll_;
    info_lines_.clear();
    if (page_ == Page::Mods) build_mods();
    else if (page_ == Page::ModInfo) build_mod_info();
    selected_ = items_.empty() ? 0 : std::clamp(keep, 0, int(items_.size()) - 1);
    scroll_ = top;
}

namespace {

// What a pack holds, in words.
std::string pack_contents(const mod_packs::Pack& p) {
    std::vector<std::string> what;
    if (p.textures) what.push_back("graphics");
    if (p.characters) what.push_back("characters");
    if (p.music) what.push_back("music");
    if (p.patches) what.push_back("ROM patches");
    if (p.touch) what.push_back("touch controls");
    std::string s;
    for (const auto& w : what) s += (s.empty() ? "" : ", ") + w;
    return s.empty() ? std::string("nothing yet") : s;
}

} // namespace

// Issue #42: the mod packs, highest priority first. Left/right turns the
// highlighted one on or off; Enter (or a second tap) opens its page, with
// its description and MOVE UP / MOVE DOWN. Leaving the page after a change
// has the app apply it (leave_mods), as RELOAD MODS does at once.
void Menu::build_mods() {
    items_.clear();
    if (std::vector<mod_packs::Pack>* list = packs()) {
        for (size_t i = 0; i < list->size(); ++i) {
            const mod_packs::Pack& p = (*list)[i];
            Item it{upper(p.title()),
                    [this, i] {
                        const std::vector<mod_packs::Pack>* l = packs();
                        return l && i < l->size() && (*l)[i].enabled ? std::string("ON") : std::string("OFF");
                    },
                    [this, i](int step) {
                        std::vector<mod_packs::Pack>* l = packs();
                        if (!l || i >= l->size()) return;
                        if (step == 0) {
                            mod_index_ = i;
                            set_page(Page::ModInfo);
                            return;
                        }
                        if (cfg_) mod_packs::set_enabled(cfg_->mod_order, *l, i, !(*l)[i].enabled);
                        mods_dirty_ = true;
                        if (hooks_.mods_changed) hooks_.mods_changed();
                        refresh();
                    },
                    p.loose ? std::string("your own files") : p.too_new ? std::string("NEEDS A NEWER GAME") : p.info.version};
            // Enter opens the pack's page rather than switching it.
            it.pressed = true;
            items_.push_back(std::move(it));
        }
    }
    const bool restart = hooks_.mods_need_restart && hooks_.mods_need_restart();
    items_.push_back({"RELOAD MODS", {},
                      [this](int step) {
                          if (step != 0) return;
                          mods_dirty_ = false;
                          if (hooks_.reload_mods) hooks_.reload_mods();
                          refresh();
                      },
                      restart ? "patches and stats: restart the game" : "read the Mods folder again"});
    const std::vector<mod_packs::Pack>* list = packs();
    if (!list || list->empty()) {
        Item none{"NO MODS YET", {}, {}, "a folder per mod in Mods, see README"};
        none.read_only = true;
        items_.push_back(std::move(none));
    }
    if (hooks_.share_mods_folder) {
        Item folder{"MODS FOLDER",
                    [this] { return hooks_.mods_folder && !hooks_.mods_folder().empty() ? std::string("SHARED")
                                                                                       : std::string("ALLOW ACCESS"); },
                    [this](int step) { if (step == 0 && hooks_.share_mods_folder) hooks_.share_mods_folder(); },
                    "ChaotixRecompiled/Mods in your phone's storage"};
        folder.pressed = true;
        items_.push_back(std::move(folder));
    }
    items_.push_back({"BACK", {}, [this](int) { set_page(Page::Extras); }});
}

// One pack: what mod.ini says about it (drawn under the heading, with its
// preview), and switching and moving it.
void Menu::build_mod_info() {
    items_.clear();
    std::vector<mod_packs::Pack>* list = packs();
    if (!list || mod_index_ >= list->size()) {
        items_.push_back({"BACK", {}, [this](int) { set_page(Page::Mods); }});
        return;
    }
    const mod_packs::Pack& p = (*list)[mod_index_];
    if (p.loose) {
        info_lines_.push_back("Files in the Mods folder's own Textures, Characters, Music, Patches and Touch "
                              "folders. They come before every pack.");
    } else {
        std::string by = p.info.version.empty() ? std::string() : "Version " + p.info.version;
        if (!p.info.author.empty()) by += (by.empty() ? "By " : ", by ") + p.info.author;
        if (!by.empty()) info_lines_.push_back(by);
        std::string d = p.info.description;
        for (size_t at; (at = d.find('\n')) != std::string::npos; d.erase(0, at + 1)) info_lines_.push_back(d.substr(0, at));
        if (!d.empty()) info_lines_.push_back(d);
        if (!p.info.url.empty()) info_lines_.push_back(p.info.url);
        if (!p.has_ini) info_lines_.push_back("(no mod.ini: named after its folder, " + p.id + ")");
    }
    info_lines_.push_back("Has: " + pack_contents(p) + ". Place " + std::to_string(mod_index_ + 1) + " of " +
                          std::to_string(list->size()) + ", the first winning.");
    if (p.too_new) info_lines_.push_back("Made for version " + p.info.min_version + " or later; it may not work.");
    if (p.changes_game())
        info_lines_.push_back("Patches and character stats apply when the game is started again.");

    items_.push_back({"ENABLED",
                      [this] {
                          const std::vector<mod_packs::Pack>* l = packs();
                          return l && mod_index_ < l->size() && (*l)[mod_index_].enabled ? std::string("ON")
                                                                                         : std::string("OFF");
                      },
                      [this](int s) {
                          std::vector<mod_packs::Pack>* l = packs();
                          if (!s || !l || mod_index_ >= l->size()) return;
                          if (cfg_) mod_packs::set_enabled(cfg_->mod_order, *l, mod_index_, !(*l)[mod_index_].enabled);
                          mods_dirty_ = true;
                          if (hooks_.mods_changed) hooks_.mods_changed();
                      }});
    if (!p.loose) {
        auto mover = [this](int delta) {
            return [this, delta](int step) {
                std::vector<mod_packs::Pack>* l = packs();
                if (step != 0 || !l || !cfg_) return;
                if (mod_packs::move(cfg_->mod_order, *l, mod_index_, delta)) {
                    mod_index_ = size_t(long(mod_index_) + delta);
                    mods_dirty_ = true;
                    if (hooks_.mods_changed) hooks_.mods_changed();
                    refresh();  // its place, in the text above
                }
            };
        };
        items_.push_back({"MOVE UP", {}, mover(-1), "wins over the packs below it"});
        items_.push_back({"MOVE DOWN", {}, mover(+1)});
    }
    items_.push_back({"BACK", {}, [this](int) { set_page(Page::Mods); }});
}

// Issue #48 (frontend/jukebox.h): the controls, then the SOUND TEST's songs
// by number and name. The app plays them on a machine of its own while this
// page is open, and holds the game meanwhile.
void Menu::build_music() {
    items_.clear();
    Config* c = cfg_;
    const std::vector<jukebox::Track> tracks = tracks_;
    auto status = [this] { return hooks_.music_status ? hooks_.music_status() : jukebox::Status{}; };
    auto name_of = [tracks](uint8_t id) {
        for (const auto& t : tracks)
            if (t.id == id) return jukebox::track_label(t);
        char buf[8];
        std::snprintf(buf, sizeof buf, "%02d", int(id));
        return std::string(buf);
    };
    Item now{"NOW PLAYING",
             [status, name_of] {
                 const jukebox::Status s = status();
                 if (s.failed) return std::string("NOT AVAILABLE");
                 if (!s.ready) return std::string("STARTING...");
                 if (!s.id) return std::string("STOPPED");
                 return name_of(s.id) + "  " + jukebox::format_time(s.seconds) + (s.fading ? "  FADING" : "");
             },
             [this, tracks, status](int step) {
                 // Enter here plays or stops, as a player's one button does.
                 if (step != 0 || tracks.empty()) return;
                 if (status().id) { if (hooks_.music_stop) hooks_.music_stop(); }
                 else if (hooks_.music_play) hooks_.music_play(status().last ? status().last : tracks.front().id);
             }};
    now.pressed = true;
    items_.push_back(std::move(now));
    // PLAY: the track chosen last, or the first; nothing while one plays.
    items_.push_back({"PLAY", {}, [this, tracks, status](int step) {
        if (step != 0 || !hooks_.music_play || tracks.empty() || status().id) return;
        const jukebox::Status s = status();
        hooks_.music_play(s.last ? s.last : tracks.front().id);
    }});
    items_.push_back({"STOP", {}, [this](int step) { if (step == 0 && hooks_.music_stop) hooks_.music_stop(); }});
    items_.push_back({"NEXT", {}, [this](int step) { if (step == 0 && hooks_.music_step) hooks_.music_step(+1); }});
    items_.push_back({"PREVIOUS", {}, [this](int step) { if (step == 0 && hooks_.music_step) hooks_.music_step(-1); }});
    items_.push_back({"FADE OUT", {}, [this](int step) { if (step == 0 && hooks_.music_fade) hooks_.music_fade(); }});
    if (c) {
        static const char* const kRepeat[] = {"LOOP TRACK", "PLAY ONCE", "PLAY ALL"};
        items_.push_back({"REPEAT", [c] { return std::string(kRepeat[std::clamp(c->music_repeat, 0, 2)]); },
                          [c](int s) { if (s) cycle(c->music_repeat, 3, s); }});
        static const int kFade[] = {0, 60, 90, 120, 180, 300};
        constexpr int kFades = int(sizeof kFade / sizeof kFade[0]);
        items_.push_back({"FADE OUT AFTER",
                          [c] { return c->music_fade_after > 0 ? jukebox::format_time(c->music_fade_after) : std::string("NEVER"); },
                          [c](int s) {
                              if (!s) return;
                              int at = 0;
                              for (int i = 0; i < kFades; ++i)
                                  if (kFade[i] == c->music_fade_after) at = i;
                              cycle(at, kFades, s);
                              c->music_fade_after = kFade[at];
                          },
                          "once and all"});
    }
    // The game's own SOUND TEST (issue #102), with its keyboards and level
    // meters: the game itself is taken there, as START GAME hands it over.
    if (hooks_.game_sound_test)
        items_.push_back({"GAME'S SOUND TEST", {}, [this](int) {
                              close();
                              hooks_.game_sound_test();
                          },
                          "the original screen"});
    for (const jukebox::Track& t : tracks) {
        const uint8_t id = t.id;
        Item it{jukebox::track_label(t),
                [status, id] {
                    const jukebox::Status s = status();
                    return s.id == id ? std::string("PLAYING") : std::string();
                },
                [this, id](int step) {
                    if (step != 0 || !hooks_.music_play) return;
                    hooks_.music_play(id);
                },
                hooks_.music_replaced && hooks_.music_replaced(id) ? "REPLACED BY MOD" : ""};
        it.pressed = true;  // Enter plays it; left and right do nothing
        items_.push_back(std::move(it));
    }
    items_.push_back({"BACK", {}, [this](int) { set_page(music_back_); }});
}

void Menu::move(int delta) {
    confirm_reset_ = false;
    if (items_.empty()) return;
    const int n = int(items_.size());
    const int step = delta < 0 ? -1 : 1;
    // A row that only shows something (a record) is stepped over rather than
    // stopped on, so it does not sit in the way of the entries below it.
    for (int moved = 0; moved < n; ++moved) {
        selected_ = (selected_ + step + n) % n;
        if (!items_[size_t(selected_)].read_only) break;
    }
    anim_ = 0;
}

void Menu::activate(int step) {
    if (selected_ < 0 || selected_ >= int(items_.size())) return;
    const Item& it = items_[size_t(selected_)];
    // An action ignores left/right. A value steps forward on "activate", so
    // Enter, the pad's confirm button and a tap all change it, unless it is a
    // row that is pressed rather than stepped.
    if (!it.value && step != 0) return;
    if (it.value && step == 0 && !it.pressed && !it.read_only) step = +1;
    if (it.act) it.act(step);
}

void Menu::back_out() {
    if (page_ == Page::Front) {
        close();
        if (hooks_.front_closed) {
            hooks_.front_closed();
        }
        return;
    }
    if (page_ == Page::Music) {
        set_page(music_back_);
    } else if (is_row_page(page_)) {
        close();
    } else {
        const Page up = parent_of(page_);
        set_page(up != Page::Closed ? up : return_to_);
    }
}

bool Menu::on_key(SDL_Keycode key) {
    if (!open()) return false;
    if (editing_) {
        switch (key) {
        case SDLK_RETURN: case SDLK_KP_ENTER: return edit_key(0);
        case SDLK_ESCAPE: return edit_key(1);
        case SDLK_BACKSPACE: case SDLK_LEFT: return edit_key(2);
        case SDLK_UP: return edit_key(3);
        case SDLK_DOWN: return edit_key(4);
        case SDLK_RIGHT: return edit_key(5);
        default: return true;  // typed characters come as text
        }
    }
    if (!awaiting_.empty()) {
        // Capturing a binding. Escape abandons it; on a gamepad page a key is
        // simply the wrong kind of answer and is ignored.
        if (key == SDLK_ESCAPE) awaiting_.clear();
        else if (std::string* target = capture_target(awaiting_)) {
            const char* name = SDL_GetScancodeName(SDL_GetScancodeFromKey(key, nullptr));
            if (name && *name) {
                *target = name;
                awaiting_.clear();
            }
        } else if (!keys_pad_ && cfg_) {
            const SDL_Scancode sc = SDL_GetScancodeFromKey(key, nullptr);
            const char* name = SDL_GetScancodeName(sc);
            if (name && *name) {
                auto& binds = keys_player_ == 0 ? cfg_->keys : cfg_->keys2;
                binds[awaiting_] = name;
                awaiting_.clear();
            }
        }
        return true;
    }
    if (waiting_for_title()) {
        // Before PUSH START: the keys that would drive the entries skip to
        // the title instead; Escape closes the front end as it always does.
        switch (key) {
        case SDLK_LEFT: case SDLK_RIGHT: case SDLK_UP: case SDLK_DOWN:
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
            skip_intro();
            return true;
        default:
            break;
        }
    }
    switch (key) {
    case SDLK_ESCAPE:
        back_out();
        return true;
    case SDLK_LEFT:
        if (is_row_page(page_)) move(-1); else activate(-1);
        return true;
    case SDLK_RIGHT:
        if (is_row_page(page_)) move(+1); else activate(+1);
        return true;
    case SDLK_UP:
        if (page_ == Page::Achievements) --scroll_; else if (is_list_page(page_)) move(-1);
        return true;
    case SDLK_DOWN:
        if (page_ == Page::Achievements) ++scroll_; else if (is_list_page(page_)) move(+1);
        return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
        if (page_ == Page::Achievements) set_page(return_to_); else activate(0);
        return true;
    default:
        return true;  // the menu swallows everything else while it is open
    }
}

bool Menu::on_pad(Uint8 button) {
    if (!open()) return false;
    if (editing_) {
        switch (button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: return edit_key(0);
        case SDL_GAMEPAD_BUTTON_EAST: return edit_key(1);
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return edit_key(2);
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return edit_key(3);
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return edit_key(4);
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return edit_key(5);
        default: return true;
        }
    }
    if (!awaiting_.empty()) {
        // The mirror of the keyboard case: only a gamepad page takes a button.
        // The fast-forward key wants a key; the pad's back button gives up.
        if (capture_target(awaiting_)) {
            if (button == SDL_GAMEPAD_BUTTON_EAST) awaiting_.clear();
        } else if (keys_pad_ && cfg_) {
            const char* name = SDL_GetGamepadStringForButton(SDL_GamepadButton(button));
            if (name && *name) {
                auto& binds = keys_player_ == 0 ? cfg_->pads : cfg_->pads2;
                binds[awaiting_] = name;
                awaiting_.clear();
            }
        }
        return true;
    }
    // The button that opened the menu closes it again, the way Escape does.
    if (cfg_ && SDL_GetGamepadButtonFromString(cfg_->menu_button.c_str()) == SDL_GamepadButton(button)) {
        back_out();
        return true;
    }
    if (waiting_for_title()) {
        switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        case SDL_GAMEPAD_BUTTON_DPAD_UP: case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        case SDL_GAMEPAD_BUTTON_SOUTH:
            skip_intro();
            return true;
        default:
            break;
        }
    }
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        if (is_row_page(page_)) move(-1); else activate(-1);
        return true;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        if (is_row_page(page_)) move(+1); else activate(+1);
        return true;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        if (page_ == Page::Achievements) --scroll_; else if (is_list_page(page_)) move(-1);
        return true;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        if (page_ == Page::Achievements) ++scroll_; else if (is_list_page(page_)) move(+1);
        return true;
    case SDL_GAMEPAD_BUTTON_SOUTH:
        if (page_ == Page::Achievements) set_page(return_to_); else activate(0);
        return true;
    case SDL_GAMEPAD_BUTTON_EAST:
        // The pad's "back" button backs out exactly like Escape, so closing
        // the front menu with it hands the game over too.
        back_out();
        return true;
    default:
        return true;
    }
}

bool Menu::on_touch_down(float x, float y) {
    if (!open()) return false;
    // A tap anywhere ends the typing, keeping the text (the on-screen
    // keyboard's own return key does too).
    if (editing_) end_edit(true);
    drag_y_ = y;
    drag_total_ = 0;
    for (const Hit& h : hits_) {
        if (x < h.r.x || x > h.r.x + h.r.w || y < h.r.y || y > h.r.y + h.r.h) continue;
        // The markers of a list longer than the screen step through it.
        if (h.index == kScrollUp || h.index == kScrollDown) {
            // AWARDS scrolls its cards; the lists move their selection.
            if (page_ == Page::Achievements) scroll_ += h.index == kScrollUp ? -1 : +1;
            else move(h.index == kScrollUp ? -1 : +1);
            drag_total_ = 1e9f;
            return true;
        }
        // The showcase's chips, arrows and START act on the first tap.
        if (h.tap) {
            if (h.index >= 0 && h.index != selected_) {
                selected_ = h.index;
                anim_ = 0;
            }
            const std::function<void()> act = h.tap;  // it may rebuild the page
            act();
            drag_total_ = 1e9f;
            return true;
        }
        // A tap on an entry selects it; a second tap acts on it, which keeps
        // one finger enough to drive the whole menu. On a value, the "<"
        // arrow steps it back and anywhere else on the row steps it forward.
        if (h.index == selected_) {
            const bool back = h.back_w > 0 && x >= h.back_x && x < h.back_x + h.back_w;
            activate(back ? -1 : 0);
        } else {
            selected_ = h.index;
            anim_ = 0;
        }
        drag_total_ = 1e9f;  // this touch is spoken for
        return true;
    }
    return true;
}

bool Menu::on_touch_up() {
    if (!open()) return false;
    if (page_ == Page::Achievements && drag_total_ < 1e8f) set_page(return_to_);
    return true;
}

// Issue #3: the front end is drawn on the game's own title screen, which fills
// the window as the game does in play; the app says where it is.
void Menu::set_picture(const SDL_FRect& image, int fb_width, int fb_height, int fb_extra) {
    picture_ = image;
    picture_fb_w_ = std::max(1, fb_width);
    picture_fb_h_ = std::max(1, fb_height);
    picture_extra_ = std::max(0, fb_extra);
}

SDL_FRect Menu::push_start_box(const SDL_FRect& image, int fb_width, int fb_height, int fb_extra) {
    // The title screen shows PUSH START in a lime-green frame under the
    // logo: columns 114-206 and lines 75-101 of its 320x224 picture once it
    // has settled (from about frame 1520 after power-on; it zooms in before
    // that), measured on the program's own screenshots of it. The selected
    // entry covers the inside of the frame's outer edge, columns 115-205 and
    // lines 76-100, so the game's prompt does not show beside ours. A wide
    // frame buffer has its extra columns either side (the title's horizon
    // goes on there since issue #77), so the box moves with the margin.
    constexpr float kX = 115, kY = 76, kW = 90, kH = 24;
    const float sx = image.w / float(std::max(1, fb_width)), sy = image.h / float(std::max(1, fb_height));
    return {image.x + (float(std::max(0, fb_extra)) + kX) * sx, image.y + kY * sy, kW * sx, kH * sy};
}

float Menu::title_picture_y(float y, float window_w, float window_h) {
    // A portrait window has room to spare above and below the picture; the
    // front end wants it all below, for its list.
    return window_h > window_w ? std::min(y, std::round(window_h * 0.03f)) : y;
}

SDL_FRect Menu::picture_rect(ui::Ui& g) const {
    if (picture_.w > 0 && picture_.h > 0) return picture_;
    const float W = g.width(), H = g.height();
    float w = W, h = W * 3.0f / 4.0f;
    if (h > H) {
        h = H;
        w = H * 4.0f / 3.0f;
    }
    return {(W - w) * 0.5f, title_picture_y((H - h) * 0.5f, W, H), w, h};
}

SDL_FRect Menu::title_box(ui::Ui& g) const {
    if (picture_.w > 0 && picture_.h > 0)
        return push_start_box(picture_, picture_fb_w_, picture_fb_h_, picture_extra_);
    return push_start_box(picture_rect(g), 320, 224, 0);
}

void Menu::set_backdrop(SDL_Texture* texture, float width, float height, ui::Color tint, ui::Color accent) {
    backdrop_ = texture;
    backdrop_w_ = width;
    backdrop_h_ = height;
    zone_tint_ = tint;
    zone_accent_ = accent;
}

bool Menu::backdrop_on() const { return backdrop_ && cfg_ && cfg_->menu_backdrop_on(); }

ui::Color Menu::accent() const { return backdrop_on() ? zone_accent_ : award_art::color::magenta; }

void Menu::fill(ui::Ui& g, const SDL_FRect& r, ui::Color plain, uint8_t tint_alpha) const {
    if (!backdrop_on()) {
        g.rect(r, plain);
        return;
    }
    // The zone's picture, then its own colour over it: translucent enough to
    // show the place, dark enough that white and yellow letters stay clear.
    g.image_cover(backdrop_, backdrop_w_, backdrop_h_, r);
    g.rect(r, zone_tint_.alpha(tint_alpha));
}

void Menu::draw(ui::Ui& g, achievements::Tracker& ach) {
    if (!open()) return;
    hits_.clear();
    anim_ += 0.08f;
    // The game's letters are the menu's only: the touch controls and the
    // notifications keep the pixel face.
    g.enable_game_font(cfg_ && cfg_->menu_game_font);

    // The front end is the title screen itself, with nothing over it but its
    // entries. Every other page dims the game behind it rather than covering
    // it, in the awards' near-black purple.
    if (page_ != Page::Front) g.rect({0, 0, g.width(), g.height()}, award_art::color::ink.alpha(150));

    if (page_ == Page::Front)
        draw_front(g);
    else if (page_ == Page::Main)
        draw_row_page(g, "Left/Right choose  Enter select  Esc close");
    else if (is_showcase())
        draw_showcase(g);
    else if (is_list_page(page_))
        draw_options(g);
    else
        draw_achievements(g, ach);
    g.enable_game_font(false);
    if (page_opened_ms_) {
        // Out of the dark, the same time at any refresh rate.
        constexpr float kPageFadeMs = 140.0f;
        const float t = float(SDL_GetTicks() - page_opened_ms_) / kPageFadeMs;
        if (t >= 1.0f) page_opened_ms_ = 0;
        else g.rect({0, 0, g.width(), g.height()}, award_art::color::ink.alpha(uint8_t(220.0f * (1.0f - t))));
    }
}

// The front end (issue #3): the entries on the game's title screen. The
// selected entry is on a purple plaque laid over the box the title shows PUSH
// START in, with a spinning ring at each end -- the two rings of the game's
// combi -- for the cursor, so the chosen entry is always where the player
// looks for PUSH START. In a landscape window the entries after it, in the
// order Down reaches them (round past the last to the first), are stacked
// under it on smaller banners over the characters, three of them, fading. In
// a portrait one the whole menu is a column in the room under the picture, in
// its own order, the selected entry's place marked. Nothing is drawn above the
// box, so the logo and the line under it stay clear. Before PUSH START is up
// only the hint is drawn (set_title_ready), and while the front end is open
// the app keeps the title from giving way to the attract demo
// (runtime/title_hold.h), so the plaque is only ever over the title.
void Menu::draw_front(ui::Ui& g) {
    namespace art = award_art;
    namespace col = award_art::color;
    using ui::Font;
    const size_t n = items_.size();
    if (n == 0) return;
    const float W = g.width(), H = g.height();
    const bool portrait = H > W;
    const float edge = std::max(2.0f, std::round(g.px(3)));
    const float margin = g.px(8);
    const int sel = std::clamp(selected_, 0, int(n) - 1);
    // A larger MENU SIZE (issue #49) spaces the entries a little more as well,
    // as the lists do, so a finger has more to land on.
    const float extra = g.px(4.0f * float(g.zoom()));
    const float lp = g.line_height(Font::Pixel), ls = g.line_height(Font::Small);
    auto clamp_to = [](float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); };

    // The hint along the foot, on a dark strip of its own so that it reads
    // over any part of the title; whole phrases only. Before PUSH START is up
    // (the boot screens, the intro and the logo's zoom) it is all there is:
    // the plaque would float over the city and the road, and the first press
    // skips to the title and brings the entries up (waiting_for_title).
    const bool waiting = waiting_for_title();
    const char* hint = waiting ? "Enter skip intro  Esc close" : "Up/Down choose  Enter select  Esc close";
    if (g.text_width(kHintFont, hint) + g.px(24) > W - margin * 2)
        hint = waiting ? "Enter skip intro" : "Up/Down choose  Enter select";
    const float lh_hint = g.line_height(kHintFont);
    const float hint_h = lh_hint + g.px(12);
    auto draw_hint = [&] {
        const float hw = std::min(g.text_width(kHintFont, hint) + g.px(24), W - margin * 2);
        const SDL_FRect hr{(W - hw) * 0.5f, H - hint_h - g.px(4), hw, hint_h};
        g.rect(hr, col::ink.alpha(170), hint_h * 0.5f);
        g.text_fit(kHintFont, hr.x + g.px(12), hr.y + (hint_h - lh_hint) * 0.5f, hw - g.px(24), hint, col::text_dim);
    };
    if (waiting) {
        // A tap anywhere skips too: a finger has no Enter.
        hits_.push_back({{0, 0, W, H}, -1, 0, 0, [this] { skip_intro(); }});
        draw_hint();
        return;
    }
    const float top = g.px(4), bottom = std::max(top + lp * 2, H - hint_h - g.px(8));

    // The plaque: at least the PUSH START box, larger if its letters need it.
    // The large letters, unless they and the rings would not fit across the
    // window (a portrait one at a larger MENU SIZE): then the smaller ones,
    // so that it still has its rings.
    const SDL_FRect box = title_box(g);
    const std::string& label = items_[size_t(sel)].label;
    Font font = Font::PixelBig;
    for (Font f : {Font::PixelBig, Font::Pixel}) {
        font = f;
        const float h = std::max(box.h, g.line_height(f) + g.px(20) + extra);
        const float r = std::min(h * 0.3f, g.line_height(f) * 0.75f);
        // ...or when they would make the plaque much taller than the box
        // (a portrait window, where the letters are sized to its height).
        if (g.text_width(f, label) + h * 0.32f + (r * 2 + g.px(12)) * 2 + g.px(24) <= W - margin * 2 &&
            h <= box.h * 1.35f)
            break;
    }
    const float lf = g.line_height(font);
    const float plaque_h = std::min(std::max(box.h, lf + g.px(20) + extra), bottom - top);
    const float lean = plaque_h * 0.32f;
    const float ring_r = std::min(plaque_h * 0.3f, lf * 0.75f);
    const float ring_room = ring_r * 2 + g.px(12);
    const float tw = g.text_width(font, label);
    const float plaque_w = std::min(std::max(box.w + lean, tw + lean + ring_room * 2 + g.px(24)), W - margin * 2);
    // Too narrow for the rings and the words even so: the words keep the room.
    const bool rings = plaque_w - lean - ring_room * 2 - g.px(12) >= tw;
    const float cx = clamp_to(box.x + box.w * 0.5f, margin + plaque_w * 0.5f, W - margin - plaque_w * 0.5f);
    // Taller than the box, it grows downwards only: above the box are the
    // logo and the line under it, which stay clear.
    const float cy = clamp_to(box.y + std::max(box.h, plaque_h) * 0.5f, top + plaque_h * 0.5f,
                              bottom - plaque_h * 0.5f);
    const SDL_FRect plaque{cx - plaque_w * 0.5f, cy - plaque_h * 0.5f, plaque_w, plaque_h};

    // The entries after it, all as wide as the longest.
    // A portrait column is a little closer set, so that a phone holds all of
    // it at LARGE.
    const float pitch = lp + g.px(portrait ? 12.0f : 16.0f) + extra;
    const float row_h = lp + g.px(10) + extra * 0.5f;
    const float nlean = row_h * 0.4f;
    float nw = 0;
    for (const Item& it : items_) nw = std::max(nw, g.text_width(Font::Pixel, it.label));
    nw = std::min(nw + nlean * 2 + g.px(36), W - margin * 2);
    const float text_room = std::max(0.0f, nw - nlean * 2 - g.px(16));
    const float gap = g.px(6) + extra * 0.5f;
    float list_y = plaque.y + plaque.h + gap;
    if (portrait) list_y = std::max(list_y, picture_rect(g).y + picture_rect(g).h + gap);
    const int room = int(std::max(0.0f, bottom - list_y) / pitch);
    auto entry = [&](int i, float slot_y, uint8_t a, bool here) {
        const SDL_FRect slot{cx - nw * 0.5f, slot_y, nw, pitch};
        const SDL_FRect b{slot.x, slot_y + (pitch - row_h) * 0.5f, nw, row_h};
        // The selected entry's own place in the column: purple edged in
        // magenta, in full brightness, as the plaque is.
        if (here) art::banner(g, b, nlean, col::magenta, col::magenta, 0);
        art::banner(g, here ? SDL_FRect{b.x + edge, b.y + edge, b.w - edge * 2, b.h - edge * 2} : b, nlean,
                    here ? col::purple : col::night.alpha(a), col::ink.alpha(a), edge);
        const std::string& l = items_[size_t(i)].label;
        const float w = std::min(g.text_width(Font::Pixel, l), text_room);
        g.text_fit(Font::Pixel, cx - w * 0.5f, b.y + (row_h - lp) * 0.5f, text_room, l,
                   here ? ui::theme::text : col::text_dim.alpha(a));
        hits_.push_back({slot, i});
    };
    // Which entries the arrows beside the plaque need to stand in for.
    bool before_shown = sel == 0, after_shown = sel == int(n) - 1;
    if (portrait) {
        // The whole menu in its own order, START GAME to QUIT, with the
        // selected entry's place marked; a column too long for the room
        // shows a window of it that keeps the entries either side of the
        // selection in view.
        const int fit = std::max(1, std::min(int(n), room));
        const int first = std::clamp(sel - (fit - 1) / 2, 0, int(n) - fit);
        for (int k = 0; k < fit; ++k) {
            const int i = first + k;
            entry(i, list_y + pitch * float(k), 235, i == sel);
        }
        before_shown = before_shown || sel - 1 >= first;
        after_shown = after_shown || sel + 1 < first + fit;
    } else {
        // Landscape: the entries after it, in the order Down reaches them
        // (round past the last to the first), fading out over the characters.
        const int shown = std::min({int(n) - 1, 3, room});
        for (int d = 1; d <= shown; ++d)
            entry((sel + d) % int(n), list_y + pitch * float(d - 1), uint8_t(d == 1 ? 235 : d == 2 ? 185 : 135),
                  false);
        before_shown = before_shown || shown >= int(n) - 1;
        after_shown = after_shown || shown > 0;
    }

    // The plaque, with a magenta stripe under it as under the AWARDS title.
    art::banner(g, {plaque.x + g.px(10), plaque.y + plaque.h - g.px(4), plaque.w - g.px(10), g.px(12)},
                lean * 0.3f, col::magenta, col::ink, edge);
    art::banner(g, plaque, lean, col::purple, col::ink, edge);

    // Arrows that step the selection, since a finger has no Up/Down: up
    // whenever the entry before this one is not shown (where a tap would
    // reach it), down likewise for the one after. Beside the plaque, or with
    // no room beside it at its right-hand end in place of the ring.
    const bool more_up = !before_shown, more_down = !after_shown;
    const float mh = std::min(plaque.h * 0.5f - g.px(3), lp + g.px(12)), mw = mh * 1.7f;
    const bool inside = plaque.x + plaque.w + g.px(8) + mw > W - g.px(4);
    const bool markers = more_up || more_down;
    const float mx = inside ? plaque.x + plaque.w - lean * 0.5f - edge - g.px(4) - mw : plaque.x + plaque.w + g.px(8);
    const float right_room = markers && inside ? mw + g.px(8) : (rings ? ring_room : 0.0f);
    const float left_room = rings ? ring_room : (markers && inside ? mw + g.px(8) : 0.0f);

    const float ring_x = lean * 0.5f + edge + g.px(6) + ring_r;
    if (rings) {
        const float turn = anim_ * 0.12f;
        art::ring(g, plaque.x + ring_x, cy, ring_r, turn);
        if (!(markers && inside))
            art::ring(g, plaque.x + plaque.w - ring_x, cy, ring_r, turn + 0.25f);  // a quarter turn behind
    }
    const float text_w = std::max(0.0f, plaque.w - lean - left_room - right_room - g.px(12));
    const float mid = plaque.x + lean * 0.5f + g.px(6) + left_room + text_w * 0.5f;
    g.text_fit(font, mid - std::min(tw, text_w) * 0.5f, cy - lf * 0.5f, text_w, label, ui::theme::text);

    // The arrows' targets go before the plaque's, which they may sit on.
    if (markers) {
        auto marker = [&](float my, bool up, int index) {
            const SDL_FRect m{mx, my, mw, mh};
            art::banner(g, m, mh * 0.3f, col::magenta, col::ink, edge);
            art::arrowhead(g, m.x + mw * 0.5f, m.y + mh * 0.5f, mh * 0.3f, up, col::gold, edge);
            hits_.push_back({{m.x - g.px(2), m.y - g.px(2), m.w + g.px(4), m.h + g.px(4)}, index});
        };
        if (more_up) marker(cy - mh - g.px(3), true, kScrollUp);
        if (more_down) marker(cy + g.px(3), false, kScrollDown);
    }
    hits_.push_back({plaque, sel});
    draw_hint();
}

void Menu::draw_row_page(ui::Ui& g, const char* hint) {
    const float item_h = g.line_height(ui::Font::Pixel);
    const float hint_h = g.line_height(kHintFont);
    const float gap = g.px(34);
    // Measure first so each line can be centred as a whole. A row too wide for
    // the window (a larger MENU SIZE, a narrow window) carries on onto a
    // second line rather than running off both sides.
    const float room = g.width() - g.px(32) * 2;
    std::vector<std::pair<size_t, float>> lines;  // first entry, width
    for (size_t i = 0; i < items_.size(); ++i) {
        const float w = g.text_width(ui::Font::Pixel, items_[i].label);
        if (lines.empty() || lines.back().second + gap + w > room) lines.push_back({i, w});
        else lines.back().second += gap + w;
    }
    if (lines.empty()) lines.push_back({0, 0.0f});
    const float line_h = item_h + g.px(22);
    // A band across the lower part, tall enough to hold the row and the hint,
    // so the art above it stays visible.
    const float band_h = g.px(26) + item_h + line_h * float(lines.size() - 1) + g.px(14) + hint_h + g.px(16);
    const float band_y = std::max(0.0f, g.height() - band_h - g.px(24));
    // Solid, or a zone's picture under a heavy tint: the title screen behind
    // it is bright, and a bar across the bottom is what these menus looked
    // like anyway.
    fill(g, {0, band_y, g.width(), band_h}, ui::theme::background, 205);
    g.rect({0, band_y, g.width(), g.px(3)}, accent());
    g.rect({0, band_y + band_h - g.px(3), g.width(), g.px(3)}, accent());

    size_t line = 0;
    float x = 0, y = band_y + g.px(26);
    for (size_t i = 0; i < items_.size(); ++i) {
        if (line < lines.size() && lines[line].first == i) {
            if (i > 0) y += line_h;
            // A single entry wider than the window is shortened rather than
            // centred off both of its sides.
            x = std::max(g.px(28), (g.width() - lines[line].second) * 0.5f);
            ++line;
        }
        const bool sel = int(i) == selected_;
        const float fit = std::max(0.0f, g.width() - x - g.px(8));
        const float w = std::min(g.text_width(ui::Font::Pixel, items_[i].label), fit);
        if (sel) {
            // The selected entry gets a marker that bobs, the way these menus
            // have always done it.
            const float bob = std::sin(anim_ * 3.0f) * g.px(2);
            g.text(ui::Font::Pixel, x - g.px(24) + bob, y, ">", accent());
            g.text_fit(ui::Font::Pixel, x, y, fit, items_[i].label, ui::theme::text);
            g.rect({x, y + item_h + g.px(4), w, g.px(3)}, accent());
        } else {
            g.text_fit(ui::Font::Pixel, x, y, fit, items_[i].label, ui::theme::text_faint);
        }
        SDL_FRect r{x - g.px(24), y - g.px(10), w + g.px(34), item_h + g.px(20)};
        r.x = std::max(0.0f, r.x);
        r.w = std::min(r.w, g.width() - r.x);
        hits_.push_back({r, int(i)});
        x += w + gap;
    }

    g.text_fit(kHintFont, std::max(g.px(8), (g.width() - g.text_width(kHintFont, hint)) * 0.5f),
               band_y + band_h - g.px(16) - hint_h, g.width() - g.px(16), hint, faint());
}

void Menu::draw_options(ui::Ui& g) {
    // Room for the selection marker inside the panel, and a margin outside
    // it; on a window too narrow for the whole panel (a portrait one, a larger
    // MENU SIZE) both shrink before the column does.
    const bool narrow = g.width() < g.px(560 + 34 * 2 + 24);
    const float pad = narrow ? g.px(28) : g.px(34);
    const char* heading = page_ == Page::TimeAttack ? "TIME ATTACK"
                        : page_ == Page::LevelSelect ? "LEVEL SELECT"
                        : page_ == Page::Cheats ? "CHEATS"
                        : page_ == Page::Video ? "VIDEO"
                        : page_ == Page::Sound ? "SOUND"
                        : page_ == Page::Input ? "INPUT"
                        : page_ == Page::Extras ? "EXTRAS"
                        : page_ == Page::Controls ? "CONTROLS"
                        : page_ == Page::States ? "SAVE STATES"
                        : page_ == Page::StateKeys ? "STATES AND REWIND"
                        : page_ == Page::Keys
                              ? (keys_player_ == 0 ? (keys_pad_ ? "1P GAMEPAD" : "1P KEYBOARD")
                                                   : (keys_pad_ ? "2P GAMEPAD" : "2P KEYBOARD"))
                        : page_ == Page::Mods ? "MODS"
                        : page_ == Page::Music ? "MUSIC PLAYER"
                        : page_ == Page::Netplay ? "NETPLAY"
                        : "OPTIONS";
    std::string pack_heading = heading;
    if (page_ == Page::ModInfo) {
        const std::vector<mod_packs::Pack>* list = packs();
        pack_heading = list && mod_index_ < list->size() ? upper((*list)[mod_index_].title()) : "MOD";
    }
    const float widest = std::min(g.width() - pad * 2 - (narrow ? g.px(8) : g.px(24)), g.px(560));
    // As wide as the page needs (issue #3), not a fixed width with half of it
    // empty: its longest row with its value, its heading on its banner and
    // its hint, in steps of 40 units so that stepping a value does not make
    // the panel twitch. A pack's page keeps the whole width for its words.
    float need = g.px(300);
    if (!info_lines_.empty()) need = widest;
    for (const Item& it : items_) {
        float w = g.text_width(ui::Font::Pixel, it.label);
        if (!it.note.empty()) w += g.text_width(ui::Font::Small, it.note) + g.px(16);
        if (it.value) w += g.text_width(ui::Font::Pixel, it.value()) + g.px(60);
        need = std::max(need, w);
    }
    need = std::max({need, g.text_width(kHintFont, "Up/Down choose  Left/Right change  Esc back"),
                     g.text_width(ui::Font::PixelBig, pack_heading) +
                         (g.line_height(ui::Font::PixelBig) + g.px(18)) * 0.9f + g.px(40)});
    need = std::ceil(need / g.px(40)) * g.px(40);
    const float column = std::min(widest, need);
    float x = (g.width() - column) * 0.5f;
    // The music player's keyboards (issue #102) go to the right of its list
    // when the window has room for both; the pair is centred together.
    const jukebox::Keys* keys = page_ == Page::Music && hooks_.music_keys ? hooks_.music_keys() : nullptr;
    const float keys_gap = g.px(24);
    const float keys_w = std::min(g.px(560), g.width() - (column + pad * 2) - keys_gap - g.px(32));
    const bool show_keys = keys && keys_w >= g.px(300);
    if (show_keys) x = (g.width() - (column + pad * 2 + keys_gap + keys_w)) * 0.5f + pad;
    // A larger MENU SIZE (issue #49) spaces the rows a little more as well,
    // so a finger has more to land on than the letters alone would give it.
    const float extra = g.px(4.0f * float(g.zoom()));
    const float row_h = g.line_height(ui::Font::Pixel) + g.px(16) + extra;
    const float head_h = g.line_height(ui::Font::PixelBig) + g.px(22);
    const float hint_h = g.line_height(kHintFont);
    // A value too wide to sit beside its label - a controller's own name, say
    // - gets a line of its own rather than running into it.
    std::vector<bool> wrapped(items_.size(), false);
    // A note sits between the label and the value, so it counts towards the
    // room a row needs.
    auto note_width = [&g](const Item& it) {
        return it.note.empty() ? 0.0f : g.text_width(ui::Font::Small, it.note) + g.px(16);
    };
    float rows_h = 0;
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].value) {
            const float need = g.text_width(ui::Font::Pixel, items_[i].label) + note_width(items_[i]) +
                               g.text_width(ui::Font::Pixel, items_[i].value()) + g.px(60);
            wrapped[i] = need > column;
        }
        rows_h += wrapped[i] ? row_h * 2 : row_h;
    }

    // A pack's page (issue #42): its description under the heading, wrapped
    // to the panel beside its preview image, in at most a third of the
    // screen.
    const float info_line_h = g.line_height(ui::Font::Small) + g.px(4);
    std::vector<std::string> info;
    SDL_Texture* preview = nullptr;
    SDL_FRect preview_r{};
    float info_h = 0;
    float preview_w = 0, preview_h = 0;
    if (!info_lines_.empty()) {
        const std::vector<mod_packs::Pack>* list = packs();
        if (page_ == Page::ModInfo && hooks_.mod_preview && list && mod_index_ < list->size())
            preview = hooks_.mod_preview((*list)[mod_index_], &preview_w, &preview_h);
        const float pw = preview_w, ph = preview_h;
        const float max_info = std::max(info_line_h * 3, g.height() / 3);
        float text_w = column;
        if (preview && pw > 0 && ph > 0) {
            const float k = std::min({column * 0.4f / pw, max_info / ph, g.px(160) / ph});
            preview_r = {x + column - pw * k, 0, pw * k, ph * k};
            text_w = column - preview_r.w - g.px(14);
        }
        // Word wrap, by bytes at spaces: a word too long for a line keeps a
        // line to itself and is shortened when drawn.
        for (const std::string& para : info_lines_) {
            std::string line;
            size_t at = 0;
            while (at <= para.size()) {
                size_t sp = para.find(' ', at);
                if (sp == std::string::npos) sp = para.size();
                const std::string word = para.substr(at, sp - at);
                const std::string tried = line.empty() ? word : line + " " + word;
                if (!line.empty() && g.text_width(ui::Font::Small, tried) > text_w) {
                    info.push_back(line);
                    line = word;
                } else {
                    line = tried;
                }
                at = sp + 1;
            }
            info.push_back(line);
        }
        const size_t max_lines = size_t(std::max(1.0f, max_info / info_line_h));
        if (info.size() > max_lines) {
            info.resize(max_lines);
            info.back() += " ...";
        }
        info_h = std::max(float(info.size()) * info_line_h, preview_r.h) + g.px(12);
    }

    // A list longer than the screen (mod packs) shows a window of it that
    // follows the selection, with a marker above and below where there is
    // more; a tap on a marker steps the selection that way.
    const float rows_room = g.height() - g.px(28) - g.px(16) - g.px(30) - head_h - info_h - g.px(12) - hint_h;
    size_t first = 0, last = items_.size();  // the rows drawn: [first, last)
    if (rows_h > rows_room && !items_.empty()) {
        auto h_of = [&](size_t i) { return wrapped[i] ? row_h * 2 : row_h; };
        const size_t sel = size_t(std::clamp(selected_, 0, int(items_.size()) - 1));
        first = std::min(size_t(std::max(scroll_, 0)), sel);
        float used = 0;
        for (size_t i = first; i <= sel; ++i) used += h_of(i);
        while (used > rows_room && first < sel) used -= h_of(first++);
        last = sel + 1;
        while (last < items_.size() && used + h_of(last) <= rows_room) used += h_of(last++);
        scroll_ = int(first);
        rows_h = used;
    }
    const float body = head_h + info_h + rows_h + g.px(12) + hint_h;
    const float total = body + g.px(30);
    float y = std::max(g.px(14), (g.height() - total) * 0.5f) + g.px(16);
    const float panel_top = y - g.px(16);

    const SDL_FRect panel{x - pad, panel_top, column + pad * 2, total};
    // The awards' colours (issue #3): near-black purple, edged in magenta,
    // over the dimmed title -- no zone's picture inside it, which made two
    // pictures one over the other.
    g.panel(panel, award_art::color::ink.alpha(235), award_art::color::magenta, g.px(4), g.px(3));
    if (show_keys) draw_music_keys(g, {panel.x + panel.w + keys_gap, panel.y, keys_w, panel.h}, *keys);

    // The page's name on the AWARDS title's slanted banner (issue #3), so
    // every page reached from the title screen wears the same header.
    {
        const float lean = (g.line_height(ui::Font::PixelBig) + g.px(18)) * 0.45f;
        const float hx = std::max(g.px(2), x - lean * 0.5f - g.px(6));
        award_art::heading(g, hx, y - g.px(8), ui::Font::PixelBig, pack_heading,
                           column + (x - hx) - (first > 0 ? g.px(56) : 0.0f));
    }
    // More rows above the window: a marker at the heading's right.
    if (first > 0) {
        const float mw = g.text_width(ui::Font::Pixel, "^");
        g.text(ui::Font::Pixel, x + column - mw, y, "^", award_art::color::magenta);
        hits_.push_back({{x + column - g.px(48), y - g.px(4), g.px(56), head_h}, kScrollUp});
    }
    y += head_h;

    if (!info.empty()) {
        if (preview) {
            // Sized to the picture's own shape, so "cover" draws all of it.
            preview_r.y = y;
            g.image_cover(preview, preview_w, preview_h, preview_r);
        }
        float iy = y;
        for (const std::string& line : info) {
            g.text_fit(ui::Font::Small, x, iy, preview ? column - preview_r.w - g.px(14) : column, line,
                       ui::theme::text_dim);
            iy += info_line_h;
        }
        y += info_h;
    }

    y += extra * 0.5f;  // the rows' extra room, half above the letters
    for (size_t i = first; i < last; ++i) {
        const bool sel = int(i) == selected_;
        const Item& it = items_[i];
        if (sel) {
            // The cursor is a spinning ring, as on the title screen.
            const float rr = g.line_height(ui::Font::Pixel) * 0.36f;
            award_art::ring(g, x - g.px(10) - rr, y + g.line_height(ui::Font::Pixel) * 0.5f, rr, anim_ * 0.12f);
        }
        const float label_w = g.text_fit(ui::Font::Pixel, x, y, column, it.label,
                                     sel ? ui::theme::text : ui::theme::text_dim);
        if (!it.note.empty()) {
            // Smaller and dimmer than the label, on the same middle line, so
            // it reads as a caption rather than as a second entry.
            const float ny = y + (g.line_height(ui::Font::Pixel) - g.line_height(ui::Font::Small)) * 0.5f;
            // Shortened rather than run out of the panel on a narrow window.
            g.text_fit(ui::Font::Small, x + label_w + g.px(16), ny, column - label_w - g.px(16), it.note, ui::theme::text_faint);
        }
        const float this_h = wrapped[i] ? row_h * 2 : row_h;
        Hit hit{{x - g.px(24), y - g.px(4) - extra * 0.5f, column + g.px(48), this_h}, int(i)};
        if (it.value) {
            const std::string v = it.value();
            const float vw = g.text_width(ui::Font::Pixel, v);
            // Wrapped values sit on the next line, indented, and are shortened
            // if even a whole line cannot hold them.
            const float vy = wrapped[i] ? y + row_h : y;
            const float vx = wrapped[i] ? x + g.px(16) : x + column - vw;
            const float room = wrapped[i] ? column - g.px(46) : column;
            g.text_fit(ui::Font::Pixel, vx, vy, room, v,
                       sel ? award_art::color::magenta : ui::theme::text_dim);
            // Arrows only where left/right actually does something: on the
            // key page the value changes by capturing a press instead.
            if (sel && !it.pressed && !it.read_only) {
                const float aw = wrapped[i] ? std::min(vw, room) : vw;
                const float ax = wrapped[i] ? vx : x + column - vw;
                g.text(ui::Font::Pixel, ax - g.px(22), vy, "<", award_art::color::magenta);
                g.text(ui::Font::Pixel, ax + aw + g.px(10), vy, ">", award_art::color::magenta);
            }
            // The "<" arrow's area, padded to a finger's width. It is taken
            // from where the arrow is drawn when the row is selected, so it
            // is right for the second tap, the one that changes the value.
            if (!it.pressed && !it.read_only) {
                const float ax = wrapped[i] ? vx : x + column - vw;
                hit.back_x = ax - g.px(40);
                hit.back_w = g.px(40);
            }
        }
        hits_.push_back(hit);
        y += this_h;
    }

    y -= extra * 0.5f;
    const char* hint = page_ == Page::Keys || page_ == Page::StateKeys
                           ? "Up/Down choose  Enter rebind  Esc back"
                       : page_ == Page::Mods ? "Up/Down choose  Left/Right on/off  Enter details"
                       : page_ == Page::Music ? "Up/Down choose  Enter play  Esc back"
                                             : "Up/Down choose  Left/Right change  Esc back";
    g.text_fit(kHintFont, x, y + g.px(8), column, hint, ui::theme::text_faint);
    // More rows below the window: a marker at the hint's right.
    if (last < items_.size()) {
        const float mw = g.text_width(ui::Font::Pixel, "v");
        g.text(ui::Font::Pixel, x + column - mw, y + g.px(2), "v", award_art::color::magenta);
        hits_.push_back({{x + column - g.px(48), y - g.px(4), g.px(56), hint_h + g.px(20)}, kScrollDown});
    }
}

// Issue #102: the keyboards of the game's own SOUND TEST, beside MUSIC
// PLAYER's list. A row for each FM channel and each PSG tone, the key being
// played lit (FM red, PSG green, brighter as it is louder), a level bar at
// the row's end, and a pad for each PWM sample, lit when struck and fading,
// in its voice's colour. Seven octaves, C1 to B7, which every song keeps to.
void Menu::draw_music_keys(ui::Ui& g, const SDL_FRect& area, const jukebox::Keys& k) {
    namespace col = award_art::color;
    constexpr int kLow = 24, kOctaves = 7, kWhite = kOctaves * 7;
    const float m = g.px(14);
    const float label_w = g.text_width(ui::Font::Pixel, "PSG3") + g.px(10);
    const float meter_w = g.px(10);
    const float pads_h = g.px(46);
    // As tall as its rows, centred beside the list.
    const float row_h = std::min(g.px(36), (area.h - 2 * m - pads_h - g.px(10)) / 9.0f);
    const float content_h = 2 * m + row_h * 9.0f + g.px(6) + pads_h;
    const SDL_FRect r{area.x, area.y + (area.h - content_h) * 0.5f, area.w, content_h};
    g.panel(r, col::ink.alpha(235), col::magenta, g.px(4), g.px(3));
    const float kx = r.x + m + label_w;
    const float kw = r.w - 2 * m - label_w - meter_w - g.px(8);
    const float rows_top = r.y + m;
    const float white_w = kw / float(kWhite);
    const ui::Color white{214, 210, 228}, black{24, 18, 40};
    static const int kWhiteOf[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};  // white key index in the octave
    auto mix = [](ui::Color a, ui::Color b, float t) {
        auto f = [t](uint8_t x, uint8_t y) { return uint8_t(float(x) + (float(y) - float(x)) * t); };
        return ui::Color{f(a.r, b.r), f(a.g, b.g), f(a.b, b.b)};
    };
    for (int row = 0; row < 9; ++row) {
        const bool fm = row < 6;
        const jukebox::Keys::Key key = fm ? k.fm[row] : k.psg[row - 6];
        const ui::Color lit = fm ? ui::Color{236, 40, 40} : ui::Color{60, 220, 90};
        const float y = rows_top + row_h * float(row);
        const float h = row_h - g.px(4);
        const std::string name = fm ? "FM" + std::to_string(row + 1) : "PSG" + std::to_string(row - 5);
        g.text(ui::Font::Pixel, r.x + m, y + (h - g.line_height(ui::Font::Pixel)) * 0.5f, name, col::gold);
        const int note = key.note - kLow;
        const bool on = key.note >= 0 && note >= 0 && note < kOctaves * 12;
        const ui::Color hot = mix(white, lit, 0.45f + 0.55f * key.level);
        for (int w = 0; w < kWhite; ++w) {
            const bool this_key = on && kWhiteOf[note % 12] >= 0 && note / 12 * 7 + kWhiteOf[note % 12] == w;
            g.rect({kx + white_w * float(w), y, std::max(1.0f, white_w - g.px(1)), h}, this_key ? hot : white);
        }
        for (int o = 0; o < kOctaves; ++o)
            for (int s = 0; s < 12; ++s) {
                if (kWhiteOf[s] >= 0) continue;
                const bool this_key = on && note == o * 12 + s;
                const float bx = kx + white_w * float(o * 7 + kWhiteOf[s - 1] + 1) - white_w * 0.3f;
                g.rect({bx, y, white_w * 0.6f, h * 0.6f}, this_key ? hot : black);
            }
        // The channel's level.
        const float mx = kx + kw + g.px(8);
        g.rect({mx, y, meter_w, h}, col::night);
        const float lh = h * (on ? key.level : 0.0f);
        g.rect({mx, y + h - lh, meter_w, lh}, lit);
    }
    // PWM: a pad for each sample, two staggered rows.
    const float py = rows_top + row_h * 9.0f + g.px(6);
    g.text(ui::Font::Pixel, r.x + m, py + (pads_h - g.line_height(ui::Font::Pixel)) * 0.5f, "PWM", col::gold);
    static const ui::Color kVoice[4] = {{255, 210, 40}, {236, 40, 40}, {200, 60, 230}, {60, 220, 90}};
    constexpr int kPads = jukebox::Keys::kPwmSamples / 2;
    const float pw = std::min(kw / (float(kPads) + 0.5f), pads_h * 0.6f);
    for (int i = 0; i < jukebox::Keys::kPwmSamples; ++i) {
        const int rr = i / kPads, c = i % kPads;
        const float cx = kx + pw * (float(c) + 0.5f + 0.5f * float(rr));
        const float cy = py + pads_h * (rr ? 0.72f : 0.28f);
        const float t = float(k.pwm[i]) / 255.0f;
        const ui::Color fill = t > 0 ? mix(white, kVoice[k.pwm_voice[i] & 3], 0.3f + 0.7f * t) : white;
        const float rx = pw * 0.48f, ry = pads_h * 0.24f;
        const SDL_FPoint hex[6] = {{cx - rx, cy}, {cx - rx * 0.5f, cy - ry}, {cx + rx * 0.5f, cy - ry},
                                   {cx + rx, cy}, {cx + rx * 0.5f, cy + ry}, {cx - rx * 0.5f, cy + ry}};
        g.poly(hex, 6, fill);
    }
}

// TIME ATTACK and LEVEL SELECT (issue #53): the zone showcase. A landscape
// window has the stage's picture on the left, about three fifths of the
// width, with the place's name over its foot between arrows and the rows of
// levels and times of day under it; the cards on the right. A portrait one
// (or one too narrow for both) stacks the same pieces in one column. A
// column taller than the window scrolls to keep the selected entry in view,
// as the long lists do. Everything is measured in the menu's letters, so
// MENU SIZE grows all of it.
void Menu::draw_showcase(ui::Ui& g) {
    const Showcase& s = sc_;
    if (!s.place || !s.level || !s.time || !s.player || !s.combi || !s.two_players) return;
    using ui::Font;
    const float W = g.width(), H = g.height();
    const int zoom = g.zoom();
    const float M = std::clamp(W * 0.03f, g.px(6), g.px(20));
    const float lh_s = g.line_height(Font::Small);
    const float lh_p = g.line_height(Font::Pixel);
    const float lh_b = g.line_height(Font::PixelBig);
    const ui::Color acc = accent();
    const ui::Color yellow = ui::theme::highlight;
    const float bob = std::sin(anim_ * 3.0f) * g.px(2);

    // The whole window: the zone's picture under a heavy tint, or plain.
    fill(g, {0, 0, W, H}, ui::theme::background.alpha(238), 222);

    // --- Header: the page's name, and BACK. -------------------------------
    const float head_h = lh_b + g.px(14);
    const float head_y = std::max(g.px(4), M * 0.6f);
    const std::string back_label = "< BACK";
    const float back_w = g.text_width(Font::Pixel, back_label) + g.px(28);
    const float back_h = std::min(head_h, lh_p + g.px(14) + g.px(2.0f * float(zoom)));
    const SDL_FRect back_r{W - M - back_w, head_y + (head_h - back_h) * 0.5f, back_w, back_h};
    const char* title = s.timed ? "TIME ATTACK" : "LEVEL SELECT";
    // The large letters, or the smaller ones on a narrow window at a larger
    // MENU SIZE rather than cut short.
    // (The banner it is on takes its lean twice and some room besides.)
    const Font title_font =
        g.text_width(Font::PixelBig, title) + (lh_b + g.px(18)) * 0.9f + g.px(40) <= back_r.x - M - g.px(12)
            ? Font::PixelBig : Font::Pixel;
    // On the AWARDS title's banner (issue #3), as every page from the title
    // screen is.
    award_art::heading(g, M, head_y + (head_h - g.line_height(title_font) - g.px(18)) * 0.5f, title_font, title,
                       back_r.x - M - g.px(12));
    {
        const bool sel = selected_ == s.back_row;
        g.rect(back_r, sel ? acc.alpha(90) : ui::theme::background.alpha(160), g.px(4));
        g.outline(back_r, sel ? acc : ui::theme::outline_strong, g.px(4), sel ? g.px(3) : g.px(2));
        g.text(Font::Pixel, back_r.x + g.px(14), back_r.y + (back_r.h - lh_p) * 0.5f, back_label,
               sel ? ui::theme::text : ui::theme::text_dim);
        if (s.back_row >= 0) {
            const int row = s.back_row;
            hits_.push_back({back_r, row, 0, 0, [this, row] { items_[size_t(row)].act(0); }});
        }
    }

    // --- Hint along the foot. --------------------------------------------
    const float lh_hint = g.line_height(kHintFont);
    const float hint_h = lh_hint + g.px(10);
    // As much of it as the width takes, whole phrases only.
    const char* hint = "Up/Down choose  Left/Right change  Enter start  Esc back";
    for (const char* shorter : {"Up/Down choose  Left/Right change  Esc back", "Up/Down choose  Left/Right change"})
        if (g.text_width(kHintFont, hint) > W - 2 * M) hint = shorter;
    g.text_fit(kHintFont, M, H - hint_h + (hint_h - lh_hint) * 0.5f - g.px(2), W - 2 * M, hint, faint());

    const float body_top = head_y + head_h + g.px(10);
    const float body_bottom = H - hint_h - g.px(2);
    const float body_h = std::max(g.px(40), body_bottom - body_top);
    const float body_w = W - 2 * M;

    // --- Measurements. ---------------------------------------------------
    const float gap = g.px(10);
    const float rp = g.px(8);                 // inside a row or card
    const float mark = g.px(22);              // the bobbing marker's room
    const float chip_h = lh_p + g.px(12) + g.px(3.0f * float(zoom));
    const float chip_pad = g.px(11);
    const float chip_gap = g.px(7);
    const bool special = !s.timed && *s.place == level_select::kSpecialStage;
    const std::string act_caption = special ? "STAGE" : "ACT";
    float cap_w = 0;
    for (const char* c : {"STAGE", "ACT", "TIME", "GHOST", "PLAYERS"}) cap_w = std::max(cap_w, g.text_width(Font::Pixel, c));
    cap_w += g.px(12);
    const float aw = g.text_width(Font::Pixel, "<");

    // The choices of each row of chips.
    struct Choices {
        int row = -1;
        std::string caption;
        std::vector<std::string> labels;
        std::vector<std::function<void()>> set;
        int current = -1;
    };
    std::vector<int> levels = showcase_levels();
    Choices act{s.level_row, act_caption, {}, {}, -1};
    for (size_t i = 0; i < levels.size(); ++i) {
        const int l = levels[i];
        act.labels.push_back(showcase_level_label(l));
        int* level = s.level;
        act.set.push_back([level, l] { *level = l; });
        if (l == *s.level) act.current = int(i);
    }
    Choices times{s.time_row, "TIME", {}, {}, (*s.time >> 1) & 3};
    for (int i = 0; i < stage_select::kTimeCount; ++i) {
        times.labels.push_back(stage_select::kTimes[i]);
        int* t = s.time;
        times.set.push_back([t, i] { *t = i * 2; });
    }
    Choices players{s.players_row, "PLAYERS", {"1 PLAYER", "2 PLAYERS"}, {}, *s.two_players ? 1 : 0};
    {
        bool* two = s.two_players;
        players.set = {[two] { *two = false; }, [two] { *two = true; }};
    }
    Choices ghosts{s.ghost_row, "GHOST", {}, {}, s.ghost ? *s.ghost : 0};
    if (s.ghost)
        for (int i = 0; i < 3; ++i) {
            ghosts.labels.push_back(ghost::mode_name(ghost::Mode(i)));
            int* gh = s.ghost;
            ghosts.set.push_back([gh, i] { *gh = i; });
        }

    // A row of chips laid out in `width`: the caption on the left, or above
    // the chips when there is no room beside them, and the chips flowing onto
    // further lines when one is not enough.
    struct ChipRow {
        bool caption_above = false;
        float h = 0;
        std::vector<SDL_FRect> chips;  // relative to the row
    };
    auto chip_w = [&](const std::string& label) { return g.text_width(Font::Pixel, label) + chip_pad * 2; };
    auto flow_chips = [&](const Choices& c, float width, bool above, int* lines) {
        ChipRow r;
        r.caption_above = above;
        const float x0 = above ? rp + mark : rp + mark + cap_w;
        const float y0 = above ? rp + lh_p + g.px(6) : rp;
        const float room = std::max(g.px(20), width - x0 - rp);
        float x = 0, y = 0;
        *lines = 1;
        for (const auto& l : c.labels) {
            const float w = std::min(chip_w(l), room);
            if (x > 0 && x + w > room) {
                x = 0;
                y += chip_h + chip_gap;
                ++*lines;
            }
            r.chips.push_back({x0 + x, y0 + y, w, chip_h});
            x += w + chip_gap;
        }
        r.h = y0 + y + chip_h + rp;
        return r;
    };
    auto layout_chips = [&](const Choices& c, float width) {
        int beside = 0, above = 0;
        ChipRow r = flow_chips(c, width, false, &beside);
        if (beside == 1) return r;
        ChipRow a = flow_chips(c, width, true, &above);
        return above < beside || chip_w(c.labels.empty() ? std::string() : c.labels[0]) > width - rp * 2 - mark - cap_w
                   ? a : r;
    };

    // The cards' size: the sprite's box and the room for the longest name.
    std::vector<std::string> names = characters_;
    if (names.size() != size_t(stage_select::kCharacterCount))
        names.assign(std::begin(stage_select::kCharacters), std::end(stage_select::kCharacters));
    float name_w = 0;
    for (int i = 0; i < stage_select::kCharacterCount; ++i)
        if (!stage_select::is_placeholder_name(names[size_t(i)]))
            name_w = std::max(name_w, g.text_width(Font::Pixel, names[size_t(i)]));
    // The sprites: every character's, so that one scale suits them all and
    // switching keeps them in proportion. The box is about twice the
    // tallest at NORMAL on a 720-line window.
    float sprite_w = 0, sprite_h = 0;
    if (hooks_.character_sprite)
        for (int i = 0; i < stage_select::kCharacterCount; ++i) {
            float w = 0, h = 0;
            if (hooks_.character_sprite(i, &w, &h)) {
                sprite_w = std::max(sprite_w, w);
                sprite_h = std::max(sprite_h, h);
            }
        }
    if (sprite_h <= 0) { sprite_w = 34; sprite_h = 55; }
    float sprite_box = std::max(g.px(120), lh_p * 2 + g.px(18)) + g.px(6.0f * float(zoom));
    const float least_box = lh_p * 2 + g.px(18);
    const float full_box = sprite_box;
    float card_h = sprite_box + rp * 2;
    const float card_need = mark + sprite_box + g.px(12) + aw + g.px(12) + name_w + g.px(12) + aw + rp * 2;
    // Standing up: the sprite between the caption and the name, for two
    // cards side by side in one column.
    const float vcard_h = rp + lh_p + g.px(6) + sprite_box + g.px(6) + lh_p + rp;
    const float vcard_need = std::max(rp * 2 + mark + name_w + (aw + g.px(12)) * 2, sprite_box + rp * 2);
    const float records_h = s.timed ? 0.0f : rp * 2 + lh_p + g.px(4) + lh_s;
    const float start_h = lh_b + g.px(22) + g.px(4.0f * float(zoom));
    const float pic_aspect = 320.0f / 224.0f;  // the stage pictures are 4:3, 320x224
    // The place's name over the picture: in the large letters, unless they would
    // cover too much of it (a phone's narrow picture at a larger MENU SIZE).
    auto name_font = [&](const SDL_FRect& pic, const std::string& name) {
        const bool fits = g.text_width(Font::PixelBig, name) <= pic.w - g.px(28) - (aw + g.px(14)) * 2;
        return fits && lh_b + g.px(14) <= pic.h * 0.2f ? Font::PixelBig : Font::Pixel;
    };

    // The picture's size for a width and the height it may take: as large
    // as fits, and at a whole multiple of its pixels when that is close.
    auto picture_size = [&](float max_w, float max_h) {
        float w = std::max(g.px(80), max_w), h = w / pic_aspect;
        if (h > max_h) { h = std::max(g.px(56), max_h); w = h * pic_aspect; }
        const float k = w / 320.0f;
        const float whole = std::floor(k);
        if (whole >= 1.0f && whole / k >= 0.85f) { w = 320.0f * whole; h = 224.0f * whole; }
        return SDL_FRect{0, 0, std::round(w), std::round(h)};
    };

    // --- Where everything goes. -----------------------------------------
    // Each piece in its column's own coordinates (y from the column's top);
    // the columns scroll on their own when taller than the window.
    const std::string place_name = stage_select::kPlaces[std::clamp(*s.place, 0, stage_select::kPlaceCount - 1)].name;
    bool band_below = false;
    Font below_font = Font::Pixel;
    SDL_FRect pic_box{};
    enum Piece { kPicture, kAct, kTime, kPlayer, kCombi, kRecords, kGhost, kPlayers, kStart, kPieces };
    SDL_FRect at[kPieces] = {};
    int col_of[kPieces] = {};
    SDL_FRect view[2] = {};
    float content_h[2] = {0, 0};
    int columns = 1;
    ChipRow act_l, time_l, players_l, ghost_l;
    float ghost_top = 0;  // in the records card

    auto records_height = [&](float width) {
        if (!s.timed) return records_h;
        ghost_l = layout_chips(ghosts, width);
        ghost_top = rp + lh_p + g.px(6);
        return ghost_top + ghost_l.h;
    };

    const float gutter = g.px(20);
    float right_need = std::max(card_need, rp * 2 + mark + cap_w + chip_w("1 PLAYER") + chip_gap + chip_w("2 PLAYERS"));
    float right_w = std::clamp(body_w * 0.38f, std::min(right_need, body_w * 0.5f), body_w * 0.5f);
    bool landscape = W > H * 1.05f && body_w - gutter - right_w >= body_w * 0.42f;
    if (landscape) {
        columns = 2;
        float left_w = body_w - gutter - right_w;
        // The rows under the picture, at the picture's width, which the
        // height may make narrower than the column.
        act_l = layout_chips(act, left_w);
        time_l = layout_chips(times, left_w);
        float rows_h = act_l.h + time_l.h + gap * 2;
        SDL_FRect pic = picture_size(left_w, body_h - rows_h);
        if (pic.h < body_h * 0.4f) pic = picture_size(left_w, body_h * 0.4f);
        // The rows as wide as the picture, unless that would fold them.
        float lw = std::max(pic.w, std::min(left_w, g.px(240)));
        const ChipRow act_n = layout_chips(act, lw), time_n = layout_chips(times, lw);
        if (act_n.h + time_n.h > act_l.h + time_l.h + 0.5f) {
            lw = left_w;
        } else {
            act_l = act_n;
            time_l = time_n;
        }
        rows_h = act_l.h + time_l.h + gap * 2;
        // The right column takes what the picture leaves, within reason, and
        // the two are centred across the window together.
        right_w = std::min(body_w - lw - gutter, std::max(right_need * 1.15f, body_w * 0.36f));
        const float total_w = lw + gutter + right_w;
        const float x0 = M + std::max(0.0f, (body_w - total_w) * 0.5f);
        pic_box = pic;
        if (g.text_width(Font::PixelBig, place_name) > pic.w - g.px(28) - (aw + g.px(14)) * 2) {
            // The name under the picture (see the column below).
            band_below = true;
            below_font = g.text_width(Font::PixelBig, place_name) <= lw - g.px(28) - (aw + g.px(14)) * 2
                             ? Font::PixelBig : Font::Pixel;
            at[kPicture] = {0, 0, lw, pic.h + g.px(6) + g.line_height(below_font) + g.px(14)};
        } else {
            at[kPicture] = {0, 0, pic.w, pic.h};
        }
        at[kAct] = {0, at[kPicture].h + gap, lw, act_l.h};
        at[kTime] = {0, at[kAct].y + act_l.h + gap, lw, time_l.h};
        content_h[0] = at[kTime].y + time_l.h;
        float y = 0;
        players_l = layout_chips(players, right_w);
        // The cards give up some of their height on a short window, down to
        // two lines of letters, so that the column fits if it can.
        {
            const float rest = records_height(right_w) + players_l.h + start_h + gap * 4;
            const float fit = (std::max(body_h, content_h[0]) - rest) * 0.5f - rp * 2;
            sprite_box = std::clamp(fit, least_box, sprite_box);
            card_h = sprite_box + rp * 2;
        }
        for (Piece p : {kPlayer, kCombi}) {
            at[p] = {0, y, right_w, card_h};
            y += card_h + gap;
        }
        const float rh = records_height(right_w);
        at[kRecords] = {0, y, right_w, rh};
        y += rh + gap;
        at[kPlayers] = {0, y, right_w, players_l.h};
        y += players_l.h + gap;
        at[kStart] = {0, y, right_w, start_h};
        content_h[1] = y + start_h;
        // START at the foot of the picture's rows when the right column is
        // the shorter, so the two columns end together.
        if (content_h[1] < content_h[0]) {
            at[kStart].y += content_h[0] - content_h[1];
            content_h[1] = content_h[0];
        }
        for (Piece p : {kPlayer, kCombi, kRecords, kPlayers, kStart}) col_of[p] = 1;
        const float tall = std::max(content_h[0], content_h[1]);
        const float top = tall < body_h ? body_top + (body_h - tall) * 0.5f : body_top;
        view[0] = {x0, top, lw, std::min(body_h, body_bottom - top)};
        view[1] = {x0 + lw + gutter, top, right_w, std::min(body_h, body_bottom - top)};
        // A larger MENU SIZE on a short window leaves the picture's column no
        // room: one column then, which scrolls, with a picture worth seeing.
        if (content_h[0] > body_h + 0.5f) landscape = false;
    }
    if (!landscape) {
        columns = 1;
        band_below = false;
        for (int& c : col_of) c = 0;
        sprite_box = full_box;
        card_h = sprite_box + rp * 2;
        // One column, as wide as a phone allows and no wider than reads well.
        const float cw = std::min(body_w, std::max(g.px(420), body_w * 0.7f));
        act_l = layout_chips(act, cw);
        time_l = layout_chips(times, cw);
        players_l = layout_chips(players, cw);
        // On a short, wide window the picture may take most of the height: the
        // column scrolls anyway.
        const SDL_FRect pic = picture_size(cw, W > H ? body_h * 0.8f : std::max(body_h * 0.45f, g.px(120)));
        float y = 0;
        pic_box = pic;
        // A name too long to go over the picture goes under it instead.
        if (g.text_width(Font::PixelBig, place_name) > pic.w - g.px(28) - (aw + g.px(14)) * 2) {
            band_below = true;
            below_font = g.text_width(Font::PixelBig, place_name) <= cw - g.px(28) - (aw + g.px(14)) * 2
                             ? Font::PixelBig : Font::Pixel;
            at[kPicture] = {0, y, cw, pic.h + g.px(6) + g.line_height(below_font) + g.px(14)};
        } else {
            at[kPicture] = {(cw - pic.w) * 0.5f, y, pic.w, pic.h};
        }
        y += at[kPicture].h + gap;
        at[kAct] = {0, y, cw, act_l.h};
        y += act_l.h + gap;
        at[kTime] = {0, y, cw, time_l.h};
        y += time_l.h + gap;
        // The two cards side by side when they fit, standing up if they
        // must, else one above the other.
        if (cw >= card_need * 2 + gap || cw >= vcard_need * 2 + gap) {
            const float half = (cw - gap) * 0.5f;
            const float h = cw >= card_need * 2 + gap ? card_h : vcard_h;
            at[kPlayer] = {0, y, half, h};
            at[kCombi] = {half + gap, y, half, h};
            y += h + gap;
        } else {
            at[kPlayer] = {0, y, cw, card_h};
            y += card_h + gap;
            at[kCombi] = {0, y, cw, card_h};
            y += card_h + gap;
        }
        const float rh = records_height(cw);
        at[kRecords] = {0, y, cw, rh};
        y += rh + gap;
        at[kPlayers] = {0, y, cw, players_l.h};
        y += players_l.h + gap;
        at[kStart] = {0, y, cw, start_h};
        content_h[0] = y + start_h;
        const float top = content_h[0] < body_h ? body_top + (body_h - content_h[0]) * 0.5f : body_top;
        view[0] = {M + (body_w - cw) * 0.5f, top, cw, std::min(body_h, body_bottom - top)};
    }
    if (s.timed) {
        at[kGhost] = {at[kRecords].x, at[kRecords].y + ghost_top, at[kRecords].w, ghost_l.h};
        col_of[kGhost] = col_of[kRecords];
    }

    // The selected entry's piece, kept in view.
    auto piece_of_row = [&](int row) -> int {
        if (row < 0) return -1;
        if (row == s.place_row) return kPicture;
        if (row == s.level_row) return kAct;
        if (row == s.time_row) return kTime;
        if (row == s.player_row) return kPlayer;
        if (row == s.combi_row) return kCombi;
        if (row == s.ghost_row) return kGhost;
        if (row == s.players_row) return kPlayers;
        if (row == s.start_row) return kStart;
        return -1;
    };
    for (int c = 0; c < columns; ++c) {
        const float most = std::max(0.0f, content_h[c] - view[c].h);
        float& sc = sc_scroll_[c];
        const int p = piece_of_row(selected_);
        if (p >= 0 && col_of[p] == c) {
            const float y0 = at[p].y - gap * 0.5f, y1 = at[p].y + at[p].h + gap * 0.5f;
            if (y0 < sc) sc = y0;
            if (y1 > sc + view[c].h) sc = y1 - view[c].h;
        }
        sc = std::clamp(sc, 0.0f, most);
        if (most <= 0) sc = 0;
    }
    // A piece on the screen.
    auto screen = [&](int p) {
        const int c = col_of[p];
        return SDL_FRect{view[c].x + at[p].x, view[c].y + at[p].y - sc_scroll_[c], at[p].w, at[p].h};
    };
    // Touch targets are only where they can be seen: cut to the column.
    int clip_col = 0;
    auto add_hit = [&](SDL_FRect r, int row, std::function<void()> tap = {}, float back_x = 0, float back_w = 0) {
        if (row < 0) return;
        const SDL_FRect v = view[clip_col];
        const float y0 = std::max(r.y, v.y), y1 = std::min(r.y + r.h, v.y + v.h);
        if (y1 - y0 < std::min(r.h, g.px(16)) * 0.6f) return;
        const float x0 = std::max(r.x, 0.0f), x1 = std::min(r.x + r.w, W);
        if (x1 <= x0) return;
        hits_.push_back({{x0, y0, x1 - x0, y1 - y0}, row, back_x, back_w, std::move(tap)});
    };
    auto step_tap = [this](int row, int step) {
        return std::function<void()>([this, row, step] {
            if (row >= 0 && items_[size_t(row)].act) items_[size_t(row)].act(step);
        });
    };

    // A frame for a row or a card: quiet, or in the accent when selected.
    auto frame = [&](const SDL_FRect& r, bool sel) {
        g.rect(r, ui::theme::background.alpha(sel ? 200 : 150), g.px(6));
        g.outline(r, sel ? acc : ui::theme::outline, g.px(6), sel ? g.px(3) : g.px(1.5f));
    };
    auto marker = [&](float x, float y, bool sel) {
        if (sel) g.text(Font::Pixel, x + bob, y, ">", acc);
    };
    // Text in the yellow of a choice: the game's letters tinted, or the
    // pixel face in that colour.
    auto yellow_text = [&](Font f, float x, float y, float w, const std::string& t) {
        g.tint_game_font(true);
        const float drawn = g.text_fit(f, x, y, w, t, yellow);
        g.tint_game_font(false);
        return drawn;
    };
    auto draw_chips = [&](const SDL_FRect& r, const Choices& c, const ChipRow& l, bool sel) {
        frame(r, sel);
        const float cy = r.y + (l.caption_above ? rp : rp + (chip_h - lh_p) * 0.5f);
        marker(r.x + rp - g.px(2), cy, sel);
        g.text_fit(Font::Pixel, r.x + rp + mark, cy, r.w - rp * 2 - mark, c.caption,
                   sel ? ui::theme::text : ui::theme::text_dim);
        for (size_t i = 0; i < l.chips.size(); ++i) {
            const SDL_FRect cr{r.x + l.chips[i].x, r.y + l.chips[i].y, l.chips[i].w, l.chips[i].h};
            const bool cur = int(i) == c.current;
            if (cur) {
                g.rect(cr, yellow.alpha(sel ? 70 : 46), g.px(4));
                g.outline(cr, yellow.alpha(sel ? 255 : 200), g.px(4), g.px(2));
            } else {
                g.rect(cr, ui::theme::surface.alpha(170), g.px(4));
                g.outline(cr, ui::theme::outline, g.px(4), g.px(1));
            }
            const float tw = std::min(g.text_width(Font::Pixel, c.labels[i]), cr.w - chip_pad);
            const float tx = cr.x + (cr.w - tw) * 0.5f, ty = cr.y + (cr.h - lh_p) * 0.5f;
            if (cur) yellow_text(Font::Pixel, tx, ty, cr.w - chip_pad, c.labels[i]);
            else g.text_fit(Font::Pixel, tx, ty, cr.w - chip_pad, c.labels[i], ui::theme::text_faint);
            if (i < c.set.size()) add_hit(cr, c.row, c.set[i]);
        }
        // The row's caption and the space around the chips select it.
        add_hit(r, c.row);
    };
    // Hits are pushed in drawing order and the first that contains a tap
    // wins, so a chip goes before the row it lies on (draw_chips adds the
    // row last); the same for the arrows on the picture and the cards.
    auto draw_card = [&](const SDL_FRect& r, int row, const char* caption, int who, bool mirror) {
        const bool sel = selected_ == row;
        frame(r, sel);
        // Lying down (the sprite on the left) when the card is as tall as
        // one, else standing up (the sprite between the caption and name).
        const bool lying = r.h < vcard_h - 0.5f;
        const float bw = std::min(sprite_box, lying ? r.w * 0.4f : r.w - rp * 2);
        const SDL_FRect box = lying ? SDL_FRect{r.x + rp + mark, r.y + rp, bw, sprite_box}
                                    : SDL_FRect{r.x + (r.w - bw) * 0.5f, r.y + rp + lh_p + g.px(6), bw, sprite_box};
        // who < 0: PARTNER NONE (issue #55), an empty stand rather than a
        // character -- the box dimmed, a faint frame where one would stand.
        const bool none = who < 0;
        g.rect(box, none ? ui::theme::background.alpha(110) : ui::theme::surface.alpha(150), g.px(4));
        float tw = 0, th = 0;
        SDL_Texture* tex = !none && hooks_.character_sprite ? hooks_.character_sprite(who, &tw, &th) : nullptr;
        if (none) {
            const float in = g.px(10);
            g.outline({box.x + in, box.y + in, box.w - in * 2, box.h - in * 2}, ui::theme::outline, g.px(4),
                      g.px(1.5f));
            const float sw = g.text_width(Font::Pixel, "SOLO");
            g.text_fit(Font::Pixel, box.x + std::max(in, (box.w - sw) * 0.5f), box.y + (box.h - lh_p) * 0.5f,
                       box.w - in * 2, "SOLO", ui::theme::text_faint);
        } else if (tex && tw > 0 && th > 0) {
            // One whole multiple of their pixels for every character (from
            // the largest), standing on the box's floor.
            const float fit = std::min((box.w - g.px(8)) / sprite_w, (box.h - g.px(8)) / sprite_h);
            const float k = fit >= 1.0f ? std::floor(fit) : fit;
            const float w = tw * k, h = th * k;
            g.image(tex, {0, 0, tw, th},
                    {std::round(box.x + (box.w - w) * 0.5f), std::round(box.y + box.h - g.px(4) - h), w, h}, fit < 1.0f,
                    mirror);
        } else {
            g.text(Font::PixelBig, box.x + (box.w - g.text_width(Font::PixelBig, "?")) * 0.5f,
                   box.y + (box.h - lh_b) * 0.5f, "?", ui::theme::text_faint);
        }
        float tx, tr, ty, ny;
        if (lying) {
            tx = box.x + box.w + g.px(12);
            tr = r.x + r.w - rp;
            const float block = lh_p * 2 + g.px(8);
            ty = r.y + (r.h - block) * 0.5f;
            ny = ty + lh_p + g.px(8);
            marker(r.x + rp - g.px(2), r.y + (r.h - lh_p) * 0.5f, sel);
        } else {
            tx = r.x + rp + mark;
            tr = r.x + r.w - rp;
            ty = r.y + rp;
            ny = box.y + box.h + g.px(6);
            marker(r.x + rp - g.px(2), ty, sel);
        }
        g.text_fit(Font::Pixel, tx, ty, tr - tx, caption, sel ? ui::theme::text_dim : ui::theme::text_faint);
        const float lx = lying ? tx : r.x + rp;
        const std::string name = none ? std::string("NONE")
                                 : who < int(names.size()) ? names[size_t(who)] : std::string("?");
        g.text(Font::Pixel, lx, ny, "<", sel ? acc : ui::theme::text_faint);
        g.text(Font::Pixel, tr - aw, ny, ">", sel ? acc : ui::theme::text_faint);
        const float room = tr - aw - g.px(8) - (lx + aw + g.px(8));
        const float nw = std::min(g.text_width(Font::Pixel, name), room);
        g.text_fit(Font::Pixel, lx + aw + g.px(8) + (room - nw) * 0.5f, ny, room, name,
                   sel ? ui::theme::text : ui::theme::text_dim);
        // The arrows' targets: the name's line, a finger's height either side;
        // the rest of the card selects it.
        const float hw = std::max(g.px(40), aw + g.px(20));
        const float hy = std::max(r.y, ny - g.px(14));
        const float hh = std::min(r.y + r.h, ny + lh_p + g.px(14)) - hy;
        add_hit({lx - g.px(10), hy, hw, hh}, row, step_tap(row, -1));
        add_hit({tr - hw + g.px(10), hy, hw, hh}, row, step_tap(row, +1));
        add_hit(r, row);
    };

    // --- Drawing, a column at a time. -------------------------------------
    const stage_select::Request req = showcase_request();
    for (int c = 0; c < columns; ++c) {
        clip_col = c;
        const SDL_FRect v = view[c];
        // Room for the frames' outlines and the marker's bob either side.
        const SDL_FRect clip{v.x - g.px(6), v.y - g.px(4), v.w + g.px(12), v.h + g.px(8)};
        g.clip(&clip);
        const size_t first_hit = hits_.size();
        for (int p = 0; p < kPieces; ++p) {
            if (col_of[p] != c) continue;
            const SDL_FRect r = screen(p);
            if (r.y > v.y + v.h || r.y + r.h < v.y) continue;
            switch (p) {
            case kPicture: {
                const bool sel = selected_ == s.place_row;
                float tw = 0, th = 0;
                int state = kPictureNone;
                SDL_Texture* tex = hooks_.stage_picture ? hooks_.stage_picture(req, &tw, &th, &state) : nullptr;
                const bool exact = state == kPictureReady && tex;
                // The picture, and the band with the place's name: over its
                // foot, or under it at the column's width (band_below).
                const SDL_FRect pr = band_below ? SDL_FRect{std::round(r.x + (r.w - pic_box.w) * 0.5f), r.y, pic_box.w, pic_box.h} : r;
                const Font nf = band_below ? below_font : name_font(r, place_name);
                const float band_h = g.line_height(nf) + g.px(14);
                const std::string& name = place_name;
                const float o = sel ? g.px(3) : g.px(2);
                g.rect({pr.x - o, pr.y - o, pr.w + o * 2, pr.h + o * 2}, sel ? acc : ui::theme::outline_strong, g.px(3));
                if (tex && tw > 0 && th > 0) {
                    if (exact && std::fabs(tw / th - pic_aspect) < 0.01f) {
                        const float k = pr.w / tw;
                        g.image(tex, {0, 0, tw, th}, pr, std::fabs(k - std::round(k)) > 0.01f);
                    } else {
                        g.image_cover(tex, tw, th, pr);
                    }
                    if (!exact) g.rect(pr, ui::theme::background.alpha(120));
                } else {
                    // Nothing of the place yet (or none to be had): the menu's own
                    // zone, darkened, rather than an empty box.
                    g.rect(pr, ui::theme::surface);
                    if (backdrop_) {
                        g.image_cover(backdrop_, backdrop_w_, backdrop_h_, pr);
                        g.rect(pr, ui::theme::background.alpha(185));
                    }
                }
                if (state == kPictureMaking) {
                    // Being made: a word and a few dots that move, in the
                    // middle of the picture, until it is there.
                    const int dots = int(anim_ * 2.0f) % 4;
                    const std::string word = "LOADING" + std::string(size_t(dots), '.');
                    const float ww = g.text_width(Font::Pixel, "LOADING...");
                    g.text(Font::Pixel, pr.x + (pr.w - ww) * 0.5f, pr.y + ((band_below ? pr.h : pr.h - band_h) - lh_p) * 0.5f, word,
                           ui::theme::text_dim);
                }
                // The place's name over the picture's foot, between arrows.
                const SDL_FRect band = band_below ? SDL_FRect{r.x, pr.y + pr.h + g.px(6), r.w, band_h}
                                                  : SDL_FRect{r.x, r.y + r.h - band_h, r.w, band_h};
                if (band_below) {
                    g.rect(band, ui::theme::background.alpha(170), g.px(6));
                    g.outline(band, sel ? acc : ui::theme::outline, g.px(6), sel ? g.px(3) : g.px(1.5f));
                } else {
                    g.rect({band.x, band.y - g.px(10), band.w, g.px(10)}, ui::theme::background.alpha(70));
                    g.rect(band, ui::theme::background.alpha(170));
                }
                const float ny = band.y + (band_h - g.line_height(nf)) * 0.5f;
                const float ay = band.y + (band_h - lh_p) * 0.5f;
                const float lx = band.x + g.px(14);
                const float room = std::max(g.px(20), band.w - g.px(28) - (aw + g.px(14)) * 2);
                g.text(Font::Pixel, lx, ay, "<", sel ? acc : ui::theme::text_dim);
                const float nw = g.text_fit(nf, lx + aw + g.px(14), ny, room, name, ui::theme::text);
                const float rx = lx + aw + g.px(14) + nw + g.px(14);
                g.text(Font::Pixel, rx, ay, ">", sel ? acc : ui::theme::text_dim);
                const float hw = std::max(g.px(44), aw + g.px(28));
                const int row = s.place_row;
                add_hit({band.x, band.y, std::max(hw, lx + aw + g.px(8) - band.x), band.h}, row, step_tap(row, -1));
                add_hit({rx - g.px(10), band.y, hw, band.h}, row, step_tap(row, +1));
                add_hit(band, row);
                break;
            }
            case kAct: draw_chips(r, act, act_l, selected_ == s.level_row); break;
            case kTime: draw_chips(r, times, time_l, selected_ == s.time_row); break;
            case kPlayer: draw_card(r, s.player_row, "PLAYER", *s.player, false); break;
            case kCombi:
                draw_card(r, s.combi_row, *s.two_players ? "PARTNER  2P" : "PARTNER",
                          showcase_no_partner() ? -1 : *s.combi, true);
                break;
            case kRecords:
                if (s.timed) {
                    // The record for the stage chosen, and the ghost row.
                    const bool sel = selected_ == s.ghost_row;
                    g.rect(r, ui::theme::background.alpha(150), g.px(6));
                    g.outline(r, ui::theme::outline, g.px(6), g.px(1.5f));
                    const float y = r.y + rp + g.px(2);
                    g.text(Font::Pixel, r.x + rp + mark, y, "BEST TIME", ui::theme::text_dim);
                    const std::string best = s.best_row >= 0 ? items_[size_t(s.best_row)].value() : std::string();
                    const float bw = g.text_width(Font::Pixel, best);
                    yellow_text(Font::Pixel, r.x + r.w - rp - g.px(6) - bw, y, bw + g.px(2), best);
                    const SDL_FRect gr = screen(kGhost);
                    if (sel) g.outline(gr, acc, g.px(6), g.px(3));
                    // The ghost's chips, without a frame of their own.
                    const float cy = gr.y + (ghost_l.caption_above ? rp : rp + (chip_h - lh_p) * 0.5f);
                    marker(gr.x + rp - g.px(2), cy, sel);
                    g.text(Font::Pixel, gr.x + rp + mark, cy, "GHOST", sel ? ui::theme::text : ui::theme::text_dim);
                    for (size_t i = 0; i < ghost_l.chips.size(); ++i) {
                        const SDL_FRect cr{gr.x + ghost_l.chips[i].x, gr.y + ghost_l.chips[i].y, ghost_l.chips[i].w,
                                           ghost_l.chips[i].h};
                        const bool cur = int(i) == ghosts.current;
                        g.rect(cr, cur ? yellow.alpha(sel ? 70 : 46) : ui::theme::surface.alpha(170), g.px(4));
                        g.outline(cr, cur ? yellow.alpha(sel ? 255 : 200) : ui::theme::outline, g.px(4),
                                  cur ? g.px(2) : g.px(1));
                        const float tw = g.text_width(Font::Pixel, ghosts.labels[i]);
                        const float tx = cr.x + (cr.w - tw) * 0.5f, ty = cr.y + (cr.h - lh_p) * 0.5f;
                        if (cur) yellow_text(Font::Pixel, tx, ty, cr.w, ghosts.labels[i]);
                        else g.text(Font::Pixel, tx, ty, ghosts.labels[i], ui::theme::text_faint);
                        add_hit(cr, s.ghost_row, ghosts.set[i]);
                    }
                    add_hit(gr, s.ghost_row);
                } else {
                    // LEVEL SELECT keeps nothing: it says so where the
                    // records would be.
                    g.rect(r, ui::theme::background.alpha(150), g.px(6));
                    g.outline(r, ui::theme::outline, g.px(6), g.px(1.5f));
                    g.text_fit(Font::Pixel, r.x + rp + mark, r.y + rp, r.w - rp * 2 - mark, "PRACTICE", acc);
                    // With PARTNER NONE (issue #55), how the level is played,
                    // or that the partner stays where NONE does not apply.
                    const bool kept_none = s.no_partner && *s.no_partner && !showcase_no_partner();
                    g.text_fit(Font::Small, r.x + rp + mark, r.y + rp + lh_p + g.px(4), r.w - rp * 2 - mark,
                               showcase_no_partner() ? "No partner: played as PARTNER SOLO."
                               : kept_none           ? "NONE is for 1 player in the attractions."
                                                     : "No clock, records or saving.",
                               ui::theme::text_dim);
                }
                break;
            case kPlayers: draw_chips(r, players, players_l, selected_ == s.players_row); break;
            case kStart: {
                const bool sel = selected_ == s.start_row;
                g.rect(r, acc.alpha(sel ? 210 : 120), g.px(8));
                g.outline(r, sel ? yellow : acc, g.px(8), sel ? g.px(3) : g.px(2));
                const float tw = g.text_width(Font::PixelBig, "START");
                const float ty = r.y + (r.h - lh_b) * 0.5f;
                if (sel) {
                    yellow_text(Font::PixelBig, r.x + (r.w - tw) * 0.5f + bob, ty, tw + g.px(4), "START");
                } else {
                    g.text(Font::PixelBig, r.x + (r.w - tw) * 0.5f, ty, "START", ui::theme::text);
                }
                const int row = s.start_row;
                add_hit(r, row, [this, row] { items_[size_t(row)].act(0); });
                break;
            }
            default: break;
            }
        }
        g.clip(nullptr);
        // More above or below: markers at the column's right, which step the
        // selection that way.
        const float most = content_h[c] - v.h;
        const float mw = g.px(48);
        if (sc_scroll_[c] > 0.5f) {
            const SDL_FRect m{v.x + v.w - mw, v.y, mw, lh_p + g.px(10)};
            g.rect(m, ui::theme::background.alpha(220), g.px(4));
            g.text(Font::Pixel, m.x + (mw - g.text_width(Font::Pixel, "^")) * 0.5f, m.y + g.px(5), "^", acc);
            hits_.insert(hits_.begin() + std::ptrdiff_t(first_hit), Hit{m, kScrollUp});
        }
        if (most - sc_scroll_[c] > 0.5f) {
            const SDL_FRect m{v.x + v.w - mw, v.y + v.h - lh_p - g.px(10), mw, lh_p + g.px(10)};
            g.rect(m, ui::theme::background.alpha(220), g.px(4));
            g.text(Font::Pixel, m.x + (mw - g.text_width(Font::Pixel, "v")) * 0.5f, m.y + g.px(5), "v", acc);
            hits_.insert(hits_.begin() + std::ptrdiff_t(first_hit), Hit{m, kScrollDown});
        }
    }
}

// AWARDS (issue #3): in the manner of the game's own screens rather than a
// settings list -- a slanted purple title banner edged in black, a ring-gold
// meter, and a card for each award with its emblem (award_art.h). Two columns
// of cards where the window is wide enough, one on a 4:3 or narrow window.
void Menu::draw_achievements(ui::Ui& g, achievements::Tracker& ach) {
    namespace art = award_art;
    namespace col = award_art::color;
    using ui::Font;
    const float W = g.width(), H = g.height();
    const float column = std::min(W - g.px(28), g.px(1180));
    const float x = (W - column) * 0.5f;
    const float edge = std::max(2.0f, std::round(g.px(3)));
    float y = std::max(g.px(12), H * 0.03f);

    fill(g, {0, 0, W, H}, ui::theme::background.alpha(200), 210);
    // A wash of the game's purple from the top, under the title.
    g.rect({0, 0, W, y + g.px(80)}, col::purple.alpha(70));

    // The title: AWARDS on a slanted banner, a magenta stripe under it.
    const float lean = (g.line_height(Font::PixelBig) + g.px(18)) * 0.45f;
    const SDL_FRect title_box = art::heading(g, x - lean * 0.5f, y, Font::PixelBig, "AWARDS", column);
    const float head_h = title_box.h;

    // Right of it: how many, and the points, beside a ring.
    char count[48], points[48];
    std::snprintf(count, sizeof count, "%d OF %zu", ach.unlocked_count(), ach.list().size());
    std::snprintf(points, sizeof points, "%d OF %d PTS", ach.points_earned(), ach.points_total());
    const float lp = g.line_height(Font::Pixel);
    const float info_w = std::max(g.text_width(Font::Pixel, count), g.text_width(Font::Pixel, points));
    const float ring_r = lp * 0.95f;
    const float info_x = x + column - info_w;
    const bool info_fits = info_x - ring_r * 2 - g.px(12) > title_box.x + title_box.w;
    if (info_fits) {
        const float iy = y + (head_h - lp * 2 - g.px(4)) * 0.5f;
        art::emblem(g, info_x - ring_r - g.px(12), y + head_h * 0.5f, ring_r, "ring", true, anim_ * 0.12f);
        g.text(Font::Pixel, info_x, iy, count, ui::theme::text);
        g.text(Font::Pixel, info_x, iy + lp + g.px(4), points, ui::theme::text);
    }
    y += head_h + g.px(14);
    if (!info_fits) {  // a narrow window: the figures go under the title
        g.text_fit(Font::Pixel, x, y, column, std::string(count) + "  " + points, ui::theme::text);
        y += lp + g.px(8);
    }

    // The meter: points earned, in ring gold.
    const float bar_h = std::max(g.px(12), 6.0f);
    const int total = ach.points_total();
    const float done = total > 0 ? float(ach.points_earned()) / float(total) : 0.0f;
    g.rect({x, y, column, bar_h}, col::ink, bar_h * 0.5f);
    const SDL_FRect track{x + edge, y + edge, column - edge * 2, bar_h - edge * 2};
    g.rect(track, col::locked_dark, track.h * 0.5f);
    if (done > 0) {
        const SDL_FRect f{track.x, track.y, std::max(track.h, track.w * done), track.h};
        g.rect(f, col::gold_dark, f.h * 0.5f);
        g.rect({f.x, f.y, f.w, f.h * 0.55f}, col::gold, f.h * 0.27f);
    }
    y += bar_h + g.px(12);

    // The cards.
    const float hint_h = g.line_height(kHintFont) + g.px(18);
    // Two columns only where a card is still wide enough for its line of
    // description: a 16:9 window, not a 4:3 one or a phone at a larger size.
    const int cols = column >= g.px(1120) ? 2 : 1;
    const float gap = g.px(10);
    const float card_w = (column - gap * float(cols - 1)) / float(cols);
    const float pad = g.px(10);
    const float card_h = lp + g.line_height(Font::Body) + pad * 2 + g.px(4);
    const float bottom = H - hint_h;
    const auto& all = ach.list();
    const int lines = (int(all.size()) + cols - 1) / cols;
    rows_ = std::max(1, int((bottom - y + gap) / (card_h + gap)));
    row_h_ = card_h + gap;
    scroll_ = std::clamp(scroll_, 0, std::max(0, lines - rows_));
    for (int line = scroll_; line < std::min(lines, scroll_ + rows_); ++line) {
        for (int c = 0; c < cols; ++c) {
            const size_t i = size_t(line * cols + c);
            if (i >= all.size()) break;
            const auto& a = all[i];
            const float cx0 = x + float(c) * (card_w + gap);
            const SDL_FRect card{cx0, y, card_w, card_h};
            const float cl = g.px(12);  // the cards lean a little, as the title does
            art::banner(g, card, cl, a.unlocked ? col::night : ui::Color{20, 16, 34}, col::ink, edge);
            if (a.unlocked)  // a magenta edge along the top of an earned one
                art::banner(g, {card.x + cl, card.y + edge, card.w - cl * 2.0f, std::max(2.0f, g.px(3))}, 0,
                            col::magenta, col::magenta, 0);
            const float er = card_h * 0.5f - g.px(4);
            unsigned seed = 0;
            for (char ch : a.title) seed = seed * 31u + unsigned(uint8_t(ch));
            art::emblem(g, card.x + cl * 0.5f + pad + er, card.y + card_h * 0.5f, er, a.icon, a.unlocked, 0, seed);

            // The points, at the right: a small ring and the number.
            char pts[16];
            std::snprintf(pts, sizeof pts, "%d", a.points);
            const float pw = g.text_width(Font::Pixel, pts);
            const float rr = lp * 0.4f;
            const float px_r = card.x + card_w - cl * 0.5f - pad - pw;
            art::emblem(g, px_r - rr - g.px(6), card.y + pad + lp * 0.5f, rr, "ring", a.unlocked);
            g.text(Font::Pixel, px_r, card.y + pad, pts, a.unlocked ? ui::theme::text : ui::theme::text_dim);

            const float tx = card.x + cl * 0.5f + pad + er * 2 + g.px(10);
            const float tw = std::max(g.px(40), px_r - rr * 2 - g.px(14) - tx);
            // Not earned: the game's letters greyed (tinted), not just dimmed.
            g.tint_game_font(!a.unlocked);
            g.text_fit(Font::Pixel, tx, card.y + pad, tw, a.title,
                       a.unlocked ? ui::theme::text : ui::Color{150, 140, 175});
            g.tint_game_font(false);
            const float dw = card.x + card_w - cl - pad - tx;
            g.text_fit(Font::Body, tx, card.y + pad + lp + g.px(4), dw, a.description,
                       a.unlocked ? col::text : col::text_dim);
        }
        y += card_h + gap;
    }

    // More above or below: two markers at the bottom right that step the
    // list, since a finger has no Up/Down. A tap anywhere else closes it.
    const float mh = std::min(hint_h - g.px(4), lp + g.px(12)), mw = g.px(64);
    const float my = H - mh - g.px(3);
    auto marker = [&](float mx, bool up, int index, bool live) {
        const SDL_FRect m{mx, my, mw, mh};
        art::banner(g, m, mh * 0.3f, live ? col::magenta : col::locked_dark, col::ink, edge);
        // An arrowhead, edged in black like everything else.
        art::arrowhead(g, m.x + mw * 0.5f, m.y + mh * 0.5f, mh * 0.3f, up, live ? col::gold : col::locked, edge);
        if (live) hits_.push_back({{m.x - g.px(6), m.y - g.px(14), m.w + g.px(12), m.h + g.px(14)}, index});
    };
    const bool more = lines > rows_;
    if (more) {
        marker(x + column - mw, false, kScrollDown, scroll_ + rows_ < lines);
        marker(x + column - mw * 2 - g.px(8), true, kScrollUp, scroll_ > 0);
    }
    const float hint_w = column - (more ? mw * 2 + g.px(16) : 0.0f);
    g.text_fit(kHintFont, x, H - g.line_height(kHintFont) - g.px(10), hint_w,
               "Up/Down scroll  Enter or Esc back", faint());
}

} // namespace chaotix
