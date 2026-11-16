#pragma once
// Block tool primitives: each is ONE convex brush filling a box. Pure core.
#include "core/brush.hpp"
#include <optional>
#include <string>

namespace scal {

enum class Primitive { Box, Wedge, Cylinder, Cone, Pyramid, Sphere };

const char* primitiveName(Primitive p);

// `axis` = the axis the shape stands on (cylinder / cone / pyramid point along it; the
// wedge's slope rises along it). For a wedge, `slopeAxis` is the axis it slopes along.
// `sides` = edges around for cylinder / cone / sphere (sphere also uses it for rings).
// Corners are rounded to whole units when `roundToWhole`.
std::optional<Brush> makePrimitive(Primitive kind, const AABB& box, int axis, int slopeAxis,
                                   int sides, bool roundToWhole, const std::string& texture,
                                   double texScale);

} // namespace scal
