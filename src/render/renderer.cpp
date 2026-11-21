#include "render/renderer.hpp"
#include "core/texture.hpp"
#include "render/model_library.hpp"
#include "render/sky_library.hpp"
#include "render/shader_library.hpp"
#include "render/texture_library.hpp"
#include <algorithm>
#include <cmath>
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
    s.shaders = shaders_;
    s.problems = &problems_;
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
    // Materials with settings (v0.12.4)
    {
        std::set<std::string> used, shaders, probs;
        auto note = [&](const std::string& name) {
            const MaterialIndex::Entry* e = textures.material(name);
            if (!e) return;
            if (!e->vmt.empty() || e->kind == MaterialIndex::Kind::Variant) used.insert(name);
            shaders.insert(e->mat.shader);
            if (!e->mat.problem.empty()) probs.insert(name + ": " + e->mat.problem);
        };
        for (const SceneBrush& b : *brushes) {
            for (const Polygon& p : b.polys)
                if (p.face >= 0 && p.face < (int)b.faces.size()) note(b.faces[p.face].texture);
            for (const auto& l : b.layers) note(l.texture);
        }
        st.materials = used.size();
        st.shaders.assign(shaders.begin(), shaders.end());
        st.materialProblems.assign(probs.begin(), probs.end());
    }

    const Environment& env = doc.environment();
    s.ambient = env.ambient;
    st.sky = env.sky;
    if (!env.sky.empty()) {
        if (const Texture2D* t = skies.get(env.sky)) {
            s.skyTexture = t->id;
            if (const Texture2D* r = skies.reflection(env.sky)) s.skyReflection = r->id;
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

BakeInput bakeInput(const Document& doc, ModelLibrary& models, SkyLibrary& skies, BakeQuality quality, TextureLibrary* textures) {
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
    // What surfaces bounce (v0.12.2): textures' average colours; props' models by their
    // materials (texture average x colour factor, by how many triangles use each).
    if (textures) {
        auto addTex = [&](const std::string& t) {
            if (t.empty() || in.reflectivity.count(t)) return;
            const Vec3 r = textures->reflectivity(t);
            if (r.x >= 0) in.reflectivity[t] = r;
        };
        for (const Brush& b : in.brushes) {
            for (const Face& f : b.faces) addTex(f.texture);
            if (b.terrain)
                for (const TerrainLayer& l : b.terrain->layers) addTex(l.texture);
        }
        for (const MathPrim& p : in.prims) addTex(p.texture);
        // Materials (v0.12.4): $reflectivity overrides the picture's average; texlights,
        // cutouts and see-through faces for the light and shadows.
        std::set<std::string> names;
        for (const Brush& b : in.brushes) {
            for (const Face& f : b.faces) names.insert(f.texture);
            if (b.terrain)
                for (const TerrainLayer& l : b.terrain->layers) names.insert(l.texture);
        }
        for (const MathPrim& p : in.prims) names.insert(p.texture);
        for (const std::string& n : names) {
            const MaterialIndex::Entry* e = textures->material(n);
            if (!e) continue;
            const Material& m = e->mat;
            Vec3 r;
            if (m.has("$reflectivity") && parseVmtVector(m.str("$reflectivity"), r)) in.reflectivity[n] = r;
            BakeMaterial bm;
            Vec3 c;
            double br = 0;
            if (m.has("%texlight") && parseTexLight(m.str("%texlight"), c, br) && br > 0) {
                auto lin = [](double v) { return std::pow(std::clamp(v, 0.0, 255.0) / 255.0, 2.2); };
                bm.emit = Vec3{lin(c.x), lin(c.y), lin(c.z)} * (br / 255.0);
            }
            bm.translucent = m.flag("$translucent") || m.num("$alpha", 1) < 1;
            bm.alphaTest = m.flag("$alphatest") && !bm.translucent;
            bm.alphaRef = m.num("$alphatestreference", 0.5);
            const bool glows = bm.emit.x > 0 || bm.emit.y > 0 || bm.emit.z > 0;
            if (!glows && !bm.translucent && !bm.alphaTest) continue;
            const TexSize ts = textures->size(n);
            bm.texW = ts.w;
            bm.texH = ts.h;
            if (bm.alphaTest) {
                int w = 0, h = 0;
                if (!textures->alpha(n, w, h, bm.alpha) || w != ts.w || h != ts.h) {
                    bm.alpha.clear();
                    bm.alphaTest = false;
                }
            }
            if (!glows && !bm.translucent && !bm.alphaTest) continue;
            in.materials[n] = std::move(bm);
        }
    }
    for (const auto& [name, m] : *found) {
        if (!m || in.modelReflectivity.count(name)) continue;
        std::vector<Vec3> matRefl(m->materials.size(), Vec3{1, 1, 1});
        for (size_t i = 0; i < m->materials.size(); ++i) {
            const ModelMaterial& mat = m->materials[i];
            Vec3 avg{1, 1, 1};
            Image img{};
            if (!mat.image.empty()) {
                const bool png = mat.image.size() > 4 && mat.image[0] == 0x89 && mat.image[1] == 'P';
                img = LoadImageFromMemory(png ? ".png" : ".jpg", mat.image.data(), (int)mat.image.size());
            } else if (!mat.imageFile.empty())
                img = LoadImage(mat.imageFile.c_str());
            if (img.data) {
                ImageResize(&img, std::min(img.width, 64), std::min(img.height, 64));
                ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
                const unsigned char* px = (const unsigned char*)img.data;
                const size_t n = (size_t)img.width * img.height;
                double acc[3] = {0, 0, 0};
                for (size_t k = 0; k < n; ++k)
                    for (int c = 0; c < 3; ++c) acc[c] += srgbToLinear(px[3 * k + c] / 255.0);
                if (n) avg = {acc[0] / n, acc[1] / n, acc[2] / n};
                UnloadImage(img);
            }
            const Vec3 f{srgbToLinear(mat.color.x / 255), srgbToLinear(mat.color.y / 255), srgbToLinear(mat.color.z / 255)};
            matRefl[i] = {avg.x * f.x, avg.y * f.y, avg.z * f.z};
        }
        Vec3 sum;
        double w = 0;
        for (const ModelMesh& mesh : m->meshes) {
            const double t = (double)mesh.triangles();
            const Vec3 r = mesh.material >= 0 && mesh.material < (int)matRefl.size() ? matRefl[mesh.material] : Vec3{kDefaultReflectivity, kDefaultReflectivity, kDefaultReflectivity};
            sum += r * t;
            w += t;
        }
        if (w > 0) in.modelReflectivity[name] = sum / w;
    }
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
    problems_.clear();
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
