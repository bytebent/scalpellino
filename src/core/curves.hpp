#pragma once
// Curve generators: curved shapes built from flat-faced convex brushes ("slices").
// Neighbouring slices are built from the very same computed points, so they share exact
// corners (no cracks, no leaks). Pure core: no graphics.
#include "core/brush.hpp"
#include "core/document.hpp" // KeyValues
#include <string>
#include <vector>

namespace scal {

enum class CurveShape { Arch, Ring, CurvedWall, SpiralStairs, CarveArch };

const char* curveShapeName(CurveShape s);       // "arch", "ring", ...
bool curveShapeFromName(const std::string& n, CurveShape& out);

// Everything needed to (re)generate a shape. Shapes are drawn in a 2D view: (u, v) are
// that view's horizontal / vertical world axes, d its depth axis.
struct CurveParams {
    CurveShape shape = CurveShape::Arch;
    int axU = 1, axV = 2, axD = 0;          // world axes (front view by default)
    double u0 = 0, v0 = 0, u1 = 128, v1 = 128; // box on the view plane (min / max)
    double d0 = 0, d1 = 16;                 // depth range
    int segments = 8;                       // slices along the curve
    double thickness = 16;                  // band (arch / ring), wall, step slab
    // Ring: sweep in degrees (0 = +u direction, counter-clockwise on screen).
    double startDeg = 0, endDeg = 360;
    // Arch: square top (fill up to the box top) and legs down to the box bottom.
    bool flatTop = true, legs = true;
    // Curved wall: from (u0, v0) to (u1, v1), bent towards control point (cu, cv).
    double cu = 64, cv = 64;
    // Spiral stairs (top view: u = x, v = y, d = z): steps around the box centre.
    double innerRadius = 16, turnDeg = 360, rise = 16;
    int steps = 16;
    bool clockwise = false;
    bool roundToWhole = true;               // round corners to whole units (clean .map)
    std::string texture = "default";
    double texScale = 0.25;
};

KeyValues curveToKeys(const CurveParams& p);
bool curveFromKeys(const KeyValues& kv, CurveParams& p);

struct CurveResult {
    // For CarveArch: `brushes` = the cutter (a plain box through the wall), and `fill` =
    // arch slices that refill the box above the curve (within the wall's depth), so the
    // result is clean: rectangular wall pieces + radial slices around the arch.
    std::vector<Brush> brushes;
    std::vector<Brush> fill;
    std::string error;
    bool ok() const { return error.empty() && !brushes.empty(); }
};

CurveResult generateCurve(const CurveParams& p);

// Moves a shape's parameters by a world offset (used when a generated group was moved
// before being edited again).
void shiftCurve(CurveParams& p, const Vec3& delta);

} // namespace scal
