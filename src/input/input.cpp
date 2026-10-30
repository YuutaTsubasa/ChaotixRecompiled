#include "input.h"
#include <cstring>
#include <initializer_list>

namespace chaotix {

namespace {
struct Name { const char* name; uint16_t bit; };
const Name kNames[] = {
    {"up", PAD_UP}, {"down", PAD_DOWN}, {"left", PAD_LEFT}, {"right", PAD_RIGHT},
    {"a", PAD_A}, {"b", PAD_B}, {"c", PAD_C}, {"start", PAD_START},
    {"x", PAD_X}, {"y", PAD_Y}, {"z", PAD_Z}, {"mode", PAD_MODE},
};
} // namespace

uint16_t OppositeDirections::clean(uint16_t raw) {
    uint16_t out = raw;
    for (const uint16_t axis : {uint16_t(PAD_LEFT | PAD_RIGHT), uint16_t(PAD_UP | PAD_DOWN)}) {
        const uint16_t now = raw & axis;
        uint16_t give = now;
        if (now == axis) {
            const uint16_t fresh = now & ~held;
            give = fresh == axis ? 0 : fresh ? fresh : chosen & axis;
        }
        out = uint16_t((out & ~axis) | give);
    }
    held = raw;
    chosen = out;
    return out;
}

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
