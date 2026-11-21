#pragma once
// Baked light (milestone 12, v0.12.1): direct light and shadows, done the way Source's
// VRAD does it (formulas checked against the Source SDK 2013 code; PLAN.md "VRAD
// reference"). No graphics: the renderer uploads what this makes.
//  - Lightmaps on every drawn brush face (a grid of luxels along the face's texture axes,
//    `Face::lightmapScale` cm apart) and over each terrain face (like a displacement).
//  - A small 3D lightmap around each primitive (or combination): cells near the surface
//    are lit at the nearest surface point; the shader reads it where its ray hits.
//  - Light: point, spot (VRAD's falloff, cones and exponent), the sun (with its spread for
//    soft shadows) and sky light (162 directions, cosine weighted; colour from the sky
//    picture). Shadows from brushes (nodraw ones too), terrain, primitives and props that
//    cast shadows. Smooth normals across faces less than `smoothAngle` apart.
//  - Source's numbers are in Hammer units (inches): distances are converted (cm / 2.54).
//  - Bounces (v0.12.2): VRAD's radiosity. Surfaces are cut into patches (4 luxels), each
//    patch's direct light is passed between patches that see each other (form factors,
//    gathered until a bounce adds less than 1/255), and blended back onto the lightmaps.
//    Primitives and props reflect light too (patches of their own; Source doesn't), and
//    primitives gather it into their 3D lightmaps (VRAD's ambient-cube gather).
//  - Props (v0.12.3): baked per vertex (VRAD's -StaticPropLighting: direct light with
//    shadows, sky light, and the bounce gathered from the patches), or "like a Source
//    model": the nearest sample of the light grid (ambient cubes) plus the 2 strongest
//    lights, drawn with Half-Lambert; the other lights are folded into its cube.
//  - The light grid (v0.12.3): ambient cubes every 48 x 48 x 96 Hammer units over the map
//    (Half-Life 2's grid), each from 162 rays: what the surfaces hit reflect, and the sky.
//  - Light is linear: 1 = the texture as it is (VRAD's 255).
#include "core/brush.hpp"
#include "core/mathprim.hpp"
#include "core/prop.hpp"
#include "core/world.hpp"
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace scal {

constexpr double kCmPerHammerUnit = 2.54;

// The map's sky as linear colours, for the sky light: an equirectangular panorama (rows top
// first, the middle column facing +X, as core/sky.hpp).
struct SkyPicture {
    int w = 0, h = 0;
    std::vector<float> rgb;
    bool valid() const { return w > 0 && h > 0 && rgb.size() == (size_t)w * h * 3; }
    Vec3 at(const Vec3& dir) const; // bilinear; dir need not be unit
};
// sRGB byte <-> linear.
double srgbToLinear(double c01);
double linearToSrgb(double lin01);
// A panorama (RGB bytes, rows top first) made small and linear for the sky light.
SkyPicture makeSkyPicture(const unsigned char* rgb, int w, int h, int outW = 128);

enum class BakeQuality { Draft, Normal, High };

struct BakeInput {
    std::vector<Brush> brushes;  // all of them (hidden ones too: hiding is an editing aid)
    std::vector<Light> lights;
    std::vector<MathPrim> prims;
    std::vector<Prop> props;
    ModelFinder models;          // for props' triangles (shadows)
    Environment env;
    SkyPicture sky;              // invalid: the ambient colour lights from every open direction
    BakeQuality quality = BakeQuality::Normal;
    // Bounces: how much of the light each texture reflects, and in what colour (its average
    // colour, linear), by texture name; a prop's by model name. Missing: kDefaultReflectivity.
    std::map<std::string, Vec3> reflectivity;
    std::map<std::string, Vec3> modelReflectivity;
    int maxBounces = -1;         // -1: as the quality says (Draft 10, else VRAD's 100)
};
constexpr double kDefaultReflectivity = 0.2;

// One face's lightmap. Plain faces: a point P is at luxel (s, t) with
//   s = dot(axisS, P) - s0, t = dot(axisT, P) - t0 (luxel i's centre at s = i).
// Terrain (terrain = true): luxel (s, t) = (i / (n - 1) * (w - 1), j / (n - 1) * (h - 1))
// for grid point (i, j).
struct FaceLightmap {
    BrushId brush = 0;
    int face = -1;
    bool terrain = false;
    int w = 0, h = 0;
    std::vector<float> rgb;      // linear, rows of w luxels
    Vec3 axisS, axisT;           // world direction / lightmap scale (luxels per cm)
    double s0 = 0, t0 = 0;
};

// A primitive's (or combination's) 3D lightmap: cell (i, j, k) is centred at
// origin + (i, j, k) * cell.
struct PrimLightmap {
    std::uint32_t combo = 0;     // the combination, or 0 for a lone primitive:
    MathPrimId prim = 0;         //   then this one
    Vec3 origin;
    double cell = 40;
    int nx = 0, ny = 0, nz = 0;
    std::vector<float> rgb;      // linear, x fastest, then y, then z
};

// One light as a model gets it when drawn (Source's 2 strongest lights), with the falloff
// terms VRAD uses (distances in Hammer units).
struct ModelLight {
    LightKind kind = LightKind::Point;
    Vec3 pos, dir, intensity;
    double c = 0, l = 0, q = 1, startFade = 0, endFade = -1, capDist = 1e22;
    double stopdot = 1, stopdot2 = 1, exponent = 0;
};

// A prop's light. Baked: per mesh, per model vertex, linear RGB. Like a Source model:
// where it was measured, its ambient cube (+x, -x, +y, -y, +z, -z) and up to 2 lights.
struct PropLighting {
    PropId id = 0;
    bool modelStyle = false;
    std::vector<std::vector<float>> vertexRgb;
    Vec3 origin;
    Vec3 cube[6];
    std::vector<ModelLight> lights;
};

// Half-Life 2's grid of ambient cubes: sample (i, j, k) at origin + (i, j, k) * spacing;
// samples inside brushes or primitives are not valid.
struct LightGrid {
    Vec3 origin, spacing;
    int nx = 0, ny = 0, nz = 0;
    std::vector<char> valid;
    std::vector<Vec3> cube;      // 6 per sample
    size_t samples() const { return (size_t)nx * ny * nz; }
    bool empty() const { return samples() == 0; }
    Vec3 at(int i, int j, int k) const { return origin + Vec3{i * spacing.x, j * spacing.y, k * spacing.z}; }
    int nearest(const Vec3& p) const; // the nearest valid sample, -1 if none
};
// Source's ambient cube read along normal n: n.x^2 A[x+/-] + n.y^2 A[y+/-] + n.z^2 A[z+/-].
Vec3 ambientCubeAt(const Vec3* cube, const Vec3& n);

struct BakeResult {
    std::vector<FaceLightmap> faces;
    std::vector<PrimLightmap> prims;
    std::vector<std::string> notes; // for the render log (warnings, lights not used...)
    std::vector<std::string> bounces; // VRAD's "Bounce #n added RGB(r, g, b)" lines
    size_t luxels = 0, cells = 0, triangles = 0;
    size_t patches = 0, transfers = 0;
    std::vector<PropLighting> props; // every prop with a model, in input order
    LightGrid grid;
    size_t propVertices = 0;
    double secondsProps = 0, secondsGrid = 0;
    double secondsDirect = 0, secondsTransfers = 0, secondsBounce = 0; // where the time went
    double seconds = 0;
    bool cancelled = false;
};

// Progress of a bake that runs on other threads (the render window shows it; Esc stops it).
struct BakeProgress {
    std::atomic<long long> done{0}, total{1};
    std::atomic<bool> cancel{false};
};

// Bakes on all of the machine's threads (`threads` 0) or as many as asked.
BakeResult bakeLight(const BakeInput& in, BakeProgress* progress = nullptr, int threads = 0);

// Hash of everything the bake depends on (the renderer keeps a bake while it is the same).
std::uint64_t bakeKey(const BakeInput& in);

// ---- VRAD's pieces, exposed for tests

// A light as VRAD uses it: linear intensity (1 = full), attenuation terms (Hammer units),
// fade distances, spot cone cosines.
struct BakeLight {
    LightKind kind = LightKind::Point;
    Vec3 pos, dir;               // dir: where a spot / the sun points
    Vec3 intensity;              // linear
    double c = 0, l = 0, q = 1;  // 1 / (c + l d + q d^2), d in Hammer units
    double startFade = 0, endFade = -1, capDist = 1e22; // Hammer units; end <= start: none
    double stopdot = 1, stopdot2 = 1, exponent = 0;      // spot
    double spreadSin = 0;        // sun
    std::vector<std::string> notes;
};
BakeLight prepareLight(const Light& l, const Environment& env);
// Distance falloff times spot cone at distance `distCm` from the light; `toSample` is the
// unit direction from the light to the point (spots).
double lightFalloff(const BakeLight& L, double distCm, const Vec3& toSample);
// VRAD's mathlib: a, b, c with 1 / (a x^2 + b x + c) through three points, shifted toward a
// straight line until it is monotonic.
bool solveInverseQuadraticMonotonic(double x1, double y1, double x2, double y2, double x3, double y3, double& a, double& b, double& c);
// VRAD's sky / sun sample directions (Halton, bases 2 and 3, from element 1).
std::vector<Vec3> haltonSphere(int n);

} // namespace scal
