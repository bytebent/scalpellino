#include "render/mirrors.hpp"
#include "render/model_library.hpp"
#include "render/texture_library.hpp"
#include "rlgl.h"
#include <algorithm>

namespace scal {

MirrorSet::~MirrorSet() {
    for (Plane& p : planes) {
        if (p.refl.id) UnloadRenderTexture(p.refl);
        if (p.refr.id) UnloadRenderTexture(p.refr);
    }
}

std::vector<MirrorFace> mirrorFaces(const Scene& scene) {
    std::vector<MirrorFace> out;
    if (!scene.textures) return out;
    for (const SceneBrush& b : scene.brushes) {
        if (scene.hidden.count(b.id)) continue;
        const size_t plain = b.terrain ? b.terrainFirst : b.polys.size();
        for (size_t i = 0; i < plain; ++i) {
            const Polygon& p = b.polys[i];
            if (p.face < 0 || p.face >= (int)b.faces.size() || p.verts.size() < 3) continue;
            const MaterialIndex::Entry* e = scene.textures->material(b.faces[p.face].texture);
            const int parts = e ? mirrorParts(e->mat) : 0;
            if (!parts) continue;
            MirrorFace f;
            f.verts = p.verts;
            f.n = normalize(p.normal);
            f.d = dot(f.n, p.verts[0]);
            f.parts = parts;
            out.push_back(std::move(f));
        }
    }
    return out;
}

std::shared_ptr<MirrorSet> MirrorBuilder::build(const Camera& cam, const Scene& sceneIn, int w, int h, bool smooth) {
    size_t dropped = 0;
    const std::vector<MirrorPlane> planes = mirrorPlanes(mirrorFaces(sceneIn), cam, kMaxMirrors, &dropped);
    if (planes.empty()) return nullptr;
    if (!view_) view_ = std::make_unique<Viewport>(ViewKind::Persp);
    auto set = std::make_shared<MirrorSet>();
    set->dropped = dropped;
    set->w = std::max(1, w);
    set->h = std::max(1, h);
    Scene scene = sceneIn;
    scene.linearOut = true;
    scene.exposure = 1;
    scene.mirrors = nullptr; // a mirror inside a mirror reflects the probes
    scene.clipOn = true;
    // the picture's texture filtering (as Renderer::draw)
    const bool texWas = scene.textures ? scene.textures->smooth() : smooth;
    const bool modelWas = scene.models ? scene.models->smooth() : smooth;
    if (scene.textures && texWas != smooth) scene.textures->setSmooth(smooth);
    if (scene.models && modelWas != smooth) scene.models->setSmooth(smooth);
    Grid g;
    g.visible = false;
    view_->setFloatTarget(true);
    auto draw = [&](const Camera& c, const Vec3& n, double d) -> RenderTexture2D {
        scene.clipN = n;
        scene.clipD = d;
        Camera lens = c;
        lens.width = set->w;
        lens.height = set->h;
        view_->resize(set->w, set->h, 1.0f);
        if (!view_->target().id) return RenderTexture2D{};
        view_->setLens(lens);
        view_->render(g, scene);
        RenderTexture2D rt = view_->takeTarget();
        SetTextureFilter(rt.texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(rt.texture, TEXTURE_WRAP_CLAMP);
        return rt;
    };
    for (const MirrorPlane& p : planes) {
        const double t0 = GetTime();
        MirrorSet::Plane mp;
        mp.p = p;
        // Source's clip planes sit 0.1 units off the glass
        if (p.parts & kMirrorReflect) mp.refl = draw(mirrorCamera(cam, p.n, p.d), p.n, p.d + 0.1);
        if (p.parts & kMirrorRefract) mp.refr = draw(cam, p.n * -1.0, -p.d + 0.1);
        mp.seconds = GetTime() - t0;
        set->planes.push_back(mp);
    }
    if (scene.textures && texWas != smooth) scene.textures->setSmooth(texWas);
    if (scene.models && modelWas != smooth) scene.models->setSmooth(modelWas);
    return set;
}

} // namespace scal
