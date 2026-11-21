#pragma once
// The models found in the Bottega's `models/` folder, loaded on first use and kept (every
// prop of a model shares the one loaded Model: instancing starts here). No graphics, so it
// is tested headless; the renderer keeps its own GPU copies keyed by the same names.
//
// One self-contained folder per model: `models/tree/tree.glb` (+ its textures) is the model
// "models/tree". A folder's only file is its model whatever it is called; in a folder with
// several files the one named like the folder is the model, and if none is, each file is
// its own model ("models/props/barrel"). A loose file
// directly in `models/` is a model too. `.glb` and `.gltf` are read; `.mazz` (Mazzuola)
// joins once its format is final.
//
// Nothing here watches the disk: rescan() is the one way to see changes (Bottega's Rescan).
#include "core/material.hpp"
#include "core/prop.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace scal {

class ModelStore {
public:
    // The Bottega root ("" = none). Rescans.
    void setBottegaRoot(const std::string& root);
    const std::string& bottegaRoot() const { return root_; }
    // Looks at the disk again and forgets every loaded model.
    void rescan();

    // Sorted names of every model found ("models/tree").
    const std::vector<std::string>& names() const { return names_; }
    bool exists(const std::string& name) const { return files_.count(name) > 0; }
    std::string fileOf(const std::string& name) const;

    // The model (loaded the first time); null if it isn't there or can't be read.
    const Model* find(const std::string& name);
    // Why `name` has no model ("" if it has one or hasn't been tried): not found, damaged...
    std::string problem(const std::string& name) const;
    // Files left out because another file already has their name (e.g. "Stone Wall.glb"
    // and "Stone_Wall.glb", or "tree.glb" and "tree.gltf"): one line each, paths inside
    // the Bottega. The first in name order keeps the name (v0.10.2).
    const std::vector<std::string>& clashes() const { return clashes_; }
    // Increases on every rescan, so caches know their pointers are stale.
    unsigned generation() const { return generation_; }
    ModelFinder finder() { return [this](const std::string& n) { return find(n); }; }

private:
    std::string root_;
    std::map<std::string, std::string> files_;           // name -> file
    std::vector<std::string> names_;
    std::map<std::string, std::unique_ptr<Model>> loaded_;
    std::map<std::string, std::string> problems_;
    std::vector<std::string> clashes_;
    unsigned generation_ = 0;
};

// The models under `<root>/models`, as name -> file (see the rules above). Files whose
// name is already taken are left out and listed in `clashes` (if given).
// A model material's settings file (v0.12.4b): a `.vmt` beside the material's texture file
// (same name), else `<material name>.vmt` in the model's folder. Its keys add to what the
// glTF says (Phong, rim light, another bump map...). `found` false: there is none.
struct ModelMaterialSettings {
    bool found = false;
    std::string file;      // the .vmt
    std::string dir;       // its folder: texture values are looked up here first
    Material mat;
};
ModelMaterialSettings modelMaterialSettings(const Model& m, size_t material);
// A texture value written in such a file, as a file in `dir` ("" if none): as written,
// or with .png / .jpg / .jpeg / .tga / .bmp added.
std::string modelTextureFile(const std::string& dir, const std::string& value);

std::map<std::string, std::string> scanModelFiles(const std::string& root, std::vector<std::string>* clashes = nullptr);

} // namespace scal
