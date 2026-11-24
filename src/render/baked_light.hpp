#pragma once
// Baked light on the graphics card (milestone 12, v0.12.1): the lightmaps core/bake.hpp
// makes, packed into float textures, and the drawing of brushes with them (renders and the
// Camera Preview only; the editor never shows them).
//  - Face lightmaps go into atlas pages with a one-luxel border (copies of the edge), read
//    with bilinear filtering, as Source does: luxel centres at texel centres.
//  - Each primitive's 3D lightmap is one texture holding its slices side by side (the
//    shader blends two slices: trilinear).
//  - Shading is linear: texture (to linear) x light x exposure, then back to sRGB; fog after.
#include "core/bake.hpp"
#include "raylib.h"
#include "render/scene.hpp"
#include <functional>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace scal {

class TextureLibrary;

class BakedLight {
public:
    BakedLight() = default;
    ~BakedLight() { release(); }
    BakedLight(const BakedLight&) = delete;
    BakedLight& operator=(const BakedLight&) = delete;

    // Uploads `r` (kept: the faces' mapping is read while drawing). Same result again: nothing.
    void upload(std::shared_ptr<const BakeResult> r);
    void release();
    bool ready() const { return result_ != nullptr; }
    const BakeResult* result() const { return result_.get(); }
    const std::shared_ptr<const BakeResult>& shared() const { return result_; }

    // A bumped face (v0.12.4b) has its 3 bump-direction maps next to the flat one, each
    // `bumpStep` pixels further right (or down, `bumpDown`, if 4 don't fit across a page).
    struct FaceRef { int page = 0; int x = 0, y = 0; const FaceLightmap* lm = nullptr; int bumpStep = 0; bool bumpDown = false; };
    const FaceRef* face(BrushId b, int face, bool terrain) const;
    struct Page { unsigned tex = 0; int w = 0, h = 0; };
    const std::vector<Page>& pages() const { return pages_; }
    // Where a face's luxel coordinate (s, t) is in its page (0..1).
    static void uv(const FaceRef& f, const Page& p, double s, double t, float& u, float& v);

    struct PrimRef { unsigned tex = 0; int tilesX = 1, texW = 1, texH = 1; const PrimLightmap* lm = nullptr; };
    // The lightmap of a combination (combo != 0) or a lone primitive.
    const PrimRef* prim(std::uint32_t combo, MathPrimId id) const;

private:
    std::shared_ptr<const BakeResult> result_;
    std::vector<Page> pages_;
    std::map<std::tuple<BrushId, int, bool>, FaceRef> faces_;
    std::map<std::pair<std::uint32_t, MathPrimId>, PrimRef> prims_;
};

class ShaderLibrary;

struct BakedDrawContext {
    Vec3 camPos;
    float exposure = 1;     // multiplier (2^stops)
    bool fog = false;
    Vec3 fogColor;          // 0..255
    double fogStart = 0, fogEnd = 1, fogBase = 0, fogThickness = 0;
    // Materials (v0.12.4): each face is drawn by its material's shader file.
    ShaderLibrary* shaders = nullptr;      // null: the plain lightmap shader (no materials)
    bool orthoOn = false;
    Vec3 viewDir;                          // an orthographic camera's direction
    unsigned skyTex = 0;                   // the sky for reflections (small), 0 = none
    float skyCover = 1;
    Vec3 skyBelow;                         // 0..255
    Vec3 skyFlat;                          // linear, without a sky picture
    // Which faces: opaque ones (with cutouts), or the see-through ones ($translucent,
    // $alpha < 1), drawn last, far to near, without writing depth.
    bool translucent = false;
    std::vector<std::string>* problems = nullptr; // shaders that fail, textures materials miss
    TextureLibrary* textures = nullptr;    // props' settings files find textures here too (v0.12.4b)
    // Decals (v0.12.5): set -> this pass draws these decals (in their order) on the brushes'
    // lit surfaces instead of the brushes; `nearDist` is the camera's near distance.
    const std::vector<Scene::SceneDecal>* decals = nullptr;
    double nearDist = 2;
    // Reflection probes (v0.12.6): what `$envmap env_cubemap` reflects (null: the sky);
    // `linearOut`: building a probe's picture (linear light, no exposure).
    const struct ProbeAtlas* probes = nullptr;
    bool linearOut = false;
    // Mirrors (v0.12.7): the pictures of the mirror planes in view (null: none; mirror
    // faces reflect the probes). `clip`: drawing a mirror's picture, only what has
    // dot(p, clip.n) >= clip.d. `viewW/H`: the target's pixels (where a mirror reads its picture).
    const struct MirrorSet* mirrors = nullptr;
    bool clipOn = false;
    Vec3 clipN;
    double clipD = 0;
    int viewW = 1, viewH = 1;
};

// Draws the brushes (textured, terrain with its painted layers) lit by `baked`. Inside the
// 3D camera mode. False if the shader can't run (then nothing was drawn).
bool drawBakedBrushes(const SceneBrushes& brushes, const BakedLight& baked, TextureLibrary& textures, const BakedDrawContext& ctx);
// Sets the uniforms every material shader has (render/shader_library.hpp) from `ctx`.
void setMaterialCommon(Shader& sh, const std::function<int(const char*)>& loc, const BakedDrawContext& ctx);

} // namespace scal
