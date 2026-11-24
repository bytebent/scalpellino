#include "render/baked_light.hpp"
#include "core/texture.hpp"
#include "render/shader_library.hpp"
#include "render/probes.hpp"
#include "render/mirrors.hpp"
#include "core/mirror.hpp"
#include "render/texture_library.hpp"
#include "raylib.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace scal {

namespace {
constexpr int kPageSize = 2048;
constexpr int kLightmapUnit = 5; // texture unit the lightmap page is bound to (the batch uses 0..4)
constexpr int kSkyUnit = 6;      // the sky for reflections (material shaders)
constexpr int kFirstMaterialUnit = 7; // a material's own textures (detail, masks...)

unsigned uploadFloatRGB(const std::vector<float>& rgb, int w, int h) {
    const unsigned id = rlLoadTexture(rgb.data(), w, h, PIXELFORMAT_UNCOMPRESSED_R32G32B32, 1);
    if (!id) return 0;
    rlTextureParameters(id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
    rlTextureParameters(id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
    rlTextureParameters(id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
    rlTextureParameters(id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
    return id;
}

const char* kVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;   // xy: where the luxel is in the lightmap page
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec2 fragLm;
out vec4 fragColor;
out vec3 fragPos;
void main() {
    fragTexCoord = vertexTexCoord;
    fragLm = vertexNormal.xy;
    fragColor = vertexColor;
    fragPos = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})";
const char* kFS = R"(#version 330
in vec2 fragTexCoord;
in vec2 fragLm;
in vec4 fragColor;
in vec3 fragPos;
uniform sampler2D texture0;
uniform sampler2D lightmap;
uniform vec4 colDiffuse;
uniform float exposure;
uniform int fogOn;
uniform vec3 camPos;
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
void main() {
    vec4 t = texture(texture0, fragTexCoord) * colDiffuse;
    vec3 light = texture(lightmap, fragLm).rgb;
    vec3 c = toSrgb(toLinear(t.rgb) * light * exposure);
    if (fogOn == 1) c = mix(c, fogColor, fogAmount(fragPos));
    finalColor = vec4(c, t.a * fragColor.a);
})";

struct BakedShader {
    Shader sh{};
    bool tried = false, ok = false;
    int lightmap = -1, exposure = -1, fogOn = -1, camPos = -1, fogColor = -1, fogRange = -1, fogHeight = -1;
};
BakedShader& bakedShader() {
    static BakedShader s;
    if (!s.tried) {
        s.tried = true;
        s.sh = LoadShaderFromMemory(kVS, kFS);
        s.ok = s.sh.id != 0 && s.sh.id != rlGetShaderIdDefault();
        if (s.ok) {
            s.lightmap = GetShaderLocation(s.sh, "lightmap");
            s.exposure = GetShaderLocation(s.sh, "exposure");
            s.fogOn = GetShaderLocation(s.sh, "fogOn");
            s.camPos = GetShaderLocation(s.sh, "camPos");
            s.fogColor = GetShaderLocation(s.sh, "fogColor");
            s.fogRange = GetShaderLocation(s.sh, "fogRange");
            s.fogHeight = GetShaderLocation(s.sh, "fogHeight");
        }
    }
    return s;
}
} // namespace

void BakedLight::release() {
    for (Page& p : pages_)
        if (p.tex) rlUnloadTexture(p.tex);
    for (auto& [k, p] : prims_)
        if (p.tex) rlUnloadTexture(p.tex);
    pages_.clear();
    faces_.clear();
    prims_.clear();
    result_.reset();
}

void BakedLight::upload(std::shared_ptr<const BakeResult> r) {
    if (r == result_) return;
    release();
    if (!r) return;
    result_ = std::move(r);
    const BakeResult& res = *result_;

    // ---- face lightmaps: shelf packing, tallest first, a one-luxel border around each
    std::vector<size_t> order(res.faces.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return res.faces[a].h > res.faces[b].h; });
    struct Placed { size_t face; int page, x, y; int step; bool down; };
    std::vector<Placed> placed;
    std::vector<int> pageUsedH;
    int page = 0, x = 4, y = 0, shelfH = 4; // (0, 0)..(3, 3): full light, for anything without a lightmap
    pageUsedH.push_back(4);
    for (size_t i : order) {
        const FaceLightmap& f = res.faces[i];
        const bool bumped = !f.bump.empty();
        // bumped: the flat map and the 3 bump maps side by side (Source's layout), or one
        // under the other when 4 don't fit across a page
        const bool down = bumped && 4 * (f.w + 2) > kPageSize;
        const int w = (f.w + 2) * (bumped && !down ? 4 : 1), h = (f.h + 2) * (down ? 4 : 1);
        if (x + w > kPageSize) {
            y += shelfH;
            x = 0;
            shelfH = 0;
        }
        if (y + h > kPageSize) {
            ++page;
            pageUsedH.push_back(0);
            x = y = shelfH = 0;
        }
        placed.push_back({i, page, x, y, bumped ? (down ? f.h + 2 : f.w + 2) : 0, down});
        x += w;
        shelfH = std::max(shelfH, h);
        pageUsedH[page] = std::max(pageUsedH[page], y + h);
    }
    std::vector<std::vector<float>> data(pageUsedH.size());
    for (size_t p = 0; p < pageUsedH.size(); ++p) {
        Page pg;
        pg.w = kPageSize;
        pg.h = std::max(4, pageUsedH[p]);
        pages_.push_back(pg);
        data[p].assign((size_t)pg.w * pg.h * 3, 0.0f);
    }
    for (int yy = 0; yy < 4; ++yy)
        for (int xx = 0; xx < 4; ++xx)
            for (int c = 0; c < 3; ++c) data[0][((size_t)yy * kPageSize + xx) * 3 + c] = 1.0f;
    for (const Placed& pl : placed) {
        const FaceLightmap& f = res.faces[pl.face];
        std::vector<float>& d = data[pl.page];
        const int W = pages_[pl.page].w;
        const size_t nl = (size_t)f.w * f.h;
        for (int m = 0; m < (pl.step ? 4 : 1); ++m) {
            const float* map = m == 0 ? f.rgb.data() : &f.bump[(size_t)(m - 1) * nl * 3];
            const int ox = pl.x + (pl.down ? 0 : m * pl.step), oy = pl.y + (pl.down ? m * pl.step : 0);
            for (int j = -1; j <= f.h; ++j)
                for (int i = -1; i <= f.w; ++i) {
                    const int si = std::clamp(i, 0, f.w - 1), sj = std::clamp(j, 0, f.h - 1);
                    const float* src = &map[((size_t)sj * f.w + si) * 3];
                    float* dst = &d[((size_t)(oy + 1 + j) * W + (ox + 1 + i)) * 3];
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                }
        }
        FaceRef ref;
        ref.page = pl.page;
        ref.x = pl.x;
        ref.y = pl.y;
        ref.lm = &f;
        ref.bumpStep = pl.step;
        ref.bumpDown = pl.down;
        faces_[{f.brush, f.face, f.terrain}] = ref;
    }
    for (size_t p = 0; p < pages_.size(); ++p) pages_[p].tex = uploadFloatRGB(data[p], pages_[p].w, pages_[p].h);

    // ---- primitive lightmaps: the slices side by side, each with a one-cell border
    for (const PrimLightmap& lm : res.prims) {
        if (lm.nx <= 0 || lm.ny <= 0 || lm.nz <= 0) continue;
        PrimRef ref;
        ref.lm = &lm;
        ref.tilesX = std::max(1, (int)std::ceil(std::sqrt((double)lm.nz)));
        const int tilesY = (lm.nz + ref.tilesX - 1) / ref.tilesX;
        ref.texW = ref.tilesX * (lm.nx + 2);
        ref.texH = tilesY * (lm.ny + 2);
        std::vector<float> d((size_t)ref.texW * ref.texH * 3, 0.0f);
        for (int k = 0; k < lm.nz; ++k) {
            const int ox = (k % ref.tilesX) * (lm.nx + 2), oy = (k / ref.tilesX) * (lm.ny + 2);
            for (int j = -1; j <= lm.ny; ++j)
                for (int i = -1; i <= lm.nx; ++i) {
                    const int si = std::clamp(i, 0, lm.nx - 1), sj = std::clamp(j, 0, lm.ny - 1);
                    const float* src = &lm.rgb[(((size_t)k * lm.ny + sj) * lm.nx + si) * 3];
                    float* dst = &d[((size_t)(oy + 1 + j) * ref.texW + (ox + 1 + i)) * 3];
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                }
        }
        ref.tex = uploadFloatRGB(d, ref.texW, ref.texH);
        prims_[{lm.combo, lm.combo ? 0 : lm.prim}] = ref;
    }
}

const BakedLight::FaceRef* BakedLight::face(BrushId b, int face, bool terrain) const {
    auto it = faces_.find({b, face, terrain});
    return it == faces_.end() ? nullptr : &it->second;
}

const BakedLight::PrimRef* BakedLight::prim(std::uint32_t combo, MathPrimId id) const {
    auto it = prims_.find({combo, combo ? 0 : id});
    return it == prims_.end() ? nullptr : &it->second;
}

void BakedLight::uv(const FaceRef& f, const Page& p, double s, double t, float& u, float& v) {
    u = (float)((f.x + 1 + s + 0.5) / p.w);
    v = (float)((f.y + 1 + t + 0.5) / p.h);
}

void setMaterialCommon(Shader& sh, const std::function<int(const char*)>& loc, const BakedDrawContext& ctx) {
    auto f1 = [&](const char* n, float v) { SetShaderValue(sh, loc(n), &v, SHADER_UNIFORM_FLOAT); };
    auto i1 = [&](const char* n, int v) { SetShaderValue(sh, loc(n), &v, SHADER_UNIFORM_INT); };
    auto f3 = [&](const char* n, const Vec3& v) {
        const float a[3] = {(float)v.x, (float)v.y, (float)v.z};
        SetShaderValue(sh, loc(n), a, SHADER_UNIFORM_VEC3);
    };
    f1("exposure", ctx.exposure);
    i1("fogOn", ctx.fog ? 1 : 0);
    f3("camPos", ctx.camPos);
    f3("fogColor", ctx.fogColor / 255.0);
    const float fr[2] = {(float)ctx.fogStart, (float)ctx.fogEnd}, fh[2] = {(float)ctx.fogBase, (float)ctx.fogThickness};
    SetShaderValue(sh, loc("fogRange"), fr, SHADER_UNIFORM_VEC2);
    SetShaderValue(sh, loc("fogHeight"), fh, SHADER_UNIFORM_VEC2);
    i1("orthoOn", ctx.orthoOn ? 1 : 0);
    f3("viewDir", ctx.viewDir);
    i1("skyTex", kSkyUnit);
    i1("skyOn", ctx.skyTex ? 1 : 0);
    f1("skyCover", ctx.skyCover);
    f3("skyBelow", ctx.skyBelow / 255.0);
    f3("skyFlat", ctx.skyFlat);
    i1("lightmap", kLightmapUnit);
    // Reflection probes (v0.12.6). Their two textures stay bound on units 14 and 15 (nothing
    // else samples those units).
    i1("linearOut", ctx.linearOut ? 1 : 0);
    i1("probeTex", kProbeTexUnit);
    i1("probeData", kProbeDataUnit);
    const ProbeAtlas* pa = ctx.probes && ctx.probes->tex && ctx.probes->data ? ctx.probes : nullptr;
    i1("probeCount", pa ? (int)pa->slots.size() : 0);
    const float as[2] = {pa ? (float)pa->w : 1.0f, pa ? (float)pa->h : 1.0f};
    SetShaderValue(sh, loc("probeAtlasSize"), as, SHADER_UNIFORM_VEC2);
    rlActiveTextureSlot(kProbeTexUnit);
    rlEnableTexture(pa ? pa->tex : rlGetTextureIdDefault());
    rlActiveTextureSlot(kProbeDataUnit);
    rlEnableTexture(pa ? pa->data : rlGetTextureIdDefault());
    // Mirrors (v0.12.7): no picture until a mirror face says which (drawBakedBrushes).
    i1("reflectTex", kMirrorReflectUnit);
    i1("refractTex", kMirrorRefractUnit);
    i1("mirrorReflect", 0);
    i1("mirrorRefract", 0);
    rlActiveTextureSlot(kMirrorReflectUnit);
    rlEnableTexture(rlGetTextureIdDefault());
    rlActiveTextureSlot(kMirrorRefractUnit);
    rlEnableTexture(rlGetTextureIdDefault());
    i1("frameCopyOn", 0); // Refract (v0.12.7b): set per face
    i1("clipOn", ctx.clipOn ? 1 : 0);
    const float cp[4] = {(float)ctx.clipN.x, (float)ctx.clipN.y, (float)ctx.clipN.z, (float)ctx.clipD};
    SetShaderValue(sh, loc("clipPlane"), cp, SHADER_UNIFORM_VEC4);
    const float vs[2] = {(float)ctx.viewW, (float)ctx.viewH};
    SetShaderValue(sh, loc("viewSize"), vs, SHADER_UNIFORM_VEC2);
    // Endless planes (v0.13.0): off until a plane is drawn; cloud planes for their shadows
    i1("planeLightOn", 0);
    const int nc = (int)std::min<size_t>(ctx.clouds.size(), 4);
    i1("cloudCount", nc);
    if (nc > 0) {
        float z[4] = {0, 0, 0, 0}, pp[16] = {0};
        int q[8] = {0};
        for (int i = 0; i < nc; ++i) {
            const auto& c = ctx.clouds[i];
            z[i] = (float)c.z;
            pp[4 * i] = (float)c.p.size;
            pp[4 * i + 1] = (float)c.p.cover;
            pp[4 * i + 2] = (float)c.p.softness;
            pp[4 * i + 3] = (float)c.p.opacity;
            q[2 * i] = c.p.complexity;
            q[2 * i + 1] = c.p.seed;
        }
        SetShaderValueV(sh, loc("cloudZ"), z, SHADER_UNIFORM_FLOAT, nc);
        SetShaderValueV(sh, loc("cloudP"), pp, SHADER_UNIFORM_VEC4, nc);
        SetShaderValueV(sh, loc("cloudQ"), q, SHADER_UNIFORM_IVEC2, nc);
    }
    const BakeResult* br = ctx.bake;
    const Vec3 st = br && br->hasSun ? br->sunToward : Vec3{0, 0, 1};
    f3("sunToward", st);
    rlActiveTextureSlot(0);
}

std::vector<BakedDrawContext::CloudLayer> cloudLayers(const std::vector<Scene::ScenePlane>& planes, TextureLibrary& textures) {
    std::vector<BakedDrawContext::CloudLayer> out;
    for (const Scene::ScenePlane& sp : planes) {
        const MaterialIndex::Entry* e = textures.material(sp.plane.face.texture);
        if (!e || planeKind(&e->mat) != PlaneKind::Clouds) continue;
        out.push_back({sp.plane.z, cloudParams(e->mat)});
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.z < b.z; });
    if (out.size() > 4) out.resize(4);
    return out;
}

