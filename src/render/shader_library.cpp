#include "render/shader_library.hpp"
#include "core/bottega.hpp"
#include "render/texture_library.hpp"
#include "rlgl.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>

namespace scal {

namespace fs = std::filesystem;

namespace {

// Brushes and terrain (the lightmap shader's vertex stage, render/baked_light.cpp): the
// normal travels in the vertex colour (rgb), terrain's layer blend in its alpha.
const char* kWorldVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;   // xy: where the luxel is in the lightmap page; z: bump maps' step
in vec4 vertexColor;    // rgb: the surface normal (0..1), a: terrain layer blend
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec2 fragLm;
out vec4 fragColor;
out vec3 fragPos;
flat out float fragBump;
void main() {
    fragTexCoord = vertexTexCoord;
    fragLm = vertexNormal.xy;
    fragBump = vertexNormal.z;
    fragColor = vertexColor;
    fragPos = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})";

const char* kWorldFS = R"(#version 330
in vec2 fragTexCoord;
in vec2 fragLm;
in vec4 fragColor;
in vec3 fragPos;
flat in float fragBump;
uniform sampler2D texture0;
uniform sampler2D lightmap;
uniform vec4 colDiffuse;
out vec4 finalColor;
vec2 baseUV() { return fragTexCoord; }
vec3 worldPos() { return fragPos; }
vec3 worldNormal() {
    vec3 n = normalize(fragColor.rgb * 2.0 - 1.0);
    return gl_FrontFacing ? n : -n;
}
vec3 bakedLight() { return texture(lightmap, fragLm).rgb; }
// bumped faces (v0.12.4b): the 3 bump-direction lightmaps next to the flat one
bool hasBumpLight() { return fragBump != 0.0; }
vec3 bumpLight(int k) {
    vec2 off = fragBump > 0.0 ? vec2(fragBump * float(k + 1), 0.0) : vec2(0.0, -fragBump * float(k + 1));
    return texture(lightmap, fragLm + off).rgb;
}
vec4 baseColor() { return texture(texture0, fragTexCoord); }
float layerAlpha() { return fragColor.a; }
vec3 unbumpedLightFor(vec3 n) { return bakedLight(); }
// no model lights on brushes
vec3 ambientCube(vec3 n) { return bakedLight(); }
int modelLightCount() { return 0; }
void modelLightAt(int i, out vec3 L, out vec3 col) { L = vec3(0.0, 0.0, 1.0); col = vec3(0.0); }
)";

// Props (render/model_library.cpp's baked vertex stage).
const char* kModelVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform sampler2D vlight;
uniform int base;       // < 0: like a Source model
uniform int bumpedVL;   // 1: 4 values a vertex (flat, then the 3 bump directions)
out vec2 uv;
out vec3 nrm;
out vec3 wpos;
out vec3 vl;
out vec3 vb0;
out vec3 vb1;
out vec3 vb2;
void main() {
    uv = vertexTexCoord;
    nrm = normalize(mat3(matNormal) * vertexNormal);
    wpos = (matModel * vec4(vertexPosition, 1.0)).xyz;
    vl = vec3(0.0);
    vb0 = vb1 = vb2 = vec3(0.0);
    if (base >= 0) {
        if (bumpedVL == 1) {
            int id = base + 4 * gl_VertexID;
            vl = texelFetch(vlight, ivec2(id % 4096, id / 4096), 0).rgb;
            vb0 = texelFetch(vlight, ivec2((id + 1) % 4096, (id + 1) / 4096), 0).rgb;
            vb1 = texelFetch(vlight, ivec2((id + 2) % 4096, (id + 2) / 4096), 0).rgb;
            vb2 = texelFetch(vlight, ivec2((id + 3) % 4096, (id + 3) / 4096), 0).rgb;
        } else {
            int id = base + gl_VertexID;
            vl = texelFetch(vlight, ivec2(id % 4096, id / 4096), 0).rgb;
        }
    }
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})";

