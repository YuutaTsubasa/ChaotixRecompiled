#include "mod_packs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace chaotix::mod_packs {

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool is_kind_name(const std::string& name) {
    const std::string low = lower(name);
    for (const char* k : kKinds)
        if (low == lower(k)) return true;
    return false;
}

// A folder of one kind counts when it holds a file other than the README
// the app writes into the Mods folder's own ones.
bool has_content(const fs::path& dir) {
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = lower(it->path().filename().u8string());
        if (name != "readme.txt" && name.rfind('.', 0) != 0) return true;
    }
    return false;
}

// The kind folder in `dir` by its name in any case (a pack made on Windows
// may say "textures"), empty if there is none.
fs::path find_kind(const fs::path& dir, const char* kind) {
    std::error_code ec;
    const fs::path exact = dir / kind;
    if (fs::is_directory(exact, ec)) return exact;
    const std::string want = lower(kind);
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec) && lower(it->path().filename().u8string()) == want) return it->path();
    return {};
}

void scan_kinds(Pack& p) {
    const fs::path dir = fs::u8path(p.dir);
    bool* flags[] = {&p.textures, &p.characters, &p.music, &p.patches, &p.touch, &p.samples};
    for (size_t i = 0; i < std::size(kKinds); ++i) {
        const fs::path k = find_kind(dir, kKinds[i]);
        *flags[i] = !k.empty() && has_content(k);
    }
}

std::string with_slash(std::string dir) {
    if (dir.empty() || (dir.back() != '/' && dir.back() != '\\')) dir += '/';
    return dir;
}

} // namespace

void parse_info(const std::string& text_in, Info& out, const std::string& where, std::vector<std::string>* messages) {
    auto say = [&](int line, const std::string& m) {
        if (messages) messages->push_back(where + ":" + std::to_string(line) + ": " + m);
    };
    std::string text = text_in;
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);  // a UTF-8 BOM, as Notepad writes
    std::istringstream in(text);
    std::string line;
    int number = 0;
    bool described = false;
    while (std::getline(in, line)) {
        ++number;
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            const std::string section = lower(trim(line.substr(1, line.size() - 2)));
            if (section != "mod") say(number, "only a [Mod] section is read");
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) { say(number, "expected key = value"); continue; }
        const std::string key = lower(trim(line.substr(0, eq)));
        std::string value = trim(line.substr(eq + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
        if (key == "name" || key == "title") out.name = value;
        else if (key == "author" || key == "authors") out.author = value;
        else if (key == "version") out.version = value;
        else if (key == "description") {
            // "\n" in the value, or the key again, starts a new line.
            std::string d;
            for (size_t i = 0; i < value.size(); ++i) {
                if (value[i] == '\\' && i + 1 < value.size() && value[i + 1] == 'n') { d += '\n'; ++i; }
                else d += value[i];
            }
            out.description += (described ? "\n" : "") + d;
            described = true;
        } else if (key == "url" || key == "link" || key == "website") out.url = value;
        else if (key == "preview" || key == "image") out.preview = value;
        else if (key == "requires" || key == "min_version" || key == "game_version") out.min_version = value;
        else say(number, "unknown key '" + key + "'");
    }
}

bool Pack::has(const char* kind) const {
    const std::string k = kind;
    if (k == kTextures) return textures;
    if (k == kCharacters) return characters;
    if (k == kMusic) return music;
    if (k == kPatches) return patches;
    if (k == kTouch) return touch;
    if (k == kSamples) return samples;
    return false;
}

std::string Pack::title() const {
    if (loose) return "Loose files";
    return info.name.empty() ? id : info.name;
}

std::vector<Pack> discover(const std::string& root_in, const std::string& game_version,
                           std::vector<std::string>* messages) {
    const std::string root = with_slash(root_in);
    std::vector<Pack> packs;
    Pack loose;
    loose.loose = true;
    loose.dir = root;
    scan_kinds(loose);
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(root), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        const std::string name = it->path().filename().u8string();
        if (name.empty() || name[0] == '.' || is_kind_name(name)) continue;
        Pack p;
        p.id = name;
        p.dir = with_slash(it->path().u8string());
        const fs::path ini = it->path() / kIniName;
        std::error_code e2;
        p.has_ini = fs::is_regular_file(ini, e2);
        if (p.has_ini) {
            std::ifstream f(ini, std::ios::binary);
            parse_info(std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()), p.info,
                       name + "/" + kIniName, messages);
        }
        scan_kinds(p);
        bool any_kind = false;
        for (const char* k : kKinds) any_kind = any_kind || !find_kind(it->path(), k).empty();
        if (!p.has_ini && !any_kind) {
            if (messages)
                messages->push_back(name + ": not a mod pack (no " + kIniName +
                                    ", and none of Textures, Characters, Music, Patches or Touch in it)");
            continue;
        }
        if (p.info.name.empty()) p.info.name = name;
        p.too_new = !p.info.min_version.empty() && compare_versions(p.info.min_version, game_version) > 0;
        if (p.too_new && messages)
            messages->push_back(name + ": made for version " + p.info.min_version + " or later (this is " +
                                game_version + "); it may not work");
        packs.push_back(std::move(p));
    }
    std::sort(packs.begin(), packs.end(), [](const Pack& a, const Pack& b) { return a.id < b.id; });
    if (loose.textures || loose.characters || loose.music || loose.patches || loose.touch)
        packs.insert(packs.begin(), std::move(loose));
    return packs;
}

