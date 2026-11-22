#pragma once
// v0.12.6 — reflection probes: Hammer's env_cubemap. Placed by hand; Render builds a small
// picture of the surroundings from each one ("building cubemaps"), and shiny materials
// ($envmap env_cubemap) reflect them. Pure data and maths; building and drawing live in
// render/probes.*.
//
// How Source does it (VBSP's cubemap.cpp, SDK 2013): `cubemapsize` 0 = the default 32 a
// side, otherwise 2^(n-1); with no env_cubemap in the map, shiny faces get the skybox as a
// 32-a-side cubemap; a face takes the probe nearest its centre, preferring probes in front of
// it (no visibility test). Scalpellino BLENDS instead (agreed with the user, 2026-11-22):
// every shaded point takes its 4 nearest probes (see probeWeights).
#include "core/math.hpp"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace scal {

using ProbeId = std::uint32_t;

struct Probe {
    ProbeId id = 0;
    Vec3 pos;
    int size = 0; // Hammer's cubemapsize: 0 = default (32 a side), n = 2^(n-1) a side, up to 9 (256)
    std::vector<std::pair<std::string, std::string>> otherKeys; // kept from loaded files
    bool operator==(const Probe& o) const { return id == o.id && pos == o.pos && size == o.size && otherKeys == o.otherKeys; }
    bool operator!=(const Probe& o) const { return !(*this == o); }
};

constexpr int kProbeDefaultSide = 32;  // VBSP's DEFAULT_CUBEMAP_SIZE
constexpr int kProbeMaxSize = 9;       // Hammer's list ends at 256 x 256
constexpr double kProbeFloorHeight = 170;  // cm above a floor clicked in 3D (the user's height)
constexpr double kProbeWallDistance = 81.28; // cm out from a wall or ceiling (32 Hammer units)

// Pixels a side of a probe's picture for its `size` key.
int probeSide(int size);
// Hammer's choice list: "Default (32 x 32)", "1 x 1" ... "256 x 256".
std::string probeSizeLabel(int size);
// Render quality (0 Draft, 1 Normal, 2 High) scales the side: Draft half, Normal as set,
// High double (at most 512). At least 4 pixels (a 1 x 1 or 2 x 2 probe is drawn as 4 x 4).
int probeSideForQuality(int size, int quality);
// Where a 3D click on a surface puts a probe: 170 cm above a floor (normal mostly up),
// else 81 cm (32 Hammer units) out from the wall or ceiling.
Vec3 probePlacement(const Vec3& hit, const Vec3& normal);

// Blending (the user's choice): the 4 nearest probes, each weighed 1 / (d^2 + soft^2), a probe
// behind the surface (dot(probe - p, n) < 0) times kProbeBehind; the 5th largest weight is
// taken off the others, so a probe fades out smoothly as another one replaces it. Weights
// add up to 1. The shader does exactly this (render/shader_library.cpp, probeEnv).
constexpr double kProbeSoft = 16;      // cm: keeps the weight finite right at a probe
constexpr double kProbeBehind = 0.05;  // a probe behind the surface counts this much
struct ProbeWeight {
    size_t index = 0;
    double weight = 0;
};
std::vector<ProbeWeight> probeWeights(const std::vector<Vec3>& probes, const Vec3& p, const Vec3& n);

// The cube's faces as OpenGL orders them: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z. A direction's
// face and its place on it (x, y in -1..1; y grows down the face's picture), and back.
int cubeFace(const Vec3& d, double& x, double& y);
Vec3 cubeDir(int face, double x, double y);

} // namespace scal
