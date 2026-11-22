#pragma once
// Convex brush defined by planes (Quake/Hammer style), with Valve 220 texture axes.
#include "core/decal.hpp"
#include "core/math.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace scal {

using BrushId = std::uint32_t;

struct TexAxis {
    Vec3 axis;          // direction in world space
    double offset = 0;  // in texels
};

// Hammer's default lightmap scale, 16 Hammer units (inches), in cm, rounded (agreed 2026-11-20).
constexpr double kDefaultLightmapScale = 40;
constexpr double kMinLightmapScale = 1, kMaxLightmapScale = 512;

struct Face {
    Vec3 p[3];          // three plane points, as stored in the .map
    std::string texture = "default";
    TexAxis u{{1, 0, 0}, 0};
    TexAxis v{{0, -1, 0}, 0};
    double rotation = 0;
    double scaleU = 1, scaleV = 1;
    double lightmapScale = kDefaultLightmapScale; // baked light: units (cm) per lightmap pixel (Hammer's lightmap scale)

    Plane plane() const { return Plane::fromPoints(p[0], p[1], p[2]); }
};

// Polygon of one face, vertices counter-clockwise seen from outside.
struct Polygon {
    std::vector<Vec3> verts;
    Vec3 normal;
    int face = -1; // index into Brush::faces
};

// Terrain (milestone 7): one face of the brush becomes a grid of (2^power)+1 points per
// side, each lifted along the face normal by `heights`. Painted textures are layers over
// the face's own texture, bottom first; each has a blend value (0..1) per grid point.
// The brush itself stays an ordinary convex brush; only that face is drawn as the
// terrain surface. Functions are in core/terrain.hpp.
// Paint is a share per point: each layer's alpha is how much of that texture shows
// there (0..1); the layers' shares add up to at most 1 and the face's texture has the rest.
struct TerrainLayer {
    std::string texture;
    std::vector<double> alpha;   // same layout as heights, 0..1 (share of this texture)
    bool operator==(const TerrainLayer& o) const { return texture == o.texture && alpha == o.alpha; }
    bool operator!=(const TerrainLayer& o) const { return !(*this == o); }
};

struct Terrain {
    int face = -1;               // index into Brush::faces (that face must have 3 or 4 corners)
    int power = 3;               // 2^power cells per side
    Vec3 start;                  // grid starts at the face corner nearest this point
    std::vector<double> heights; // size()*size(), row-major: [j * size() + i]
    std::vector<TerrainLayer> layers; // painted textures, bottom first
    int size() const { return (1 << power) + 1; }
    bool operator==(const Terrain& o) const {
        return face == o.face && power == o.power && heights == o.heights && layers == o.layers &&
               length(start - o.start) < 1e-9;
    }
    bool operator!=(const Terrain& o) const { return !(*this == o); }
};

struct Brush {
    BrushId id = 0;
    std::uint32_t group = 0; // generated group (curve tool) this brush belongs to; 0 = none
    std::uint32_t visgroup = 0; // named visibility group; 0 = none
    bool hidden = false;        // hidden with "Hide selected" (until "Show all")
    std::vector<Face> faces;
    std::optional<Terrain> terrain;
    // Decals put on this brush (v0.12.5): they move, turn, size and copy with it.
    std::vector<Decal> decals;

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
    // Moves the decals with any other change of the brush's points (`f`: old -> new).
    void mapDecals(const std::function<Vec3(const Vec3&)>& f);
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
