// Exercise Android-style document streams without requiring a provider.
// Synthetic inputs cover rejection; an optional local ROM covers successful import.
#include "platform/sdl_setup.h"
#include "frontend/setup.h"
#include "test_framework.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
namespace fs = std::filesystem;

struct Store {
    fs::path path = fs::temp_directory_path() / "chaotix_stream_install_test";
    Store() { fs::create_directories(path); }
    ~Store() { std::error_code ec; fs::remove_all(path, ec); }
    std::string root() const { return path.string() + "/"; }
};

std::vector<Uint8> synthetic_rom(size_t size = 4096) {
    std::vector<Uint8> bytes(size, 0);
    std::memcpy(bytes.data() + 0x100, "SEGA 32X", 8);
    return bytes;
}

// No seek or size operation, small partial reads, and an optional I/O error.
struct Source {
    std::vector<Uint8> bytes = synthetic_rom();
    size_t pos = 0;
    bool broken = false;
    bool closed = false;
    SDL_IOStream* open() {
        SDL_IOStreamInterface iface;
        SDL_INIT_INTERFACE(&iface);
        iface.read = [](void* userdata, void* dst, size_t size, SDL_IOStatus* status) -> size_t {
            auto& s = *static_cast<Source*>(userdata);
            if (s.broken && s.pos >= 512) {
                SDL_SetError("document provider read failed");
                *status = SDL_IO_STATUS_ERROR;
                return 0;
            }
            const size_t n = std::min({size, size_t(256), s.bytes.size() - s.pos});
            std::memcpy(dst, s.bytes.data() + s.pos, n);
            s.pos += n;
            if (!n) *status = SDL_IO_STATUS_EOF;
            return n;
        };
        iface.close = [](void* userdata) -> bool {
            static_cast<Source*>(userdata)->closed = true;
            return true;
        };
        return SDL_OpenIO(&iface, this);
    }
};
}

TEST(sdl_import, verifies_all_bytes_from_a_nonseekable_document_stream) {
    Store store;
    Source source;
    std::string installed, error;
    CHECK(!chaotix::install_rom_from_stream(source.open(), store.root(), &installed, &error));
    CHECK(source.closed);
    CHECK_EQ(source.pos, source.bytes.size());
    CHECK(error.find(chaotix::sha1_hex(source.bytes.data(), source.bytes.size())) != std::string::npos);
    CHECK(installed.empty());
    CHECK(!fs::exists(chaotix::setup::installed_rom_path(store.root())));
    CHECK(!fs::exists(chaotix::setup::installed_rom_path(store.root()) + ".import"));
}

TEST(sdl_import, failed_imports_preserve_existing_bytes) {
    Store store;
    const std::string installed = chaotix::setup::installed_rom_path(store.root());
    fs::create_directories(fs::path(installed).parent_path());
    {
        std::ofstream f(installed, std::ios::binary);
        f << "previous installed data";
    }
    for (int failure = 0; failure < 5; ++failure) {
        Source bad;
        if (failure == 0) bad.bytes.clear();
        if (failure == 1) bad.bytes[0x100] = 0;
        if (failure == 2) bad.broken = true;
        if (failure == 3) bad.bytes.resize(8 * 1024 * 1024 + 1);
        // Case 4 has a valid 32X header but an unrecognised hash.
        std::string error;
        CHECK(!chaotix::install_rom_from_stream(bad.open(), store.root(), nullptr, &error));
        CHECK(!error.empty());
        if (failure == 1 || failure == 4) {
            CHECK(error.find(chaotix::sha1_hex(bad.bytes.data(), bad.bytes.size())) != std::string::npos);
            CHECK(error.find("0c2fff7bc79ed26507c08ac47464c3af19f7ced7") != std::string::npos);
        }
        CHECK(bad.closed);
        std::ifstream f(installed, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(f)), {});
        CHECK_STR(bytes, "previous installed data");
        CHECK(!fs::exists(installed + ".import"));
    }
}

TEST(sdl_import, reports_failure_to_open_the_selected_document) {
    Store store;
    std::string error;
    SDL_SetError("document permission denied");
    CHECK(!chaotix::install_rom_from_stream(nullptr, store.root(), nullptr, &error));
    CHECK(error.find("document permission denied") != std::string::npos);
    CHECK(!fs::exists(chaotix::setup::installed_rom_path(store.root())));
}

TEST(sdl_import, a_write_failure_names_the_staging_copy_not_the_chosen_file) {
    // Failing to write the private staging copy is the user data folder's
    // problem, not the chosen file's: the message keeps the staging path (in
    // SDL's "Couldn't open ...") and only adds which file was chosen.
    Store store;
    const fs::path source = store.path / "chosen.32x";
    {
        std::ofstream file(source, std::ios::binary);
        file << "not a rom";
    }
    const std::string staging = chaotix::setup::installed_rom_path(store.root()) + ".import";
    fs::create_directories(fs::path(staging) / "blocker");
    std::string error;
    CHECK(!chaotix::install_rom_file(source.string(), store.root(), nullptr, &error));
    CHECK(error.rfind("File: " + source.string() + "\n", 0) == 0);
    CHECK(error.find("cannot write ROM import") != std::string::npos);
    CHECK(error.find(staging) != std::string::npos);
}
#ifdef CHAOTIX_TEST_ROM
TEST(sdl_import, verified_nonseekable_import_and_selecting_installed_file) {
    Store store;
    Source source;
    std::ifstream rom(CHAOTIX_TEST_ROM, std::ios::binary);
    CHECK(rom.good());
    source.bytes.assign(std::istreambuf_iterator<char>(rom), {});
    std::string installed, error;
    CHECK(chaotix::install_rom_from_stream(source.open(), store.root(), &installed, &error));
    CHECK(source.closed);
    CHECK_EQ(source.pos, source.bytes.size());
    CHECK(chaotix::install_rom_file(installed, store.root(), nullptr, &error));
    CHECK(chaotix::setup::is_installed(store.root(), "0c2fff7bc79ed26507c08ac47464c3af19f7ced7"));
    CHECK(!fs::exists(installed + ".import"));
}
#endif
