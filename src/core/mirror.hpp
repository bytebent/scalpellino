#pragma once
// v0.12.7 — mirrors (Source's LightmappedReflective): the material makes a brush face a mirror
// (no brush entity, agreed with the user, batch 8). A render draws the scene again from a
// mirrored camera for each flat mirror plane in view (up to kMaxMirrors, the ones covering the
// most of the picture first; Source: only the first one in view), and what is behind the glass
// for planes whose material sees through. Pure maths; drawing is in render/mirrors.*.
//
// Source (SDK 2013, CReflectiveGlassView): the camera's position, forward and up are
// reflected in the plane; the picture is drawn with a clip plane at the glass (0.1 units off);
// the see-through picture is drawn from the camera itself, clipped to what is behind the plane.
// The mirrored camera's picture is the reflection flipped left to right, so the mirror reads
// it at (1 - x, y) of its own place on the screen.
#include "core/camera.hpp"
#include "core/material.hpp"
#include "core/math.hpp"
#include <vector>

namespace scal {

constexpr size_t kMaxMirrors = 8;
constexpr double kMirrorMinCover = 0.0005; // planes covering less of the picture reflect the probes

// What a LightmappedReflective material needs (Source's $reflecttexture / $refracttexture,
// defaults _rt_WaterReflection / _rt_WaterRefraction: on; set to anything not starting with
// "_rt_" (or empty): off). Refraction is also off when $minreflectivity >= 1 (the Fresnel
// mix then never shows it).
enum MirrorParts { kMirrorReflect = 1, kMirrorRefract = 2, kMirrorBase = 4 };
bool isReflectiveMaterial(const Material& m);
int mirrorParts(const Material& m); // 0 when not LightmappedReflective

// The mirrored camera (Source's CReflectiveGlassView::AdjustView): plane dot(n, p) = d.
Camera mirrorCamera(const Camera& c, const Vec3& n, double d);

// A mirror face as the renderer sees it: its polygon, its plane (normal out of the brush) and
// the parts its material needs.
struct MirrorFace {
    std::vector<Vec3> verts;
    Vec3 n;
    double d = 0;
    int parts = 0;
};
struct MirrorPlane {
    Vec3 n;
    double d = 0;
    int parts = 0;
    double cover = 0; // how much of the picture its faces cover (0..1)
};
// The planes worth drawing for camera `c`: faces in view, with the camera in front of them,
// covering at least kMirrorMinCover of the picture together,
// merged by plane (normals within about 0.8 degrees, distances within 0.5 cm), the largest
// cover first. At most `limit`; `dropped` gets how many more there were.
std::vector<MirrorPlane> mirrorPlanes(const std::vector<MirrorFace>& faces, const Camera& c, size_t limit, size_t* dropped = nullptr);
// The share of camera c's picture polygon `verts` covers (0..1; clipped to the picture).
double pictureCover(const std::vector<Vec3>& verts, const Camera& c);
// Whether a polygon with normal n through d is on mirror plane `p`.
bool onMirrorPlane(const MirrorPlane& p, const Vec3& n, double d);

} // namespace scal
