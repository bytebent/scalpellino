#pragma once
// Texture library: textures found in the Bottega, the user's own folders and the built-in
// folder (shipped inside the app), searched in that order (the first match wins).
// A texture's name is its file name without extension (spaces become underscores, since
// .map files can't hold spaces), except for textures in the Bottega: those go by their path
// relative to the Bottega root, e.g. "textures/walls/Brick_Red". Files load on first use.
//
// Materials (v0.12.4, core/material.hpp): every texture is a material; `.vmt` files beside
// them add settings, variants and parents. Names here are material names: a variant shows
// its base texture's picture (the editor draws only the base texture). Helper maps
// (`_normal`, `_mask`...) are left out of names() unless shown.
#include "raylib.h"
#include "core/material.hpp"
#include "core/texture.hpp"
#include <map>
#include <string>
#include <vector>

namespace scal {

class TextureLibrary {
public:
    ~TextureLibrary();

    void setBuiltinFolder(const std::string& dir);
    // The Bottega root ("" = none); its textures/ folder is searched first. Rescans.
    void setBottegaRoot(const std::string& root);
    const std::string& bottegaRoot() const { return bottega_; }
    const std::vector<std::string>& userFolders() const { return userFolders_; }
    void addUserFolder(const std::string& dir);
    void removeUserFolder(size_t index);
    // Reads the folders again (pictures and .vmt files); loaded pictures still there are kept.
    void rescan();

    // Sorted names of every texture and variant found (helper maps too when shown).
    const std::vector<std::string>& names() const { return names_; }
    void setShowHelpers(bool on);
    bool showHelpers() const { return showHelpers_; }
    bool exists(const std::string& name) const { return index_.find(name) != nullptr; }
    std::string folderOf(const std::string& name) const;

    // Materials: the index (entries, resolved settings, where files go).
    const MaterialIndex& materials() const { return index_; }
    const MaterialIndex::Entry* material(const std::string& name) const { return index_.find(name); }
    bool isVariant(const std::string& name) const;
    bool isBuiltin(const std::string& name) const;

    // GPU texture for `name` (its picture: a variant's base texture; loads it on first use).
    // Unknown or broken -> magenta checker.
    const Texture2D& get(const std::string& name);
    TexSize size(const std::string& name);
    // The picture's average colour, linear, 0..1 (how much light it bounces, as Source's
    // $reflectivity). Read once from the file; missing textures: -1 in x.
    Vec3 reflectivity(const std::string& name);
    // The picture's alpha (rows top first) for cutout shadows; false if it can't be read.
    bool alpha(const std::string& name, int& w, int& h, std::vector<unsigned char>& out);

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
        bool reflDone = false;
        Vec3 refl{-1, 0, 0};
    };
    void applyFilter(Texture2D& t) const;
    const Texture2D& missing();
    Entry* picture(const std::string& name); // the picture entry behind a material name

    std::string builtin_;
    std::string bottega_;
    std::vector<std::string> userFolders_;
    MaterialIndex index_;
    std::map<std::string, Entry> entries_; // pictures, by texture name
    std::vector<std::string> names_;
    Texture2D missing_{};
    bool smooth_ = false;
    bool showHelpers_ = false;
};

} // namespace scal
