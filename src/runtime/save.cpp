#include "save.h"
#include "log.h"
#include "system.h"
#include <cstdio>
#include <filesystem>

namespace chaotix {

SaveStore::SaveStore(std::string root) : root_(std::move(root)) {
    if (!root_.empty() && root_.back() != '/' && root_.back() != '\\') root_ += '/';
    std::error_code ec;
    for (const std::string& d : {save_dir(), config_dir(), mappings_dir()}) std::filesystem::create_directories(d, ec);
}

std::string SaveStore::sram_path(const Machine& m) const {
    return save_dir() + "chaotix_" + m.rom.sha1.substr(0, 8) + ".srm";
}

bool SaveStore::load_sram(Machine& m) const {
    FILE* f = std::fopen(sram_path(m).c_str(), "rb");
    if (!f) return false;
    size_t n = std::fread(m.sram, 1, sizeof m.sram, f);
    std::fclose(f);
    m.sram_dirty = false;
    LOGI("save", "loaded %zu bytes of cartridge SRAM from %s", n, sram_path(m).c_str());
    return n > 0;
}

bool SaveStore::store_sram(Machine& m) const {
    // Write to a temporary file first so a crash never corrupts the save.
    std::string path = sram_path(m), tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) { LOGW("save", "cannot write %s", tmp.c_str()); return false; }
    bool ok = std::fwrite(m.sram, 1, sizeof m.sram, f) == sizeof m.sram;
    ok = std::fclose(f) == 0 && ok;
    if (!ok) return false;
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) { LOGW("save", "cannot replace %s: %s", path.c_str(), ec.message().c_str()); return false; }
    m.sram_dirty = false;
    return true;
}

} // namespace chaotix
