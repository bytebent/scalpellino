#include "render/model_library.hpp"
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
    drawCalls_ = 0;
    culled_ = 0;
    if (props.empty()) return;
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
