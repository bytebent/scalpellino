#pragma once
// Rebuilding brushes from their corner points (vertex editing, snap to grid).
// A brush is the convex hull of its vertices; textures are carried over from the
// source face whose normal is closest.
#include "core/brush.hpp"
#include <string>
#include <vector>

namespace scal {

// Unique corner points of a brush (cleaned of floating-point noise).
std::vector<Vec3> brushVertices(const Brush& b);

struct HullResult {
    bool ok = false;
    std::string error;
    Brush brush;
};

// Builds the convex brush through `points`. Fails if the points don't enclose a volume
// or if any point would end up inside the solid (that corner would be lost — the shape
// the user asked for is not convex). Keeps `source.id` and its textures.
// With requireAllCorners=false, inner points are simply dropped (used for previews).
HullResult brushFromVertices(const std::vector<Vec3>& points, const Brush& source,
                             bool requireAllCorners = true);

// Rounds every vertex to the grid and rebuilds. Refuses if a face would have to bend
// (split into more faces) or the brush would collapse.
HullResult snapBrushToGrid(const Brush& b, double grid);

// True if all vertices are on the grid (within 1e-6).
bool brushOnGrid(const Brush& b, double grid);

} // namespace scal
