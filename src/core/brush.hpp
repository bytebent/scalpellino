#pragma once
// Convex brush defined by planes (Quake/Hammer style), with Valve 220 texture axes.
#include "core/math.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace scal {

using BrushId = std::uint32_t;

struct TexAxis {
    Vec3 axis;          // direction in world space
    double offset = 0;  // in texels
};

struct Face {
    Vec3 p[3];          // three plane points, as stored in the .map
    std::string texture = "default";
    TexAxis u{{1, 0, 0}, 0};
    TexAxis v{{0, -1, 0}, 0};
    double rotation = 0;
    double scaleU = 1, scaleV = 1;

    Plane plane() const { return Plane::fromPoints(p[0], p[1], p[2]); }
};

// Polygon of one face, vertices counter-clockwise seen from outside.
struct Polygon {
    std::vector<Vec3> verts;
    Vec3 normal;
    int face = -1; // index into Brush::faces
};

struct Brush {
    BrushId id = 0;
    std::vector<Face> faces;

    // Computes face polygons. Faces that clip away completely produce no polygon.
    std::vector<Polygon> polygons() const;
    AABB bounds() const;
    // True if every face has a polygon and the solid is closed.
    bool valid() const;

    // Moves the brush. With texture lock, textures stay fixed to the surface.
    void translate(const Vec3& delta, bool textureLock);
    // Rotates around `center` about unit `axis` by `radians` (right-hand rule).
    void rotate(const Vec3& center, const Vec3& axis, double radians, bool textureLock);
    // Maps the brush's bounds from `from` to `to` (per-axis scale + move).
    void remapBounds(const AABB& from, const AABB& to, bool textureLock);
};

// Rotates v about unit `axis` by `radians` (Rodrigues).
Vec3 rotateVec(const Vec3& v, const Vec3& axis, double radians);

// World-aligned default texture axes for a face normal (Hammer style).
void defaultTexAxes(const Vec3& normal, Vec3& u, Vec3& v);

// Face whose plane passes through `point` with outward normal `n`.
Face makeFace(const Vec3& n, const Vec3& point, const std::string& texture);

// Axis-aligned box.
Brush makeBox(const AABB& box, const std::string& texture);

// Clips a convex polygon, keeping the part behind the plane (distance <= 0).
std::vector<Vec3> clipPolygon(const std::vector<Vec3>& poly, const Plane& plane, double eps = 1e-7);

// Ray vs brush. Returns distance along dir (unit) of the entry point, or <0 if missed.
double rayHit(const Brush& b, const Vec3& origin, const Vec3& dir);

} // namespace scal
