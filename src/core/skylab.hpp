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

    // Clouds (v0.10.5): two layers of noise on the sky, stratus high and thin, cumulus
    // lower and thick (as in Bryce). Each layer: cover 0..100 (how much sky), frequency
    // 2..150 (smaller formations as it rises), amplitude 50..500 (soft and faint to dense
    // and hard-edged), turbulence 0..100 (stirred, denser pattern), complexity 1..8
    // (detail, the noise's octaves). Off by default, so older skies stay as they were.
    struct CloudLayer {
        bool on = false;
        double cover = 40, frequency = 40, amplitude = 120, turbulence = 25, complexity = 6;
    };
    CloudLayer stratus{false, 35, 30, 90, 20, 6};
    CloudLayer cumulus{false, 40, 35, 150, 30, 6};
    double cloudHeight = 35;        // 0..100: higher = smaller, further clouds and a wider haze band
    Vec3 cumulusColor{178, 182, 192};
    bool sphericalClouds = false;   // rounder, denser clouds
    int cloudSeed = 1;              // another number, another pattern
    bool cloudShadows = false;      // stored for the renderer (clouds shading the ground)

    // Moon (v0.10.6). It moves freely (not always opposite the sun). Phase 0..100 is the
    // month: 0 new, 50 full, 100 new again; the lit side always faces the sun. Texture:
    // "builtin" (NASA's map), "none" (plain colour) or a Bottega key (skies/moons/...):
    // a 2:1 image is a map wrapped on the ball, a square one a photo of the face.
    bool moonVisible = false;
    double moonAzimuth = 240, moonAltitude = 30;
    double moonPhase = 50;
    double moonSize = 2.5;          // degrees across
    double moonIntensity = 80;      // 0..100
    double moonGlow = 30;           // 0..100
    double earthshine = 15;         // 0..100: how much the dark part shows
    double moonSoftness = 20;       // 0..100: soft edge and terminator
    Vec3 moonColor{255, 255, 255};  // tint
    std::string moonTexture = "builtin";

    // Eclipses. Solar: the moon over the sun (darkens the sky, a corona when total).
    // Lunar: Earth's shadow across the moon, 0..100, in the eclipse colour.
    double coronaIntensity = 60, coronaSize = 30;
    double lunarEclipse = 0;
    Vec3 eclipseColor{176, 72, 40};

    // Rings round the sun and the moon (ice halos). Radius 0..100 (50 = 22 degrees).
    bool rings = false;
    double ringIntensity = 50, ringRadius = 50;
    bool secondaryRing = false;

    // Stars: a random field (seeded) or Earth's own sky (to magnitude 6.5). Amount =
    // how many show (brightest first). Latitude and star time place Earth's sky (and turn
    // the random one): time is the sidereal hour, the right ascension due south.
    bool stars = false;
    bool earthStars = false;
    double starIntensity = 60, starAmount = 3000;
    int starSeed = 1;
    double starLatitude = 45, starTime = 6;
    bool comets = false;
    double cometIntensity = 60, cometAmount = 2;

    // Rainbow opposite the sun (only while the sun is up). Radius 0..100 = how wide the
    // bow is (Bryce's default 33.33), opacity 0..100.
    bool rainbow = false;
    double rainbowRadius = 33.33, rainbowOpacity = 30;
    bool secondaryBow = true;

    // Blend with Sun: haze and fog take the sun's colour and brightness (sunsets, night).
    // Soft and Darker skies only; Custom and Atmosphere off keep their colours.
    bool blendSun = true;
    double blendColor = 60, blendLuminance = 100; // 0..100
    bool blendHaze = true, blendFog = true;

    // Scene effects, stored for the renderer (M12).
    bool volumetric = false;
    double volumetricQuality = 50, volumetricDensity = 50;
    bool colorPerspective = false;
    Vec3 colorPerspectiveRgb{0, 1, 88};
    bool ibl = false;               // the baked sky lights the scene
    double iblIntensity = 100;

    // Scene lighting, stored for the renderer (M12); not used by the sky picture.
    double shadows = 80;            // 0..100 shadow strength
    bool softShadows = false;
    double shadowSoftness = 0;      // 0..100
    double sunDiffuse = 100, sunSpecular = 100; // -999..999, 100 = normal
    Vec3 skyDome{0, 0, 0};          // colour wash over the scene (black = none)

    int bakeSize = 2048;            // panorama width when baked (2048, 4096 or 8192)
    std::string bakedFog;           // the map fog the sky was baked with (out of date if it changes)

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

