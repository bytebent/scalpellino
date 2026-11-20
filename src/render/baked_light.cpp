#include "render/baked_light.hpp"
#include "core/texture.hpp"
#include "render/texture_library.hpp"
#include "raylib.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>

namespace scal {

namespace {
constexpr int kPageSize = 2048;
constexpr int kLightmapUnit = 5; // texture unit the lightmap page is bound to (the batch uses 0..4)

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
    struct Placed { size_t face; int page, x, y; };
    std::vector<Placed> placed;
    std::vector<int> pageUsedH;
    int page = 0, x = 4, y = 0, shelfH = 4; // (0, 0)..(3, 3): full light, for anything without a lightmap
    pageUsedH.push_back(4);
    for (size_t i : order) {
        const FaceLightmap& f = res.faces[i];
        const int w = f.w + 2, h = f.h + 2;
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
        placed.push_back({i, page, x, y});
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
        for (int j = -1; j <= f.h; ++j)
            for (int i = -1; i <= f.w; ++i) {
                const int si = std::clamp(i, 0, f.w - 1), sj = std::clamp(j, 0, f.h - 1);
                const float* src = &f.rgb[((size_t)sj * f.w + si) * 3];
                float* dst = &d[((size_t)(pl.y + 1 + j) * W + (pl.x + 1 + i)) * 3];
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
            }
        FaceRef ref;
        ref.page = pl.page;
        ref.x = pl.x;
        ref.y = pl.y;
        ref.lm = &f;
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

bool drawBakedBrushes(const SceneBrushes& brushes, const BakedLight& baked, TextureLibrary& textures, const BakedDrawContext& ctx) {
    BakedShader& S = bakedShader();
    if (!S.ok || !baked.ready() || baked.pages().empty()) return false;
    rlDrawRenderBatchActive();
    BeginShaderMode(S.sh);
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

    const std::vector<BakedLight::Page>& pages = baked.pages();
    const BakedLight::FaceRef full{0, -1, -1, nullptr}; // the full-light block at (0, 0)
    for (int page = 0; page < (int)pages.size(); ++page) {
        rlDrawRenderBatchActive();
        rlActiveTextureSlot(unit);
        rlEnableTexture(pages[page].tex);
        rlActiveTextureSlot(0);
        const BakedLight::Page& pg = pages[page];
        for (const SceneBrush& b : brushes) {
            const size_t plain = b.terrain ? b.terrainFirst : b.polys.size();
            for (size_t pi = 0; pi < plain; ++pi) {
                const Polygon& p = b.polys[pi];
                if (p.face < 0 || p.face >= (int)b.faces.size() || p.verts.size() < 3) continue;
                const BakedLight::FaceRef* ref = baked.face(b.id, p.face, false);
                if (!ref) ref = &full;
                if (ref->page != page) continue;
                const Face& f = b.faces[p.face];
                const Texture2D& tex = textures.get(f.texture);
                const TexSize ts{std::max(1, tex.width), std::max(1, tex.height)};
                rlCheckRenderBatchLimit((int)(p.verts.size() - 2) * 3);
                rlSetTexture(tex.id);
                rlBegin(RL_TRIANGLES);
                rlColor4ub(255, 255, 255, 255);
                for (size_t i = 1; i + 1 < p.verts.size(); ++i)
                    for (size_t k : {(size_t)0, i, i + 1}) {
                        const Vec3& v = p.verts[k];
                        float lu = 1.5f / pg.w, lv = 1.5f / pg.h;
                        if (ref->lm) BakedLight::uv(*ref, pg, dot(ref->lm->axisS, v) - ref->lm->s0, dot(ref->lm->axisT, v) - ref->lm->t0, lu, lv);
                        const UV uv = texCoord(f, v, ts);
                        rlTexCoord2f((float)uv.u, (float)uv.v);
                        rlNormal3f(lu, lv, 0);
                        rlVertex3f((float)v.x, (float)v.y, (float)v.z);
                    }
                rlEnd();
                rlSetTexture(0);
            }
            if (!b.terrain) continue;
            const BakedLight::FaceRef* ref = baked.face(b.id, b.terrain->face, true);
            if (!ref) ref = &full;
            if (ref->page != page) continue;
            const Face& f = b.faces[b.terrain->face];
            const int n = b.terrain->n;
            const size_t cells = b.polys.size() - b.terrainFirst;
            for (int pass = 0; pass < 1 + (int)b.layers.size(); ++pass) {
                const Texture2D& tex = textures.get(pass == 0 ? f.texture : b.layers[pass - 1].texture);
                const TexSize ts{std::max(1, tex.width), std::max(1, tex.height)};
                for (size_t c = 0; c < cells; ++c) {
                    const Polygon& p = b.polys[b.terrainFirst + c];
                    const float* al = pass == 0 ? nullptr : &b.layers[pass - 1].cellAlpha[4 * c];
                    if (pass > 0 && al[0] <= 0 && al[1] <= 0 && al[2] <= 0 && al[3] <= 0) continue;
                    const int ci = (int)(c % (size_t)(n - 1)), cj = (int)(c / (size_t)(n - 1));
                    const int gi[4] = {ci, ci + 1, ci + 1, ci}, gj[4] = {cj, cj, cj + 1, cj + 1};
                    rlCheckRenderBatchLimit(6);
                    rlSetTexture(tex.id);
                    rlBegin(RL_TRIANGLES);
                    for (int k : {0, 1, 2, 0, 2, 3}) {
                        const unsigned char a = pass == 0 ? 255 : (unsigned char)std::lround(std::clamp(al[k], 0.0f, 1.0f) * 255);
                        rlColor4ub(255, 255, 255, a);
                        float lu = 1.5f / pg.w, lv = 1.5f / pg.h;
                        if (ref->lm)
                            BakedLight::uv(*ref, pg, (double)gi[k] / (n - 1) * (ref->lm->w - 1), (double)gj[k] / (n - 1) * (ref->lm->h - 1), lu, lv);
                        const UV uv = texCoord(f, p.verts[k], ts);
                        rlTexCoord2f((float)uv.u, (float)uv.v);
                        rlNormal3f(lu, lv, 0);
                        rlVertex3f((float)p.verts[k].x, (float)p.verts[k].y, (float)p.verts[k].z);
                    }
                    rlEnd();
                    rlSetTexture(0);
                }
            }
        }
    }
    rlDrawRenderBatchActive();
    rlActiveTextureSlot(unit);
    rlDisableTexture();
    rlActiveTextureSlot(0);
    EndShaderMode();
    return true;
}

} // namespace scal
