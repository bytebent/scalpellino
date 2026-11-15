#pragma once
// Where per-user files (window layout, later: recent files, texture folders) live.
// Always on the user's machine, never inside the repo.
#include <string>

namespace scal {

// Returns the settings directory, creating it if needed.
//   macOS: ~/Library/Application Support/Scalpellino
//   other: $XDG_CONFIG_HOME/scalpellino or ~/.config/scalpellino
// Returns an empty string if no home directory is available.
std::string settingsDir();

} // namespace scal