// What the sky needs from outside Sky Lab's own settings: the map's fog (shared with
// World > Environment; base height and thickness put it in the sky) and the moon's image.
struct SkyImage {
    int width = 0, height = 0;
    const unsigned char* rgb = nullptr; // width*height*3, top row first
};
struct SkyExtras {
    bool fogOn = false;
    Vec3 fogColor{128, 128, 128};   // 0..255
    double fogStart = 0, fogEnd = 4096; // map units: clear up to start, full from end
    double fogBase = 0, fogThickness = 0; // map units; thickness 0 = not in the sky
    SkyImage moon;                  // the moon's texture (empty = plain colour)
};

// Makes the panorama row by row (so a big bake can show progress). Width w, height w/2,
// RGB, top row first. The same settings and size always give the same bytes.
class SkyGenerator {
public:
    SkyGenerator(const SkyLab& s, int width, const SkyExtras& extras = {});
    int width() const { return w_; }
    int height() const { return h_; }
    // Writes rows [y0, y1) into `rgb` (the whole image, w*h*3 bytes).
    void rows(int y0, int y1, unsigned char* rgb) const;
    // The same, split over `threads` threads (0 = as many as the machine has).
    void rowsParallel(int y0, int y1, unsigned char* rgb, int threads = 0) const;
    // Linear-light colour (before tone mapping and dithering) in direction `dir`, for tests.
    Vec3 radiance(const Vec3& dir) const;

private:
    struct Row;
    Row row(double elevation, double pixelAngle) const;
    // Display colour 0..1 (linear) of one pixel; `star` is the light of stars and comets
    // there (they show only where the sky is dark).
    Vec3 pixel(const Row& r, double cosYaw, double sinYaw, double azDiff, double pixelAngle, const Vec3& star) const;
    // The moon's own light in direction d, and how much of the disk covers it (0..1).
    Vec3 moonAt(const Vec3& d, double cosToMoon, double pixelAngle, double& cover) const;
    Vec3 moonAlbedo(const Vec3& n, double u, double v) const;
    // Adds the stars and comets of row j to `buf` (one entry per column).
    void addStars(int j, std::vector<Vec3>& buf) const;
    Vec3 atmosphere(double elevation, double azDiff) const; // tone-mapped sky, linear 0..1
    // Clouds over `sky` in direction d (unit, above the horizon).
    Vec3 clouds(const Vec3& sky, const Vec3& d, double cosToSun, double pixelAngle) const;
    Vec3 cloudLit_, cloudShade_, stratusTint_; // cloud colours, linear
    SkyLab s_;
    SkyExtras ex_;
    bool physical_ = true; // Soft or Darker
    double sunAlt_ = 0;    // radians
    // Moon: its frame on the sky, the light falling on it (in that frame), its texture.
    Vec3 moonDir_, moonRight_, moonUp_, moonLightDir_, moonTint_;
    double moonRadius_ = 0, moonCosOuter_ = 1, moonLight_ = 0;
    std::vector<float> moonTex_; // linear RGB
    int moonTexW_ = 0, moonTexH_ = 0;
    bool moonMap_ = false;       // 2:1 map (else a photo of the face)
    double eclipse_ = 0;         // share of the sun the moon covers
    double eclipseLight_ = 1;    // what is left of the daylight
    Vec3 hazeTint_{1, 1, 1}, fogTint_{1, 1, 1}; // Blend with Sun
    Vec3 fogColor_;              // linear
    struct StarPoint { Vec3 dir; Vec3 color; double sigma; };
    std::vector<StarPoint> stars_;
    std::vector<std::vector<int>> rowStars_;
    struct Comet { Vec3 head, along, side; double length, width; Vec3 color; };
    std::vector<Comet> comets_;
    std::vector<std::vector<int>> rowComets_;
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

// The whole panorama, made on all of the machine's threads.
std::vector<unsigned char> generateSky(const SkyLab& s, int width, const SkyExtras& extras = {});

// Solar eclipse: the share of the sun's disk the moon covers (0..1), from their places
// and sizes (horizon illusion included).
double solarEclipseCover(const SkyLab& s);

} // namespace scal