const char* kModelFS = R"(#version 330
in vec2 uv;
in vec3 nrm;
in vec3 wpos;
in vec3 vl;
in vec3 vb0;
in vec3 vb1;
in vec3 vb2;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform int hasTex;
uniform float cutoff;        // the model's own cutout (glTF alpha mask), < 0 none
uniform int base;
uniform int bumpedVL;
uniform vec3 cube[6];
uniform int lightCount;
uniform int lkind[2];        // 0 point, 1 spot, 2 sun
uniform vec3 lpos[2];
uniform vec3 ldir[2];
uniform vec3 lcol[2];
uniform vec3 latt[2];        // constant, linear, quadratic (Hammer units)
uniform vec3 lfade[2];       // start, end, cap
uniform vec3 lspot[2];       // stopdot, stopdot2, exponent
out vec4 finalColor;
vec2 baseUV() { return uv; }
vec3 worldPos() { return wpos; }
vec3 worldNormal() {
    vec3 n = normalize(nrm);
    return gl_FrontFacing ? n : -n;
}
// The ambient cube read along n (Source's model ambient).
vec3 ambientCube(vec3 n) {
    vec3 n2 = n * n;
    return n2.x * (n.x < 0.0 ? cube[1] : cube[0]) + n2.y * (n.y < 0.0 ? cube[3] : cube[2]) + n2.z * (n.z < 0.0 ? cube[5] : cube[4]);
}
// The prop's 2 strongest lights (as Source gives a model): direction to the light and its
// colour with VRAD's falloff at this point (no N.L).
int modelLightCount() { return lightCount; }
void modelLightAt(int i, out vec3 L, out vec3 col) {
    col = vec3(0.0);
    L = vec3(0.0, 0.0, 1.0);
    if (lkind[i] == 2) { L = -ldir[i]; col = lcol[i]; return; }
    vec3 d = lpos[i] - wpos;
    float dcm = length(d);
    L = d / max(dcm, 1e-4);
    float dist = dcm / 2.54;
    bool fade = lfade[i].y > lfade[i].x;
    if (fade && dist > lfade[i].y) return;
    dist = max(dist, 1.0);
    float e = min(dist, lfade[i].z);
    float den = latt[i].x + latt[i].y * e + latt[i].z * e * e;
    if (den <= 0.0) return;
    float f = 1.0 / den;
    if (lkind[i] == 1) {
        float d2 = dot(-L, ldir[i]);
        if (d2 <= lspot[i].y) return;
        f *= d2;
        if (d2 <= lspot[i].x) {
            float m = clamp((d2 - lspot[i].y) / max(lspot[i].x - lspot[i].y, 1e-6), 0.0, 1.0);
            if (lspot[i].z != 0.0 && lspot[i].z != 1.0) m = pow(m, lspot[i].z);
            f *= m;
        }
    }
    if (fade) {
        float t = 1.0 - clamp((dist - lfade[i].x) / (lfade[i].y - lfade[i].x), 0.0, 1.0);
        f *= t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    }
    col = lcol[i] * f;
}
// A Source model's light: the ambient cube read along n, plus 2 lights with Half-Lambert.
vec3 modelLight(vec3 n) {
    vec3 c = ambientCube(n);
    for (int i = 0; i < 2; ++i) {
        if (i >= lightCount) break;
        vec3 L, col;
        modelLightAt(i, L, col);
        float hl = 0.5 * dot(n, L) + 0.5;   // Half-Lambert
        c += col * hl * hl;
    }
    return c;
}
vec3 bakedLight() { return base >= 0 ? vl : modelLight(worldNormal()); }
vec3 tangentToWorld(vec3 n);
// a bumped normal without bump lightmaps: a Source model lights it with its cube and lights
vec3 unbumpedLightFor(vec3 n) { return base >= 0 ? vl : modelLight(tangentToWorld(n)); }
// per-vertex props with a bump map: the 3 bump-direction values (v0.12.4b)
bool hasBumpLight() { return base >= 0 && bumpedVL == 1; }
vec3 bumpLight(int k) { return k == 0 ? vb0 : k == 1 ? vb1 : vb2; }
vec4 baseColor() {
    vec4 t = colDiffuse;
    if (hasTex == 1) t *= texture(texture0, uv);
    if (cutoff >= 0.0 && t.a < cutoff) discard;
    return t;
}
float layerAlpha() { return 1.0; }
)";