std::vector<Pack> arrange(std::vector<Pack> found, const Order& order) {
    std::vector<Pack> out;
    std::vector<bool> placed(found.size(), false);
    for (size_t i = 0; i < found.size(); ++i)
        if (found[i].loose) { out.push_back(found[i]); placed[i] = true; }
    for (const std::string& id : order.order)
        for (size_t i = 0; i < found.size(); ++i)
            if (!placed[i] && found[i].id == id) { out.push_back(found[i]); placed[i] = true; }
    for (size_t i = 0; i < found.size(); ++i)  // new packs, in id order (found is sorted)
        if (!placed[i]) out.push_back(found[i]);
    for (Pack& p : out) p.enabled = p.loose ? order.loose : order.disabled.count(p.id) == 0;
    return out;
}

void remember(Order& order, const std::vector<Pack>& arranged) {
    std::vector<std::string> ids;
    for (const Pack& p : arranged)
        if (!p.loose) ids.push_back(p.id);
    for (const std::string& id : order.order)
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    order.order = std::move(ids);
}

void set_enabled(Order& order, std::vector<Pack>& arranged, size_t index, bool on) {
    if (index >= arranged.size()) return;
    Pack& p = arranged[index];
    p.enabled = on;
    if (p.loose) order.loose = on;
    else if (on) order.disabled.erase(p.id);
    else order.disabled.insert(p.id);
    remember(order, arranged);
}

bool move(Order& order, std::vector<Pack>& arranged, size_t index, int delta) {
    if (index >= arranged.size() || arranged[index].loose || (delta != -1 && delta != 1)) return false;
    const long to = long(index) + delta;
    if (to < 0 || to >= long(arranged.size()) || arranged[size_t(to)].loose) return false;
    std::swap(arranged[index], arranged[size_t(to)]);
    remember(order, arranged);
    return true;
}

std::vector<std::string> folders(const std::vector<Pack>& arranged, const char* kind) {
    std::vector<std::string> out;
    for (auto it = arranged.rbegin(); it != arranged.rend(); ++it) {
        if (!it->enabled || !it->has(kind)) continue;
        const fs::path k = find_kind(fs::u8path(it->dir), kind);
        if (!k.empty()) out.push_back(k.u8string());
    }
    return out;
}

std::vector<std::string> patch_files(const std::string& dir) {
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string ext = lower(it->path().extension().u8string());
        if (it->is_regular_file(ec) && (ext == ".ips" || ext == ".bps")) files.push_back(it->path().u8string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::string> patch_files(const std::vector<Pack>& arranged) {
    std::vector<std::string> files;
    for (const std::string& dir : folders(arranged, kPatches)) {
        const std::vector<std::string> some = patch_files(dir);
        files.insert(files.end(), some.begin(), some.end());
    }
    return files;
}

int compare_versions(const std::string& a, const std::string& b) {
    auto numbers = [](const std::string& s) {
        std::vector<long> n;
        size_t i = 0;
        if (i < s.size() && (s[i] == 'v' || s[i] == 'V')) ++i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            long v = 0;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) v = v * 10 + (s[i++] - '0');
            n.push_back(v);
            if (i < s.size() && s[i] == '.') ++i;
            else break;
        }
        return n;
    };
    const std::vector<long> x = numbers(a), y = numbers(b);
    for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
        const long u = i < x.size() ? x[i] : 0, v = i < y.size() ? y[i] : 0;
        if (u != v) return u < v ? -1 : 1;
    }
    return 0;
}

std::vector<std::string> split_list(const std::string& text) {
    std::vector<std::string> out;
    std::string item;
    std::istringstream in(text);
    while (std::getline(in, item, ',')) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::string join_list(const std::vector<std::string>& items) {
    std::string s;
    for (const auto& i : items) s += (s.empty() ? "" : ", ") + i;
    return s;
}

} // namespace chaotix::mod_packs
