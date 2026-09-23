// First-run setup: candidate scanning, verification and installing.
// Uses small synthetic files, so it needs no ROM.
#include "frontend/setup.h"
#include "test_framework.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace chaotix;
namespace fs = std::filesystem;

namespace {

fs::path temp_root() {
    fs::path p = fs::temp_directory_path() / "chaotix_setup_test";
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

// A file of `size` bytes that is not a valid ROM.
void write_blob(const fs::path& path, size_t size, uint8_t fill = 0x5A) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary);
    std::vector<char> data(size, char(fill));
    f.write(data.data(), std::streamsize(data.size()));
}

} // namespace

TEST(setup, ignores_files_that_are_not_roms) {
    fs::path root = temp_root();
    write_blob(root / "roms" / "notes.txt", 1024);        // wrong extension
    write_blob(root / "roms" / "tiny.32x", 4096);         // too small
    write_blob(root / "roms" / "huge.bin", 9u << 20);     // too large
    auto found = setup::scan_candidates({(root / "roms").string()});
    CHECK_EQ(int(found.size()), 0);
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(setup, reports_unloadable_candidates) {
    fs::path root = temp_root();
    write_blob(root / "roms" / "bogus.32x", 1u << 20);  // right size, not a ROM
    auto found = setup::scan_candidates({(root / "roms").string()});
    CHECK_EQ(int(found.size()), 1);
    CHECK(!found[0].loadable);
    CHECK(!found[0].verified);
    CHECK(!found[0].label.empty());
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(setup, missing_directories_are_skipped) {
    fs::path root = temp_root();
    auto found = setup::scan_candidates({(root / "does_not_exist").string(), (root / "also_missing").string()});
    CHECK_EQ(int(found.size()), 0);
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(setup, install_rejects_a_file_that_is_not_a_rom) {
    fs::path root = temp_root();
    write_blob(root / "bogus.32x", 1u << 20);
    std::string installed, err;
    CHECK(!setup::install((root / "bogus.32x").string(), (root / "store/").string(), &installed, &err));
    CHECK(!err.empty());
    CHECK(!setup::is_installed((root / "store/").string(), ""));
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(setup, installed_path_is_inside_the_store) {
    const std::string root = "/tmp/store/";
    const std::string p = setup::installed_rom_path(root);
    CHECK(p.rfind(root, 0) == 0);
    CHECK(p.find("chaotix") != std::string::npos);
}
