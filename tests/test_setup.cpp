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


TEST(setup, rejects_unknown_rom_without_overwriting_installed_data) {
    fs::path root = temp_root();
    const std::string store = (root / "store").string() + "/";
    const fs::path source = root / "unknown.32x";
    write_blob(source, 512u * 1024);
    {
        std::fstream f(source, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(0x100);
        f.write("SEGA 32X", 8);
    }
    const auto candidate = setup::check_file(source.string());
    CHECK(candidate.loadable);
    CHECK(!candidate.verified);
    const std::string installed = setup::installed_rom_path(store);
    write_blob(installed, 4096, 0xA5);
    std::string error;
    CHECK(!setup::install(source.string(), store, nullptr, &error));
    CHECK(error.find(source.string()) != std::string::npos);
    CHECK(error.find(candidate.sha1) != std::string::npos);
    CHECK(error.find("0c2fff7bc79ed26507c08ac47464c3af19f7ced7") != std::string::npos);
    CHECK_EQ(fs::file_size(installed), 4096u);
    std::ifstream f(installed, std::ios::binary);
    CHECK_EQ(f.get(), 0xA5);
    f.close();
    CHECK(!fs::exists(installed + ".part"));
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(setup, unknown_installed_rom_is_not_valid_even_with_matching_saved_hash) {
    fs::path root = temp_root();
    const std::string store = (root / "store").string() + "/";
    const std::string installed = setup::installed_rom_path(store);
    write_blob(installed, 512u * 1024);
    {
        std::fstream f(installed, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(0x100);
        f.write("SEGA 32X", 8);
    }
    const auto candidate = setup::check_file(installed);
    CHECK(candidate.loadable);
    CHECK(!setup::is_installed(store, ""));
    CHECK(!setup::is_installed(store, candidate.sha1));
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(setup, invalid_header_reports_the_actual_and_expected_hash) {
    fs::path root = temp_root();
    const fs::path source = root / "invalid-header.32x";
    write_blob(source, 512u * 1024);
    const auto candidate = setup::check_file(source.string());
    const std::vector<uint8_t> bytes(512u * 1024, 0x5A);
    const std::string hash = sha1_hex(bytes.data(), bytes.size());
    CHECK(!candidate.loadable);
    CHECK_STR(candidate.sha1, hash);
    std::string error;
    CHECK(!setup::install(source.string(), (root / "store").string() + "/", nullptr, &error));
    CHECK(error.find(hash) != std::string::npos);
    CHECK(error.find("0c2fff7bc79ed26507c08ac47464c3af19f7ced7") != std::string::npos);
    CHECK(error.find(source.string()) != std::string::npos);
    std::error_code ec;
    fs::remove_all(root, ec);
}
