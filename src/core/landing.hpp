#pragma once
// Snap to Land / Snap to Ground (v0.9.2, from Bryce): drop an object straight down
// until it rests on what is below it. Objects are primitives (one, or a whole
// combination, which lands as one) and, later, props. Brushes are never moved by it,
// but objects land on them (and on terrain and other primitives). Hidden brushes and
// the object itself are left out by the caller.
#include "core/brush.hpp"
#include "core/mathprim.hpp"
#include <optional>
#include <vector>

namespace scal {

// Points on the object's outer surface (members' surfaces, kept where they are on the
// combined shape; pulled onto it for blended combinations).
std::vector<Vec3> objectSurfaceSamples(const std::vector<const MathPrim*>& object, int around = 32, int up = 16);

// How far the object must move along Z (negative = down) to rest on the obstacles below
// it, touching but not inside them; nullopt if nothing is below it.
std::optional<double> landingMove(const std::vector<const MathPrim*>& object, const std::vector<const Brush*>& brushes,
                                  const std::vector<const MathPrim*>& otherPrims);

// The object's lowest point (Z) on its surface.
double objectBottom(const std::vector<const MathPrim*>& object);

} // namespace scal
