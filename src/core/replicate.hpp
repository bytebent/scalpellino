#pragma once
// Multi-Replicate (v0.9.2, from Bryce): copies of the selection, each one step further
// than the one before. Brushes only move (rotation and scale are left out for them, so
// they stay on the grid); primitives and props also turn and scale; lights move and turn.
#include "core/brush.hpp"
#include "core/mathprim.hpp"
#include "core/prop.hpp"
#include "core/world.hpp"
#include <vector>

namespace scal {

struct ReplicateSpec {
    int copies = 4;
    Vec3 offset{100, 0, 0};          // added per copy
    Vec3 rotate;                     // degrees per copy about X, then Y, then Z (objects)
    Vec3 scale{100, 100, 100};       // % per copy (primitives)
    bool scaleOffset = false;        // each step's offset is scaled too (Bryce's "scale translation")
    enum class Pivot { Selection, Origin, Point } pivot = Pivot::Selection;
    Vec3 point;                      // Pivot::Point
    // Selection: turn and scale about the selection's centre, which travels with the
    // copies; Origin / Point: about a fixed point (radial patterns).
};

struct ReplicateCopy {
    std::vector<Brush> brushes;
    std::vector<MathPrim> prims;
    std::vector<Light> lights;
    std::vector<Prop> props;
};

// One entry per copy (in order). Ids, groups and combinations are the originals'; the
// caller gives them new ones. `models` gives props' boxes for the selection centre.
std::vector<ReplicateCopy> replicate(const std::vector<Brush>& brushes, const std::vector<MathPrim>& prims,
                                     const std::vector<Light>& lights, const ReplicateSpec& spec,
                                     const std::vector<Prop>& props = {}, const ModelFinder& models = {});

} // namespace scal
