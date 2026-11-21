#pragma once
// Shader files (milestone 12, v0.12.4): GLSL, one file per shader, with a few comment lines
// at the top giving its name, its kind and its settings (agreed 2026-11-18), so a tool can
// list a shader and show its settings without running it. Source's own shaders
// (LightmappedGeneric, VertexLitGeneric, UnlitGeneric) ship with the app in this format; the
// user's go in the Bottega's `shaders/` and get the same inputs. No graphics here: this
// only reads the header (the renderer compiles the body).
//
//   // scalpellino shader 1
//   // name: LightmappedGeneric
//   // kind: material
//   // about: Source's world shader: ...
//   // setting: $basetexture texture | the picture
//   // setting: $detailscale float 4 | detail repeats per base texture
//   // setting: $envmaptint color 1 1 1
//   ...
//   (GLSL: defines vec4 shade(), see docs/ARCHITECTURE.md "Materials and shaders")
//
// Setting types: texture, float, int, bool, color (3 numbers, Source's "[r g b]"), text,
// light ("r g b brightness", lights.rad's texlight format).
#include <string>
#include <vector>

namespace scal {

struct ShaderSetting {
    std::string key;   // "$detailscale" (lower case)
    std::string type;  // texture, float, int, bool, color, text, light
    std::string def;   // default as a .vmt value ("" none)
    std::string help;
};

struct ShaderFile {
    std::string name;  // as a .vmt names it
    std::string kind = "material";
    std::string about;
    int version = 1;
    std::vector<ShaderSetting> settings;
    std::string body;  // everything after the header
    int bodyLine = 1;  // the file's line number where the body starts (for GLSL's #line)
    std::string path;
    const ShaderSetting* setting(const std::string& key) const;
};

// Reads a shader file's text. False (with `error`) if the header has no name.
bool parseShaderFile(const std::string& text, ShaderFile& out, std::string* error = nullptr);
// The GLSL name of a setting's uniform: "$detailscale" -> "u_detailscale".
std::string shaderUniformName(const std::string& key);

} // namespace scal