const char* kCommon = R"(
uniform float exposure;
uniform int fogOn;
uniform vec3 camPos;
uniform vec3 fogColor;
uniform vec2 fogRange;
uniform vec2 fogHeight;     // the fog's base and thickness (thickness 0: everywhere)
uniform int orthoOn;        // an orthographic camera looks along viewDir everywhere
uniform vec3 viewDir;
uniform sampler2D skyTex;   // the map's sky, small (the reflection)
uniform int skyOn;
uniform float skyCover;     // the part of the panorama's height it holds
uniform vec3 skyBelow;      // 0..1, the plain colour below that
uniform vec3 skyFlat;       // linear, no sky picture
vec3 toLinear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c)); }
vec3 toSrgb(vec3 l) { l = clamp(l, 0.0, 1.0); return mix(l * 12.92, 1.055 * pow(l, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), l)); }
float luminance(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 eyeVector() { return orthoOn == 1 ? -viewDir : normalize(camPos - worldPos()); }
// The texture's frame at this pixel (v0.12.4b): x along the texture's s (u), y along t (v,
// down the picture), z the normal; the same frame the bake used for the bump directions
// (VRAD's GetBumpNormals).
mat3 tangentFrame() {
    vec3 N = worldNormal();
    vec3 p = worldPos();
    vec2 tc = baseUV();
    vec3 dp1 = dFdx(p), dp2 = dFdy(p);
    vec2 duv1 = dFdx(tc), duv2 = dFdy(tc);
    vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    vec3 b0 = T - N * dot(N, T);
    if (dot(b0, b0) < 1e-20) b0 = abs(N.z) < 0.9 ? cross(vec3(0.0, 0.0, 1.0), N) : cross(vec3(1.0, 0.0, 0.0), N);
    b0 = normalize(b0);
    vec3 b1 = normalize(cross(N, b0));
    if (dot(b1, B) < 0.0) b1 = -b1;
    return mat3(b0, b1, N);
}
vec3 tangentToWorld(vec3 n) { return normalize(tangentFrame() * n); }
// A normal from a bump map texel (Source's convention, X+ Y- Z+; flipGreen for the other).
vec3 bumpNormal(vec4 texel, bool flipGreen) {
    vec3 n = texel.rgb * 2.0 - 1.0;
    if (flipGreen) n.y = -n.y;
    return length(n) > 1e-4 ? normalize(n) : vec3(0.0, 0.0, 1.0);
}
// Source's bump basis in the texture's frame.
const vec3 bumpBasis0 = vec3(0.81649658, 0.0, 0.57735027);
const vec3 bumpBasis1 = vec3(-0.40824829, 0.70710678, 0.57735027);
const vec3 bumpBasis2 = vec3(-0.40824829, -0.70710678, 0.57735027);
// The baked light for a bumped normal n (in the texture's frame): Source SDK 2013's
// radiosity normal mapping, squared weights over their sum. Without bump light: flat.
vec3 bakedLightBumped(vec3 n) {
    if (!hasBumpLight()) return unbumpedLightFor(n);
    vec3 dp = clamp(vec3(dot(n, bumpBasis0), dot(n, bumpBasis1), dot(n, bumpBasis2)), 0.0, 1.0);
    dp *= dp;
    float sum = max(dp.x + dp.y + dp.z, 1e-4);
    return (dp.x * bumpLight(0) + dp.y * bumpLight(1) + dp.z * bumpLight(2)) / sum;
}
// SSBump (Green 2007): the texel holds the 3 weights itself.
vec3 bakedLightSSBump(vec3 w) {
    if (!hasBumpLight()) return bakedLight();
    return w.x * bumpLight(0) + w.y * bumpLight(1) + w.z * bumpLight(2);
}
float fogAmount(vec3 p) {
    float d = length(p - camPos);
    float path = d;
    if (fogHeight.y > 0.0) {
        float lo = fogHeight.x, hi = fogHeight.x + fogHeight.y, z0 = camPos.z, z1 = p.z;
        if (abs(z1 - z0) < 0.001) path = (z0 >= lo && z0 <= hi) ? d : 0.0;
        else {
            float a = clamp((lo - z0) / (z1 - z0), 0.0, 1.0), b = clamp((hi - z0) / (z1 - z0), 0.0, 1.0);
            path = abs(b - a) * d;
        }
    }
    return clamp((path - fogRange.x) / max(fogRange.y - fogRange.x, 1.0), 0.0, 1.0);
}
// The sky in direction d (the panorama as the 3D view draws it: u from the yaw, v from the
// height, the middle column facing +X), linear.
vec3 envmap(vec3 d) {
    if (skyOn == 0) return skyFlat;
    d = normalize(d);
    float u = 0.5 - atan(d.y, d.x) / 6.28318531;
    float v = 0.5 - asin(clamp(d.z, -1.0, 1.0)) / 3.14159265;
    if (v > skyCover) return toLinear(skyBelow);
    return toLinear(texture(skyTex, vec2(u, v / max(skyCover, 0.01))).rgb);
}
)";

