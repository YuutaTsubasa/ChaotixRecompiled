// New version notice (issue #90): at start the frontend asks GitHub for the
// latest release once and, when it is newer than this build, offers it on the
// title menu. This part has no network in it, so it can be tested: what the
// answer says, and whether it is newer. The asking itself is per platform
// (platform/http_get.h).
#pragma once

#include <string>
#include <vector>

namespace chaotix::update_check {

// Where the answer comes from: GitHub's "latest release" of this project (a
// published release, never a draft or prerelease).
inline constexpr const char* kLatestReleaseApi =
    "https://api.github.com/repos/YuutaTsubasa/ChaotixRecompiled/releases/latest";
// The page a player is sent to for a version: its release notes and downloads.
std::string release_page(const std::string& tag);

// "v0.15.18" or "0.15.18" as its numbers {0, 15, 18}; empty if it is not a
// version (a word, an empty string, a number followed by anything else).
std::vector<int> parse_version(const std::string& text);
// Whether `tag` names a later version than `current`. Missing parts count as
// 0 (0.16 is 0.16.0); anything that is not a version is never newer.
bool is_newer(const std::string& tag, const std::string& current);
// The release's tag_name in GitHub's JSON answer, or "" without one.
std::string tag_from_json(const std::string& body);
// A tag as the menu shows it: "0.15.19" for "v0.15.19".
std::string display_version(const std::string& tag);

}  // namespace chaotix::update_check
