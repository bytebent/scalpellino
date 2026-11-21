#include "render/model_library.hpp"
#include "core/bake.hpp"
#include "render/baked_light.hpp"
#include "render/shader_library.hpp"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace scal {

namespace {

constexpr size_t kChunkVertices = 65535 - 65535 % 3; // a mesh's vertex count stays below 65536

const char* kVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec2 uv;
out vec3 nrm;
out vec3 wpos;
void main() {
    uv = vertexTexCoord;
    nrm = normalize(mat3(matNormal) * vertexNormal);
    wpos = (matModel * vec4(vertexPosition, 1.0)).xyz;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})";

// The same for many props of one model in one draw call: each prop's transform comes in
// as an instance attribute; `mvp` is then view x projection only.
const char* kVSInstanced = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in mat4 instanceTransform;
uniform mat4 mvp;
out vec2 uv;
out vec3 nrm;
out vec3 wpos;
void main() {
    uv = vertexTexCoord;
    nrm = normalize(transpose(inverse(mat3(instanceTransform))) * vertexNormal);
    vec4 w = instanceTransform * vec4(vertexPosition, 1.0);
    wpos = w.xyz;
    gl_Position = mvp * w;
})";

// The editor's own shading (same as brushes) plus the fog preview.
const char* kFS = R"(#version 330
in vec2 uv;
in vec3 nrm;
in vec3 wpos;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform int hasTex;
uniform float cutoff;      // < 0: no cutout
uniform vec3 camPos;
uniform int fogOn;
uniform vec3 fogColor;
uniform vec2 fogRange;
out vec4 finalColor;
uniform vec2 fogHeight; // the fog's base and thickness (thickness 0: everywhere)
// How fogged a point is: the stretch of the line of sight inside the fog layer, through the
// fog's start and end distances (as Sky Lab's fog in the sky).
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
void main() {
    vec4 c = colDiffuse;
    if (hasTex == 1) c *= texture(texture0, uv);
    if (cutoff >= 0.0 && c.a < cutoff) discard;
    vec3 n = normalize(nrm);
    if (!gl_FrontFacing) n = -n;
    vec3 l = normalize(vec3(0.35, 0.55, 0.85));
    float k = 0.5 + 0.5 * max(0.0, dot(n, l));
    if (n.z < -0.5) k = 0.42;
    vec3 rgb = c.rgb * k;
    if (fogOn == 1) {
        float f = fogAmount(wpos);
        rgb = mix(rgb, fogColor, f);
    }
    finalColor = vec4(rgb, 1.0);
})";

