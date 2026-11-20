#pragma once
// Milestone 11 — camera objects: what Render (M12) and the Camera Preview (v0.11.1) look
// through. A map holds any number; exactly one is ACTIVE (the only one used). Placed and
// aimed like a spot light, plus roll. Pure data and maths; drawing lives in render/.
//
// Angles are degrees, as for lights: yaw turns around +Z from +X, pitch is negative when
// looking down, roll turns the camera around its view direction (positive = it tips to
// the right, clockwise as seen from behind it).
// The field of view is measured across the picture's LONGER side; the lens length in mm
// is the same angle on a 36 mm wide (full-frame) sensor across that side.
#include "core/math.hpp"
#include "core/mathprim.hpp" // Mat3
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace scal {

using CameraId = std::uint32_t;

struct Camera {
    CameraId id = 0;
    std::string name = "camera";
    Vec3 pos;
    double pitch = 0, yaw = 0, roll = 0;
    double fov = 39.6;          // degrees across the longer side (39.6 = 50 mm)
    double nearDist = 4;        // nothing closer is drawn (look into a room through its wall)
    double farDist = 100000;    // nothing further is drawn (the sky still shows)
    int width = 1920, height = 1080; // picture size in pixels
    bool ortho = false;         // no perspective (isometric-style pictures)
    double orthoSize = 1024;    // orthographic: units across the longer side
    double exposure = 0;        // stops; stored for the renderer's HDR (not used yet)
    Vec3 color{255, 190, 40};   // marker colour, 0..255
    bool active = false;        // the one Render and the Camera Preview use
    std::vector<std::pair<std::string, std::string>> otherKeys; // kept from loaded files
    bool operator==(const Camera& o) const;
    bool operator!=(const Camera& o) const { return !(*this == o); }
};

// Limits every value to something usable (loading, typed fields).
void clampCamera(Camera& c);

// Lens length (mm) <-> field of view (degrees), on a 36 mm sensor.
double lensToFov(double mm);
double fovToLens(double fovDeg);
// Lens presets: ultra-wide 14, wide 24, normal 50, portrait 85, telephoto 135 mm.
struct LensPreset { const char* name; double mm; };
const std::vector<LensPreset>& lensPresets();
// Picture size presets (any typed size works too).
struct SizePreset { const char* name; int width, height; };
const std::vector<SizePreset>& sizePresets();

// The camera's axes in the world: col[0] = where it looks, col[1] = the picture's left,
// col[2] = the picture's up.
Mat3 cameraBasis(const Camera& c);
// Sets pitch / yaw / roll from axes (tidy: 45 stays 45; yaw 0..360, roll -180..180).
void setCameraBasis(Camera& c, const Mat3& m);
Vec3 cameraForward(const Camera& c);
// Turns the camera about a world axis (0 = X, 1 = Y, 2 = Z) through its own position.
void rotateCamera(Camera& c, int axis, double degrees);
// Turns it about its own view direction (roll).
void rollCamera(Camera& c, double degrees);
// Looks at `target`, keeping the roll. False (nothing changed) if the target is the
// camera's own position.
bool aimCamera(Camera& c, const Vec3& target);

// Tangents of the half angles across the picture (perspective): x = width, y = height.
void cameraHalfTangents(const Camera& c, double& tx, double& ty);
// Half the picture's size in units (orthographic): x = width, y = height.
void cameraHalfSize(const Camera& c, double& hx, double& hy);
// The four corners of the picture's frame at distance `dist` along the view direction:
// top-left, top-right, bottom-right, bottom-left (as seen through the camera).
std::array<Vec3, 4> cameraFrame(const Camera& c, double dist);

// The lines that draw a camera (world segments): a pyramid from its position to the
// frame at distance `len` (a box for an orthographic camera) and a small triangle on the
// frame's top edge showing which way is up.
std::vector<std::pair<Vec3, Vec3>> cameraShape(const Camera& c, double len);

// Exactly one active camera: none -> the first; several -> the first of them stays.
// Returns the ids whose `active` changed.
std::vector<CameraId> fixActiveCamera(std::vector<Camera>& cams);

} // namespace scal
