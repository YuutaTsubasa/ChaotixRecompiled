// Synthetic ROMs only: exercise the stream returned by Android's file picker
// without requiring a document provider or commercial game data on the host.
#include "platform/sdl_setup.h"
#include "frontend/setup.h"
#include "test_framework.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
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

TEST(sdl_import, installs_a_nonseekable_document_stream) {
    Store store;
    Source source;
    std::string installed, error;
    CHECK(chaotix::install_rom_from_stream(source.open(), store.root(), &installed, &error));
    CHECK(source.closed);
    CHECK_STR(installed, chaotix::setup::installed_rom_path(store.root()));
    CHECK(chaotix::setup::is_installed(store.root(), chaotix::sha1_hex(source.bytes.data(), source.bytes.size())));
    CHECK(!fs::exists(installed + ".import"));
}

TEST(sdl_import, failed_imports_preserve_an_existing_install) {
    Store store;
    Source good;
    std::string installed, error;
    CHECK(chaotix::install_rom_from_stream(good.open(), store.root(), &installed, &error));
    const std::string hash = chaotix::sha1_hex(good.bytes.data(), good.bytes.size());
    for (int failure = 0; failure < 4; ++failure) {
        Source bad;
        if (failure == 0) bad.bytes.clear();
        if (failure == 1) bad.bytes[0x100] = 0;
        if (failure == 2) bad.broken = true;
        if (failure == 3) bad.bytes.resize(8 * 1024 * 1024 + 1);
        error.clear();
        CHECK(!chaotix::install_rom_from_stream(bad.open(), store.root(), nullptr, &error));
        CHECK(!error.empty());
        CHECK(bad.closed);
        CHECK(chaotix::setup::is_installed(store.root(), hash));
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

TEST(sdl_import, can_select_the_already_installed_file) {
    Store store;
    Source source;
    std::string installed, error;
    CHECK(chaotix::install_rom_from_stream(source.open(), store.root(), &installed, &error));
    CHECK(chaotix::install_rom_from_stream(SDL_IOFromFile(installed.c_str(), "rb"),
                                          store.root(), nullptr, &error));
    CHECK(chaotix::setup::is_installed(store.root(), chaotix::sha1_hex(source.bytes.data(), source.bytes.size())));
}