// Baked light (renders, v0.12.3): the light comes per vertex from a float texture (each
// prop's chunks one after another; gl_VertexID counts the chunk's vertices), or, "like a
// Source model", from its ambient cube and up to 2 lights with Half-Lambert. Linear:
// texture x light x exposure, back to sRGB, then fog.
const char* kVSBaked = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform sampler2D vlight;
uniform int base;       // < 0: like a Source model
out vec2 uv;
out vec3 nrm;
out vec3 wpos;
out vec3 vl;
void main() {
    uv = vertexTexCoord;
    nrm = normalize(mat3(matNormal) * vertexNormal);
    wpos = (matModel * vec4(vertexPosition, 1.0)).xyz;
    vl = vec3(0.0);
    if (base >= 0) {
        int id = base + gl_VertexID;
        vl = texelFetch(vlight, ivec2(id % 4096, id / 4096), 0).rgb;
    }
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})";
const char* kFSBaked = R"(#version 330
in vec2 uv;
in vec3 nrm;
in vec3 wpos;
in vec3 vl;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform int hasTex;
uniform float cutoff;
uniform int base;
uniform float exposure;
uniform vec3 cube[6];
uniform int lightCount;
uniform int lkind[2];        // 0 point, 1 spot, 2 sun
uniform vec3 lpos[2];
uniform vec3 ldir[2];
uniform vec3 lcol[2];
uniform vec3 latt[2];        // constant, linear, quadratic (Hammer units)
uniform vec3 lfade[2];       // start, end, cap
uniform vec3 lspot[2];       // stopdot, stopdot2, exponent
uniform vec3 camPos;
uniform int fogOn;
uniform vec3 fogColor;
uniform vec2 fogRange;
uniform vec2 fogHeight;
out vec4 finalColor;
vec3 toLinear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c)); }
vec3 toSrgb(vec3 l) { l = clamp(l, 0.0, 1.0); return mix(l * 12.92, 1.055 * pow(l, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), l)); }
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
vec3 modelLight(vec3 n) {
    vec3 n2 = n * n;
    vec3 c = n2.x * (n.x < 0.0 ? cube[1] : cube[0]) + n2.y * (n.y < 0.0 ? cube[3] : cube[2]) + n2.z * (n.z < 0.0 ? cube[5] : cube[4]);
    for (int i = 0; i < 2; ++i) {
        if (i >= lightCount) break;
        vec3 L;
        float f = 1.0;
        if (lkind[i] == 2) L = -ldir[i];
        else {
            vec3 d = lpos[i] - wpos;
            float dcm = length(d);
            L = d / max(dcm, 1e-4);
            float dist = dcm / 2.54;
            bool fade = lfade[i].y > lfade[i].x;
            if (fade && dist > lfade[i].y) continue;
            dist = max(dist, 1.0);
            float e = min(dist, lfade[i].z);
            float den = latt[i].x + latt[i].y * e + latt[i].z * e * e;
            if (den <= 0.0) continue;
            f = 1.0 / den;
            if (lkind[i] == 1) {
                float d2 = dot(-L, ldir[i]);
                if (d2 <= lspot[i].y) continue;
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
        }
        float hl = 0.5 * dot(n, L) + 0.5;   // Half-Lambert
        c += lcol[i] * f * hl * hl;
    }
    return c;
}
void main() {
    vec4 t = colDiffuse;
    if (hasTex == 1) t *= texture(texture0, uv);
    if (cutoff >= 0.0 && t.a < cutoff) discard;
    vec3 n = normalize(nrm);
    if (!gl_FrontFacing) n = -n;
    vec3 light = base >= 0 ? vl : modelLight(n);
    vec3 rgb = toSrgb(toLinear(t.rgb) * light * exposure);
    if (fogOn == 1) rgb = mix(rgb, fogColor, fogAmount(wpos));
    finalColor = vec4(rgb, 1.0);
})";

unsigned char byteOf(double v) { return (unsigned char)std::clamp((int)std::lround(v), 0, 255); }

// raylib's matrices as m0..m15, and raylib's MatrixMultiply (its formula, written out
// here: raymath.h itself doesn't build warning-free with our flags).
void toArray(const Matrix& m, float a[16]) {
    const float v[16] = {m.m0, m.m1, m.m2, m.m3, m.m4, m.m5, m.m6, m.m7, m.m8, m.m9, m.m10, m.m11, m.m12, m.m13, m.m14, m.m15};
    for (int i = 0; i < 16; ++i) a[i] = v[i];
}
void multiply(const Matrix& left, const Matrix& right, float out[16]) {
    float a[16], b[16];
    toArray(left, a);
    toArray(right, b);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[4 * r + k] * b[4 * k + c];
            out[4 * r + c] = s;
        }
}

Matrix propMatrix(const Prop& p) {
    const Mat3 R = rotationMatrix(p.rot);
    const Vec3 c0 = R.col[0] * p.scale.x, c1 = R.col[1] * p.scale.y, c2 = R.col[2] * p.scale.z;
    Matrix m{};
    m.m0 = (float)c0.x; m.m1 = (float)c0.y; m.m2 = (float)c0.z; m.m3 = 0;
    m.m4 = (float)c1.x; m.m5 = (float)c1.y; m.m6 = (float)c1.z; m.m7 = 0;
    m.m8 = (float)c2.x; m.m9 = (float)c2.y; m.m10 = (float)c2.z; m.m11 = 0;
    m.m12 = (float)p.pos.x; m.m13 = (float)p.pos.y; m.m14 = (float)p.pos.z; m.m15 = 1;
    return m;
}

