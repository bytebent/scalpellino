#pragma once
// Milestone 10 — props: the data of a static model (no graphics, no physics). A model is
// what a file (`.glb`, later Mazzuola's `.mazz`) holds: meshes with materials, already in
// Scalpellino's own space: centimetres, Z up, the file's origin is the pivot.
//
// The meshes are kept as plain arrays so the core can pick and land on the triangles
// headless; the renderer uploads them to the graphics card once per model and draws every
// copy from that one upload (instancing). Static meshes only: animations, skins, cameras
// and lights in a file are ignored.
#include "core/math.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace scal {

struct ModelMaterial {
    std::string name;
    Vec3 color{255, 255, 255};            // base colour factor, 0..255 (tints the texture)
    bool cutout = false;                  // alpha cutout (foliage, fences); never blended
    double cutoff = 0.5;                  // cutout: pixels below this alpha are dropped
    std::vector<unsigned char> image;     // an embedded texture: the encoded PNG / JPG bytes
    std::string imageFile;                // or a texture file next to the model (full path)
    bool hasTexture() const { return !image.empty() || !imageFile.empty(); }
    // the glTF's normal map (v0.12.4b; OpenGL's convention, green up), the same ways
    std::vector<unsigned char> normalImage;
    std::string normalImageFile;
    bool hasNormalMap() const { return !normalImage.empty() || !normalImageFile.empty(); }
};

// One mesh with one material: 3 floats per vertex position and normal, 2 per texture
// coordinate (v counted from the top of the image, as in the image file).
struct ModelMesh {
    std::vector<float> pos, normal, uv;
    std::vector<std::uint32_t> index;     // 3 per triangle
    int material = 0;                     // into Model::materials
    size_t vertices() const { return pos.size() / 3; }
    size_t triangles() const { return index.size() / 3; }
};

struct Model {
    std::string name;                     // how maps refer to it, e.g. "models/tree"
    std::string file;                     // the file it was read from
    std::vector<ModelMaterial> materials;
    std::vector<ModelMesh> meshes;
    AABB bounds;                          // of every vertex, in model space
    // What the light bake sees of it (shape and cutouts), set when it is loaded: a
    // replaced file with the same name gives another one (v0.12.6 review: the bake was
    // reused with the old model's shadows).
    std::uint64_t fingerprint = 0;
    size_t triangles() const {
        size_t n = 0;
        for (const ModelMesh& m : meshes) n += m.triangles();
        return n;
    }
    // Models above this many triangles get a warning (they are drawn once per copy).
    static constexpr size_t kTriangleBudget = 200000;
};

// Model::fingerprint worked out from its meshes and cutout materials.
std::uint64_t modelFingerprint(const Model& m);

// The box a prop shows while its model is missing (and its size when nothing else is known):
// 64 wide and deep, 64 high, standing on its origin.
AABB placeholderBounds();

// Nearest hit of a ray (o + t d, any length of d) with the model's triangles, in model
// space, both sides. `n` is the unit surface normal facing the ray. False if it misses.
bool rayHitModel(const Model& m, const Vec3& o, const Vec3& d, double& t, Vec3& n);
// The same for a box (slab test).
bool rayHitBox(const AABB& b, const Vec3& o, const Vec3& d, double& t, Vec3& n);

} // namespace scal
