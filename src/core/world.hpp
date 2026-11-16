#pragma once
// Milestone 8 — World: lights, the environment (sky, ambient light, fog), camera
// bookmarks and the scale figure. Pure data and maths; drawing lives in render/.
//
// Lights are saved as Hammer-style entities (light, light_spot, light_environment) so
// other tools see them. Directions are Hammer-style pitch/yaw in degrees: yaw turns
// around +Z from +X, pitch is negative when pointing down (as in the "pitch" key).
#include "core/math.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace scal {

using LightId = std::uint32_t;
enum class LightKind { Point, Spot, Sun };
const char* lightKindName(LightKind k); // "point", "spot", "sun"

struct Light {
    LightId id = 0;
    LightKind kind = LightKind::Point;
    Vec3 pos;                 // where it is (a sun's marker: anywhere, only its direction counts)
    Vec3 color{255, 255, 255}; // 0..255 per channel (Hammer "_light")
    double brightness = 200;   // Hammer "_light" 4th value; 200 = normal
    double range = 512;        // reach in units (point, spot): no light past this
    double pitch = -90;        // direction (spot, sun): degrees, negative = down
    double yaw = 0;
    double inner = 30;         // spot: full brightness inside this half-angle (degrees)
    double outer = 45;         // spot: no light outside this half-angle
    // Keys from a loaded file that we don't edit (kept so saving never loses them).
    std::vector<std::pair<std::string, std::string>> otherKeys;
    bool operator==(const Light& o) const;
    bool operator!=(const Light& o) const { return !(*this == o); }
    bool hasDirection() const { return kind != LightKind::Point; }
};

// Unit vector a light points along (spot, sun).
Vec3 lightDirection(double pitch, double yaw);
Vec3 lightDirection(const Light& l);
// Pitch/yaw (degrees) of a direction. Straight up/down keeps `keepYaw`.
void directionAngles(const Vec3& dir, double& pitch, double& yaw, double keepYaw = 0);
// Turns the light's direction around a world axis (0 = X, 1 = Y, 2 = Z) by `degrees`.
void rotateLight(Light& l, int axis, double degrees);

struct Fog {
    Vec3 color{150, 160, 175}; // 0..255
    double start = 512;        // fully clear up to here
    double end = 4096;         // fully fogged from here
    bool operator==(const Fog& o) const { return color == o.color && start == o.start && end == o.end; }
};

// Map-wide settings (World menu > Environment). The editor shows them only roughly.
struct Environment {
    std::string sky;            // sky name ("" = none)
    Vec3 ambient{40, 40, 48};   // 0..255: light everywhere, even where no light reaches
    bool fogOn = false;         // the map has fog (the editor shows it only on request)
    Fog fog;
    bool operator==(const Environment& o) const {
        return sky == o.sky && ambient == o.ambient && fogOn == o.fogOn && fog == o.fog;
    }
    bool operator!=(const Environment& o) const { return !(*this == o); }
};

// A saved 3D viewpoint (editor-only, saved with the map).
struct CameraBookmark {
    std::string name;
    Vec3 pos;
    double yaw = 0, pitch = 0; // degrees, as the 3D view uses them
};

// `n` points on a circle around `center` in the plane across world axis `axis` (0..2),
// starting on the next axis and turning by the right-hand rule.
std::vector<Vec3> axisCircle(const Vec3& center, int axis, double radius, int n = 64);

// ---- very rough lighting preview (no shadows)

// Light arriving at point `p` with surface normal `n` (unit), as a colour multiplier
// (1 = the texture as it is; can go above 1). Ambient + each light's Lambert term with
// a smooth falloff to its range (point, spot) and the spot cone; suns reach everywhere.
Vec3 lightAt(const Environment& env, const std::vector<Light>& lights, const Vec3& p, const Vec3& n);

} // namespace scal