bool decodeTexture(const std::vector<unsigned char>& bytes, const std::string& file, Texture2D& out) {
    Image img{};
    if (!bytes.empty()) {
        const unsigned char* d = bytes.data();
        const char* ext = (bytes.size() > 3 && d[0] == 0xFF && d[1] == 0xD8) ? ".jpg" : ".png";
        img = LoadImageFromMemory(ext, d, (int)bytes.size());
    } else if (!file.empty()) {
        img = LoadImage(file.c_str());
    }
    if (!img.data) return false;
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    out = LoadTextureFromImage(img);
    UnloadImage(img);
    if (!out.id) return false;
    GenTextureMipmaps(&out);
    return true;
}

} // namespace

ModelLibrary::~ModelLibrary() = default; // GL resources are freed by unloadAll() while the window is still open

bool ModelLibrary::loadShaderSet(ShaderSet& s, const char* vs) {
    s.shader = LoadShaderFromMemory(vs, kFS);
    s.ok = s.shader.id != 0 && s.shader.id != rlGetShaderIdDefault();
    if (!s.ok) return false;
    s.locHasTex = GetShaderLocation(s.shader, "hasTex");
    s.locCutoff = GetShaderLocation(s.shader, "cutoff");
    s.locCam = GetShaderLocation(s.shader, "camPos");
    s.locFogOn = GetShaderLocation(s.shader, "fogOn");
    s.locFogColor = GetShaderLocation(s.shader, "fogColor");
    s.locFogRange = GetShaderLocation(s.shader, "fogRange");
    s.locFogHeight = GetShaderLocation(s.shader, "fogHeight");
    s.material = LoadMaterialDefault(); // its maps are reused for every draw
    s.material.shader = s.shader;
    return true;
}

void ModelLibrary::initShader() {
    if (shaderTried_) return;
    shaderTried_ = true;
    loadShaderSet(plain_, kVS);
    // Instanced only if the GPU takes it and raylib found its instance attribute.
    if (loadShaderSet(inst_, kVSInstanced) && inst_.shader.locs[SHADER_LOC_VERTEX_INSTANCETRANSFORM] < 0) inst_.ok = false;
    if (std::getenv("SCAL_PROPS_PLAIN")) inst_.ok = false; // testing: compare with the plain path
    shaderOk_ = plain_.ok || inst_.ok;
}

bool ModelLibrary::shaderOk() {
    initShader();
    return shaderOk_;
}

void ModelLibrary::setBottegaRoot(const std::string& root) {
    unloadAll();
    store_.setBottegaRoot(root);
}

void ModelLibrary::rescan() {
    unloadAll();
    store_.rescan();
}

void ModelLibrary::setSmooth(bool smooth) {
    smooth_ = smooth;
    for (auto& [n, g] : gpu_)
    {
        for (Texture2D& t : g.textures)
            if (t.id) SetTextureFilter(t, smooth_ ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_POINT);
        for (Texture2D& t : g.normals)
            if (t.id) SetTextureFilter(t, smooth_ ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_POINT);
        for (auto& [f, t] : g.files)
            if (t.id) SetTextureFilter(t, smooth_ ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_POINT);
    }
}

void ModelLibrary::freeGpu(Gpu& g) {
    for (Chunk& c : g.chunks) UnloadMesh(c.mesh);
    for (Texture2D& t : g.textures)
        if (t.id) UnloadTexture(t);
    for (Texture2D& t : g.normals)
        if (t.id) UnloadTexture(t);
    for (auto& [f, t] : g.files)
        if (t.id) UnloadTexture(t);
    g = Gpu{};
}

void ModelLibrary::unloadAll() {
    for (auto& [n, g] : gpu_) freeGpu(g);
    gpu_.clear();
    if (bakeTex_) rlUnloadTexture(bakeTex_);
    bakeTex_ = 0;
    bakeSrc_.reset();
    bakeBase_.clear();
    bakeIndex_.clear();
    bakeBumped_.clear();
}