// Mirrors (v0.12.7): point the material shader at mirror plane `index`'s pictures (-1: none).
void setMirrorPictures(Shader& sh, const std::function<int(const char*)>& loc, const BakedDrawContext& ctx, int index) {
    const MirrorSet::Plane* p = ctx.mirrors && index >= 0 && index < (int)ctx.mirrors->planes.size() ? &ctx.mirrors->planes[index] : nullptr;
    const int r = p && p->refl.id ? 1 : 0, t = p && p->refr.id ? 1 : 0;
    SetShaderValue(sh, loc("mirrorReflect"), &r, SHADER_UNIFORM_INT);
    SetShaderValue(sh, loc("mirrorRefract"), &t, SHADER_UNIFORM_INT);
    rlActiveTextureSlot(kMirrorReflectUnit);
    rlEnableTexture(r ? p->refl.texture.id : rlGetTextureIdDefault());
    rlActiveTextureSlot(kMirrorRefractUnit);
    rlEnableTexture(t ? p->refr.texture.id : rlGetTextureIdDefault());
    rlActiveTextureSlot(0);
}

namespace {
// The bump maps' offset as the shader gets it in the normal's z: the step to the next map as
// a share of the page, > 0 across, < 0 down; 0 = not bumped.
float bumpZ(const BakedLight::FaceRef& r, const BakedLight::Page& pg) {
    if (!r.bumpStep) return 0;
    return r.bumpDown ? -(float)r.bumpStep / (float)pg.h : (float)r.bumpStep / (float)pg.w;
}
// One thing to draw: a plain face, or a terrain's base or painted layer.
struct Item {
    const SceneBrush* b = nullptr;
    int poly = -1;          // plain face: its polygon; terrain: -1
    int layer = -1;         // terrain: -1 base, else the painted layer
    const std::string* material = nullptr;
    int page = 0;
    double dist = 0;        // see-through faces: from the camera (far first)
    int mirror = -1;        // a mirror face (v0.12.7): its plane in ctx.mirrors
    bool refract = false;   // Refract (v0.12.7b): bends a copy of the picture drawn so far
    int plane = -1;         // an endless plane (v0.13.0): its index in ctx.planes
};
bool isSeeThrough(const Material* m) {
    if (!m || isReflectiveMaterial(*m) || isWaterMaterial(*m)) return false;
    if (planeKind(m) == PlaneKind::Clouds) return true; // cloud planes (v0.13.0)
    return isRefractMaterial(*m) || m->flag("$translucent") || m->num("$alpha", 1) < 1; // Refract (v0.12.7b): with the see-through faces
}

bool boxesMeet(const AABB& a, const AABB& b) {
    for (int k = 0; k < 3; ++k)
        if (a.max[k] < b.min[k] || b.max[k] < a.min[k]) return false;
    return true;
}

// Decals (v0.12.5): each one clips the lit surfaces in its box (their lightmap coordinates,
// bump offset and normal carried along), and is drawn over them by its material's shader,
// alpha-blended (DecalModulate: multiplied), a little towards the camera, without writing
// depth, in order.
template <class Use, class Release, class BindPage>
void drawDecalsPass(const SceneBrushes& brushes, const BakedLight& baked, TextureLibrary& textures, const BakedDrawContext& ctx,
                    Use& use, Release& release, BindPage& bindPage) {
    const std::vector<BakedLight::Page>& pages = baked.pages();
    const BakedLight::FaceRef full{0, -1, -1, nullptr};
    // About 3 depth steps at that distance, but never more than 1 cm: with a tiny near
    // plane far away, 3 steps are many centimetres and pushed the decal past a thin object
    // in front of it (v0.12.6 review).
    auto lift = [&](const Vec3& p) {
        const double d = length(p - ctx.camPos);
        return std::clamp(3.0 * d * d / (std::max(ctx.nearDist, 0.01) * 16777216.0), 0.05, 1.0);
    };
    // DecalVert::a: 0, 1 lightmap u, v; 2 bump offset; 3..5 normal
    auto vert = [](const Vec3& p, float lu, float lv, float bz, const Vec3& n) {
        DecalVert v;
        v.p = p;
        v.a = {lu, lv, bz, (float)n.x, (float)n.y, (float)n.z, 0, 0};
        return v;
    };
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    for (const Scene::SceneDecal& sd : *ctx.decals) {
        if (sd.pending) continue;
        const Decal& dc = sd.decal;
        const AABB box = decalBounds(dc);
        std::map<int, std::vector<DecalVert>> tris; // by lightmap page, 3 a triangle
        auto add = [&](int page, const std::vector<DecalVert>& poly) {
            const std::vector<DecalVert> c = clipToDecal(dc, poly);
            for (size_t i = 1; i + 1 < c.size(); ++i) {
                std::vector<DecalVert>& t = tris[page];
                t.push_back(c[0]);
                t.push_back(c[i]);
                t.push_back(c[i + 1]);
            }
        };
        for (const SceneBrush& b : brushes) {
            if (!boxesMeet(box, b.bounds)) continue;
            const size_t plain = b.terrain ? b.terrainFirst : b.polys.size();
            for (size_t pi = 0; pi < plain; ++pi) {
                const Polygon& p = b.polys[pi];
                if (p.face < 0 || p.face >= (int)b.faces.size() || p.verts.size() < 3 || !decalTakes(dc, p.normal)) continue;
                const BakedLight::FaceRef* ref = baked.face(b.id, p.face, false);
                if (!ref) ref = &full;
                const BakedLight::Page& pg = pages[ref->page];
                const float bz = bumpZ(*ref, pg);
                std::vector<DecalVert> poly;
                for (const Vec3& v : p.verts) {
                    float lu = 1.5f / pg.w, lv = 1.5f / pg.h;
                    if (ref->lm) BakedLight::uv(*ref, pg, dot(ref->lm->axisS, v) - ref->lm->s0, dot(ref->lm->axisT, v) - ref->lm->t0, lu, lv);
                    poly.push_back(vert(v, lu, lv, bz, p.normal));
                }
                add(ref->page, poly);
            }
            if (!b.terrain) continue;
            const BakedLight::FaceRef* ref = baked.face(b.id, b.terrain->face, true);
            if (!ref) ref = &full;
            const BakedLight::Page& pg = pages[ref->page];
            const float bz = bumpZ(*ref, pg);
            const int n = b.terrain->n;
            const size_t cells = b.polys.size() - b.terrainFirst;
            for (size_t c = 0; c < cells; ++c) {
                const Polygon& p = b.polys[b.terrainFirst + c];
                AABB cb;
                for (const Vec3& v : p.verts) cb.add(v);
                if (!boxesMeet(box, cb)) continue;
                const int ci = (int)(c % (size_t)(n - 1)), cj = (int)(c / (size_t)(n - 1));
                const int gi[4] = {ci, ci + 1, ci + 1, ci}, gj[4] = {cj, cj, cj + 1, cj + 1};
                DecalVert q[4];
                for (int k = 0; k < 4; ++k) {
                    float lu = 1.5f / pg.w, lv = 1.5f / pg.h;
                    if (ref->lm)
                        BakedLight::uv(*ref, pg, (double)gi[k] / (n - 1) * (ref->lm->w - 1), (double)gj[k] / (n - 1) * (ref->lm->h - 1), lu, lv);
                    const Vec3 nn = 4 * c + k < b.cellNormals.size() ? b.cellNormals[4 * c + k] : b.terrain->normal;
                    q[k] = vert(p.verts[k], lu, lv, bz, nn);
                }
                for (const auto& t : {std::array<int, 3>{0, 1, 2}, std::array<int, 3>{0, 2, 3}}) {
                    Vec3 gn = normalize(cross(q[t[1]].p - q[t[0]].p, q[t[2]].p - q[t[0]].p));
                    if (dot(gn, b.terrain->normal) < 0) gn = gn * -1;
                    if (!decalTakes(dc, gn)) continue;
                    add(ref->page, {q[t[0]], q[t[1]], q[t[2]]});
                }
            }
        }
        if (tris.empty()) continue;
        auto bd = use(dc.material);
        if (!bd.sh.id) continue;
        const bool modulate = bd.prog && bd.prog->file && bd.prog->file->blend == "modulate";
        if (bd.prog) {
            const int one = 1;
            SetShaderValue(bd.sh, bd.prog->at("decalPass"), &one, SHADER_UNIFORM_INT);
        }
        if (modulate) {
            rlSetBlendFactors(0x0306 /* GL_DST_COLOR */, 0x0300 /* GL_SRC_COLOR */, 0x8006 /* GL_FUNC_ADD */);
            BeginBlendMode(BLEND_CUSTOM);
        } else
            BeginBlendMode(BLEND_ALPHA);
        const Texture2D& tex = textures.get(dc.material);
        for (const auto& [page, vs] : tris) {
            bindPage(page);
            rlCheckRenderBatchLimit((int)vs.size());
            rlSetTexture(tex.id);
            rlBegin(RL_TRIANGLES);
            for (const DecalVert& v : vs) {
                const Vec3 nn{v.a[3], v.a[4], v.a[5]};
                auto ch = [](double x) { return (unsigned char)std::clamp((int)std::lround((x * 0.5 + 0.5) * 255), 0, 255); };
                rlColor4ub(ch(nn.x), ch(nn.y), ch(nn.z), 255);
                double u = 0, w = 0;
                decalUV(dc, v.p, u, w);
                rlTexCoord2f((float)u, (float)w);
                rlNormal3f(v.a[0], v.a[1], v.a[2]);
                const Vec3 p = v.p + normalize(nn) * lift(v.p);
                rlVertex3f((float)p.x, (float)p.y, (float)p.z);
            }
            rlEnd();
            rlSetTexture(0);
        }
        rlDrawRenderBatchActive();
        if (bd.prog) {
            const int zero = 0;
            SetShaderValue(bd.sh, bd.prog->at("decalPass"), &zero, SHADER_UNIFORM_INT);
        }
        release(bd);
        EndBlendMode();
    }
    rlEnableDepthMask();
}
} // namespace

