#pragma once
// v0.12.5 — decals (Hammer's infodecal and info_overlay as one object, agreed 2026-11-22):
// a picture laid on surfaces (posters, cracks, stains, puddles). A decal is a rectangle
// facing `normal`, centred at `pos`, `width` x `height` (cm), turned so the picture's up
// is `up`. It is projected straight along its facing onto every surface within its box
// (the rectangle, `reach` in front of and behind it) that faces it, wrapping over edges:
// as info_overlay, on the faces infodecal would pick. Its picture's light is the light of
// the surface under it; the bake ignores decals (as Source).
//
// A decal belongs to the brush it was put on (Brush::decals): it moves, turns, sizes and
// copies with it, and goes when the brush goes. It still lands on any surface in its box,
// the brush's own or others' (brushes and terrain; not props, not math primitives yet).
//
// Frame: n = normal, up (in the surface), right = up x n (seen from the front, the
// picture's right). Picture coordinates: u along right (0 left .. 1 right), v down the
// picture (0 top .. 1 bottom).
#include "core/math.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace scal {

struct Brush;

using DecalId = std::uint32_t;

// Source's default: a decal's picture is 1 Hammer unit a texel, times $decalscale.
constexpr double kDecalUnitsPerTexel = 2.54;
// How far in front of and behind its surface a new decal reaches (cm, 4 Hammer units).
constexpr double kDefaultDecalReach = 10.16;
// Faces turned further than this from the decal (cos of 75 degrees) don't take it.
constexpr double kDecalMinFacing = 0.26;
// A decal's order is kept within these (the editor clamps; the file reader clamps too).
constexpr int kDecalOrderMin = -1000000;
constexpr int kDecalOrderMax = 1000000;
// A typed order (any number) as a valid one: rounded and clamped.
inline int decalOrderFrom(double x) {
    if (!(x == x)) return 0; // NaN
    if (x < kDecalOrderMin) return kDecalOrderMin;
    if (x > kDecalOrderMax) return kDecalOrderMax;
    return (int)(x < 0 ? x - 0.5 : x + 0.5);
}

struct Decal {
    DecalId id = 0;
    std::string material;          // texture / material name
    Vec3 pos;                      // centre, on the surface
    Vec3 normal{0, 0, 1};          // facing (out of the surface)
    Vec3 up{0, 1, 0};              // the picture's up (in the surface)
    double width = 64, height = 64; // cm
    double reach = kDefaultDecalReach;
    int order = 0;                 // drawn in order: higher on top; equal: placed later on top
    std::uint32_t visgroup = 0;    // its own visgroup (0 = none)
    bool operator==(const Decal& o) const;
    bool operator!=(const Decal& o) const { return !(*this == o); }
};

// The picture's up a new decal gets on a surface facing `n`: world up (+Z) on walls, +Y
// on floors and ceilings.
Vec3 decalDefaultUp(const Vec3& n);
// The decal's frame (unit vectors; `up` made square to the normal).
void decalFrame(const Decal& d, Vec3& right, Vec3& up, Vec3& n);
// Turn of the picture from decalDefaultUp, degrees (counter-clockwise seen from the front).
double decalRotation(const Decal& d);
void setDecalRotation(Decal& d, double degrees);
// Corners: top left, top right, bottom right, bottom left (seen from the front).
std::array<Vec3, 4> decalCorners(const Decal& d);
// World box around the decal's box (the rectangle +- reach).
AABB decalBounds(const Decal& d);
// Size from the picture: texels x $decalscale x 1 Hammer unit.
void decalSizeFromPicture(Decal& d, int texW, int texH, double decalScale);

// A point of a surface being clipped to the decal, with what it carries (texture
// coordinates, lightmap coordinates, normal...) in `a`.
struct DecalVert {
    Vec3 p;
    std::array<float, 8> a{};
};
// Clips a convex polygon to the decal's box. Empty if nothing is left.
std::vector<DecalVert> clipToDecal(const Decal& d, const std::vector<DecalVert>& poly);
// The picture's coordinates of a point.
void decalUV(const Decal& d, const Vec3& p, double& u, double& v);
// Does a surface facing `n` take the decal?
bool decalTakes(const Decal& d, const Vec3& n);
// Is the decal's picture drawn at point `p` of a surface facing `n`? (In its box, within
// reach of its plane, on a surface that takes it.) For picking where it shows.
bool decalDrawnAt(const Decal& d, const Vec3& p, const Vec3& n);

// The decal as drawn on the brushes (the editor's flat view, picking): triangles with the
// picture's coordinates and the surface's normal.
struct DecalTri {
    Vec3 p[3];
    double u[3], v[3];
    Vec3 n;
};
std::vector<DecalTri> decalMesh(const Decal& d, const std::vector<Brush>& brushes,
                                const std::function<bool(const Brush&)>& visible = {});
// The same on brushes already picked out (visible, their box overlapping decalBounds):
// for callers that keep the brushes' boxes, since working out a box clips the brush.
std::vector<DecalTri> decalMeshOn(const Decal& d, const std::vector<const Brush*>& brushes);

// Moves the decal with an affine change of the world (a brush moved, turned, sized or
// mirrored): its centre and size follow; a mirror doesn't mirror the picture.
void mapDecal(Decal& d, const std::function<Vec3(const Vec3&)>& f);

// Ray (unit `dir`) against the decal's rectangle from the front (or either side); t or -1.
double rayHitDecal(const Decal& d, const Vec3& origin, const Vec3& dir);

} // namespace scal