ModelLibrary::Gpu* ModelLibrary::gpuFor(const std::string& name) {
    auto it = gpu_.find(name);
    if (it != gpu_.end()) return &it->second;
    const Model* m = store_.find(name);
    if (!m) return nullptr;
    Gpu g;
    for (const ModelMaterial& mat : m->materials) {
        Texture2D t{};
        const bool wants = mat.hasTexture();
        if (wants && decodeTexture(mat.image, mat.imageFile, t)) {
            SetTextureFilter(t, smooth_ ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_POINT);
            SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
        } else {
            t = Texture2D{};
        }
        g.textures.push_back(t);
        // A texture that is wanted but can't be read shows as grey.
        const double k = (wants && !t.id) ? 0.6 : 1.0;
        g.tint.push_back({byteOf(mat.color.x * k), byteOf(mat.color.y * k), byteOf(mat.color.z * k), 255});
        g.cutoff.push_back(mat.cutout ? (float)mat.cutoff : -1.0f);
        g.textured.push_back(t.id != 0);
        Texture2D nt{};
        if (mat.hasNormalMap() && decodeTexture(mat.normalImage, mat.normalImageFile, nt)) {
            SetTextureFilter(nt, smooth_ ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_POINT);
            SetTextureWrap(nt, TEXTURE_WRAP_REPEAT);
        } else
            nt = Texture2D{};
        g.normals.push_back(nt);
        g.settings.push_back(modelMaterialSettings(*m, g.settings.size()));
    }
    // Un-indexed chunks (a raylib mesh indexes with 16 bits): a copy of the triangles, once.
    for (const ModelMesh& mm : m->meshes) {
        for (size_t at = 0; at < mm.index.size(); at += kChunkVertices) {
            const size_t n = std::min(kChunkVertices, mm.index.size() - at);
            Mesh mesh{};
            mesh.vertexCount = (int)n;
            mesh.triangleCount = (int)(n / 3);
            mesh.vertices = (float*)MemAlloc((unsigned)(n * 3 * sizeof(float)));
            mesh.normals = (float*)MemAlloc((unsigned)(n * 3 * sizeof(float)));
            mesh.texcoords = (float*)MemAlloc((unsigned)(n * 2 * sizeof(float)));
            for (size_t i = 0; i < n; ++i) {
                const size_t v = mm.index[at + i];
                std::memcpy(&mesh.vertices[3 * i], &mm.pos[3 * v], 3 * sizeof(float));
                std::memcpy(&mesh.normals[3 * i], &mm.normal[3 * v], 3 * sizeof(float));
                std::memcpy(&mesh.texcoords[2 * i], &mm.uv[2 * v], 2 * sizeof(float));
            }
            UploadMesh(&mesh, false);
            g.chunks.push_back({mesh, std::clamp(mm.material, 0, std::max(0, (int)m->materials.size() - 1))});
        }
    }
    return &(gpu_[name] = std::move(g));
}