bool drawBakedBrushes(const SceneBrushes& brushes, const BakedLight& baked, TextureLibrary& textures, const BakedDrawContext& ctx) {
    BakedShader& S = bakedShader();
    if (!baked.ready() || baked.pages().empty()) return false;
    if (!S.ok && !ctx.shaders) return false;
    const std::vector<BakedLight::Page>& pages = baked.pages();
    const BakedLight::FaceRef full{0, -1, -1, nullptr}; // the full-light block at (0, 0)
    auto materialOf = [&](const std::string& n) -> const Material* {
        const MaterialIndex::Entry* e = textures.material(n);
        return e ? &e->mat : nullptr;
    };

    // ---- what to draw in this pass
    std::vector<Item> items;
    if (!ctx.decals) for (const SceneBrush& b : brushes) {
        const size_t plain = b.terrain ? b.terrainFirst : b.polys.size();
        for (size_t pi = 0; pi < plain; ++pi) {
            const Polygon& p = b.polys[pi];
            if (p.face < 0 || p.face >= (int)b.faces.size() || p.verts.size() < 3) continue;
            const std::string& mat = b.faces[p.face].texture;
            if (isSeeThrough(ctx.shaders ? materialOf(mat) : nullptr) != ctx.translucent) continue;
            const BakedLight::FaceRef* ref = baked.face(b.id, p.face, false);
            const bool level = p.normal.z >= kWaterLevelCos; // water must be level (Source)
            Item it;
            it.b = &b;
            it.poly = (int)pi;
            it.material = &mat;
            it.page = ref ? ref->page : 0;
            if (ctx.mirrors && !ctx.mirrors->planes.empty()) // a mirror face: its plane's pictures
                if (const Material* m = materialOf(mat); m && mirrorParts(*m) && (level || !isWaterMaterial(*m)))
                    for (size_t k = 0; k < ctx.mirrors->planes.size(); ++k)
                        if (onMirrorPlane(ctx.mirrors->planes[k].p, p.normal, dot(p.normal, p.verts[0]))) { it.mirror = (int)k; break; }
            if (ctx.translucent) {
                Vec3 c;
                for (const Vec3& v : p.verts) c += v;
                it.dist = length(c / (double)p.verts.size() - ctx.camPos);
                const Material* m = ctx.shaders ? materialOf(mat) : nullptr;
                it.refract = m && isRefractMaterial(*m);
            }
            items.push_back(it);
        }
        if (!b.terrain) continue;
        const BakedLight::FaceRef* ref = baked.face(b.id, b.terrain->face, true);
        for (int layer = -1; layer < (int)b.layers.size(); ++layer) {
            const std::string& mat = layer < 0 ? b.faces[b.terrain->face].texture : b.layers[layer].texture;
            if (isSeeThrough(ctx.shaders ? materialOf(mat) : nullptr) != ctx.translucent) continue;
            Item it;
            it.b = &b;
            it.layer = layer;
            it.material = &mat;
            it.page = ref ? ref->page : 0;
            if (ctx.translucent) it.dist = length(b.bounds.center() - ctx.camPos);
            items.push_back(it);
        }
    }
    // Endless planes (v0.13.0): ground and water with the solid faces, clouds with the
    // see-through ones (far first: a cloud layer is behind the map's glass)
    if (!ctx.decals && ctx.planes)
        for (size_t i = 0; i < ctx.planes->size(); ++i) {
            const EndlessPlane& pl = (*ctx.planes)[i].plane;
            const std::string& mat = pl.face.texture;
            const Material* m = ctx.shaders ? materialOf(mat) : nullptr;
            if (isSeeThrough(m) != ctx.translucent) continue;
            Item it;
            it.plane = (int)i;
            it.material = &mat;
            it.dist = 1e30 - std::fabs(ctx.camPos.z - pl.z); // before every face; the farther plane first
            if (ctx.mirrors && m && mirrorParts(*m))
                for (size_t k = 0; k < ctx.mirrors->planes.size(); ++k)
                    if (onMirrorPlane(ctx.mirrors->planes[k].p, {0, 0, 1}, pl.z)) { it.mirror = (int)k; break; }
            items.push_back(it);
        }
    if (items.empty() && !ctx.decals) return true;
    if (ctx.translucent)
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.dist > b.dist; });
    else // terrain layers after everything else (bottom layer first), then by material and page
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            if (a.layer != b.layer) return a.layer < b.layer;
            if (*a.material != *b.material) return *a.material < *b.material;
            if (a.mirror != b.mirror) return a.mirror < b.mirror;
            return a.page < b.page;
        });

    // ---- the program for a material: its shader file, else the built-in LightmappedGeneric,
    // else the plain lightmap shader
    struct Bound {
        Shader sh{};
        ShaderLibrary::Program* prog = nullptr;
        bool nocull = false;
    };
    rlDrawRenderBatchActive();
    if (ctx.translucent) rlDisableDepthMask();
    auto plainUniforms = [&] {
        const int unit = kLightmapUnit;
        SetShaderValue(S.sh, S.lightmap, &unit, SHADER_UNIFORM_INT);
        SetShaderValue(S.sh, S.exposure, &ctx.exposure, SHADER_UNIFORM_FLOAT);
        const int fogOn = ctx.fog ? 1 : 0;
        SetShaderValue(S.sh, S.fogOn, &fogOn, SHADER_UNIFORM_INT);
        const float cam[3] = {(float)ctx.camPos.x, (float)ctx.camPos.y, (float)ctx.camPos.z};
        SetShaderValue(S.sh, S.camPos, cam, SHADER_UNIFORM_VEC3);
        const float fc[3] = {(float)(ctx.fogColor.x / 255), (float)(ctx.fogColor.y / 255), (float)(ctx.fogColor.z / 255)};
        SetShaderValue(S.sh, S.fogColor, fc, SHADER_UNIFORM_VEC3);
        const float fr[2] = {(float)ctx.fogStart, (float)ctx.fogEnd}, fh[2] = {(float)ctx.fogBase, (float)ctx.fogThickness};
        SetShaderValue(S.sh, S.fogRange, fr, SHADER_UNIFORM_VEC2);
        SetShaderValue(S.sh, S.fogHeight, fh, SHADER_UNIFORM_VEC2);
    };
    std::set<std::string> reported;
    auto report = [&](const std::string& what) {
        if (ctx.problems && reported.insert(what).second) ctx.problems->push_back(what);
    };
    auto use = [&](const std::string& matName) -> Bound {
        Bound bd;
        const MaterialIndex::Entry* e = textures.material(matName);
        const Material* m = e ? &e->mat : nullptr;
        if (ctx.shaders) {
            const std::string shader = m ? m->shader : "LightmappedGeneric";
            ShaderLibrary::Program* p = ctx.shaders->program(shader, ShaderGeometry::World);
            if (!p) report("the shader " + shader + " (material " + matName + ") isn't there: drawn with LightmappedGeneric");
            else if (!p->ok) report(p->error + ": drawn with LightmappedGeneric");
            if (!p || !p->ok) p = ctx.shaders->program("LightmappedGeneric", ShaderGeometry::World);
            if (p && p->ok) {
                bd.prog = p;
                bd.sh = p->shader;
                BeginShaderMode(bd.sh);
                setMaterialCommon(bd.sh, [&](const char* n) { return p->at(n); }, ctx);
                for (const std::string& miss : ctx.shaders->bind(*p, m, ShaderLibrary::libraryFinder(textures, e), kFirstMaterialUnit))
                    report("the material " + matName + " uses " + miss + ", which isn't there");
                rlActiveTextureSlot(kSkyUnit);
                rlEnableTexture(ctx.skyTex ? ctx.skyTex : rlGetTextureIdDefault());
                rlActiveTextureSlot(0);
                bd.nocull = m && m->flag("$nocull");
                if (bd.nocull) rlDisableBackfaceCulling();
                return bd;
            }
        }
        if (!S.ok) return bd;
        bd.sh = S.sh;
        BeginShaderMode(S.sh);
        plainUniforms();
        return bd;
    };
    auto release = [&](Bound& bd) {
        rlDrawRenderBatchActive();
        if (bd.prog) ctx.shaders->unbind(kFirstMaterialUnit);
        if (bd.nocull) rlEnableBackfaceCulling();
        if (bd.sh.id) EndShaderMode();
        bd = Bound{};
    };
    auto bindPage = [&](int page) {
        rlDrawRenderBatchActive();
        rlActiveTextureSlot(kLightmapUnit);
        rlEnableTexture(pages[page].tex);
        rlActiveTextureSlot(0);
    };
    auto normalColor = [](const Vec3& n, unsigned char a) {
        auto c = [](double v) { return (unsigned char)std::clamp((int)std::lround((v * 0.5 + 0.5) * 255), 0, 255); };
        rlColor4ub(c(n.x), c(n.y), c(n.z), a);
    };
    ShaderLibrary::Program* bdProg = nullptr; // the program planes are being drawn with
    auto emitPlane = [&](const Item& it) {
        const EndlessPlane& pl = (*ctx.planes)[it.plane].plane;
        const Face f = planeFace(pl);
        const Texture2D& tex = textures.get(f.texture);
        const TexSize ts{std::max(1, tex.width), std::max(1, tex.height)};
        auto quad = [&](double x0, double y0, double x1, double y1) {
            const Vec3 q[4] = {{x0, y0, pl.z}, {x1, y0, pl.z}, {x1, y1, pl.z}, {x0, y1, pl.z}};
            rlCheckRenderBatchLimit(6);
            rlBegin(RL_TRIANGLES);
            for (int k : {0, 1, 2, 0, 2, 3}) {
                const UV uv = texCoord(f, q[k], ts);
                normalColor({0, 0, 1}, 255);
                rlTexCoord2f((float)uv.u, (float)uv.v);
                rlNormal3f(0, 0, 0);
                rlVertex3f((float)q[k].x, (float)q[k].y, (float)q[k].z);
            }
            rlEnd();
        };
        // a grid of squares around the camera, within the far clip ...
        const int n = 8;
        const double half = ctx.planeReach, cell = 2 * half / n, cx = ctx.camPos.x, cy = ctx.camPos.y;
        rlSetTexture(tex.id);
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) quad(cx - half + i * cell, cy - half + j * cell, cx - half + (i + 1) * cell, cy - half + (j + 1) * cell);
        // ... and a ring out to kPlaneReach (10 km) beyond it, kept just inside the far clip by
        // the vertex stage, so the plane runs to the horizon whatever the camera's far distance
        if (half < kPlaneReach && bdProg) {
            rlDrawRenderBatchActive();
            const int on = 1, off = 0;
            SetShaderValue(bdProg->shader, bdProg->at("planeFar"), &on, SHADER_UNIFORM_INT);
            const double R = kPlaneReach;
            quad(cx - R, cy - R, cx + R, cy - half);
            quad(cx - R, cy + half, cx + R, cy + R);
            quad(cx - R, cy - half, cx - half, cy + half);
            quad(cx + half, cy - half, cx + R, cy + half);
            rlDrawRenderBatchActive();
            SetShaderValue(bdProg->shader, bdProg->at("planeFar"), &off, SHADER_UNIFORM_INT);
        }
        rlSetTexture(0);
    };
    auto planeUniforms = [&](Bound& bd, int plane) {
        if (!bd.prog) return;
        rlDrawRenderBatchActive();
        ShaderLibrary::Program* p = bd.prog;
        const int on = plane >= 0 ? 1 : 0;
        SetShaderValue(bd.sh, p->at("planeLightOn"), &on, SHADER_UNIFORM_INT);
        if (plane < 0) return;
        const BakeResult* br = baked.result();
        const PlaneLight* L = nullptr;
        if (br)
            for (const PlaneLight& x : br->planes)
                if (x.id == (*ctx.planes)[plane].plane.id) L = &x;
        float sky[12] = {0}, sun[12] = {0};
        for (int k = 0; k < 4 && L; ++k) {
            sky[3 * k] = (float)L->sky[k].x; sky[3 * k + 1] = (float)L->sky[k].y; sky[3 * k + 2] = (float)L->sky[k].z;
            sun[3 * k] = (float)L->sun[k].x; sun[3 * k + 1] = (float)L->sun[k].y; sun[3 * k + 2] = (float)L->sun[k].z;
        }
        SetShaderValueV(bd.sh, p->at("planeSky"), sky, SHADER_UNIFORM_VEC3, 4);
        SetShaderValueV(bd.sh, p->at("planeSun"), sun, SHADER_UNIFORM_VEC3, 4);
        const int bumped = L && L->bumped ? 1 : 0;
        SetShaderValue(bd.sh, p->at("planeBumped"), &bumped, SHADER_UNIFORM_INT);
    };
    auto emit = [&](const Item& it) {
        const SceneBrush& b = *it.b;
        const BakedLight::Page& pg = pages[it.page];
        if (it.poly >= 0) {
            const Polygon& p = b.polys[it.poly];
            const BakedLight::FaceRef* ref = baked.face(b.id, p.face, false);
            if (!ref) ref = &full;
            const Face& f = b.faces[p.face];
            const Texture2D& tex = textures.get(f.texture);
            const TexSize ts{std::max(1, tex.width), std::max(1, tex.height)};
            const float bz = bumpZ(*ref, pg);
            rlCheckRenderBatchLimit((int)(p.verts.size() - 2) * 3);
            rlSetTexture(tex.id);
            rlBegin(RL_TRIANGLES);
            for (size_t i = 1; i + 1 < p.verts.size(); ++i)
                for (size_t k : {(size_t)0, i, i + 1}) {
                    const Vec3& v = p.verts[k];
                    float lu = 1.5f / pg.w, lv = 1.5f / pg.h;
                    if (ref->lm) BakedLight::uv(*ref, pg, dot(ref->lm->axisS, v) - ref->lm->s0, dot(ref->lm->axisT, v) - ref->lm->t0, lu, lv);
                    const UV uv = texCoord(f, v, ts);
                    normalColor(p.normal, 255);
                    rlTexCoord2f((float)uv.u, (float)uv.v);
                    rlNormal3f(lu, lv, bz);
                    rlVertex3f((float)v.x, (float)v.y, (float)v.z);
                }
            rlEnd();
            rlSetTexture(0);
            return;
        }
        const BakedLight::FaceRef* ref = baked.face(b.id, b.terrain->face, true);
        if (!ref) ref = &full;
        const Face& f = b.faces[b.terrain->face];
        const int n = b.terrain->n;
        const size_t cells = b.polys.size() - b.terrainFirst;
        const Texture2D& tex = textures.get(*it.material);
        const TexSize ts{std::max(1, tex.width), std::max(1, tex.height)};
        const float bz = bumpZ(*ref, pg);
        for (size_t c = 0; c < cells; ++c) {
            const Polygon& p = b.polys[b.terrainFirst + c];
            const float* al = it.layer < 0 ? nullptr : &b.layers[it.layer].cellAlpha[4 * c];
            if (al && al[0] <= 0 && al[1] <= 0 && al[2] <= 0 && al[3] <= 0) continue;
            const int ci = (int)(c % (size_t)(n - 1)), cj = (int)(c / (size_t)(n - 1));
            const int gi[4] = {ci, ci + 1, ci + 1, ci}, gj[4] = {cj, cj, cj + 1, cj + 1};
            rlCheckRenderBatchLimit(6);
            rlSetTexture(tex.id);
            rlBegin(RL_TRIANGLES);
            for (int k : {0, 1, 2, 0, 2, 3}) {
                const unsigned char a = !al ? 255 : (unsigned char)std::lround(std::clamp(al[k], 0.0f, 1.0f) * 255);
                const Vec3 nn = 4 * c + k < b.cellNormals.size() ? b.cellNormals[4 * c + k] : b.terrain->normal;
                normalColor(nn, a);
                float lu = 1.5f / pg.w, lv = 1.5f / pg.h;
                if (ref->lm)
                    BakedLight::uv(*ref, pg, (double)gi[k] / (n - 1) * (ref->lm->w - 1), (double)gj[k] / (n - 1) * (ref->lm->h - 1), lu, lv);
                const UV uv = texCoord(f, p.verts[k], ts);
                rlTexCoord2f((float)uv.u, (float)uv.v);
                rlNormal3f(lu, lv, bz);
                rlVertex3f((float)p.verts[k].x, (float)p.verts[k].y, (float)p.verts[k].z);
            }
            rlEnd();
            rlSetTexture(0);
        }
    };

    if (ctx.decals) {
        drawDecalsPass(brushes, baked, textures, ctx, use, release, bindPage);
        rlActiveTextureSlot(kLightmapUnit);
        rlDisableTexture();
        rlActiveTextureSlot(kSkyUnit);
        rlDisableTexture();
        rlActiveTextureSlot(0);
        return true;
    }
    Bound bd;
    const std::string* curMat = nullptr;
    int curPage = -1, curMirror = -1, curPlane = -1;
    for (const Item& it : items) {
        if (!curMat || *curMat != *it.material) {
            release(bd);
            bd = use(*it.material);
            curMat = it.material;
            curPage = -1;
            curMirror = -1; // use() left no mirror picture bound
            curPlane = -1;  // ... and no plane light
            if (!bd.sh.id) continue;
        }
        if (!bd.sh.id) continue;
        if (it.plane != curPlane) {
            planeUniforms(bd, it.plane);
            curPlane = it.plane;
        }
        if (it.refract && bd.prog && ctx.copyFrame) {
            // Refract (v0.12.7b, as Source's frame buffer copy before each renderable): the
            // picture drawn so far, far see-through faces included
            rlDrawRenderBatchActive();
            const unsigned copy = ctx.copyFrame();
            ShaderLibrary::Program* p = bd.prog;
            const int on = copy ? 1 : 0;
            SetShaderValue(bd.sh, p->at("frameCopyOn"), &on, SHADER_UNIFORM_INT);
            rlActiveTextureSlot(kMirrorRefractUnit);
            rlEnableTexture(copy ? copy : rlGetTextureIdDefault());
            rlActiveTextureSlot(0);
        }
        if (it.mirror != curMirror && bd.prog) {
            rlDrawRenderBatchActive();
            ShaderLibrary::Program* p = bd.prog;
            setMirrorPictures(bd.sh, [&](const char* n) { return p->at(n); }, ctx, it.mirror);
            curMirror = it.mirror;
        }
        if (it.plane >= 0) { // endless planes are seen from both sides (water from below, clouds)
            rlDrawRenderBatchActive();
            rlDisableBackfaceCulling();
            bdProg = bd.prog;
            emitPlane(it);
            rlDrawRenderBatchActive();
            if (!bd.nocull) rlEnableBackfaceCulling();
            continue;
        }
        if (it.page != curPage) {
            bindPage(it.page);
            curPage = it.page;
        }
        emit(it);
    }
    release(bd);
    rlActiveTextureSlot(kLightmapUnit);
    rlDisableTexture();
    rlActiveTextureSlot(kSkyUnit);
    rlDisableTexture();
    rlActiveTextureSlot(0);
    if (ctx.translucent) rlEnableDepthMask();
    return true;
}

} // namespace scal
