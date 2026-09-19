// Cross-platform save/config storage. Game/runtime code only uses logical
// locations (SaveData/, Config/, ControllerMappings/); the platform layer
// supplies the root directory (e.g. SDL_GetPrefPath on desktop and mobile).
#pragma once
#include <string>

namespace chaotix {

class Machine;

class SaveStore {
public:
    SaveStore() = default;
    explicit SaveStore(std::string root);

    const std::string& root() const { return root_; }
    std::string save_dir() const { return root_ + "SaveData/"; }
    std::string config_dir() const { return root_ + "Config/"; }
    std::string mappings_dir() const { return root_ + "ControllerMappings/"; }
    std::string config_file() const { return config_dir() + "chaotix.ini"; }

    // Battery-backed cartridge RAM, keyed by ROM hash.
    bool load_sram(Machine& m) const;
    bool store_sram(Machine& m) const;

private:
    std::string sram_path(const Machine& m) const;
    std::string root_;
};

} // namespace chaotix
