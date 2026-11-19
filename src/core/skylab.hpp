#pragma once
// Sky Lab (milestone 10c, from Bryce): skies made from settings instead of loaded images.
// The settings are saved in the map (worldspawn key `_scal_skylab`); the generator turns
// them into an RGB panorama in the layout of core/sky.hpp (2:1, middle faces +X), which
// is baked to a PNG and drawn like any other sky. No graphics dependencies.
//
// What makes up the sky picture is generated here. What lights the scene (shadows, sun
// diffuse / specular, sky dome colour...) is only stored, for the renderer (M12).
// Steps: v0.10.4 sky modes, sun, haze band, ground; v0.10.5 clouds; v0.10.6 moon, stars,
// comets, rings, rainbow, fog in the sky, eclipses. Fields a newer version wrote and this
// one doesn't know are kept as they are (`otherFields`).
#include "core/math.hpp"
#include <string>
#include <utility>
#include <vector>

namespace scal {

// Sky modes (Bryce's Sky & Fog palette): Soft and Darker follow the sun; Custom uses
// three colours of your own; Off is one plain colour.
enum class SkyMode { Soft, Darker, Custom, Off };
const char* skyModeName(SkyMode m);

struct SkyLab {
    bool used = false; // the map has Sky Lab settings (else nothing is saved)
    SkyMode mode = SkyMode::Soft;

    // Sun. Azimuth is measured like a light's yaw (0 = +X, turning counterclockwise seen
    // from above); altitude is degrees above the horizon (negative = below).
    double sunAzimuth = 60, sunAltitude = 35;
    Vec3 sunColor{255, 247, 232};   // 0..255
    bool sunVisible = true;         // the disk shows (it still lights the scene when off)
    double diskIntensity = 75;      // 0..100: brightness of the disk itself
    double glowIntensity = 50;      // 0..100: the halo bleeding into the sky around it
    double diskSize = 2.5;          // degrees across
    double horizonIllusion = 0;     // % larger at the horizon (0 = none)
    bool lockSun = false;           // follows the map's sun light (Sky Lab turns the light)

    // Custom mode: sky (overhead), glow around the sun, horizon. Off mode: one colour.
    Vec3 customSky{62, 104, 196}, customGlow{255, 214, 160}, customHorizon{196, 212, 232};
    Vec3 offColor{0, 0, 0};

    // Haze: the band at the horizon. Density 0..100 (how strong), thickness 0..100 (how
    // high it reaches), base height -100..100 (moves the band down / up).
    bool hazeOn = true;
    Vec3 hazeColor{214, 222, 232};
    double hazeDensity = 25, hazeThickness = 25, hazeBase = 0;

    Vec3 groundColor{96, 92, 84};   // below the horizon (the haze fades into it)

    // Scene lighting, stored for the renderer (M12); not used by the sky picture.
    double shadows = 80;            // 0..100 shadow strength
    bool softShadows = false;
    double shadowSoftness = 0;      // 0..100
    double sunDiffuse = 100, sunSpecular = 100; // -999..999, 100 = normal
    Vec3 skyDome{0, 0, 0};          // colour wash over the scene (black = none)

    int bakeSize = 2048;            // panorama width when baked (2048, 4096 or 8192)

    std::vector<std::pair<std::string, std::string>> otherFields;

    bool operator==(const SkyLab& o) const;
    bool operator!=(const SkyLab& o) const { return !(*this == o); }
};

// `_scal_skylab` value: `name=value` pairs separated by `;` (colours as "r g b").
std::string skyLabToString(const SkyLab& s);
// Reads what it knows (each field checked; bad numbers keep the default), keeps the rest
// in otherFields. Always marks the result as used.
SkyLab skyLabFromString(const std::string& text);

// Unit vector toward the sun.
Vec3 skyLabSunDirection(const SkyLab& s);
// Sun lock: the sky's sun is where a sun light's light comes from.
void sunFromLightAngles(double lightPitch, double lightYaw, double& azimuth, double& altitude);
void lightAnglesFromSun(double azimuth, double altitude, double& lightPitch, double& lightYaw);

// Clamps every value to its range (typed values, files from elsewhere).
void clampSkyLab(SkyLab& s);

// A random sky (Randomize Sky), the same for the same seed on every machine.
SkyLab randomSkyLab(unsigned seed);

// Makes the panorama row by row (so a big bake can show progress). Width w, height w/2,
// RGB, top row first. The same settings and size always give the same bytes.
class SkyGenerator {
public:
    SkyGenerator(const SkyLab& s, int width);
    int width() const { return w_; }
    int height() const { return h_; }
    // Writes rows [y0, y1) into `rgb` (the whole image, w*h*3 bytes).
    void rows(int y0, int y1, unsigned char* rgb) const;
    // Linear-light colour (before tone mapping and dithering) in direction `dir`, for tests.
    Vec3 radiance(const Vec3& dir) const;

private:
    struct Row;
    Row row(double elevation, double pixelAngle) const;
    // Display colour 0..1 (linear) of one pixel.
    Vec3 pixel(const Row& r, double cosYaw, double sinYaw, double azDiff, double pixelAngle) const;
    Vec3 atmosphere(double elevation, double azDiff) const; // tone-mapped sky, linear 0..1
    SkyLab s_;
    int w_, h_;
    Vec3 sunDir_;
    Vec3 sunLight_;        // sun colour, linear, 1 = white
    Vec3 sunTransmit_;     // what reaches the ground of the sun's light (reddens low down)
    double dayLevel_ = 1;  // 1 by day, small at night (haze and ground get dimmer)
    double diskRadius_ = 0; // radians, horizon illusion included
    double diskCosOuter_ = 1;
    Vec3 diskColor_, offLinear_, groundLinear_;
    std::vector<double> cosYaw_, sinYaw_, azDiff_; // per column
    std::vector<double> srgb_;                     // linear -> sRGB table
    // Soft / Darker: a table of the sky (elevation x azimuth difference to the sun).
    std::vector<Vec3> lut_;
    int lutE_ = 0, lutA_ = 0;
};

std::vector<unsigned char> generateSky(const SkyLab& s, int width);

} // namespace scal
