#pragma once
// Snap to Land / Snap to Ground (v0.9.2, from Bryce): drop an object straight down
// until it rests on what is below it. Objects are primitives (one, or a whole
// combination, which lands as one) and props (v0.10.0). Brushes are never moved by it,
// but objects land on them (and on terrain, other primitives and props). Hidden brushes
// and the object itself are left out by the caller.
#include "core/brush.hpp"
#include "core/document.hpp"
#include "core/mathprim.hpp"
#include "core/prop.hpp"
#include <optional>
#include <vector>

namespace scal {

// Points on the object's outer surface (members' surfaces, kept where they are on the
// combined shape; pulled onto it for blended combinations).
std::vector<Vec3> objectSurfaceSamples(const std::vector<const MathPrim*>& object, int around = 32, int up = 16);

// A prop with its model (null = the missing-model box).
struct PropRef {
    const Prop* prop = nullptr;
    const Model* model = nullptr;
};

// How far the object must move along Z (negative = down) to rest on the obstacles below
// it, touching but not inside them; nullopt if nothing is below it.
std::optional<double> landingMove(const std::vector<const MathPrim*>& object, const std::vector<const Brush*>& brushes,
                                  const std::vector<const MathPrim*>& otherPrims,
                                  const std::vector<PropRef>& otherProps = {});
// The same for a prop (its triangles rest on the obstacles).
std::optional<double> propLandingMove(const PropRef& prop, const std::vector<const Brush*>& brushes,
                                      const std::vector<const MathPrim*>& otherPrims,
                                      const std::vector<PropRef>& otherProps);

// The object's lowest point (Z) on its surface; none when nothing of it is left (a
// combination cut away entirely).
std::optional<double> objectBottom(const std::vector<const MathPrim*>& object);

// Snap to Land for the document's selected primitives and props (moved here from the
// editor in v0.10.3): each lands on the visible brushes, shown primitives and props below
// it. Several land lowest first; one lands on another selected object only if it started
// entirely above it (stacks stay stacks, overlapping copies don't pile up). The result
// holds the moved ones (to apply as one command) and how many had nothing below them.
struct LandingResult {
    std::vector<MathPrim> prims;
    std::vector<Prop> props;
    int nothingBelow = 0;
};
LandingResult landSelection(const Document& doc, const ModelFinder& models);

} // namespace scal
