#include "frontend/update_check.h"

#include <cctype>

namespace chaotix::update_check {

std::string release_page(const std::string& tag) {
    return "https://github.com/YuutaTsubasa/ChaotixRecompiled/releases/tag/" + tag;
}

std::vector<int> parse_version(const std::string& text) {
    size_t i = 0;
    if (i < text.size() && (text[i] == 'v' || text[i] == 'V')) ++i;
    std::vector<int> parts;
    while (i < text.size()) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return {};
        long n = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            n = n * 10 + (text[i++] - '0');
            if (n > 1000000) return {};
        }
        parts.push_back(int(n));
        if (i == text.size()) break;
        if (text[i] != '.' || i + 1 == text.size()) return {};
        ++i;
    }
    return parts;
}

bool is_newer(const std::string& tag, const std::string& current) {
    std::vector<int> a = parse_version(tag), b = parse_version(current);
    if (a.empty() || b.empty()) return false;
    a.resize(std::max(a.size(), b.size()));
    b.resize(a.size());
    return a > b;
}

std::string tag_from_json(const std::string& body) {
    const std::string key = "\"tag_name\"";
    size_t at = body.find(key);
    if (at == std::string::npos) return "";
    at += key.size();
    while (at < body.size() && std::isspace(static_cast<unsigned char>(body[at]))) ++at;
    if (at >= body.size() || body[at] != ':') return "";
    ++at;
    while (at < body.size() && std::isspace(static_cast<unsigned char>(body[at]))) ++at;
    if (at >= body.size() || body[at] != '"') return "";
    const size_t end = body.find('"', ++at);
    if (end == std::string::npos) return "";
    std::string tag = body.substr(at, end - at);
    // Only what a version tag is made of: nothing from the answer reaches the
    // screen or a URL unless it is one.
    return parse_version(tag).empty() ? "" : tag;
}

std::string display_version(const std::string& tag) {
    return !tag.empty() && (tag[0] == 'v' || tag[0] == 'V') ? tag.substr(1) : tag;
}

}  // namespace chaotix::update_check
