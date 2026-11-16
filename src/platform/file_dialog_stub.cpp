// Non-macOS builds: no native dialogs (the editor uses its typed-path dialog).
#include "platform/file_dialog.hpp"

namespace scal {
bool nativeDialogsAvailable() { return false; }
bool openFileDialog(const std::string&, const std::vector<std::string>&, std::string&) { return false; }
bool saveFileDialog(const std::string&, const std::string&, const std::vector<std::string>&, std::string&) { return false; }
bool chooseFolderDialog(const std::string&, std::string&) { return false; }
} // namespace scal
