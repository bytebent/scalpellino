#pragma once
// Milestone 9 — math primitives (Bryce-style): exact shapes, not brushes. A sphere stays
// perfectly round at any zoom because it is drawn from its equation, not from polygons.
// They sit beside brushes (which stay as they are): brush tools don't touch them.
//
// Each primitive is a unit shape inside the cube [-1, 1]^3 of its own axes, placed by
// position, rotation and size (full extents along its own axes):
//   world = pos + R * (size / 2 * local)
// Rotation: degrees about X, then Y, then Z (world axes): R = Rz * Ry * Rx.
// (A cube's rounded corners are measured in units, so they stay round when stretched.)
//
// Combining (Bryce): primitives with the same `combo` number form one shape. Add pieces
// are joined, Subtract pieces are cut away from them, and every Intersect piece keeps
// only what is inside it. A primitive on its own (combo 0) is just itself.
//
// No graphics dependencies: ray hits, outlines and bounds here are used for picking,
// drawing outlines in the 2D views and tests; the 3D view does the same maths per pixel
// in a shader (render/mathprim_shader).
#include "core/math.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace scal {

using MathPrimId = std::uint32_t;
enum class PrimKind { Sphere, Cube, Cylinder, Cone, Torus, Capsule, Disc };
constexpr int kPrimKinds = 7;
const char* primKindName(PrimKind k);   // "sphere", "cube", ...
std::optional<PrimKind> primKindFromName(const std::string& s);

enum class PrimOp { Add, Subtract, Intersect };
const char* primOpName(PrimOp o);       // "add", "subtract", "intersect"

struct MathPrim {
    MathPrimId id = 0;
    PrimKind kind = PrimKind::Sphere;
    Vec3 pos;
    Vec3 rot;                      // degrees about X, Y, Z
    Vec3 size{64, 64, 64};         // full extents along its own axes
    double round = 0;              // cube: corner rounding radius in units
    double thickness = 0.25;       // torus: tube radius as a share of the outer radius (0.05..0.5)
    PrimOp op = PrimOp::Add;       // only matters in a combination
    std::uint32_t combo = 0;       // 0 = on its own
    std::string texture;           // "" = flat colour
    Vec3 color{200, 200, 200};     // 0..255 (tints the texture too)
    double texScale = 1;           // texture repeats over the shape
    std::vector<std::pair<std::string, std::string>> otherKeys; // kept from loaded files
    bool operator==(const MathPrim& o) const;
    bool operator!=(const MathPrim& o) const { return !(*this == o); }
};

// Rotation matrix (columns = the primitive's axes in world space) and back.
struct Mat3 {
    std::array<Vec3, 3> col{Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
    Vec3 operator*(const Vec3& v) const { return col[0] * v.x + col[1] * v.y + col[2] * v.z; }
    Vec3 transposeMul(const Vec3& v) const { return {dot(col[0], v), dot(col[1], v), dot(col[2], v)}; }
};
Mat3 operator*(const Mat3& a, const Mat3& b);
Mat3 rotationMatrix(const Vec3& rotDeg);
Vec3 rotationAngles(const Mat3& r);          // degrees, tidy (45 stays 45)
Mat3 axisRotation(int axis, double degrees); // about a world axis (right-hand rule)
// Turns the primitive about a world axis through `pivot` (its own centre for rings).
void rotatePrim(MathPrim& p, int axis, double degrees, const Vec3& pivot);
void rotatePrim(MathPrim& p, int axis, double degrees);

// Where a point/direction of the unit shape is in the world, and back.
Vec3 primToWorld(const MathPrim& p, const Vec3& local);
Vec3 primToLocal(const MathPrim& p, const Vec3& world);

// A stretch of a ray (o + t*d) inside a shape; normals face out of the shape.
struct RayInterval {
    double t0, t1;
    Vec3 n0, n1;
    MathPrimId prim0 = 0, prim1 = 0; // which primitive's surface each end is on
};
// Every stretch of the ray inside one primitive (most give 0 or 1, a torus up to 2,
// a disc a zero-length one). `d` should be unit length.
std::vector<RayInterval> primIntervals(const MathPrim& p, const Vec3& o, const Vec3& d);

struct PrimHit {
    double t;
    Vec3 normal;
    MathPrimId prim;    // the primitive whose surface was hit
    MathPrimId picked;  // what a click selects: the primitive, or the first of its combination
};
// First surface hit (t >= 0) of `prims`, combinations combined. Hits from inside a solid
// are skipped (like brushes seen from inside).
std::optional<PrimHit> rayHitPrims(const std::vector<MathPrim>& prims, const Vec3& o, const Vec3& d);
// The stretches of the ray inside one combination (members listed in any order).
std::vector<RayInterval> comboIntervals(const std::vector<const MathPrim*>& members, const Vec3& o, const Vec3& d);

// World bounding box (of the primitive's own box, rotated).
AABB primBounds(const MathPrim& p);
// Outline curves for 2D views and selection (world polylines; `closed` loops).
struct Polyline { std::vector<Vec3> pts; bool closed = false; };
std::vector<Polyline> primOutline(const MathPrim& p, int segments = 48);

// Combination helpers.
std::vector<const MathPrim*> comboMembers(const std::vector<MathPrim>& all, std::uint32_t combo);
std::uint32_t newComboId(const std::vector<MathPrim>& all);

} // namespace scal
