#pragma once
// Native file dialogs. On macOS these are the system's own Open / Save / folder panels.
// Elsewhere they're unavailable and the editor falls back to its typed-path dialog.
#include <string>
#include <vector>

namespace scal {

bool nativeDialogsAvailable();

// Each returns true and sets `out` if the user picked something; false on cancel.
// `extensions` without dots, e.g. {"map"}; empty = any file.
bool openFileDialog(const std::string& startDir, const std::vector<std::string>& extensions, std::string& out);
bool saveFileDialog(const std::string& startDir, const std::string& defaultName,
                    const std::vector<std::string>& extensions, std::string& out);
bool chooseFolderDialog(const std::string& startDir, std::string& out);

} // namespace scal
