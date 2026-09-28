#include "platform/sdl_setup.h"
#include "frontend/setup.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>

namespace chaotix {

bool install_rom_from_stream(SDL_IOStream* source, const std::string& store_root,
                             std::string* installed_path, std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    using Stream = std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>;
    Stream input(source, SDL_CloseIO);
    if (!input) return fail(std::string("cannot open ROM: ") + SDL_GetError());

    // Never pass a document URI to std::filesystem or Rom::load. A private
    // staging file lets both validation and atomic installation stay shared
    // with the headless frontend, without making the runtime depend on SDL.
    namespace fs = std::filesystem;
    const fs::path staging = setup::installed_rom_path(store_root) + ".import";
    std::error_code ec;
    fs::create_directories(staging.parent_path(), ec);
    if (ec) return fail("cannot create ROM import directory: " + ec.message());
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ignored; fs::remove(path, ignored); }
    } cleanup{staging};
    Stream output(SDL_IOFromFile(staging.string().c_str(), "wb"), SDL_CloseIO);
    if (!output) return fail(std::string("cannot write ROM import: ") + SDL_GetError());

    // Document providers need not support seeking or report their size. Read
    // until EOF, but enforce the same 8 MiB limit as Rom::load while copying.
    constexpr size_t max_size = 8 * 1024 * 1024;
    Uint8 buffer[64 * 1024];
    size_t total = 0;
    for (;;) {
        const size_t n = SDL_ReadIO(input.get(), buffer, sizeof buffer);
        const SDL_IOStatus status = SDL_GetIOStatus(input.get());
        if (status == SDL_IO_STATUS_ERROR || status == SDL_IO_STATUS_NOT_READY)
            return fail(std::string("cannot read ROM: ") + SDL_GetError());
        if (!n) {
            if (status != SDL_IO_STATUS_EOF) return fail("cannot read ROM: incomplete stream");
            break;
        }
        if (n > max_size - total) return fail("unexpected ROM size");
        if (SDL_WriteIO(output.get(), buffer, n) != n)
            return fail(std::string("cannot write ROM import: ") + SDL_GetError());
        total += n;
    }
    if (!SDL_CloseIO(output.release()))
        return fail(std::string("cannot finish ROM import: ") + SDL_GetError());
    // The selected file can be the installed ROM itself. Release its handle
    // before replacing it, since Windows does not allow deleting an open file.
    input.reset();
    return setup::install(staging.string(), store_root, installed_path, error);
}

bool install_rom_file(const std::string& source, const std::string& store_root,
                      std::string* installed_path, std::string* error) {
    std::string detail;
    if (install_rom_from_stream(SDL_IOFromFile(source.c_str(), "rb"), store_root,
                                installed_path, &detail)) return true;
    const std::string staging = setup::installed_rom_path(store_root) + ".import";
    const auto pos = detail.find(staging);
    if (pos != std::string::npos) detail.replace(pos, staging.size(), source);
    else detail = "File: " + source + "\n" + detail;
    if (error) *error = detail;
    return false;
}

} // namespace chaotix
