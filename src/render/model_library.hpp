#pragma once
// Props in the 3D view (milestone 10): the models of the Bottega's `models/` folder, loaded
// from disk (io/model_store) and uploaded to the graphics card once each. Every prop of a
// model is drawn from that one upload (instancing): hundreds of copies cost a draw call
// per mesh, not a copy of the model.
//
// v1 shading is the editor's own (like brushes) plus the fog preview; the rough light
// preview does not reach props yet. Textures follow the Crisp / Smooth switch.
#include "raylib.h"
#include "core/prop.hpp"
#include "io/model_store.hpp"
#include "render/shader_library.hpp"
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

namespace scal {

struct BakeResult;
struct BakedDrawContext;

struct PropDrawContext {
    Vec3 camPos;
    // Renders (v0.12.3): props lit by the bake (per vertex, or like a Source model), linear,
    // times `exposure`. Null: the editor's shading.
    std::shared_ptr<const BakeResult> bake;
    float exposure = 1;
    bool fog = false;
    Vec3 fogColor;                 // 0..255
    double fogStart = 0, fogEnd = 1;
    double fogBase = 0, fogThickness = 0; // height fog (renders); thickness 0 = everywhere
    // Renders with materials (v0.12.4): props drawn by the VertexLitGeneric shader file
    // (its sky, camera and fog from here). Null: the built-in baked shader.
    const BakedDrawContext* material = nullptr;
};

class ModelLibrary {
public:
    ModelLibrary() = default;
    ~ModelLibrary();
    ModelLibrary(const ModelLibrary&) = delete;
    ModelLibrary& operator=(const ModelLibrary&) = delete;

    ModelStore& store() { return store_; }
    const ModelStore& store() const { return store_; }
    // The Bottega root ("" = none); its models/ folder is read. Rescans.
    void setBottegaRoot(const std::string& root);
    // Looks at the disk again; loaded models and their GPU copies are dropped and loaded
    // again when next used.
    void rescan();
    // Nearest (crisp, default) or smooth texture filtering, like the texture library.
    void setSmooth(bool smooth);
    bool smooth() const { return smooth_; }

    // Model for `name` (loaded on first use), or null.
    const Model* find(const std::string& name) { return store_.find(name); }
    ModelFinder finder() { return store_.finder(); }

    // Draws every prop whose model can be shown (missing ones are left to the outline
    // drawing). Needs the GL context, inside the 3D view's camera mode.
    void draw(const std::vector<Prop>& props, const PropDrawContext& ctx);
    // How many props the last draw() skipped as outside the view (for the benchmark).
    size_t culled() const { return culled_; }
    // False if the GPU refused the shader (then props show only as outlines).
    bool shaderOk();
    void unloadAll();

private:
    struct Chunk {
        Mesh mesh{};
        int material = 0;
    };
    struct Gpu {
        std::vector<Chunk> chunks;
        std::vector<Texture2D> textures; // per material; id 0 = none
        std::vector<Color> tint;         // per material
        std::vector<float> cutoff;       // per material; < 0 = no cutout
        std::vector<char> textured;      // per material: has a texture (even a broken one)
        // v0.12.4b: per material, the glTF's normal map (id 0 none) and its settings file
        std::vector<Texture2D> normals;
        std::vector<ModelMaterialSettings> settings;
        std::map<std::string, Texture2D> files;   // textures its settings name, by file
    };
    Gpu* gpuFor(const std::string& name);
    void freeGpu(Gpu& g);
    void initShader();

    ModelStore store_;
    size_t culled_ = 0;
    std::map<std::string, Gpu> gpu_;
    bool smooth_ = false;
    // The prop shader, twice: instanced (one draw call per model mesh for all its props,
    // v0.10.3) and plain (one call per prop), the fallback if a GPU refuses the first.
    struct ShaderSet {
        Shader shader{};
        ::Material material{};
        bool ok = false;
        int locHasTex = -1, locCutoff = -1, locCam = -1, locFogOn = -1, locFogColor = -1, locFogRange = -1, locFogHeight = -1;
    };
    bool loadShaderSet(ShaderSet& s, const char* vs);
    ShaderSet plain_, inst_;
    // Baked light (renders): one more shader, one draw per prop, and the props' vertex
    // light in a float texture (each prop's chunks one after another).
    struct BakedSet {
        Shader shader{};
        ::Material material{};
        bool ok = false, tried = false;
        std::map<std::string, int> locs;
        ::Material fileMaterial{}; // for the shader file's program (v0.12.4)
        bool fileMaterialMade = false;
        int at(const char* n) {
            auto it = locs.find(n);
            if (it != locs.end()) return it->second;
            return locs[n] = GetShaderLocation(shader, n);
        }
    } baked_;
    std::shared_ptr<const BakeResult> bakeSrc_;
    unsigned bakeTex_ = 0;
    std::map<std::uint32_t, std::vector<int>> bakeBase_;   // prop id -> base per chunk
    std::map<std::uint32_t, size_t> bakeIndex_;            // prop id -> its PropLighting
    std::set<std::uint32_t> bakeBumped_;                    // props with 4 values a vertex
    void prepareBake(const std::vector<Prop>& props, const std::shared_ptr<const BakeResult>& bake);
    void drawBaked(const std::vector<Prop>& props, const PropDrawContext& ctx);
    void bindPropMaterial(ShaderLibrary::Program& prog, Gpu& g, size_t mi, const BakedDrawContext& ctx);
    bool shaderTried_ = false, shaderOk_ = false;
    size_t drawCalls_ = 0;
public:
    // Draw calls made by the last draw() (for the benchmark).
    size_t drawCalls() const { return drawCalls_; }
};

} // namespace scal
