#pragma once
// Material shaders on the graphics card (milestone 12, v0.12.4): the GLSL files of
// core/shader_file.hpp (Source's own, shipped with the app, and the user's in the Bottega's
// `shaders/`; Bottega first, the first of a name wins), compiled for renders and the Camera
// Preview only. The editor never uses them (it shows the base texture).
//
// A shader file's body defines `vec4 shade()`: the surface's colour, linear, before the
// camera's exposure, and its alpha. The app puts a prelude in front that gives it:
//   vec4 baseColor()   the base texture (sRGB as stored, alpha); props: x the model colour
//   vec2 baseUV()      base texture coordinates
//   vec3 bakedLight()  linear light from the bake (lightmap, prop vertex light, or a
//                      Source model's ambient cube + 2 lights)
//   vec3 worldPos(), worldNormal() (unit, toward the viewer), eyeVector() (unit, to the eye)
//   vec3 mirrorReflection(vec2 offset), mirrorRefraction(vec2 offset)  (v0.12.7) a mirror
//                      face's reflection / what is behind it (linear), or the probes when
//                      it has no picture; int mirrorWants: the material's parts (1, 2, 4)
//   vec3 screenBehind(vec2 offset)  (v0.12.7b) Refract: the picture drawn so far, shifted
//   vec3 envmap(vec3 dir)  the reflection (linear): with `$envmap env_cubemap` and probes in
//                      the map, its 4 nearest reflection probes blended (v0.12.6); else
//                      the map's sky (cheap chrome)
//   vec3 toLinear(vec3), toSrgb(vec3), float luminance(vec3)
//   and each setting of its header as a uniform: "$detailscale" -> u_detailscale
//   (texture: sampler2D u_x + int u_x_set; float; int / bool: int; color: vec3;
//   text: int u_x_set). After shade(): exposure, back to sRGB, fog.
#include "raylib.h"
#include "core/material.hpp"
#include "core/shader_file.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace scal {

// Texture units of the reflection probes (v0.12.6); material textures stay below them.
constexpr int kProbeTexUnit = 14, kProbeDataUnit = 15;
// ... and of the mirror pictures (v0.12.7).
constexpr int kMirrorReflectUnit = 12, kMirrorRefractUnit = 13;

class TextureLibrary;

enum class ShaderGeometry { World, Model };

class ShaderLibrary {
public:
    ~ShaderLibrary();
    void setBuiltinFolder(const std::string& dir);
    void setBottegaRoot(const std::string& root);
    // Reads the folders again; compiled programs are dropped (compiled again when used).
    void rescan();

    // A shader by name (case-insensitive; Bottega first), null if none.
    const ShaderFile* find(const std::string& name) const;
    std::vector<std::string> names() const;
    // Files that couldn't be read (name or path: why).
    const std::vector<std::string>& problems() const { return problems_; }

    struct Program {
        Shader shader{};
        bool ok = false;
        std::string error;                // the GLSL compiler's complaint (first lines)
        const ShaderFile* file = nullptr;
        std::map<std::string, int> locs;
        int at(const std::string& n);
    };
    // The compiled program of shader `name` for `geom`; null if it doesn't exist.
    // Check ok: a shader that doesn't compile keeps its error and isn't tried again.
    Program* program(const std::string& name, ShaderGeometry geom);

    // Sets the material's settings (or the header's defaults where it has none) as the
    // program's uniforms, binding its textures to units from `firstUnit` up. `texture` turns
    // a texture value written in the material into a GPU texture (0: not found). Returns the
    // texture values that couldn't be found.
    using TextureFinder = std::function<unsigned(const std::string& value)>;
    std::vector<std::string> bind(Program& p, const Material* mat, const TextureFinder& texture, int firstUnit);
    // A finder for materials of the texture library (`entry` resolves names written in it).
    static TextureFinder libraryFinder(TextureLibrary& textures, const MaterialIndex::Entry* entry);
    // Unbinds what bind() bound (call after drawing, with the batch flushed).
    void unbind(int firstUnit);

private:
    std::string builtin_, bottega_;
    std::map<std::string, std::unique_ptr<ShaderFile>> files_; // lower-case name
    std::map<std::pair<std::string, int>, Program> programs_;
    std::vector<std::string> problems_;
    int boundUnits_ = 0;
    void scan(const std::string& dir);
    void unloadPrograms();
};

// The common GLSL helpers (toLinear, toSrgb, luminance, fogAmount, envmap) and uniforms.
const char* shaderCommonGlsl();

} // namespace scal
