#include "platform/menu.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <iterator>
#include <string>

#include "frontend/backdrop.h"
#include "frontend/config.h"
#include "game/achievements.h"
#include "game/cheats.h"

namespace chaotix {

namespace {

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
           p == Page::Input || p == Page::Extras || p == Page::Lobby || p == Page::Mods || p == Page::ModInfo;
}

// Where BACK and Escape go from a page below another one.
Menu::Page Menu::parent_of(Page p) {
    switch (p) {
    case Page::Video: case Page::Sound: case Page::Input: case Page::Extras: return Page::Options;
    case Page::Cheats: case Page::Lobby: return Page::Extras;
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
    selected_ = 0;
    scroll_ = 0;
    anim_ = 0;
    awaiting_.clear();
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
    else if (p == Page::Cheats) build_cheats();
    else if (p == Page::Lobby) build_lobby();
    else if (p == Page::Mods) {
        build_mods();
        // Back from a pack's page: on that pack again.
        if (from == Page::ModInfo && mod_index_ < items_.size()) selected_ = int(mod_index_);
    } else if (p == Page::ModInfo) build_mod_info();
    else items_.clear();
}

void Menu::close() {
    leave_mods();
    page_ = Page::Closed;
}

void Menu::leave_mods() {
    if (!mods_dirty_) return;
    mods_dirty_ = false;
    if (hooks_.reload_mods) hooks_.reload_mods();
}

void Menu::open_front() { set_page(Page::Front); }

void Menu::toggle() {
    if (open()) close(); else set_page(Page::Main);
}

void Menu::show_achievements() { set_page(Page::Achievements); }

bool Menu::show_page(const std::string& name) {
    if (name == "front") set_page(Page::Front);
    else if (name == "timeattack") set_page(Page::TimeAttack);
    else if (name == "cheats") set_page(Page::Cheats);
    else if (name == "lobby") set_page(Page::Lobby);
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
    case Page::Achievements: return "awards";
    case Page::Cheats: return "cheats";
    case Page::States: return "states";
    case Page::StateKeys: return "statekeys";
    case Page::Lobby: return "lobby";
    case Page::Mods: return "mods";
    case Page::ModInfo: return "modinfo";
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

// The front end: what pressing Start on the title screen reaches. START GAME
// hands over to the game's own menus, so nothing the original offers is lost.
void Menu::build_front() {
    items_.clear();
    items_.push_back({"START GAME", {}, [this](int) {
        close();
        if (hooks_.start_game) hooks_.start_game();
    }});
    items_.push_back({"TIME ATTACK", {}, [this](int) { set_page(Page::TimeAttack); }});
    items_.push_back({"OPTIONS", {}, [this](int) { set_page(Page::Options); }});
    items_.push_back({"CONTROLS", {}, [this](int) { set_page(Page::Controls); }});
    items_.push_back({"AWARDS", {}, [this](int) { set_page(Page::Achievements); }});
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

// TIME ATTACK: the fields the game's own stage select offers, in its order, so
// the choices are the game's rather than an invention of ours. Everything is
// kept in the settings file, so the same run can be set up again in one go.
void Menu::build_time_attack() {
    using namespace stage_select;
    items_.clear();
    Config* c = cfg_;
    if (!c) return;

    // A place only has certain levels, so changing the place pulls the level
    // to one it actually has.
    auto fix_level = [c] {
        if (!has_level(c->ta_place, c->ta_level)) c->ta_level = first_level(c->ta_place);
    };
    fix_level();

    items_.push_back({"PLACE",
                      [c] { return std::string(kPlaces[c->ta_place].name); },
                      [c, fix_level](int s) {
                          if (!s) return;
                          c->ta_place = (c->ta_place + s + kPlayablePlaces) % kPlayablePlaces;
                          fix_level();
                      }});
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
    items_.push_back({"AT-TIME",
                      [c] { return std::string(kTimes[(c->ta_time >> 1) & 3]); },
                      [c](int s) { if (s) c->ta_time = ((c->ta_time / 2 + s + 4) % 4) * 2; }});
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
    items_.push_back({"PLAYER",
                      [c, name_of] { return name_of(c->ta_player); },
                      [c, step_character](int s) { if (s) step_character(c->ta_player, s); }});
    items_.push_back({"COMBI",
                      [c, name_of] { return name_of(c->ta_combi); },
                      [c, step_character](int s) { if (s) step_character(c->ta_combi, s); }});
    items_.push_back({"PLAYERS",
                      [c] { return std::string(c->ta_two_players ? "2 PLAYERS" : "1 PLAYER"); },
                      [c](int s) { if (s) c->ta_two_players = !c->ta_two_players; }});
    // Not a setting: the record for whatever is selected above, so choosing a
    // stage shows what there is to beat. Left and right do nothing.
    Item best{"BEST TIME",
              [c] {
                  const int b = best_time(*c, c->ta_place, c->ta_level);
                  return b ? format_time(b) : std::string("--");
              },
              [](int) {}};
    best.read_only = true;
    items_.push_back(std::move(best));
    items_.push_back({"START", {}, [this, c](int) {
        Request r;
        r.place = uint16_t(c->ta_place);
        r.level = uint16_t(c->ta_level);
        r.attime = uint16_t(c->ta_time);
        r.player = uint16_t(c->ta_player);
        r.combi = uint16_t(c->ta_combi);
        r.two_players = c->ta_two_players;
        if (!valid(r)) return;
        close();
        if (hooks_.start_stage) hooks_.start_stage(r);
    }});
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
    // The lobby and the INTRODUCTION Eggman scene stay 4:3 because they glitch
    // when widened (issues #13, #5); some players want them wide regardless.
    items_.push_back({"WIDE LOBBY",
                      [c] { return on_off(c->wide_everywhere); },
                      [c](int s) { if (s) c->wide_everywhere = !c->wide_everywhere; }});
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
    // -1 is the catcher; the gap in the game's table (slot 1) is skipped
    // while it has only the placeholder for a name, as TIME ATTACK does.
    items_.push_back({"PARTNER",
                      [c, names] {
                          return c->lobby_partner < 0 || c->lobby_partner >= kCharacterCount
                                     ? std::string("CATCHER")
                                     : names[size_t(c->lobby_partner)];
                      },
                      [c, names](int s) {
                          if (!s) return;
                          for (int i = 0; i <= kCharacterCount; ++i) {
                              c->lobby_partner += s;
                              if (c->lobby_partner < -1) c->lobby_partner = kCharacterCount - 1;
                              if (c->lobby_partner >= kCharacterCount) c->lobby_partner = -1;
                              if (c->lobby_partner < 0 || !is_placeholder_name(names[size_t(c->lobby_partner)])) return;
                          }
                      },
                      "your own character keeps the catcher's"});
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
    if (is_row_page(page_)) {
        close();
    } else {
        const Page up = parent_of(page_);
        set_page(up != Page::Closed ? up : return_to_);
    }
}

bool Menu::on_key(SDL_Keycode key) {
    if (!open()) return false;
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
    drag_y_ = y;
    drag_total_ = 0;
    for (const Hit& h : hits_) {
        if (x < h.r.x || x > h.r.x + h.r.w || y < h.r.y || y > h.r.y + h.r.h) continue;
        // The markers of a list longer than the screen step through it.
        if (h.index == kScrollUp || h.index == kScrollDown) {
            move(h.index == kScrollUp ? -1 : +1);
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

// The front end is a screen of its own: the game's own title art on the left,
// the list on the right, in the manner of a modern re-release. Everywhere
// else the menu sits over the game, so the image keeps the whole window.
bool Menu::art_panel(ui::Ui& g, SDL_FRect* out) const {
    if (page_ != Page::Front) return false;
    const float margin = g.px(34);
    // The 4:3 image gets the left side, as large as the height allows.
    const float max_w = g.width() * 0.54f - margin * 2;
    const float max_h = g.height() - margin * 2;
    float w = max_w, h = w * 3.0f / 4.0f;
    if (h > max_h) { h = max_h; w = h * 4.0f / 3.0f; }
    *out = {margin + (max_w - w) * 0.5f, (g.height() - h) * 0.5f, w, h};
    return true;
}

void Menu::set_backdrop(SDL_Texture* texture, float width, float height, ui::Color tint, ui::Color accent) {
    backdrop_ = texture;
    backdrop_w_ = width;
    backdrop_h_ = height;
    zone_tint_ = tint;
    zone_accent_ = accent;
}

bool Menu::backdrop_on() const { return backdrop_ && cfg_ && cfg_->menu_backdrop_on(); }

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

    // Dim the game rather than cover it: the characters on the title screen
    // are the backdrop. On the front end with a zone's picture, only the
    // title's panel is dimmed, as before; the picture has its own tint.
    SDL_FRect art{};
    if (page_ == Page::Front && backdrop_on() && art_panel(g, &art))
        g.rect(art, ui::theme::background.alpha(150));
    else
        g.rect({0, 0, g.width(), g.height()}, ui::theme::background.alpha(150));

    if (page_ == Page::Front)
        draw_front(g);
    else if (page_ == Page::Main)
        draw_row_page(g, "Left/Right choose    Enter select    Esc close");
    else if (is_list_page(page_))
        draw_options(g);
    else
        draw_achievements(g, ach);
    g.enable_game_font(false);
}

// A vertical list down the right-hand side, beside the art panel.
void Menu::draw_backdrop_dim(ui::Ui& g) const {
    // With a zone picture the title is still drawn in its panel, so the
    // window around it can be the picture instead of a darkened copy.
    fill(g, {0, 0, g.width(), g.height()}, ui::theme::background.alpha(226), 185);
}

void Menu::draw_front(ui::Ui& g) {
    SDL_FRect art{};
    art_panel(g, &art);

    // A frame around the game's image, so it reads as a picture rather than
    // as the game running underneath.
    g.outline({art.x - g.px(3), art.y - g.px(3), art.w + g.px(6), art.h + g.px(6)},
              accent(), g.px(3), g.px(3));

    const float left = art.x + art.w + g.px(40);
    const float right = g.width() - g.px(28);
    const float column = std::max(g.px(120), right - left);

    const float row_h = g.line_height(ui::Font::PixelBig) + g.px(14);
    const float title_h = g.line_height(ui::Font::PixelBig) + g.px(26);
    const float total = title_h + row_h * float(items_.size());
    float y = std::max(g.px(24), (g.height() - total) * 0.5f);

    g.text(ui::Font::Pixel, left, y, "KNUCKLES", ui::theme::text_dim);
    y += g.line_height(ui::Font::Pixel) + g.px(2);
    g.text(ui::Font::PixelBig, left, y, "CHAOTIX", accent());
    y += title_h;

    for (size_t i = 0; i < items_.size(); ++i) {
        const bool sel = int(i) == selected_;
        const float w = g.text_width(ui::Font::PixelBig, items_[i].label);
        if (sel) {
            // The selected entry sits on a bar, the way these menus mark a
            // choice, with a marker that bobs.
            const float bob = std::sin(anim_ * 3.0f) * g.px(2);
            g.rect({left - g.px(12), y - g.px(6), std::min(column, w + g.px(36)),
                    g.line_height(ui::Font::PixelBig) + g.px(12)},
                   backdrop_on() ? zone_accent_.alpha(80) : ui::theme::accent_dim, g.px(4));
            g.text(ui::Font::PixelBig, left + bob, y, items_[i].label, ui::theme::text);
        } else {
            g.text(ui::Font::PixelBig, left, y, items_[i].label, ui::theme::text_faint);
        }
        hits_.push_back({{left - g.px(12), y - g.px(6), column, row_h}, int(i)});
        y += row_h;
    }

    g.text(ui::Font::Small, left, g.height() - g.px(26),
           "Up/Down choose    Enter select", faint());
}

void Menu::draw_row_page(ui::Ui& g, const char* hint) {
    const float item_h = g.line_height(ui::Font::Pixel);
    const float hint_h = g.line_height(ui::Font::Small);
    // A band across the lower part, tall enough to hold the row and the hint,
    // so the art above it stays visible.
    const float band_h = g.px(26) + item_h + g.px(14) + hint_h + g.px(16);
    const float band_y = g.height() - band_h - g.px(24);
    // Solid, or a zone's picture under a heavy tint: the title screen behind
    // it is bright, and a bar across the bottom is what these menus looked
    // like anyway.
    fill(g, {0, band_y, g.width(), band_h}, ui::theme::background, 205);
    g.rect({0, band_y, g.width(), g.px(3)}, accent());
    g.rect({0, band_y + band_h - g.px(3), g.width(), g.px(3)}, accent());

    // Measure first so the row can be centred as a whole.
    const float gap = g.px(34);
    float total = 0;
    for (size_t i = 0; i < items_.size(); ++i) {
        total += g.text_width(ui::Font::Pixel, items_[i].label);
        if (i + 1 < items_.size()) total += gap;
    }
    float x = (g.width() - total) * 0.5f;
    const float y = band_y + g.px(26);

    for (size_t i = 0; i < items_.size(); ++i) {
        const bool sel = int(i) == selected_;
        const float w = g.text_width(ui::Font::Pixel, items_[i].label);
        if (sel) {
            // The selected entry gets a marker that bobs, the way these menus
            // have always done it.
            const float bob = std::sin(anim_ * 3.0f) * g.px(2);
            g.text(ui::Font::Pixel, x - g.px(24) + bob, y, ">", accent());
            g.text(ui::Font::Pixel, x, y, items_[i].label, ui::theme::text);
            g.rect({x, y + item_h + g.px(4), w, g.px(3)}, accent());
        } else {
            g.text(ui::Font::Pixel, x, y, items_[i].label, ui::theme::text_faint);
        }
        hits_.push_back({{x - g.px(24), y - g.px(10), w + g.px(34), item_h + g.px(20)}, int(i)});
        x += w + gap;
    }

    g.text(ui::Font::Small, (g.width() - g.text_width(ui::Font::Small, hint)) * 0.5f,
           band_y + band_h - g.px(16) - hint_h, hint, faint());
}

void Menu::draw_options(ui::Ui& g) {
    const float pad = g.px(34);   // room for the selection marker inside the panel
    const float column = std::min(g.width() - pad * 2 - g.px(24), g.px(560));
    const float x = (g.width() - column) * 0.5f;
    const float row_h = g.line_height(ui::Font::Pixel) + g.px(16);
    const float head_h = g.line_height(ui::Font::PixelBig) + g.px(22);
    const float hint_h = g.line_height(ui::Font::Small);
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
    if (backdrop_on()) {
        fill(g, panel, ui::theme::background, 205);
        g.outline(panel, accent(), g.px(4), g.px(3));
    } else {
        g.panel(panel, ui::theme::background.alpha(240), accent(), g.px(4), g.px(3));
    }

    const char* heading = page_ == Page::TimeAttack ? "TIME ATTACK"
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
                        : "OPTIONS";
    std::string pack_heading;
    if (page_ == Page::ModInfo) {
        const std::vector<mod_packs::Pack>* list = packs();
        pack_heading = list && mod_index_ < list->size() ? upper((*list)[mod_index_].title()) : "MOD";
        g.text_fit(ui::Font::PixelBig, x, y, column, pack_heading, accent());
    } else {
        g.text(ui::Font::PixelBig, x, y, heading, accent());
    }
    // More rows above the window: a marker at the heading's right.
    if (first > 0) {
        const float mw = g.text_width(ui::Font::Pixel, "^");
        g.text(ui::Font::Pixel, x + column - mw, y, "^", accent());
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

    for (size_t i = first; i < last; ++i) {
        const bool sel = int(i) == selected_;
        const Item& it = items_[i];
        if (sel) {
            const float bob = std::sin(anim_ * 3.0f) * g.px(2);
            g.text(ui::Font::Pixel, x - g.px(24) + bob, y, ">", accent());
        }
        const float label_w = g.text(ui::Font::Pixel, x, y, it.label,
                                     sel ? ui::theme::text : ui::theme::text_dim);
        if (!it.note.empty()) {
            // Smaller and dimmer than the label, on the same middle line, so
            // it reads as a caption rather than as a second entry.
            const float ny = y + (g.line_height(ui::Font::Pixel) - g.line_height(ui::Font::Small)) * 0.5f;
            g.text(ui::Font::Small, x + label_w + g.px(16), ny, it.note, faint());
        }
        const float this_h = wrapped[i] ? row_h * 2 : row_h;
        Hit hit{{x - g.px(24), y - g.px(4), column + g.px(48), this_h}, int(i)};
        if (it.value) {
            const std::string v = it.value();
            const float vw = g.text_width(ui::Font::Pixel, v);
            // Wrapped values sit on the next line, indented, and are shortened
            // if even a whole line cannot hold them.
            const float vy = wrapped[i] ? y + row_h : y;
            const float vx = wrapped[i] ? x + g.px(16) : x + column - vw;
            const float room = wrapped[i] ? column - g.px(46) : column;
            g.text_fit(ui::Font::Pixel, vx, vy, room, v,
                       sel ? accent() : ui::theme::text_dim);
            // Arrows only where left/right actually does something: on the
            // key page the value changes by capturing a press instead.
            if (sel && !it.pressed && !it.read_only) {
                const float aw = wrapped[i] ? std::min(vw, room) : vw;
                const float ax = wrapped[i] ? vx : x + column - vw;
                g.text(ui::Font::Pixel, ax - g.px(22), vy, "<", accent());
                g.text(ui::Font::Pixel, ax + aw + g.px(10), vy, ">", accent());
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

    const char* hint = page_ == Page::Keys || page_ == Page::StateKeys
                           ? "Up/Down choose    Enter rebind    Esc back"
                       : page_ == Page::Mods ? "Up/Down choose    Left/Right on/off    Enter details"
                                             : "Up/Down choose    Left/Right change    Esc back";
    g.text(ui::Font::Small, x, y + g.px(8), hint, faint());
    // More rows below the window: a marker at the hint's right.
    if (last < items_.size()) {
        const float mw = g.text_width(ui::Font::Pixel, "v");
        g.text(ui::Font::Pixel, x + column - mw, y + g.px(2), "v", accent());
        hits_.push_back({{x + column - g.px(48), y - g.px(4), g.px(56), hint_h + g.px(20)}, kScrollDown});
    }
}

void Menu::draw_achievements(ui::Ui& g, achievements::Tracker& ach) {
    const float column = std::min(g.width() - g.px(32), g.px(680));
    const float x = (g.width() - column) * 0.5f;
    float y = std::max(g.px(18), g.height() * 0.05f);

    fill(g, {0, 0, g.width(), g.height()}, ui::theme::background.alpha(200), 210);
    g.text(ui::Font::PixelBig, x, y, "AWARDS", accent());
    char head[96];
    std::snprintf(head, sizeof head, "%d OF %zu    %d / %d PTS", ach.unlocked_count(),
                  ach.list().size(), ach.points_earned(), ach.points_total());
    const float hw = g.text_width(ui::Font::Pixel, head);
    g.text(ui::Font::Pixel, x + column - hw, y + g.px(10), head, ui::theme::text_dim);
    y += g.line_height(ui::Font::PixelBig) + g.px(12);

    const float bar_h = g.px(6);
    const int total = ach.points_total();
    const float done = total > 0 ? float(ach.points_earned()) / float(total) : 0.0f;
    g.rect({x, y, column, bar_h}, ui::theme::surface_raised);
    if (done > 0) g.rect({x, y, column * done, bar_h}, accent());
    y += bar_h + g.px(14);

    const float row_h = g.line_height(ui::Font::Pixel) + g.line_height(ui::Font::Small) + g.px(16);
    const float gap = g.px(6);
    const float bottom = g.height() - g.line_height(ui::Font::Small) - g.px(20);
    const auto& all = ach.list();
    rows_ = std::max(1, int((bottom - y) / (row_h + gap)));
    row_h_ = row_h + gap;
    scroll_ = std::clamp(scroll_, 0, std::max(0, int(all.size()) - rows_));
    const int last = std::min(int(all.size()), scroll_ + rows_);
    for (int i = scroll_; i < last; ++i) {
        const auto& a = all[size_t(i)];
        const SDL_FRect row{x, y, column, row_h};
        g.panel(row, a.unlocked ? ui::theme::surface_raised : ui::theme::surface,
                a.unlocked ? accent() : ui::theme::outline, g.px(4), a.unlocked ? 2.0f : 1.0f);
        char pts[16];
        std::snprintf(pts, sizeof pts, "%d", a.points);
        const float pw = g.text_width(ui::Font::Pixel, pts);
        g.text(ui::Font::Pixel, x + column - pw - g.px(12), y + g.px(8), pts,
               a.unlocked ? accent() : ui::theme::text_faint);
        const float tw = column - pw - g.px(32);
        g.text_fit(ui::Font::Pixel, x + g.px(12), y + g.px(6), tw, a.title,
                   a.unlocked ? ui::theme::text : ui::theme::text_dim);
        g.text_fit(ui::Font::Small, x + g.px(12), y + g.px(6) + g.line_height(ui::Font::Pixel),
                   tw, a.description, a.unlocked ? ui::theme::text_dim : ui::theme::text_faint);
        y += row_h + gap;
    }
    g.text(ui::Font::Small, x, g.height() - g.line_height(ui::Font::Small) - g.px(10),
           "Up/Down scroll    Enter or Esc back", faint());
}

} // namespace chaotix
