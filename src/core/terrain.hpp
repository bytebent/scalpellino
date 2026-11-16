#pragma once
// Terrain (milestone 7). A terrain is one 4-cornered face of a brush turned into a grid
// of points that can be lifted along the face normal (sculpting) and blended between two
// textures (painting). The data lives in Brush::terrain (see core/brush.hpp).
//
// Neighbouring patches stay stitched: every operation works on "shared points" — grid
// points of different patches at the same spot (same flat position and same normal)
// are treated as one point, so their heights and blend always match.
#include "core/brush.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace scal {

constexpr int kMinTerrainPower = 1; // 2 x 2 cells
constexpr int kMaxTerrainPower = 5; // 32 x 32 cells

// The terrain's points, ready to draw or test.
struct TerrainGrid {
    int n = 0;                  // points per side
    int face = -1;
    Vec3 normal;                // outward face normal (heights go this way)
    std::array<Vec3, 4> corners; // c0 (start), c1, c2, c3 counter-clockwise from outside
    std::vector<Vec3> base;     // flat points on the face, [j * n + i]
    std::vector<Vec3> pos;      // base + normal * height
    std::vector<double> alpha;
    Vec3 at(int i, int j) const { return pos[(size_t)j * n + i]; }
};

// The 4 corners of `face`, counter-clockwise from outside, starting at the corner nearest
// `start`; nullopt if the face doesn't have exactly 4 corners.
std::optional<std::array<Vec3, 4>> faceQuad(const Brush& b, int face, const Vec3& start);

// Grid of b's terrain; nullopt if b has none or it no longer fits the brush (the face
// lost its 4 corners after an edit) — then the face is drawn as a plain face.
std::optional<TerrainGrid> terrainGrid(const Brush& b);
bool terrainValid(const Brush& b);

// Turns `face` into a flat terrain with 2^power cells per side. If b already has a
// terrain on that face, it is resampled to the new power (shape and paint kept).
// Returns an error message, or "" on success.
std::string makeTerrain(Brush& b, int face, int power);

// The face to use for "make terrain" on a whole brush: the one facing most upward (+Z)
// among faces with 4 corners, or -1.
int topQuadFace(const Brush& b);

// Mirrors the grid along its diagonal (used when a brush is mirrored).
void transposeTerrain(Terrain& t);

// ---- editing

enum class SculptMode { Raise, Lower, Smooth, Flatten, Noise, Paint, Erase };
enum class Falloff { Soft, Linear, Hard };
const char* sculptModeName(SculptMode m);
const char* falloffName(Falloff f);

struct SculptDab {
    SculptMode mode = SculptMode::Raise;
    Falloff falloff = Falloff::Soft;
    Vec3 center;            // where the dab hits the surface
    double radius = 128;    // measured along the face (flat distance)
    double amount = 8;      // units per dab: raise, lower, noise
    double strength = 0.5;  // 0..1 per dab: smooth, flatten, paint, erase
    Vec3 flattenTarget;     // flatten: points move toward the plane through this point
    unsigned seed = 1;      // noise pattern
    std::string texture;    // paint: the patch's second texture becomes this
};

// Falloff weight for a flat distance d within radius r (1 at the centre, 0 at the rim).
double falloffWeight(Falloff f, double d, double r);

// Applies one dab to every terrain in `brushes` (in place). Returns true if anything changed.
bool sculpt(std::vector<Brush>& brushes, const SculptDab& dab);

// Makes shared points of neighbouring patches equal (average). Returns how many shared
// points were changed.
int stitchTerrain(std::vector<Brush>& brushes);

// Sets heights from a grey image (row-major, top row first, values 0..1) laid over the
// combined area of all terrains in `brushes` (seen along the first terrain's normal;
// image up = +Y in the top view). Height = low + grey * (high - low).
bool applyHeightmap(std::vector<Brush>& brushes, const std::vector<float>& grey, int w, int h,
                    double low, double high);

// Ray vs a terrain grid (both sides). Returns the distance along `dir` (unit), or <0.
double rayHitGrid(const TerrainGrid& g, const Vec3& origin, const Vec3& dir);
// Ray vs b's terrain surface. Returns the distance along `dir` (unit), or <0 if missed.
double rayHitTerrain(const Brush& b, const Vec3& origin, const Vec3& dir);

} // namespace scal
