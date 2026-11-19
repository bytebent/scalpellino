#include "render/model_library.hpp"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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
        float f = clamp((length(wpos - camPos) - fogRange.x) / max(fogRange.y - fogRange.x, 1.0), 0.0, 1.0);
        rgb = mix(rgb, fogColor, f);
    }
    finalColor = vec4(rgb, 1.0);
})";

unsigned char byteOf(double v) { return (unsigned char)std::clamp((int)std::lround(v), 0, 255); }

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

bool decodeTexture(const ModelMaterial& mat, Texture2D& out) {
    Image img{};
    if (!mat.image.empty()) {
        const unsigned char* d = mat.image.data();
        const char* ext = (mat.image.size() > 3 && d[0] == 0xFF && d[1] == 0xD8) ? ".jpg" : ".png";
        img = LoadImageFromMemory(ext, d, (int)mat.image.size());
    } else if (!mat.imageFile.empty()) {
        img = LoadImage(mat.imageFile.c_str());
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

void ModelLibrary::initShader() {
    if (shaderTried_) return;
    shaderTried_ = true;
    shader_ = LoadShaderFromMemory(kVS, kFS);
    shaderOk_ = shader_.id != 0 && shader_.id != rlGetShaderIdDefault();
    if (!shaderOk_) return;
    locHasTex_ = GetShaderLocation(shader_, "hasTex");
    locCutoff_ = GetShaderLocation(shader_, "cutoff");
    locCam_ = GetShaderLocation(shader_, "camPos");
    locFogOn_ = GetShaderLocation(shader_, "fogOn");
    locFogColor_ = GetShaderLocation(shader_, "fogColor");
    locFogRange_ = GetShaderLocation(shader_, "fogRange");
    material_ = LoadMaterialDefault(); // its maps are reused for every draw
    material_.shader = shader_;
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
        for (Texture2D& t : g.textures)
            if (t.id) SetTextureFilter(t, smooth_ ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_POINT);
}

void ModelLibrary::freeGpu(Gpu& g) {
    for (Chunk& c : g.chunks) UnloadMesh(c.mesh);
    for (Texture2D& t : g.textures)
        if (t.id) UnloadTexture(t);
    g = Gpu{};
}

void ModelLibrary::unloadAll() {
    for (auto& [n, g] : gpu_) freeGpu(g);
    gpu_.clear();
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
        if (wants && decodeTexture(mat, t)) {
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

void ModelLibrary::draw(const std::vector<Prop>& props, const PropDrawContext& ctx) {
    if (props.empty()) return;
    initShader();
    if (!shaderOk_) return;
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling(); // two-sided: foliage planes and mirrored copies show from both sides
    auto setI = [&](int loc, int v) { SetShaderValue(shader_, loc, &v, SHADER_UNIFORM_INT); };
    auto set3 = [&](int loc, double x, double y, double z) {
        const float f[3] = {(float)x, (float)y, (float)z};
        SetShaderValue(shader_, loc, f, SHADER_UNIFORM_VEC3);
    };
    set3(locCam_, ctx.camPos.x, ctx.camPos.y, ctx.camPos.z);
    setI(locFogOn_, ctx.fog ? 1 : 0);
    set3(locFogColor_, ctx.fogColor.x / 255, ctx.fogColor.y / 255, ctx.fogColor.z / 255);
    {
        const float fr[2] = {(float)ctx.fogStart, (float)ctx.fogEnd};
        SetShaderValue(shader_, locFogRange_, fr, SHADER_UNIFORM_VEC2);
    }
    for (const Prop& p : props) {
        Gpu* g = gpuFor(p.model);
        if (!g) continue;
        const Matrix m = propMatrix(p);
        for (const Chunk& c : g->chunks) {
            const size_t mi = (size_t)c.material;
            if (mi >= g->tint.size()) continue;
            material_.maps[MATERIAL_MAP_DIFFUSE].texture = g->textured[mi] ? g->textures[mi] : Texture2D{};
            material_.maps[MATERIAL_MAP_DIFFUSE].color = g->tint[mi];
            setI(locHasTex_, g->textured[mi] ? 1 : 0);
            SetShaderValue(shader_, locCutoff_, &g->cutoff[mi], SHADER_UNIFORM_FLOAT);
            DrawMesh(c.mesh, material_, m);
        }
    }
    rlEnableBackfaceCulling();
}

} // namespace scal