const char* kMain = R"(
void main() {
    vec4 c = shade();
    vec3 rgb = toSrgb(c.rgb * exposure);
    if (fogOn == 1) rgb = mix(rgb, fogColor, fogAmount(worldPos()));
    finalColor = vec4(rgb, clamp(c.a, 0.0, 1.0) * layerAlpha());
}
)";

std::string settingUniforms(const ShaderFile& f) {
    std::string u;
    for (const ShaderSetting& s : f.settings) {
        if (s.key == "$basetexture") continue; // texture0, through baseColor()
        const std::string n = shaderUniformName(s.key);
        if (s.type == "texture") u += "uniform sampler2D " + n + ";\nuniform int " + n + "_set;\n";
        else if (s.type == "float") u += "uniform float " + n + ";\n";
        else if (s.type == "int" || s.type == "bool") u += "uniform int " + n + ";\n";
        else if (s.type == "color") u += "uniform vec3 " + n + ";\n";
        else if (s.type == "text") u += "uniform int " + n + "_set;\n";
    }
    return u;
}

std::optional<std::string> readText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

const char* shaderCommonGlsl() { return kCommon; }

ShaderLibrary::~ShaderLibrary() { unloadPrograms(); }

void ShaderLibrary::setBuiltinFolder(const std::string& dir) {
    builtin_ = dir;
    rescan();
}

void ShaderLibrary::setBottegaRoot(const std::string& root) {
    if (root == bottega_) return;
    bottega_ = root;
    rescan();
}

void ShaderLibrary::unloadPrograms() {
    for (auto& [k, p] : programs_)
        if (p.ok && p.shader.id) UnloadShader(p.shader);
    programs_.clear();
}

void ShaderLibrary::scan(const std::string& dir) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return;
    std::vector<std::string> paths;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = lowerCase(it->path().extension().string());
        if (ext == ".glsl" || ext == ".frag" || ext == ".fs") paths.push_back(it->path().string());
    }
    std::sort(paths.begin(), paths.end());
    for (const std::string& path : paths) {
        const std::optional<std::string> text = readText(path);
        auto f = std::make_unique<ShaderFile>();
        std::string err;
        if (!text || !parseShaderFile(*text, *f, &err)) {
            problems_.push_back(fs::path(path).filename().string() + ": " + (text ? err : "can't be read"));
            continue;
        }
        if (f->kind != "material") continue; // other kinds (camera effects...) come later
        f->path = path;
        const std::string key = lowerCase(f->name);
        if (files_.count(key)) continue; // first folder wins
        files_[key] = std::move(f);
    }
}

void ShaderLibrary::rescan() {
    unloadPrograms();
    files_.clear();
    problems_.clear();
    if (!bottega_.empty()) scan(bottega::path(bottega_, "shaders"));
    scan(builtin_);
}

const ShaderFile* ShaderLibrary::find(const std::string& name) const {
    auto it = files_.find(lowerCase(name));
    return it == files_.end() ? nullptr : it->second.get();
}

std::vector<std::string> ShaderLibrary::names() const {
    std::vector<std::string> n;
    for (const auto& [k, f] : files_) n.push_back(f->name);
    return n;
}

