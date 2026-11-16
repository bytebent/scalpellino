#pragma once
// Brush splitting, carving and hollowing. Pure core: no graphics.
#include "core/brush.hpp"
#include <optional>
#include <string>
#include <vector>

namespace scal {

// Volume of a (valid) brush.
double brushVolume(const Brush& b);

// Drops faces that don't contribute a polygon (left over after clipping).
void removeUnusedFaces(Brush& b);

// A face lying on `plane` with outward normal plane.n, given three points on it.
// The points are reordered as needed so the face points the right way.
Face faceFromPoints(Vec3 a, Vec3 b, Vec3 c, const Vec3& outward, const std::string& texture);

struct SplitResult {
    std::optional<Brush> front; // part on the side `plane.n` points to
    std::optional<Brush> back;  // the other part
};

// Splits `b` by the plane through a, b, c (normal n = cross(c - a, b - a), Quake style).
// Each side is empty if nothing (or only a sliver) of the brush is there. The new cap
// faces get `capTexture`. Both parts keep b.id (callers assign new ids as needed).
SplitResult splitBrush(const Brush& b, const Vec3& a, const Vec3& bpt, const Vec3& c,
                       const std::string& capTexture);

// Subtracts convex `cutter` from `b`. Returns the remaining pieces (convex, no slivers);
// returns {b} unchanged if they don't overlap, and {} if `b` is entirely inside.
std::vector<Brush> carveBrush(const Brush& b, const Brush& cutter);

// Volume shared by two brushes (0 if they only touch or are apart). The overlap of
// two convex brushes is the brush bounded by both sets of planes.
double overlapVolume(const Brush& a, const Brush& b);

struct Overlap {
    BrushId a = 0, b = 0;
    double volume = 0;
    AABB region; // bounds of the shared part
};
// All pairs of brushes that cut into each other by more than `minVolume`.
std::vector<Overlap> findOverlaps(const std::vector<Brush>& brushes, double minVolume = 0.01);

// Turns `b` into walls of the given thickness around a hollow inside (Hammer "hollow").
// Fails (empty + error) if the brush is too thin for that thickness.
std::vector<Brush> hollowBrush(const Brush& b, double thickness, std::string& error);

} // namespace scal
