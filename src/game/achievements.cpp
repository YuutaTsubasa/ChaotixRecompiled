#include "game/achievements.h"
#include "runtime/log.h"
#include "runtime/system.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

namespace chaotix::achievements {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

std::vector<std::string> split_words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

bool parse_number(const std::string& s, uint32_t* out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const unsigned long v = (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
                                ? std::strtoul(s.c_str() + 2, &end, 16)
                                : std::strtoul(s.c_str(), &end, 10);
    if (!end || *end) return false;
    *out = uint32_t(v);
    return true;
}

bool parse_operand(const std::string& token, const std::map<std::string, Var>& vars, Operand* op, std::string* error) {
    std::string t = trim(token);
    if (t.size() > 6 && lower(t.substr(0, 5)) == "prev(" && t.back() == ')') {
        op->previous = true;
        t = trim(t.substr(5, t.size() - 6));
    }
    if (parse_number(t, &op->value)) {
        op->literal = true;
        return true;
    }
    if (!vars.count(t)) {
        if (error) *error = "unknown variable '" + t + "'";
        return false;
    }
    op->var = t;
    return true;
}

bool parse_condition(const std::string& text, const std::map<std::string, Var>& vars, Condition* c, std::string* error) {
    static const std::pair<const char*, Cmp> ops[] = {
        {"==", Cmp::Eq}, {"!=", Cmp::Ne}, {"<=", Cmp::Le}, {">=", Cmp::Ge}, {"<", Cmp::Lt}, {">", Cmp::Gt},
    };
    for (const auto& [sym, cmp] : ops) {
        const size_t p = text.find(sym);
        if (p == std::string::npos) continue;
        // "<" must not match the "<=" that was already tried; the table order
        // puts the two-character forms first, so a hit here is the real one.
        c->cmp = cmp;
        return parse_operand(text.substr(0, p), vars, &c->lhs, error) &&
               parse_operand(text.substr(p + std::string(sym).size()), vars, &c->rhs, error);
    }
    if (error) *error = "no comparison in '" + trim(text) + "'";
    return false;
}

} // namespace

// "a == b and c >= d" -> a list of conditions that must all hold.
bool parse_conditions(const std::string& value, const std::map<std::string, Var>& vars,
                      std::vector<Condition>* out, std::string* error) {
    std::string rest = value;
    for (;;) {
        const size_t p = lower(rest).find(" and ");
        const std::string part = p == std::string::npos ? rest : rest.substr(0, p);
        Condition c;
        if (!parse_condition(part, vars, &c, error)) return false;
        out->push_back(c);
        if (p == std::string::npos) return true;
        rest = rest.substr(p + 5);
    }
}

bool parse(const std::string& text, std::map<std::string, Var>& vars,
           std::vector<Achievement>& out, std::vector<Condition>& require, std::string* error) {
    std::istringstream in(text);
    std::string line, section;
    bool ok = true;
    auto fail = [&](const std::string& msg) {
        if (error && error->empty()) *error = msg;
        ok = false;
    };
    while (std::getline(in, line)) {
        // ';' and '#' start comments.
        const size_t comment = line.find_first_of(";#");
        if (comment != std::string::npos) line = line.substr(0, comment);
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            if (lower(section).rfind("achievement", 0) == 0) {
                Achievement a;
                a.title = trim(section.substr(std::string("achievement").size()));
                a.id = a.title;
                out.push_back(a);
            }
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) { fail("expected key = value: " + line); continue; }
        const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (lower(section) == "rules") {
            if (lower(key) != "require") { fail("unknown key '" + key + "' in [rules]"); continue; }
            std::string err;
            if (!parse_conditions(value, vars, &require, &err)) fail("require: " + err);
        } else if (lower(section) == "vars") {
            auto w = split_words(value);
            Var v;
            if (w.size() != 2) { fail("expected '<byte|word|long> <address>' for " + key); continue; }
            const std::string width = lower(w[0]);
            v.width = width == "byte" ? Width::Byte : width == "long" ? Width::Long : Width::Word;
            if (width != "byte" && width != "word" && width != "long") { fail("unknown width '" + w[0] + "'"); continue; }
            if (!parse_number("0x" + w[1], &v.address)) { fail("bad address '" + w[1] + "'"); continue; }
            vars[key] = v;
        } else if (!out.empty()) {
            Achievement& a = out.back();
            const std::string k = lower(key);
            if (k == "description") a.description = value;
            else if (k == "id") a.id = value;
            else if (k == "points") a.points = std::atoi(value.c_str());
            else if (k == "when") {
                std::string err;
                if (!parse_conditions(value, vars, &a.when, &err)) fail(a.title + ": " + err);
            } else {
                fail("unknown key '" + key + "'");
            }
        } else {
            fail("value outside a section: " + line);
        }
    }
    for (const Achievement& a : out)
        if (a.when.empty()) fail(a.title + ": no 'when' conditions");
    return ok;
}

