#pragma once
// Terrain (milestone 7). A terrain is one face of a brush (3 or 4 corners) turned into a
// grid of points that can be lifted along the face normal (sculpting, or point by point
// with the vertex tool) and painted with up to kMaxTerrainLayers textures over the face's
// own. The data lives in Brush::terrain (see core/brush.hpp).
//
// A 3-cornered face is laid out like a 4-cornered one whose fourth corner sits halfway
// along the edge opposite the start corner, so the grid covers the triangle exactly and
// mirroring the brush still just swaps rows and columns.
//
// Neighbouring patches stay stitched: every operation works on "shared points" — grid
// points of different patches at the same spot (same flat position and same normal)
// are treated as one point, so their heights and blend always match.
#include "core/brush.hpp"
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace scal {

constexpr int kMinTerrainPower = 1; // 2 x 2 cells
constexpr int kMaxTerrainPower = 5; // 32 x 32 cells
constexpr int kMaxTerrainLayers = 7; // painted textures, on top of the face's own (8 in all)

// The terrain's points, ready to draw or test.
struct TerrainGrid {
    int n = 0;                  // points per side
    int face = -1;
    Vec3 normal;                // outward face normal (heights go this way)
    std::array<Vec3, 4> corners; // c0 (start), c1, c2, c3 counter-clockwise from outside
    std::vector<Vec3> base;     // flat points on the face, [j * n + i]
    std::vector<Vec3> pos;      // base + normal * height
    Vec3 at(int i, int j) const { return pos[(size_t)j * n + i]; }
};

// The 4 grid corners of `face`, counter-clockwise from outside, starting at the corner
// nearest `start` (for a 3-cornered face the third is the middle of the opposite edge);
// nullopt if the face doesn't have 3 or 4 corners.
std::optional<std::array<Vec3, 4>> faceQuad(const Brush& b, int face, const Vec3& start);

// Grid of b's terrain; nullopt if b has none or it no longer fits the brush (the face
// lost its corners after an edit) — then the face is drawn as a plain face.
std::optional<TerrainGrid> terrainGrid(const Brush& b);
bool terrainValid(const Brush& b);

// Turns `face` into a flat terrain with 2^power cells per side. If b already has a
// terrain on that face, it is resampled to the new power (shape and paint kept).
// Returns an error message, or "" on success.
std::string makeTerrain(Brush& b, int face, int power);

// The face facing most upward (+Z) among faces with 3 or 4 corners, or -1.
int topQuadFace(const Brush& b);
// The face whose outward normal points most along `dir` (and at least a little), or -1.
// Used to pick a face in a 2D view: dir points from the brush toward the viewer.
int faceFacing(const Brush& b, const Vec3& dir);

// Mirrors the grid along its diagonal (used when a brush is mirrored).
void transposeTerrain(Terrain& t);

// After `rebuilt` was made from `old` by moving corners (vertex tool, snap to grid):
// if the terrain face is still there with the same number of corners, each at
// moved(old corner), the terrain is kept on it (heights and paint stay). Otherwise
// `rebuilt` gets no terrain. Returns true if the terrain was kept.
bool carryTerrain(const Brush& old, Brush& rebuilt, const std::function<Vec3(const Vec3&)>& moved);

// The layer painting `texture`, or -1.
int terrainLayer(const Terrain& t, const std::string& texture);
// Drops layers with no paint left. Returns true if any were dropped.
bool pruneTerrainLayers(Terrain& t);

// ---- editing

enum class SculptMode { Raise, Lower, Smooth, Flatten, Noise, Paint, Erase };
enum class Falloff { Soft, Linear, Hard };
const char* sculptModeName(SculptMode m);
const char* falloffName(Falloff f);

struct SculptDab {
    SculptMode mode = SculptMode::Raise;
    Falloff falloff = Falloff::Soft;
    Vec3 center;            // where the dab hits the surface
    double radius = 128;    // measured along the face (flat distance); surfaces farther than
                            // this from `center` (across the face) are not touched
    double amount = 8;      // units per dab: raise, lower, noise
    double strength = 0.5;  // 0..1 per dab: smooth, flatten, paint, erase
    Vec3 flattenTarget;     // flatten: points move toward the plane through this point
    unsigned seed = 1;      // noise pattern
    std::string texture;    // paint: this texture (a new layer on top if the patch lacks it;
                            // the face's own texture = uncover it)
};

// Falloff weight for a flat distance d within radius r (1 at the centre, 0 at the rim).
double falloffWeight(Falloff f, double d, double r);

// Applies one dab to every terrain in `brushes` (in place). Returns true if anything
// changed. `full` (optional) is set if a patch already had kMaxTerrainLayers painted
// textures and couldn't take a new one.
bool sculpt(std::vector<Brush>& brushes, const SculptDab& dab, bool* full = nullptr);

// Makes shared points of neighbouring patches equal (average). Returns how many shared
// points were changed.
int stitchTerrain(std::vector<Brush>& brushes);

// One grid point: brushes[brush].terrain, point index j * size + i.
struct TerrainPoint { size_t brush; int idx; };
// Raises the given points by `amount` along their normal (or, with `absolute`, sets
// their height to it). Points shared with neighbouring patches move with them, and a
// point listed twice moves once. Returns true if anything changed.
bool moveTerrainPoints(std::vector<Brush>& brushes, const std::vector<TerrainPoint>& pts, double amount,
                       bool absolute = false);

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
