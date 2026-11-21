#pragma once
// Sky library (milestone 8): sky images from the Bottega, the user's sky folders and the
// built-in folder (shipped inside the app), searched in that order. A sky's name is its
// file name without extension, except for skies in the Bottega: those go by their path
// relative to the Bottega root, e.g. "skies/Dusk".
// Panoramas (2:1) load as they are; cube crosses (4:3) are turned into panoramas
// (core/sky.hpp). Files load on first use.
// Memory (v0.11.2): a plain-coloured bottom (Sky Lab's ground) isn't kept on the graphics
// card: only the rows down to 5 degrees past where the colour stops changing; the rest is
// drawn in that colour (`cover`, `below`). The files stay whole panoramas.
#include "raylib.h"
#include "core/bake.hpp"
#include <map>
#include <string>
#include <vector>

namespace scal {

class SkyLibrary {
public:
    ~SkyLibrary();
    void setBuiltinFolder(const std::string& dir);
    // The Bottega root ("" = none); its skies/ folder is searched first. Rescans.
    void setBottegaRoot(const std::string& root);
    const std::string& bottegaRoot() const { return bottega_; }
    const std::vector<std::string>& userFolders() const { return userFolders_; }
    void addUserFolder(const std::string& dir);
    void removeUserFolder(size_t index);
    void rescan();

    const std::vector<std::string>& names() const { return names_; }
    bool exists(const std::string& name) const { return entries_.count(name) > 0; }
    // Panorama texture for `name`, or nullptr (unknown, unreadable, or not 2:1 / 4:3).
    const Texture2D* get(const std::string& name);
    // How much of the panorama's height the texture holds (1 = all) and the plain colour
    // drawn below it. Call after get().
    struct Cut { float cover = 1; unsigned char below[3] = {0, 0, 0}; };
    Cut cut(const std::string& name) const;
    // A small copy (128 pixels wide, the same cut) for reflections (cheap chrome): about the
    // size of Source's default cubemap (32 a side), so reflections are as soft. Call after get().
    const Texture2D* reflection(const std::string& name) const;
    static constexpr double kCutMarginDeg = 5;
    // The sky as small linear colours for the baked sky light (core/bake.hpp), read from the
    // file (whole panorama); invalid if it can't be read. Kept until a rescan.
    const SkyPicture& picture(const std::string& name);
    // Why get() failed for `name` ("" if it didn't).
    std::string problem(const std::string& name) const;
    void unloadAll();

private:
    struct Entry {
        std::string path;
        Texture2D tex{};
        Texture2D refl{};
        bool loaded = false, failed = false;
        Cut cut;
        std::string problem;
        bool pictureDone = false;
        SkyPicture picture;
    };
    void scanFolder(const std::string& dir, const std::string& keyRoot = "");
    std::string builtin_;
    std::string bottega_;
    std::vector<std::string> userFolders_;
    std::map<std::string, Entry> entries_;
    std::vector<std::string> names_;
};

} // namespace scal