bool Tracker::load_definitions_text(const std::string& text, std::string* error) {
    vars_.clear();
    achievements_.clear();
    previous_.clear();
    have_previous_ = false;
    require_.clear();
    return parse(text, vars_, achievements_, require_, error);
}

bool Tracker::load_definitions(const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return load_definitions_text(ss.str(), error);
}

bool Tracker::load_progress(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string id = trim(line.substr(0, eq));
        const uint64_t when = std::strtoull(trim(line.substr(eq + 1)).c_str(), nullptr, 10);
        for (Achievement& a : achievements_)
            if (a.id == id) {
                a.unlocked = true;
                a.unlocked_at = when;
            }
    }
    return true;
}

void Tracker::reset_progress() {
    for (Achievement& a : achievements_) {
        a.unlocked = false;
        a.unlocked_at = 0;
    }
    LOGI("achievements", "progress reset");
}

bool Tracker::save_progress(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "# Knuckles' Chaotix Recompiled - unlocked achievements\n");
    for (const Achievement& a : achievements_)
        if (a.unlocked) std::fprintf(f, "%s = %llu\n", a.id.c_str(), (unsigned long long)a.unlocked_at);
    std::fclose(f);
    return true;
}

uint32_t Tracker::read(const uint8_t* wram, size_t size, const Var& v) const {
    const uint32_t a = v.address & 0xFFFF;
    auto at = [&](uint32_t off) -> uint32_t { return off < size ? wram[off] : 0u; };
    switch (v.width) {
    case Width::Byte: return at(a);
    case Width::Long: return (at(a) << 24) | (at(a + 1) << 16) | (at(a + 2) << 8) | at(a + 3);
    case Width::Word:
    default: return (at(a) << 8) | at(a + 1);
    }
}

void Tracker::update_memory(const uint8_t* wram, size_t size, const std::function<void(const Achievement&)>& unlocked) {
    if (achievements_.empty()) return;
    std::map<std::string, uint32_t> now;
    for (const auto& [name, v] : vars_) now[name] = read(wram, size, v);

    auto value_of = [&](const Operand& o) -> uint32_t {
        if (o.literal) return o.value;
        if (o.previous) {
            auto it = previous_.find(o.var);
            return it == previous_.end() ? 0u : it->second;
        }
        auto it = now.find(o.var);
        return it == now.end() ? 0u : it->second;
    };

    auto holds = [&](const std::vector<Condition>& conds) {
        for (const Condition& c : conds) {
            const uint32_t l = value_of(c.lhs), r = value_of(c.rhs);
            bool ok = false;
            switch (c.cmp) {
            case Cmp::Eq: ok = l == r; break;
            case Cmp::Ne: ok = l != r; break;
            case Cmp::Lt: ok = l < r; break;
            case Cmp::Le: ok = l <= r; break;
            case Cmp::Gt: ok = l > r; break;
            case Cmp::Ge: ok = l >= r; break;
            }
            if (!ok) return false;
        }
        return true;
    };

    // A "previous" comparison needs one frame of history before it can hold.
    // Nothing unlocks unless the definitions' [rules] require line holds: that
    // is what keeps the attract demo, which plays the game by itself, from
    // earning anything.
    if (have_previous_ && holds(require_)) {
        for (Achievement& a : achievements_) {
            if (a.unlocked || !holds(a.when)) continue;
            a.unlocked = true;
            a.unlocked_at = uint64_t(std::time(nullptr));
            LOGI("achievements", "unlocked: %s (%d points)", a.title.c_str(), a.points);
            if (unlocked) unlocked(a);
        }
    }
    previous_ = now;
    have_previous_ = true;
}

void Tracker::update(const Machine& m, const std::function<void(const Achievement&)>& unlocked) {
    update_memory(m.wram, sizeof m.wram, unlocked);
}

int Tracker::unlocked_count() const {
    return int(std::count_if(achievements_.begin(), achievements_.end(), [](const Achievement& a) { return a.unlocked; }));
}

int Tracker::points_earned() const {
    int n = 0;
    for (const Achievement& a : achievements_)
        if (a.unlocked) n += a.points;
    return n;
}

int Tracker::points_total() const {
    int n = 0;
    for (const Achievement& a : achievements_) n += a.points;
    return n;
}

} // namespace chaotix::achievements
