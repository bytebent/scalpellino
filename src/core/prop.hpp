#pragma once
// Milestone 10 — props: a static model (core/model.hpp) placed in the map. An object like
// a light or a math primitive: moved, turned and sized with the shared object handles,
// dropped with Snap to Land, copied with Multi-Replicate. No physics, no behaviour.
//
//   world = pos + R * (scale * local)       (local = the model's own space: cm, Z up)
// R is the same rotation as for math primitives (degrees about X, then Y, then Z:
// R = Rz * Ry * Rx). `scale` is per axis, 1 = the model as it is; the editor makes it even
// on all three axes unless asked otherwise.
//
// A prop stores only the model's name (e.g. "models/tree"), never its data, so a map still
// opens when the file is missing: the prop then shows as a box and is kept on save.
#include "core/mathprim.hpp"
#include "core/model.hpp"
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace scal {

using PropId = std::uint32_t;

struct Prop {
    PropId id = 0;
    std::string model;             // "models/tree" (Bottega path, no extension), "" = none
    Vec3 pos;                      // where the model's origin is
    Vec3 rot;                      // degrees about X, Y, Z
    Vec3 scale{1, 1, 1};           // per axis, 1 = as made
    bool castShadows = true;       // in the baked light (Source's disableshadows off)
    bool modelLighting = false;    // lit "like a Source model" (ambient cube + 2 lights) instead of baked per vertex (Source's disablevertexlighting)
    std::vector<std::pair<std::string, std::string>> otherKeys; // kept from loaded files
    bool operator==(const Prop& o) const;
    bool operator!=(const Prop& o) const { return !(*this == o); }
};

// Finds the loaded model of a name; null if there is none (not found, or failed to load).
using ModelFinder = std::function<const Model*(const std::string&)>;

// The model's box in its own space, or the placeholder box when there is no model.
AABB propLocalBounds(const Model* m);

Vec3 propToWorld(const Prop& p, const Vec3& local);
Vec3 propToLocal(const Prop& p, const Vec3& world);
// The eight corners of the model's box in the world.
std::vector<Vec3> propCorners(const Prop& p, const Model* m);
// World box around the turned box.
AABB propBounds(const Prop& p, const Model* m);
// A few world points on the model's surface: its vertices (thinned to about `limit`),
// or the corners of the placeholder box.
std::vector<Vec3> propSurfacePoints(const Prop& p, const Model* m, size_t limit = 3000);
// Lowest point (Z) of the model.
double propBottom(const Prop& p, const Model* m);

struct PropHit {
    double t;
    Vec3 normal;
    PropId prop;
};
// First hit of the ray (o + t d, `d` unit length) with this prop's triangles (its box when
// the model is missing), t >= 0.
std::optional<PropHit> rayHitProp(const Prop& p, const Model* m, const Vec3& o, const Vec3& d);
// First hit with any of the props.
std::optional<PropHit> rayHitProps(const std::vector<Prop>& props, const ModelFinder& models, const Vec3& o,
                                   const Vec3& d);

// Turns the prop about a world axis through `pivot`.
void rotateProp(Prop& p, int axis, double degrees, const Vec3& pivot);
void rotateProp(Prop& p, int axis, double degrees);

} // namespace scal