void ModelLibrary::prepareBake(const std::vector<Prop>& props, const std::shared_ptr<const BakeResult>& bake) {
    if (bake == bakeSrc_) return;
    if (bakeTex_) rlUnloadTexture(bakeTex_);
    bakeTex_ = 0;
    bakeBase_.clear();
    bakeIndex_.clear();
    bakeBumped_.clear();
    bakeSrc_ = bake;
    if (!bake) return;
    std::map<std::uint32_t, const Prop*> byId;
    for (const Prop& p : props) byId[p.id] = &p;
    std::vector<float> data;
    for (size_t k = 0; k < bake->props.size(); ++k) {
        const PropLighting& pl = bake->props[k];
        bakeIndex_[pl.id] = k;
        if (pl.modelStyle) continue;
        auto it = byId.find(pl.id);
        if (it == byId.end()) continue;
        const Model* m = store_.find(it->second->model);
        if (!m || pl.vertexRgb.size() != m->meshes.size()) continue;
        std::vector<int>& bases = bakeBase_[pl.id];
        // Bumped props (v0.12.4b): 4 values a vertex, flat then the 3 bump directions.
        const size_t K = pl.bumped ? 4 : 1;
        if (pl.bumped) bakeBumped_.insert(pl.id);
        // The same chunks as gpuFor(): each mesh's triangles, kChunkVertices at a time.
        for (size_t mi = 0; mi < m->meshes.size(); ++mi) {
            const ModelMesh& mm = m->meshes[mi];
            const std::vector<float>& vr = pl.vertexRgb[mi];
            for (size_t at = 0; at < mm.index.size(); at += kChunkVertices) {
                const size_t n = std::min(kChunkVertices, mm.index.size() - at);
                bases.push_back((int)(data.size() / 3));
                for (size_t i = 0; i < n; ++i) {
                    const size_t v = mm.index[at + i];
                    for (size_t q = 0; q < K; ++q) {
                        const size_t o = 3 * (K * v + q);
                        if (o + 2 < vr.size()) data.insert(data.end(), {vr[o], vr[o + 1], vr[o + 2]});
                        else data.insert(data.end(), {1.0f, 1.0f, 1.0f});
                    }
                }
            }
        }
    }
    const size_t texels = data.size() / 3;
    const int w = 4096, h = (int)std::max<size_t>(1, (texels + w - 1) / w);
    data.resize((size_t)w * h * 3, 1.0f);
    bakeTex_ = rlLoadTexture(data.data(), w, h, PIXELFORMAT_UNCOMPRESSED_R32G32B32, 1);
    if (bakeTex_) {
        rlTextureParameters(bakeTex_, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
        rlTextureParameters(bakeTex_, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
    }
}

// A prop material's settings for the VertexLitGeneric file (v0.12.4b): its settings file's
// keys, and the glTF's normal map as $bumpmap (green up: %flipgreen) unless that file
// names another. Texture values: a file beside the settings file, else the texture library.
void ModelLibrary::bindPropMaterial(ShaderLibrary::Program& prog, Gpu& g, size_t mi, const BakedDrawContext& ctx) {
    static const std::string kGltfNormal = "\x01gltf-normal";
    Material mat;
    if (mi < g.settings.size() && g.settings[mi].found) mat = g.settings[mi].mat;
    const bool ownNormal = mi < g.normals.size() && g.normals[mi].id;
    if (!mat.has("$bumpmap") && ownNormal) {
        mat.params["$bumpmap"] = kGltfNormal;
        if (!mat.has("%flipgreen")) mat.params["%flipgreen"] = "1";
    }
    const std::string dir = mi < g.settings.size() ? g.settings[mi].dir : "";
    auto finder = [&](const std::string& v) -> unsigned {
        if (v == kGltfNormal) return ownNormal ? g.normals[mi].id : 0u;
        const std::string f = modelTextureFile(dir, v);
        if (!f.empty()) {
            auto it = g.files.find(f);
            if (it == g.files.end()) {
                Texture2D t{};
                if (decodeTexture({}, f, t)) {
                    SetTextureFilter(t, smooth_ ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_POINT);
                    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
                } else
                    t = Texture2D{};
                it = g.files.emplace(f, t).first;
            }
            return it->second.id;
        }
        if (ctx.textures) return ShaderLibrary::libraryFinder(*ctx.textures, nullptr)(v);
        return 0u;
    };
    const std::vector<std::string> missing = ctx.shaders->bind(prog, &mat, finder, 7);
    if (ctx.problems)
        for (const std::string& v : missing) {
            const std::string line = "prop texture not found: " + v;
            if (std::find(ctx.problems->begin(), ctx.problems->end(), line) == ctx.problems->end()) ctx.problems->push_back(line);
        }
}

void ModelLibrary::drawBaked(const std::vector<Prop>& props, const PropDrawContext& ctx) {
    BakedSet& sh = baked_;
    if (!sh.tried) {
        sh.tried = true;
        sh.shader = LoadShaderFromMemory(kVSBaked, kFSBaked);
        sh.ok = sh.shader.id != 0 && sh.shader.id != rlGetShaderIdDefault();
        if (sh.ok) {
            sh.material = LoadMaterialDefault();
            sh.material.shader = sh.shader;
        }
    }
    // The material shader file (VertexLitGeneric) when the render has them, else the built-in one.
    ShaderLibrary::Program* prog = nullptr;
    if (ctx.material && ctx.material->shaders) {
        prog = ctx.material->shaders->program("VertexLitGeneric", ShaderGeometry::Model);
        if (prog && !prog->ok) {
            if (ctx.material->problems) ctx.material->problems->push_back(prog->error + ": props drawn with the built-in shader");
            prog = nullptr;
        }
    }
    if (!sh.ok && !prog) return;
    prepareBake(props, ctx.bake);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    Shader S = prog ? prog->shader : sh.shader;
    ::Material& drawMat = prog ? sh.fileMaterial : sh.material;
    if (prog) {
        if (!sh.fileMaterialMade) {
            sh.fileMaterial = LoadMaterialDefault();
            sh.fileMaterialMade = true;
        }
        sh.fileMaterial.shader = prog->shader;
    }
    auto loc = [&](const char* n) { return prog ? prog->at(n) : sh.at(n); };
    auto setI = [&](const char* n, int v) { SetShaderValue(S, loc(n), &v, SHADER_UNIFORM_INT); };
    auto setF = [&](const char* n, float v) { SetShaderValue(S, loc(n), &v, SHADER_UNIFORM_FLOAT); };
    auto set3 = [&](const char* n, const Vec3& v) {
        const float f[3] = {(float)v.x, (float)v.y, (float)v.z};
        SetShaderValue(S, loc(n), f, SHADER_UNIFORM_VEC3);
    };
    set3("camPos", ctx.camPos);
    setI("fogOn", ctx.fog ? 1 : 0);
    set3("fogColor", ctx.fogColor / 255.0);
    {
        const float fr[2] = {(float)ctx.fogStart, (float)ctx.fogEnd}, fh[2] = {(float)ctx.fogBase, (float)ctx.fogThickness};
        SetShaderValue(S, loc("fogRange"), fr, SHADER_UNIFORM_VEC2);
        SetShaderValue(S, loc("fogHeight"), fh, SHADER_UNIFORM_VEC2);
    }
    setF("exposure", ctx.exposure);
    const int unit = 5; // the props' vertex light (6: the sky, 7 on: material textures)
    if (prog) {
        BakedDrawContext common = *ctx.material;
        common.camPos = ctx.camPos;
        common.exposure = ctx.exposure;
        setMaterialCommon(S, [&](const char* n) { return prog->at(n); }, common);
        rlActiveTextureSlot(6);
        rlEnableTexture(ctx.material->skyTex ? ctx.material->skyTex : rlGetTextureIdDefault());
    }
    setI("vlight", unit);
    rlActiveTextureSlot(unit);
    rlEnableTexture(bakeTex_ ? bakeTex_ : rlGetTextureIdDefault());
    rlActiveTextureSlot(0);
    for (const Prop& p : props) {
        Gpu* g = gpuFor(p.model);
        if (!g) continue;
        auto ii = bakeIndex_.find(p.id);
        const PropLighting* pl = ii != bakeIndex_.end() && ii->second < bakeSrc_->props.size() ? &bakeSrc_->props[ii->second] : nullptr;
        auto bi = bakeBase_.find(p.id);
        const bool perVertex = pl && !pl->modelStyle && bi != bakeBase_.end() && bi->second.size() == g->chunks.size();
        setI("bumpedVL", perVertex && bakeBumped_.count(p.id) ? 1 : 0);
        {
            // like a Source model (or no bake for it: full light from every side); per-vertex
            // props get them too, for Phong and rim light (v0.12.4b)
            float cube[18];
            for (int a = 0; a < 6; ++a) {
                const Vec3 c = pl ? pl->cube[a] : Vec3{1, 1, 1};
                cube[3 * a] = (float)c.x; cube[3 * a + 1] = (float)c.y; cube[3 * a + 2] = (float)c.z;
            }
            SetShaderValueV(S, loc("cube"), cube, SHADER_UNIFORM_VEC3, 6);
            const int n = pl ? (int)std::min<size_t>(2, pl->lights.size()) : 0;
            setI("lightCount", n);
            int kind[2] = {0, 0};
            float pos[6] = {}, dir[6] = {}, col[6] = {}, att[6] = {}, fade[6] = {}, spot[6] = {};
            for (int i = 0; i < n; ++i) {
                const ModelLight& L = pl->lights[i];
                kind[i] = L.kind == LightKind::Sun ? 2 : L.kind == LightKind::Spot ? 1 : 0;
                const Vec3 v[6] = {L.pos, L.dir, L.intensity, {L.c, L.l, L.q}, {L.startFade, L.endFade, std::min(L.capDist, 1e30)}, {L.stopdot, L.stopdot2, L.exponent}};
                float* dst[6] = {pos, dir, col, att, fade, spot};
                for (int a = 0; a < 6; ++a) {
                    dst[a][3 * i] = (float)v[a].x; dst[a][3 * i + 1] = (float)v[a].y; dst[a][3 * i + 2] = (float)v[a].z;
                }
            }
            SetShaderValueV(S, loc("lkind"), kind, SHADER_UNIFORM_INT, 2);
            SetShaderValueV(S, loc("lpos"), pos, SHADER_UNIFORM_VEC3, 2);
            SetShaderValueV(S, loc("ldir"), dir, SHADER_UNIFORM_VEC3, 2);
            SetShaderValueV(S, loc("lcol"), col, SHADER_UNIFORM_VEC3, 2);
            SetShaderValueV(S, loc("latt"), att, SHADER_UNIFORM_VEC3, 2);
            SetShaderValueV(S, loc("lfade"), fade, SHADER_UNIFORM_VEC3, 2);
            SetShaderValueV(S, loc("lspot"), spot, SHADER_UNIFORM_VEC3, 2);
        }
        const Matrix mat = propMatrix(p);
        for (size_t c = 0; c < g->chunks.size(); ++c) {
            const Chunk& ch = g->chunks[c];
            const size_t mi = (size_t)ch.material;
            if (mi >= g->tint.size()) continue;
            setI("base", perVertex ? bi->second[c] : -1);
            drawMat.maps[MATERIAL_MAP_DIFFUSE].texture = g->textured[mi] ? g->textures[mi] : Texture2D{};
            drawMat.maps[MATERIAL_MAP_DIFFUSE].color = g->tint[mi];
            setI("hasTex", g->textured[mi] ? 1 : 0);
            setF("cutoff", g->cutoff[mi]);
            if (prog) bindPropMaterial(*prog, *g, mi, *ctx.material);
            DrawMesh(ch.mesh, drawMat, mat);
            if (prog) ctx.material->shaders->unbind(7);
            ++drawCalls_;
        }
    }
    rlActiveTextureSlot(unit);
    rlDisableTexture();
    if (prog) {
        rlActiveTextureSlot(6);
        rlDisableTexture();
    }
    rlActiveTextureSlot(0);
    rlEnableBackfaceCulling();
}

void ModelLibrary::draw(const std::vector<Prop>& props, const PropDrawContext& ctx) {
    drawCalls_ = 0;
    culled_ = 0;
    if (props.empty()) return;
    if (ctx.bake) {
        initShader();
        drawBaked(props, ctx);
        return;
    }
    initShader();
    if (!shaderOk_) return;
    ShaderSet& sh = inst_.ok ? inst_ : plain_;
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling(); // two-sided: foliage planes and mirrored copies show from both sides
    auto setI = [&](int loc, int v) { SetShaderValue(sh.shader, loc, &v, SHADER_UNIFORM_INT); };
    auto set3 = [&](int loc, double x, double y, double z) {
        const float f[3] = {(float)x, (float)y, (float)z};
        SetShaderValue(sh.shader, loc, f, SHADER_UNIFORM_VEC3);
    };
    set3(sh.locCam, ctx.camPos.x, ctx.camPos.y, ctx.camPos.z);
    setI(sh.locFogOn, ctx.fog ? 1 : 0);
    set3(sh.locFogColor, ctx.fogColor.x / 255, ctx.fogColor.y / 255, ctx.fogColor.z / 255);
    {
        const float fr[2] = {(float)ctx.fogStart, (float)ctx.fogEnd};
        SetShaderValue(sh.shader, sh.locFogRange, fr, SHADER_UNIFORM_VEC2);
        const float fh[2] = {(float)ctx.fogBase, (float)ctx.fogThickness};
        SetShaderValue(sh.shader, sh.locFogHeight, fh, SHADER_UNIFORM_VEC2);
    }
    // The view frustum from the camera's matrices (Gribb / Hartmann): props wholly
    // outside it are skipped (v0.10.3).
    float m[16]; // m[i] = raylib's m<i>
    multiply(rlGetMatrixModelview(), rlGetMatrixProjection(), m);
    const float rows[4][4] = {{m[0], m[4], m[8], m[12]}, {m[1], m[5], m[9], m[13]},
                              {m[2], m[6], m[10], m[14]}, {m[3], m[7], m[11], m[15]}};
    float planes[6][4];
    for (int k = 0; k < 6; ++k) {
        const float sgn = (k % 2 == 0) ? 1.0f : -1.0f;
        const float* r = rows[k / 2];
        float len = 0;
        for (int c = 0; c < 4; ++c) planes[k][c] = rows[3][c] + sgn * r[c];
        for (int c = 0; c < 3; ++c) len += planes[k][c] * planes[k][c];
        len = std::sqrt(len);
        if (len > 0) for (int c = 0; c < 4; ++c) planes[k][c] /= len;
    }
    static const bool noCull = std::getenv("SCAL_NO_CULL") != nullptr; // testing: compare with culling off
    auto visible = [&](const AABB& b) {
        if (noCull) return true;
        const Vec3 c = b.center();
        const double r = length(b.size()) * 0.5;
        for (const auto& pl : planes)
            if (pl[0] * c.x + pl[1] * c.y + pl[2] * c.z + pl[3] < -r) return false;
        return true;
    };
    // The visible props, by model (in the order the models first appear).
    std::vector<std::pair<Gpu*, std::vector<Matrix>>> groups;
    std::map<Gpu*, size_t> groupOf;
    // Props of one model usually come one after another (copies, scatters): look the
    // model up again only when the name changes.
    const std::string* lastName = nullptr;
    Gpu* g = nullptr;
    const Model* model = nullptr;
    for (const Prop& p : props) {
        if (!lastName || p.model != *lastName) {
            lastName = &p.model;
            g = gpuFor(p.model);
            model = g ? store_.find(p.model) : nullptr;
        }
        if (!g) continue;
        if (!visible(propBounds(p, model))) { ++culled_; continue; }
        auto it = groupOf.find(g);
        if (it == groupOf.end()) {
            it = groupOf.emplace(g, groups.size()).first;
            groups.push_back({g, {}});
        }
        groups[it->second].second.push_back(propMatrix(p));
    }
    for (auto& [g, mats] : groups)
        for (const Chunk& c : g->chunks) {
            const size_t mi = (size_t)c.material;
            if (mi >= g->tint.size()) continue;
            sh.material.maps[MATERIAL_MAP_DIFFUSE].texture = g->textured[mi] ? g->textures[mi] : Texture2D{};
            sh.material.maps[MATERIAL_MAP_DIFFUSE].color = g->tint[mi];
            setI(sh.locHasTex, g->textured[mi] ? 1 : 0);
            SetShaderValue(sh.shader, sh.locCutoff, &g->cutoff[mi], SHADER_UNIFORM_FLOAT);
            if (&sh == &inst_) {
                DrawMeshInstanced(c.mesh, sh.material, mats.data(), (int)mats.size());
                ++drawCalls_;
            } else {
                for (const Matrix& mm : mats) { DrawMesh(c.mesh, sh.material, mm); ++drawCalls_; }
            }
        }
    rlEnableBackfaceCulling();
}

} // namespace scal
