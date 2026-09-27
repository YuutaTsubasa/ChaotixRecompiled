// First-run setup screen (SDL): asks for the user's own ROM, verifies it and
// installs it into the user data directory. See frontend/setup.h for the
// platform-independent half.
#pragma once
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_IOStream;

namespace chaotix {

// Takes ownership of the stream (including on failure). Android's picker
// returns content:// URIs: open them with SDL_IOFromFile, not stdio. Stage the
// bounded stream locally before using the existing ROM validation/installer.
bool install_rom_from_stream(SDL_IOStream* source, const std::string& store_root,
                             std::string* installed_path, std::string* error);

// Runs until a ROM has been installed (returns its path) or the user quits
// (returns an empty string). Draws with the given renderer.
std::string run_setup_screen(SDL_Window* window, SDL_Renderer* renderer,
                             const std::string& store_root,
                             const std::vector<std::string>& search_dirs);

} // namespace chaotix
