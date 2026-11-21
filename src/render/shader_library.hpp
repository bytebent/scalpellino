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
//   vec3 envmap(vec3 dir)  the reflection (linear): the map's sky (cheap chrome)
//   vec3 toLinear(vec3), toSrgb(vec3), float luminance(vec3)
//   and each setting of its header as a uniform: "$detailscale" -> u_detailscale
//   (texture: sampler2D u_x + int u_x_set; float; int / bool: int; color: vec3;
//   text: int u_x_set). After shade(): exposure, back to sRGB, fog.
#include "raylib.h"
#include "core/material.hpp"
#include "core/shader_file.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace scal {

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
    // program's uniforms, binding its textures to units from `firstUnit` up. `entry` (may be
    // null) resolves texture names written in the material. Returns texture names that
    // couldn't be found.
    std::vector<std::string> bind(Program& p, const Material* mat, const MaterialIndex::Entry* entry, TextureLibrary* textures,
                                  int firstUnit);
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
