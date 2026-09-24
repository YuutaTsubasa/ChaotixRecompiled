// First-run setup ("install the game data"): the project ships no game data,
// so on first launch the frontend asks for the user's own ROM, verifies it and
// copies it into the user data directory. Later launches find it there and go
// straight into the game.
//
// This header holds the platform-independent logic; the SDL layer draws the
// screen and calls into it.
#pragma once
#include "runtime/rom.h"
#include <string>
#include <vector>

namespace chaotix::setup {

// A file that might be the game ROM, with the result of checking it.
struct Candidate {
    std::string path;
    std::string label;      // name and note joined, for a one-line listing
    std::string name;       // the file name on its own
    std::string note;       // what checking it found, e.g. "verified Knuckles' Chaotix"
    std::string sha1;
    RomVersion version = RomVersion::Unknown;
    size_t size = 0;
    bool verified = false;  // matches the known Knuckles' Chaotix image
    bool loadable = false;  // parses as a 32X ROM at all
};

// The installed copy inside the user data directory.
std::string installed_rom_path(const std::string& store_root);

// Reads one file and reports what it is. Never throws; a file that cannot be
// read comes back with loadable = false and an explanatory label.
Candidate check_file(const std::string& path);

// Files in `dirs` (non-recursive) that look like a ROM by extension and size,
// checked and sorted best first (verified, then loadable, then the rest).
// Duplicate paths are collapsed. Missing directories are skipped.
std::vector<Candidate> scan_candidates(const std::vector<std::string>& dirs);

// Copies `source` into the user data directory (via a temporary file, then
// rename) so the installation is atomic. Returns the installed path.
bool install(const std::string& source, const std::string& store_root, std::string* installed_path, std::string* error);

// True when the store holds a ROM whose SHA-1 matches `expect_sha1` (or any
// readable ROM when `expect_sha1` is empty). Cheap enough for startup: it
// hashes the installed file once.
bool is_installed(const std::string& store_root, const std::string& expect_sha1);

// Removes the installed copy (used by "reinstall").
bool uninstall(const std::string& store_root);

} // namespace chaotix::setup
