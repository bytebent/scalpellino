#pragma once
// Materials (milestone 12, v0.12.4): Source's `.vmt` files, read and written with Source's
// own syntax and key names, so Source materials read almost as they are. No graphics.
//
//  - Every texture is a material already (LightmappedGeneric with that picture). A `.vmt`
//    next to it, with the same name, adds settings. A `.vmt` with another name whose
//    `$basetexture` is a texture of the library is a **variant** ("Brick_wet"): its own
//    entry, on faces like a texture. Faces store the name, so maps don't change.
//  - **Parents** use Source's `patch` shader: `include` the parent `.vmt`, then `insert`
//    (adds or overrides) and `replace` (only keys the parent has) what differs. A value ""
//    removes a key. Chains of parents are allowed (later Source branches allow them too).
//  - **Helper maps** found by name hook onto their texture: `<name>_normal`, `_ssbump`,
//    `_mask`, `_selfillum` (and a few similar), in the same folder. They are hidden from the
//    list (a toggle shows them) and fill the key only when the material doesn't set it.
//  - Keys are case-insensitive in Source: they are kept in lower case here.
#include "core/math.hpp"
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace scal {

// ---- KeyValues as a .vmt holds them
struct VmtKey {
    std::string key;   // lower case
    std::string value;
};
struct VmtBlock {
    std::string name;  // as written (a shader name keeps its case)
    std::vector<VmtKey> keys;
    std::vector<VmtBlock> blocks;
};
// Reads `text` (the first block is the root). False with `error` set if it isn't one.
bool parseVmt(const std::string& text, VmtBlock& root, std::string* error = nullptr);
// Source's layout: the name, braces, one tab-indented "key" "value" per line.
std::string writeVmt(const VmtBlock& root);

std::string lowerCase(std::string s);

// ---- one .vmt file, as written
struct MaterialFile {
    std::string shader = "LightmappedGeneric"; // "patch" for a patch
    std::string include;                       // patch: the parent, as written
    std::vector<VmtKey> params;                // the shader's keys (patch: its `insert`)
    std::vector<VmtKey> replace;               // patch: `replace`
    std::vector<VmtBlock> extra;               // blocks kept as they are (Proxies, fallbacks...)

    bool isPatch() const { return lowerCase(shader) == "patch"; }
    static bool parse(const std::string& text, MaterialFile& out, std::string* error = nullptr);
    std::string text() const;
    // The file's own value for `key` (patch: insert, then replace), null if it has none.
    const std::string* own(const std::string& key) const;
    std::set<std::string> ownKeys() const;
    void setOwn(const std::string& key, const std::string& value);
    void removeOwn(const std::string& key);
};

// ---- a material with its parents applied
struct Material {
    std::string shader = "LightmappedGeneric";
    std::map<std::string, std::string> params; // lower-case keys
    std::set<std::string> own;                 // keys set by the material's own file
    std::string parentFile;                    // the parent's .vmt path ("" none)
    std::string problem;                       // a file that can't be read, a missing parent...

    bool has(const std::string& key) const { return params.count(key) > 0; }
    std::string str(const std::string& key, const std::string& def = "") const;
    double num(const std::string& key, double def = 0) const;
    bool flag(const std::string& key) const { return num(key, 0) != 0; }
    // Source vectors: "[r g b]" (as they are) or "{r g b}" (0..255); a single number = grey.
    Vec3 vec(const std::string& key, const Vec3& def) const;
};

// Parses a Source vector value (see Material::vec). False if it isn't one.
bool parseVmtVector(const std::string& v, Vec3& out);
std::string formatVmtVector(const Vec3& v); // "[r g b]"
// A texlight value, lights.rad style: "r g b brightness" (colour 0..255). False if not.
bool parseTexLight(const std::string& v, Vec3& color255, double& brightness);

// Applies `f` on top of its parent (if it is a patch). `parent(include)` gives the parent's
// file and its path (or nullopt). Depth-limited (a loop of includes stops with a problem).
using ParentLoader = std::function<std::optional<std::pair<MaterialFile, std::string>>(const std::string& include)>;
Material resolveMaterial(const MaterialFile& f, const ParentLoader& parent, int depth = 0);

// The suffixes of helper maps and the key each fills (`_ssbump` also sets $ssbump 1).
struct HelperSuffix { const char* suffix; const char* key; };
const std::vector<HelperSuffix>& helperSuffixes();

// ---- every material of the texture folders (scanned like the texture library)
class MaterialIndex {
public:
    enum class Kind { Texture, Variant, Helper, Parent };
    struct Entry {
        std::string name;        // as maps store it
        Kind kind = Kind::Texture;
        std::string picture;     // the picture file (textures and helpers)
        std::string vmt;         // its .vmt file, "" if none
        std::string folder;      // the library folder it was found in
        std::string keyRoot;     // the Bottega root for Bottega folders, else ""
        bool writable = true;    // false for the built-in folder (inside the app)
        std::string base;        // the texture that holds its picture (a texture: itself)
        std::string helperOf;    // helpers: the texture they hook onto
        Material mat;            // resolved
        MaterialFile file;       // as written (default when there is no .vmt)
    };

    void clear();
    // Folders in search order (first match wins), as the texture library scans them.
    void scanFolder(const std::string& dir, const std::string& keyRoot, bool writable);
    // Links helpers, sorts out variants and parents, resolves every material.
    void finish();

    const Entry* find(const std::string& name) const;
    // Textures and variants (and helpers when asked), sorted.
    std::vector<std::string> names(bool helpers) const;
    // Every .vmt there is (any kind), by name: candidates for a parent.
    std::vector<const Entry*> vmtEntries() const;
    // The entry of a .vmt file by its path (null if not scanned).
    const Entry* byVmt(const std::string& path) const;

    // Where `name`'s .vmt is, or would be written; "" if it can't be (built-in textures).
    std::string vmtPathFor(const std::string& name) const;
    // How a material in `fromEntry`'s folder names the parent file `parentVmt` in `include`:
    // the path from the library folder's root (the Bottega root for Bottega folders), as
    // Source names "materials/....vmt".
    std::string includeName(const Entry& from, const std::string& parentVmt) const;
    // The .vmt path an include of `from` means ("" if not found).
    std::string resolveInclude(const Entry& from, const std::string& include) const;
    // The texture name a `$basetexture` value of `from` means ("" if none matches).
    std::string resolveTexture(const Entry& from, const std::string& value) const;

    // The file as it will be written when a setting changes: its own file, or a new one
    // (`LightmappedGeneric` with the texture as `$basetexture`).
    MaterialFile fileFor(const std::string& name) const;

private:
    struct Found { std::string path, folder, keyRoot; bool writable; };
    std::vector<Found> pictures_, vmts_;
    std::map<std::string, Entry> entries_;
    std::map<std::string, std::string> byVmt_; // .vmt path -> name
    std::string nameOf(const Found& f) const;
    Material resolveEntry(const Entry& e) const;
};

} // namespace scal
