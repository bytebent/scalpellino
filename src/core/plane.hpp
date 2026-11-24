#pragma once
// v0.13.0 — endless planes (milestone 13, from Bryce; agreed with the user 2026-11-24): a flat
// layer at a chosen height running to the horizon, carrying any material. What it is follows
// from its material: a Water material makes endless water (reflection, murky depth, cheap
// water far away, as water on brushes), a Clouds material a cloud layer (see-through noise
// shapes, dimming the sun in the bake), anything else a ground. Lit by the sun and the sky
// only, everywhere (the user's choice: no shadows fall on a plane, as Bryce). Saved as our
// own entity, `scal_plane` (Hammer has nothing like it). Pure data and maths.
#include "core/brush.hpp"
#include "core/material.hpp"
#include "core/math.hpp"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace scal {

using PlaneId = std::uint32_t;

struct EndlessPlane {
    PlaneId id = 0;
    double z = 0;   // its height (cm); it faces up
    // Its material and texture mapping, as a brush face's (Face::texture = the material).
    Face face;
    std::vector<std::pair<std::string, std::string>> otherKeys; // kept from loaded files
    bool operator==(const EndlessPlane& o) const;
    bool operator!=(const EndlessPlane& o) const { return !(*this == o); }
};

// A new plane at height z with material `mat` (texture axes world-aligned, as a floor's,
// Source's default scale 0.25).
EndlessPlane makePlane(double z, const std::string& mat);
// Its face with plane points at its height (for texture coordinates).
Face planeFace(const EndlessPlane& p);

enum class PlaneKind { Ground, Water, Clouds };
PlaneKind planeKind(const Material* m); // null (no material): ground
const char* planeKindName(PlaneKind k); // "ground", "water", "clouds"

// The starter materials the three quick menu items use (textures/starter/, a new Bottega).
const char* starterPlaneMaterial(PlaneKind k);

// How far a plane is drawn around the camera (cm): to the horizon, within the far clip.
constexpr double kPlaneReach = 1.0e6;
// The square drawn for a plane around (cx, cy), `half` wide each way.
std::vector<Vec3> planeSquare(double z, double cx, double cy, double half);

// ---- clouds (the Clouds shader; the user: separate from Sky Lab's clouds, only alike)
// Settings as the Clouds material's keys: $cover 0..100 (how much sky they cover),
// $cloudsize (cm, the size of a formation), $softness 0..100 (edges), $complexity 1..8
// (detail: octaves), $opacity 0..1 (the thickest part), $seed.
struct CloudParams {
    double cover = 50, size = 4000, softness = 40, opacity = 0.9;
    int complexity = 5, seed = 1;
};
CloudParams cloudParams(const Material& m);
// How thick the cloud is at (x, y): 0 (clear) .. 1 (the thickest). The GLSL twin is in
// render/shader_library.cpp (cloudThickness); both use the same integer hash, so the bake's
// shadows fall under the clouds a render shows.
double cloudThickness(const CloudParams& c, double x, double y);
// The share of the sun getting through: 1 - opacity x thickness.
double cloudTransmit(const CloudParams& c, double x, double y);
// The value noise underneath (0..1), exposed for tests: lattice hash, smooth interpolation.
double cloudNoise(double x, double y, int seed);

} // namespace scal
