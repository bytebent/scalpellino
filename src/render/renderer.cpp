#include "render/renderer.hpp"
#include "core/texture.hpp"
#include "render/model_library.hpp"
#include "render/sky_library.hpp"
#include "render/texture_library.hpp"
#include <algorithm>
#include <cstring>
#include <set>

namespace scal {

namespace {
// Drops a brush's nodraw faces (plain polygons only; terrain cells stay). Returns how many.
size_t dropNoDraw(SceneBrush& b) {
    const size_t plain = b.terrain ? b.terrainFirst : b.polys.size();
    std::vector<Polygon> kept;
    kept.reserve(b.polys.size());
    size_t dropped = 0;
    for (size_t i = 0; i < b.polys.size(); ++i) {
        const Polygon& p = b.polys[i];
        if (i < plain && p.face >= 0 && p.face < (int)b.faces.size() && isNoDraw(b.faces[p.face].texture)) {
            ++dropped;
            continue;
        }
        kept.push_back(p);
    }
    if (dropped) {
        b.polys = std::move(kept);
        if (b.terrain) b.terrainFirst -= dropped;
    }
    return dropped;
}
} // namespace

Scene Renderer::scene(const Document& doc, TextureLibrary& textures, ModelLibrary& models, SkyLibrary& skies, Stats* stats) {
    if (doc.revision() != revision_) {
        brushes_.clear();
        brushStats_ = Stats{};
        brushes_.reserve(doc.brushes().size());
        for (const Brush& b : doc.brushes()) {
            SceneBrush sb = makeSceneBrush(b);
            brushStats_.nodrawFaces += dropNoDraw(sb);
            ++brushStats_.brushes;
            if (!doc.visible(b)) ++brushStats_.hiddenBrushes;
            if (b.terrain) ++brushStats_.terrains;
            brushes_.push_back(std::move(sb));
        }
        revision_ = doc.revision();
    }
    Stats st = brushStats_;
    Scene s;
    s.brushes = brushes_;
    s.textures = &textures;
    s.models = &models;
    for (const Light& l : doc.lights()) s.lights.push_back({l, false});
    for (const MathPrim& p : doc.prims()) s.prims.push_back({p, false});
    for (const Prop& p : doc.props()) {
        Scene::SceneProp sp;
        sp.prop = p;
        const Model* m = models.find(p.model);
        sp.local = propLocalBounds(m);
        sp.missing = m == nullptr;
        if (sp.missing) ++st.missingProps;
        s.props.push_back(std::move(sp));
    }
    st.lights = s.lights.size();
    st.prims = s.prims.size();
    st.props = s.props.size();
    // Every texture the picture uses that the library doesn't have.
    std::set<std::string> missing;
    auto want = [&](const std::string& name) {
        if (!name.empty() && !isNoDraw(name) && !textures.exists(name)) missing.insert(name);
    };
    for (const SceneBrush& b : brushes_) {
        for (const Polygon& p : b.polys)
            if (p.face >= 0 && p.face < (int)b.faces.size()) want(b.faces[p.face].texture);
        for (const auto& l : b.layers) want(l.texture);
    }
    for (const MathPrim& p : doc.prims())
        want(p.texture);
    st.missingTextures.assign(missing.begin(), missing.end());

    const Environment& env = doc.environment();
    s.ambient = env.ambient;
    st.sky = env.sky;
    if (!env.sky.empty()) {
        if (const Texture2D* t = skies.get(env.sky)) {
            s.skyTexture = t->id;
            const SkyLibrary::Cut cut = skies.cut(env.sky);
            s.skyCover = cut.cover;
            s.skyBelow = {(double)cut.below[0], (double)cut.below[1], (double)cut.below[2]};
        } else {
            st.skyProblem = skies.problem(env.sky);
            if (st.skyProblem.empty()) st.skyProblem = "not found";
        }
    }
    if (env.fogOn) {
        s.fog = true;
        s.fogColor = env.fog.color;
        s.fogStart = env.fog.start;
        s.fogEnd = env.fog.end;
        s.fogBase = env.fog.base;
        s.fogThickness = env.fog.thickness;
    }
    st.fog = env.fogOn;
    if (stats) *stats = std::move(st);
    return s;
}

void Renderer::light(const Document& doc, Scene& s) {
    s.lightPreview = true;
    s.lighting = &lighting_.get(doc, brushes_);
}

bool Renderer::draw(const Camera& cam, const Scene& s, int w, int h, float scale, bool smooth) {
    if (!view_) view_ = std::make_unique<Viewport>(ViewKind::Persp);
    view_->resize(w, h, scale);
    if (!view_->target().id || !view_->target().texture.id) {
        view_.reset();
        return false;
    }
    view_->setLens(cam);
    // The picture's own texture filtering; the editor's switch is put back after.
    const bool texWas = s.textures ? s.textures->smooth() : smooth;
    const bool modelWas = s.models ? s.models->smooth() : smooth;
    if (s.textures && texWas != smooth) s.textures->setSmooth(smooth);
    if (s.models && modelWas != smooth) s.models->setSmooth(smooth);
    Grid g;
    g.visible = false;
    view_->render(g, s);
    if (s.textures && texWas != smooth) s.textures->setSmooth(texWas);
    if (s.models && modelWas != smooth) s.models->setSmooth(modelWas);
    return true;
}

std::vector<unsigned char> Renderer::read(int& w, int& h) const {
    w = h = 0;
    if (!view_ || !view_->target().texture.id) return {};
    Image img = LoadImageFromTexture(view_->target().texture);
    if (!img.data) return {};
    if (img.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    w = img.width;
    h = img.height;
    std::vector<unsigned char> out((size_t)w * h * 4);
    // Render targets are stored bottom row first: flip to top first.
    const unsigned char* src = (const unsigned char*)img.data;
    for (int y = 0; y < h; ++y) std::memcpy(&out[(size_t)y * w * 4], &src[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
    UnloadImage(img);
    return out;
}

} // namespace scal
