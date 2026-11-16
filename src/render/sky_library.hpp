#pragma once
// Sky library (milestone 8): sky images from the built-in folder (shipped inside the
// app) and the user's sky folders. A sky's name is its file name without extension.
// Panoramas (2:1) load as they are; cube crosses (4:3) are turned into panoramas
// (core/sky.hpp). Files load on first use.
#include "raylib.h"
#include <map>
#include <string>
#include <vector>

namespace scal {

class SkyLibrary {
public:
    ~SkyLibrary();
    void setBuiltinFolder(const std::string& dir);
    const std::vector<std::string>& userFolders() const { return userFolders_; }
    void addUserFolder(const std::string& dir);
    void removeUserFolder(size_t index);
    void rescan();

    const std::vector<std::string>& names() const { return names_; }
    bool exists(const std::string& name) const { return entries_.count(name) > 0; }
    // Panorama texture for `name`, or nullptr (unknown, unreadable, or not 2:1 / 4:3).
    const Texture2D* get(const std::string& name);
    // Why get() failed for `name` ("" if it didn't).
    std::string problem(const std::string& name) const;
    void unloadAll();

private:
    struct Entry {
        std::string path;
        Texture2D tex{};
        bool loaded = false, failed = false;
        std::string problem;
    };
    void scanFolder(const std::string& dir);
    std::string builtin_;
    std::vector<std::string> userFolders_;
    std::map<std::string, Entry> entries_;
    std::vector<std::string> names_;
};

} // namespace scal
