#include "app/settings.hpp"
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace scal {

std::string settingsDir() {
    namespace fs = std::filesystem;
    const char* home = std::getenv("HOME");
    fs::path dir;
#if defined(__APPLE__)
    if (!home) return {};
    dir = fs::path(home) / "Library" / "Application Support" / "Scalpellino";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        dir = fs::path(xdg) / "scalpellino";
    else if (home)
        dir = fs::path(home) / ".config" / "scalpellino";
    else
        return {};
#endif
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return {};
    return dir.string();
}

} // namespace scal
