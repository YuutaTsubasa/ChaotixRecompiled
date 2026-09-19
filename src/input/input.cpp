#include "input.h"
#include <cstring>

namespace chaotix {

namespace {
struct Name { const char* name; uint16_t bit; };
const Name kNames[] = {
    {"up", PAD_UP}, {"down", PAD_DOWN}, {"left", PAD_LEFT}, {"right", PAD_RIGHT},
    {"a", PAD_A}, {"b", PAD_B}, {"c", PAD_C}, {"start", PAD_START},
    {"x", PAD_X}, {"y", PAD_Y}, {"z", PAD_Z}, {"mode", PAD_MODE},
};
} // namespace

uint16_t pad_button_from_name(const char* name) {
    for (const auto& n : kNames)
        if (std::strcmp(n.name, name) == 0) return n.bit;
    return 0;
}

const char* pad_button_name(uint16_t button) {
    for (const auto& n : kNames)
        if (n.bit == button) return n.name;
    return "?";
}

} // namespace chaotix
