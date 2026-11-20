#include "render/renderer.hpp"
#include "core/texture.hpp"
#include "render/model_library.hpp"
#include "render/sky_library.hpp"
#include "render/texture_library.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace scal {


Scene Renderer::scene(const Document& doc, TextureLibrary& textures, ModelLibrary& models, SkyLibrary& skies, Stats* stats) {
    // Brushes: only the ones an edit touched are rebuilt (SceneCache).
    auto brushes = brushes_.shared(doc);
    Stats st;
    st.nodrawFaces = brushes_.nodrawFaces();
    for (const Brush& b : doc.brushes()) {
        ++st.brushes;
        if (!doc.visible(b)) ++st.hiddenBrushes;
        if (b.terrain) ++st.terrains;
    }
    Scene s;
    s.brushes = brushes;
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
    for (const SceneBrush& b : *brushes) {
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

BakeInput bakeInput(const Document& doc, ModelLibrary& models, SkyLibrary& skies, BakeQuality quality) {
    BakeInput in;
    in.brushes = doc.brushes();
    in.lights = doc.lights();
    in.prims = doc.prims();
    in.props = doc.props();
    in.env = doc.environment();
    in.quality = quality;
    auto found = std::make_shared<std::map<std::string, const Model*>>();
    for (const Prop& p : in.props)
        if (!found->count(p.model)) (*found)[p.model] = models.find(p.model);
    in.models = [found](const std::string& n) -> const Model* {
        auto it = found->find(n);
        return it == found->end() ? nullptr : it->second;
    };
    if (!in.env.sky.empty()) in.sky = skies.picture(in.env.sky);
    return in;
}

void BakeTask::start() {
    thread = std::thread([this] {
        *result = bakeLight(in, &progress);
        finished = true;
    });
}

BakeTask::~BakeTask() {
    progress.cancel = true;
    if (thread.joinable()) thread.join();
}

void Renderer::useBake(std::shared_ptr<const BakeResult> bake, float exposure, Scene& s) {
    baked_.upload(std::move(bake));
    s.baked = baked_.ready() ? &baked_ : nullptr;
    s.exposure = exposure;
    s.lightPreview = false;
    s.lighting = nullptr;
}

void Renderer::light(const Document& doc, Scene& s) {
    s.lightPreview = true;
    s.lighting = &lighting_.get(doc, brushes_.get(doc));
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