int ShaderLibrary::Program::at(const std::string& n) {
    auto it = locs.find(n);
    if (it != locs.end()) return it->second;
    return locs[n] = GetShaderLocation(shader, n.c_str());
}

ShaderLibrary::Program* ShaderLibrary::program(const std::string& name, ShaderGeometry geom) {
    const ShaderFile* f = find(name);
    if (!f) return nullptr;
    const auto key = std::make_pair(lowerCase(name), (int)geom);
    auto it = programs_.find(key);
    if (it != programs_.end()) return &it->second;
    Program& p = programs_[key];
    p.file = f;
    const bool world = geom == ShaderGeometry::World;
    const std::string fs = std::string(world ? kWorldFS : kModelFS) + kCommon + settingUniforms(*f) + "#line " +
                           std::to_string(f->bodyLine) + "\n" + f->body + "\n#line 100000\n" + kMain;
    // The compiler's complaint goes to raylib's log; keep it short for ours.
    p.shader = LoadShaderFromMemory(world ? kWorldVS : kModelVS, fs.c_str());
    p.ok = p.shader.id != 0 && p.shader.id != rlGetShaderIdDefault();
    if (!p.ok) {
        p.error = "the shader " + f->name + " (" + fs::path(f->path).filename().string() + ") doesn't compile; see the terminal";
        p.shader = Shader{};
    }
    return &p;
}

ShaderLibrary::TextureFinder ShaderLibrary::libraryFinder(TextureLibrary& textures, const MaterialIndex::Entry* entry) {
    return [&textures, entry](const std::string& v) -> unsigned {
        std::string tex = entry ? textures.materials().resolveTexture(*entry, v) : "";
        if (tex.empty() && textures.exists(v)) tex = v;
        return tex.empty() ? 0u : textures.get(tex).id;
    };
}

std::vector<std::string> ShaderLibrary::bind(Program& p, const Material* mat, const TextureFinder& texture, int firstUnit) {
    std::vector<std::string> missing;
    if (!p.ok || !p.file) return missing;
    int unit = firstUnit;
    for (const ShaderSetting& s : p.file->settings) {
        if (s.key == "$basetexture") continue;
        const std::string n = shaderUniformName(s.key);
        const std::string v = mat && mat->has(s.key) ? mat->str(s.key) : s.def;
        if (s.type == "texture") {
            int set = 0;
            if (!v.empty() && unit < 16) {
                const unsigned id = texture ? texture(v) : 0u;
                if (id) {
                    rlActiveTextureSlot(unit);
                    rlEnableTexture(id);
                    SetShaderValue(p.shader, p.at(n), &unit, SHADER_UNIFORM_INT);
                    ++unit;
                    set = 1;
                } else
                    missing.push_back(v);
            }
            SetShaderValue(p.shader, p.at(n + "_set"), &set, SHADER_UNIFORM_INT);
        } else if (s.type == "float") {
            Material m;
            m.params[s.key] = v;
            const float x = (float)m.num(s.key, 0);
            SetShaderValue(p.shader, p.at(n), &x, SHADER_UNIFORM_FLOAT);
        } else if (s.type == "int" || s.type == "bool") {
            Material m;
            m.params[s.key] = v;
            const int x = (int)m.num(s.key, 0);
            SetShaderValue(p.shader, p.at(n), &x, SHADER_UNIFORM_INT);
        } else if (s.type == "color") {
            Vec3 c{1, 1, 1};
            parseVmtVector(v, c);
            const float a[3] = {(float)c.x, (float)c.y, (float)c.z};
            SetShaderValue(p.shader, p.at(n), a, SHADER_UNIFORM_VEC3);
        } else if (s.type == "text") {
            const int set = v.empty() ? 0 : 1;
            SetShaderValue(p.shader, p.at(n + "_set"), &set, SHADER_UNIFORM_INT);
        }
    }
    rlActiveTextureSlot(0);
    boundUnits_ = unit - firstUnit;
    return missing;
}

void ShaderLibrary::unbind(int firstUnit) {
    for (int i = 0; i < boundUnits_; ++i) {
        rlActiveTextureSlot(firstUnit + i);
        rlDisableTexture();
    }
    rlActiveTextureSlot(0);
    boundUnits_ = 0;
}

} // namespace scal
