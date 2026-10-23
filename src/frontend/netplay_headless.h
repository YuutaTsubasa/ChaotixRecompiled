// chaotix_headless's netplay modes (issue #46): see netplay_headless.cpp.
#pragma once

namespace chaotix {

// Whether the command line asks for a netplay run (any --netplay-* option).
bool is_netplay_headless(int argc, char** argv);
int netplay_headless_main(int argc, char** argv);

} // namespace chaotix
