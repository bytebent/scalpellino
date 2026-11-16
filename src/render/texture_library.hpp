#pragma once
// Texture library: textures found in the built-in folder (shipped inside the app) and
// the user's own folders. A texture's name is its file name without extension (spaces
// become underscores, since .map files can't hold spaces). Files load on first use.
#include "raylib.h"
#include "core/texture.hpp"
#include <map>
#include <string>
#include <vector>

namespace scal {

class TextureLibrary {
public:
    ~TextureLibrary();

    void setBuiltinFolder(const std::string& dir);
    const std::vector<std::string>& userFolders() const { return userFolders_; }
    void addUserFolder(const std::string& dir);
    void removeUserFolder(size_t index);
    void rescan();

    // Sorted names of every texture found.
    const std::vector<std::string>& names() const { return names_; }
    bool exists(const std::string& name) const { return entries_.count(name) > 0; }
    std::string folderOf(const std::string& name) const;

    // GPU texture for `name` (loads it on first use). Unknown or broken -> magenta checker.
    const Texture2D& get(const std::string& name);
    TexSize size(const std::string& name);

    // Nearest (crisp pixels, default) or smooth filtering for every texture.
    void setSmooth(bool smooth);
    bool smooth() const { return smooth_; }

    void unloadAll();

private:
    struct Entry {
        std::string path;
        std::string folder;
        Texture2D tex{};
        bool loaded = false;
        bool failed = false;
    };
    void scanFolder(const std::string& dir);
    void applyFilter(Texture2D& t) const;
    const Texture2D& missing();

    std::string builtin_;
    std::vector<std::string> userFolders_;
    std::map<std::string, Entry> entries_;
    std::vector<std::string> names_;
    Texture2D missing_{};
    bool smooth_ = false;
};

} // namespace scal
