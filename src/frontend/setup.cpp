#include "frontend/setup.h"
#include "runtime/log.h"
#include <algorithm>
#include <filesystem>
#include <system_error>

namespace chaotix::setup {

namespace fs = std::filesystem;

namespace {

// Extensions a 32X image usually has. ".bin" and ".md" are common dumps too.
bool has_rom_extension(const fs::path& p) {
    std::string e = p.extension().string();
    for (char& c : e) c = char(std::tolower((unsigned char)c));
    return e == ".32x" || e == ".bin" || e == ".md" || e == ".gen" || e == ".rom";
}

std::string file_name(const std::string& path) { return fs::path(path).filename().string(); }

} // namespace

std::string installed_rom_path(const std::string& store_root) {
    return store_root + "Game/chaotix.32x";
}

Candidate check_file(const std::string& path) {
    Candidate c;
    c.path = path;
    std::error_code ec;
    const auto size = fs::file_size(fs::path(path), ec);
    c.size = ec ? 0 : size_t(size);
    Rom rom;
    std::string err;
    if (!rom.load(path, &err)) {
        c.label = file_name(path) + "  -  " + err;
        return c;
    }
    c.loadable = true;
    c.sha1 = rom.sha1;
    c.version = rom.version;
    c.verified = rom.version != RomVersion::Unknown;
    c.label = file_name(path);
    c.label += c.verified ? "  -  verified Knuckles' Chaotix" : "  -  unrecognised 32X ROM (will run interpreted)";
    return c;
}

std::vector<Candidate> scan_candidates(const std::vector<std::string>& dirs) {
    std::vector<Candidate> out;
    std::vector<std::string> seen;
    for (const auto& d : dirs) {
        std::error_code ec;
        fs::directory_iterator it(d, ec), end;
        if (ec) continue;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            const fs::path& p = it->path();
            if (!it->is_regular_file(ec) || ec) continue;
            if (!has_rom_extension(p)) continue;
            const auto size = fs::file_size(p, ec);
            // Chaotix is 3 MiB; accept anything from 512 KiB to 8 MiB.
            if (ec || size < 512u * 1024 || size > 8u * 1024 * 1024) continue;
            // The same file often shows up through several search paths
            // (relative and next to the executable), so compare real paths.
            std::error_code cec;
            fs::path canon = fs::weakly_canonical(p, cec);
            std::string key = (cec ? p : canon).string();
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(key);
            out.push_back(check_file(key));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) {
        if (a.verified != b.verified) return a.verified;
        return a.loadable && !b.loadable;
    });
    return out;
}

bool install(const std::string& source, const std::string& store_root, std::string* installed_path, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    Candidate c = check_file(source);
    if (!c.loadable) return fail(c.label);

    const std::string dest = installed_rom_path(store_root);
    std::error_code ec;
    fs::create_directories(fs::path(dest).parent_path(), ec);
    if (ec) return fail("cannot create " + fs::path(dest).parent_path().string() + ": " + ec.message());

    // Copy to a temporary name first so an interrupted install cannot leave a
    // half-written ROM behind.
    const std::string tmp = dest + ".part";
    fs::remove(fs::path(tmp), ec);
    if (fs::equivalent(fs::path(source), fs::path(dest), ec)) {
        if (installed_path) *installed_path = dest;
        return true;  // already installed from this very file
    }
    ec.clear();
    fs::copy_file(fs::path(source), fs::path(tmp), fs::copy_options::overwrite_existing, ec);
    if (ec) return fail("cannot copy the ROM: " + ec.message());
    fs::rename(fs::path(tmp), fs::path(dest), ec);
    if (ec) {
        // Some filesystems refuse rename over an existing file.
        fs::remove(fs::path(dest), ec);
        ec.clear();
        fs::rename(fs::path(tmp), fs::path(dest), ec);
        if (ec) return fail("cannot store the ROM: " + ec.message());
    }
    LOGI("setup", "installed %s (%s) to %s", file_name(source).c_str(), c.sha1.c_str(), dest.c_str());
    if (installed_path) *installed_path = dest;
    return true;
}

bool is_installed(const std::string& store_root, const std::string& expect_sha1) {
    const std::string path = installed_rom_path(store_root);
    std::error_code ec;
    if (!fs::exists(fs::path(path), ec) || ec) return false;
    Candidate c = check_file(path);
    if (!c.loadable) return false;
    return expect_sha1.empty() || c.sha1 == expect_sha1;
}

bool uninstall(const std::string& store_root) {
    std::error_code ec;
    return fs::remove(fs::path(installed_rom_path(store_root)), ec) && !ec;
}

} // namespace chaotix::setup
