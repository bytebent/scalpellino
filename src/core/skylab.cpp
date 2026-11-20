#include "core/skylab.hpp"
#include "core/star_catalogue.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <thread>

namespace scal {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

Vec3 mul(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
Vec3 vexp(const Vec3& v) { return {std::exp(v.x), std::exp(v.y), std::exp(v.z)}; }
Vec3 lerp(const Vec3& a, const Vec3& b, double t) { return a + (b - a) * t; }
double smooth(double e0, double e1, double x) {
    const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
double toLinear(double c) { // sRGB 0..255 -> linear 0..1
    const double v = std::clamp(c / 255.0, 0.0, 1.0);
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}
Vec3 toLinear(const Vec3& c) { return {toLinear(c.x), toLinear(c.y), toLinear(c.z)}; }
double wrapAngle(double a) { // to -pi..pi
    a = std::fmod(a, 2 * kPi);
    if (a > kPi) a -= 2 * kPi;
    if (a < -kPi) a += 2 * kPi;
    return a;
}
constexpr int kSrgbSteps = 16384;
double toSrgb(double v) { // linear 0..1 -> sRGB 0..1
    v = std::clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - 0.055;
}

// ---- text form
std::string num(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}
std::string col(const Vec3& c) { return num(c.x) + " " + num(c.y) + " " + num(c.z); }
bool readNum(const std::string& v, double& out) {
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (end == v.c_str() || !std::isfinite(d)) return false;
    out = d;
    return true;
}
bool readCol(const std::string& v, Vec3& out) {
    Vec3 c;
    if (std::sscanf(v.c_str(), "%lf %lf %lf", &c.x, &c.y, &c.z) != 3) return false;
    if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z)) return false;
    out = c;
    return true;
}

// ---- deterministic randomness (Randomize Sky, dithering): integer hashing only
std::uint32_t hash32(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
struct Rand {
    std::uint32_t state;
    double next() { state = hash32(state + 0x9e3779b9U); return (state >> 8) / 16777216.0; } // 0..1
    double range(double a, double b) { return a + (b - a) * next(); }
};

// ---- noise for the clouds: gradient noise in 2D, summed over octaves, with each octave
// faded out where it is finer than a pixel (so far clouds don't flicker into grain).
double gradNoise(double x, double y, std::uint32_t seed) {
    const double fx = std::floor(x), fy = std::floor(y);
    const int ix = (int)fx, iy = (int)fy;
    const double tx = x - fx, ty = y - fy;
    // 16 gradient directions round the circle (a table: no sin / cos per sample).
    static const double kGx[16] = {1, 0.92388, 0.70711, 0.38268, 0, -0.38268, -0.70711, -0.92388, -1, -0.92388, -0.70711, -0.38268, 0, 0.38268, 0.70711, 0.92388};
    static const double kGy[16] = {0, 0.38268, 0.70711, 0.92388, 1, 0.92388, 0.70711, 0.38268, 0, -0.38268, -0.70711, -0.92388, -1, -0.92388, -0.70711, -0.38268};
    auto g = [&](int cx, int cy, double dx, double dy) {
        const std::uint32_t h = hash32((std::uint32_t)cx * 73856093U ^ (std::uint32_t)cy * 19349663U ^ seed) >> 28;
        return kGx[h] * dx + kGy[h] * dy;
    };
    auto fade = [](double t) { return t * t * t * (t * (t * 6 - 15) + 10); };
    const double u = fade(tx), v = fade(ty);
    const double a = g(ix, iy, tx, ty), b = g(ix + 1, iy, tx - 1, ty);
    const double c = g(ix, iy + 1, tx, ty - 1), d = g(ix + 1, iy + 1, tx - 1, ty - 1);
    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v; // about -0.7..0.7
}

// fBm: `octaves` layers, each twice as fine and half as strong. `footprint` is a pixel's
// size in noise units; octaves finer than about two pixels fade to nothing.
double fbm(double x, double y, std::uint32_t seed, int octaves, double footprint) {
    double sum = 0, amp = 1, norm = 0, f = 1;
    for (int o = 0; o < octaves; ++o) {
        const double keep = 1 - smooth(0.25, 0.6, footprint * f);
        if (keep <= 0) break;
        sum += gradNoise(x * f + o * 17.31, y * f - o * 9.73, seed + (std::uint32_t)o * 1013U) * amp * keep;
        norm += amp;
        amp *= 0.5;
        f *= 2.03;
    }
    return norm > 0 ? sum / norm : 0;
}

// Cells (distance to the nearest of one random point per cell), for rounder clouds.
double cells(double x, double y, std::uint32_t seed) {
    const int ix = (int)std::floor(x), iy = (int)std::floor(y);
    double best = 9;
    for (int cy = iy - 1; cy <= iy + 1; ++cy)
        for (int cx = ix - 1; cx <= ix + 1; ++cx) {
            const std::uint32_t h = hash32((std::uint32_t)cx * 2654435761U ^ (std::uint32_t)cy * 40503U ^ seed);
            const double px = cx + (h & 0xffff) / 65536.0, py = cy + (h >> 16) / 65536.0;
            best = std::min(best, (px - x) * (px - x) + (py - y) * (py - y));
        }
    return std::sqrt(best); // 0 at a point, about 0.5..0.8 between them
}

// ---- atmosphere (Soft / Darker): single scattering through a thin shell around a planet.
// Units: metres. Rayleigh makes the blue, Mie the glow around the sun and the whiteness
// low down. The table is computed once per generator.
constexpr double kR = 6360e3, kAtm = 6420e3, kHr = 8000, kHm = 1200;
const Vec3 kBetaR{5.8e-6, 13.5e-6, 33.1e-6};
constexpr double kBetaM = 21e-6, kG = 0.76;
constexpr double kEye = 2; // metres above the ground

// Distance along `d` from `o` to the top of the atmosphere (o inside).
double toTop(const Vec3& o, const Vec3& d) {
    const double b = dot(o, d), c = dot(o, o) - kAtm * kAtm;
    return -b + std::sqrt(std::max(0.0, b * b - c));
}

// Optical depth (Rayleigh, Mie) from `o` along `d` to space, or -1 if the ground is hit.
bool depthToSpace(const Vec3& o, const Vec3& d, double& odR, double& odM) {
    const double b = dot(o, d), c = dot(o, o) - kR * kR;
    if (b < 0 && b * b - c > 0 && -b - std::sqrt(b * b - c) > 0) return false; // ground
    const double len = toTop(o, d);
    const int n = 8;
    const double step = len / n;
    odR = odM = 0;
    for (int i = 0; i < n; ++i) {
        const Vec3 p = o + d * ((i + 0.5) * step);
        const double h = length(p) - kR;
        odR += std::exp(-h / kHr) * step;
        odM += std::exp(-h / kHm) * step;
    }
    return true;
}

// Rayleigh and Mie light scattered toward the eye, for a view `d` and sun `sun` (unit).
void scatter(const Vec3& d, const Vec3& sun, double mieScale, Vec3& outR, Vec3& outM) {
    const Vec3 o{0, 0, kR + kEye};
    const double len = toTop(o, d);
    const int n = 16;
    const double step = len / n;
    double odR = 0, odM = 0;
    outR = outM = Vec3{};
    for (int i = 0; i < n; ++i) {
        const Vec3 p = o + d * ((i + 0.5) * step);
        const double h = length(p) - kR;
        const double hr = std::exp(-h / kHr) * step, hm = std::exp(-h / kHm) * step;
        odR += hr;
        odM += hm;
        double lR, lM;
        if (!depthToSpace(p, sun, lR, lM)) continue; // this point is in the planet's shadow
        const Vec3 tau = kBetaR * (odR + lR) + Vec3{1, 1, 1} * (kBetaM * mieScale * 1.1 * (odM + lM));
        const Vec3 att = vexp(-tau);
        outR += att * hr;
        outM += att * hm;
    }
    const double mu = dot(d, sun);
    const double phaseR = 3 / (16 * kPi) * (1 + mu * mu);
    const double phaseM = 3 / (8 * kPi) * ((1 - kG * kG) * (1 + mu * mu)) /
                          ((2 + kG * kG) * std::pow(1 + kG * kG - 2 * kG * mu, 1.5));
    outR = mul(outR, kBetaR) * phaseR;
    outM = outM * (kBetaM * mieScale * phaseM);
}

// Distance along d (from the eye) to a cloud layer `height` metres up, following the
// planet's curve so the layers meet the horizon far away instead of running on forever.
double toLayer(const Vec3& d, double height) {
    const Vec3 o{0, 0, kR + kEye};
    const double rr = kR + height;
    const double b = dot(o, d), c = dot(o, o) - rr * rr;
    return -b + std::sqrt(std::max(0.0, b * b - c));
}

// Fraction of sunlight that reaches the ground at the sun's altitude (per channel).
Vec3 sunTransmittance(double altitude) {
    const Vec3 o{0, 0, kR + kEye};
    const double a = std::max(altitude, -0.02); // just under the horizon: still a little
    const Vec3 d{std::cos(a), 0, std::sin(a)};
    double odR = 0, odM = 0;
    if (!depthToSpace(o, d, odR, odM)) {
        const Vec3 d0{1, 0, 0};
        depthToSpace(o, d0, odR, odM);
        odR *= 1.3; odM *= 1.3;
    }
    return vexp(-(kBetaR * odR + Vec3{1, 1, 1} * (kBetaM * 1.1 * odM)));
}

// Tone mapping of the physical sky (linear in, linear 0..1 out), per mode.
Vec3 toneMap(const Vec3& c, bool darker) {
    // A cool white balance and more saturation than the plain physics give: Bryce's skies
    // are a clean saturated blue with a pale horizon, not the greyish blue of real air.
    const Vec3 gain = darker ? Vec3{0.80, 0.92, 1.25} : Vec3{0.85, 0.95, 1.18};
    const double exposure = darker ? 1.0 : 1.5;
    const Vec3 g = mul(c, gain) * exposure;
    Vec3 t{1 - std::exp(-g.x), 1 - std::exp(-g.y), 1 - std::exp(-g.z)};
    const double l = 0.2126 * t.x + 0.7152 * t.y + 0.0722 * t.z;
    t = lerp(Vec3{l, l, l}, t, darker ? 1.45 : 1.3);
    return Vec3{std::clamp(t.x, 0.0, 1.0), std::clamp(t.y, 0.0, 1.0), std::clamp(t.z, 0.0, 1.0)};
}
} // namespace

const char* skyModeName(SkyMode m) {
    switch (m) {
        case SkyMode::Soft: return "soft";
        case SkyMode::Darker: return "darker";
        case SkyMode::Custom: return "custom";
        case SkyMode::Off: return "off";
    }
    return "soft";
}

bool SkyLab::operator==(const SkyLab& o) const { return skyLabToString(*this) == skyLabToString(o) && used == o.used; }

std::string skyLabToString(const SkyLab& s) {
    std::string out;
    auto put = [&](const char* k, const std::string& v) {
        if (!out.empty()) out += ';';
        out += k;
        out += '=';
        out += v;
    };
    put("mode", skyModeName(s.mode));
    put("sun_azimuth", num(s.sunAzimuth));
    put("sun_altitude", num(s.sunAltitude));
    put("sun_color", col(s.sunColor));
    put("sun_visible", s.sunVisible ? "1" : "0");
    put("disk_intensity", num(s.diskIntensity));
    put("glow_intensity", num(s.glowIntensity));
    put("disk_size", num(s.diskSize));
    put("horizon_illusion", num(s.horizonIllusion));
    put("lock_sun", s.lockSun ? "1" : "0");
    put("custom_sky", col(s.customSky));
    put("custom_glow", col(s.customGlow));
    put("custom_horizon", col(s.customHorizon));
    put("off_color", col(s.offColor));
    put("haze", s.hazeOn ? "1" : "0");
    put("haze_color", col(s.hazeColor));
    put("haze_density", num(s.hazeDensity));
    put("haze_thickness", num(s.hazeThickness));
    put("haze_base", num(s.hazeBase));
    put("ground_color", col(s.groundColor));
    auto layer = [&](const SkyLab::CloudLayer& l) {
        return std::string(l.on ? "1 " : "0 ") + num(l.cover) + " " + num(l.frequency) + " " + num(l.amplitude) + " " + num(l.turbulence) + " " + num(l.complexity);
    };
    put("stratus", layer(s.stratus));
    put("cumulus", layer(s.cumulus));
    put("cloud_height", num(s.cloudHeight));
    put("cumulus_color", col(s.cumulusColor));
    put("spherical_clouds", s.sphericalClouds ? "1" : "0");
    put("cloud_seed", std::to_string(s.cloudSeed));
    put("cloud_shadows", s.cloudShadows ? "1" : "0");
    auto b = [](bool v) { return std::string(v ? "1" : "0"); };
    put("moon_visible", b(s.moonVisible));
    put("moon_azimuth", num(s.moonAzimuth));
    put("moon_altitude", num(s.moonAltitude));
    put("moon_phase", num(s.moonPhase));
    put("moon_size", num(s.moonSize));
    put("moon_intensity", num(s.moonIntensity));
    put("moon_glow", num(s.moonGlow));
    put("earthshine", num(s.earthshine));
    put("moon_softness", num(s.moonSoftness));
    put("moon_color", col(s.moonColor));
    {
        std::string t = s.moonTexture; // no separators inside a value
        for (char& c : t)
            if (c == ';' || c == '=') c = '_';
        put("moon_texture", t);
    }
    put("corona_intensity", num(s.coronaIntensity));
    put("corona_size", num(s.coronaSize));
    put("lunar_eclipse", num(s.lunarEclipse));
    put("eclipse_color", col(s.eclipseColor));
    put("rings", b(s.rings));
    put("ring_intensity", num(s.ringIntensity));
    put("ring_radius", num(s.ringRadius));
    put("secondary_ring", b(s.secondaryRing));
    put("stars", b(s.stars));
    put("star_field", s.earthStars ? "earth" : "random");
    put("star_intensity", num(s.starIntensity));
    put("star_amount", num(s.starAmount));
    put("star_seed", std::to_string(s.starSeed));
    put("star_latitude", num(s.starLatitude));
    put("star_time", num(s.starTime));
    put("comets", b(s.comets));
    put("comet_intensity", num(s.cometIntensity));
    put("comet_amount", num(s.cometAmount));
    put("rainbow", b(s.rainbow));
    put("rainbow_radius", num(s.rainbowRadius));
    put("rainbow_opacity", num(s.rainbowOpacity));
    put("secondary_bow", b(s.secondaryBow));
    put("blend_sun", b(s.blendSun));
    put("blend_color", num(s.blendColor));
    put("blend_luminance", num(s.blendLuminance));
    put("blend_haze", b(s.blendHaze));
    put("blend_fog", b(s.blendFog));
    put("volumetric", b(s.volumetric));
    put("volumetric_quality", num(s.volumetricQuality));
    put("volumetric_density", num(s.volumetricDensity));
    put("color_perspective", b(s.colorPerspective));
    put("color_perspective_rgb", col(s.colorPerspectiveRgb));
    put("ibl", b(s.ibl));
    put("ibl_intensity", num(s.iblIntensity));
    put("shadows", num(s.shadows));
    put("soft_shadows", s.softShadows ? "1" : "0");
    put("shadow_softness", num(s.shadowSoftness));
    put("sun_diffuse", num(s.sunDiffuse));
    put("sun_specular", num(s.sunSpecular));
    put("sky_dome", col(s.skyDome));
    put("bake_size", std::to_string(s.bakeSize));
    if (!s.bakedFog.empty()) put("baked_fog", s.bakedFog);
    for (const auto& [k, v] : s.otherFields) put(k.c_str(), v);
    return out;
}

SkyLab skyLabFromString(const std::string& text) {
    SkyLab s;
    s.used = true;
    std::istringstream in(text);
    std::string entry;
    while (std::getline(in, entry, ';')) {
        const size_t eq = entry.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        const std::string k = entry.substr(0, eq), v = entry.substr(eq + 1);
        double d = 0;
        bool known = true;
        if (k == "mode") {
            if (v == "soft") s.mode = SkyMode::Soft;
            else if (v == "darker") s.mode = SkyMode::Darker;
            else if (v == "custom") s.mode = SkyMode::Custom;
            else if (v == "off") s.mode = SkyMode::Off;
        } else if (k == "sun_azimuth") { if (readNum(v, d)) s.sunAzimuth = d; }
        else if (k == "sun_altitude") { if (readNum(v, d)) s.sunAltitude = d; }
        else if (k == "sun_color") readCol(v, s.sunColor);
        else if (k == "sun_visible") s.sunVisible = v != "0";
        else if (k == "disk_intensity") { if (readNum(v, d)) s.diskIntensity = d; }
        else if (k == "glow_intensity") { if (readNum(v, d)) s.glowIntensity = d; }
        else if (k == "disk_size") { if (readNum(v, d)) s.diskSize = d; }
        else if (k == "horizon_illusion") { if (readNum(v, d)) s.horizonIllusion = d; }
        else if (k == "lock_sun") s.lockSun = v == "1";
        else if (k == "custom_sky") readCol(v, s.customSky);
        else if (k == "custom_glow") readCol(v, s.customGlow);
        else if (k == "custom_horizon") readCol(v, s.customHorizon);
        else if (k == "off_color") readCol(v, s.offColor);
        else if (k == "haze") s.hazeOn = v != "0";
        else if (k == "haze_color") readCol(v, s.hazeColor);
        else if (k == "haze_density") { if (readNum(v, d)) s.hazeDensity = d; }
        else if (k == "haze_thickness") { if (readNum(v, d)) s.hazeThickness = d; }
        else if (k == "haze_base") { if (readNum(v, d)) s.hazeBase = d; }
        else if (k == "ground_color") readCol(v, s.groundColor);
        else if (k == "stratus" || k == "cumulus") {
            SkyLab::CloudLayer& l = k == "stratus" ? s.stratus : s.cumulus;
            int on = 0;
            SkyLab::CloudLayer t = l;
            if (std::sscanf(v.c_str(), "%d %lf %lf %lf %lf %lf", &on, &t.cover, &t.frequency, &t.amplitude, &t.turbulence, &t.complexity) == 6 &&
                std::isfinite(t.cover) && std::isfinite(t.frequency) && std::isfinite(t.amplitude) && std::isfinite(t.turbulence) && std::isfinite(t.complexity)) {
                t.on = on != 0;
                l = t;
            }
        }
        else if (k == "cloud_height") { if (readNum(v, d)) s.cloudHeight = d; }
        else if (k == "cumulus_color") readCol(v, s.cumulusColor);
        else if (k == "spherical_clouds") s.sphericalClouds = v == "1";
        else if (k == "cloud_seed") { if (readNum(v, d) && std::fabs(d) < 2e9) s.cloudSeed = (int)d; }
        else if (k == "cloud_shadows") s.cloudShadows = v == "1";
        else if (k == "moon_visible") s.moonVisible = v == "1";
        else if (k == "moon_azimuth") { if (readNum(v, d)) s.moonAzimuth = d; }
        else if (k == "moon_altitude") { if (readNum(v, d)) s.moonAltitude = d; }
        else if (k == "moon_phase") { if (readNum(v, d)) s.moonPhase = d; }
        else if (k == "moon_size") { if (readNum(v, d)) s.moonSize = d; }
        else if (k == "moon_intensity") { if (readNum(v, d)) s.moonIntensity = d; }
        else if (k == "moon_glow") { if (readNum(v, d)) s.moonGlow = d; }
        else if (k == "earthshine") { if (readNum(v, d)) s.earthshine = d; }
        else if (k == "moon_softness") { if (readNum(v, d)) s.moonSoftness = d; }
        else if (k == "moon_color") readCol(v, s.moonColor);
        else if (k == "moon_texture") { if (!v.empty()) s.moonTexture = v; }
        else if (k == "corona_intensity") { if (readNum(v, d)) s.coronaIntensity = d; }
        else if (k == "corona_size") { if (readNum(v, d)) s.coronaSize = d; }
        else if (k == "lunar_eclipse") { if (readNum(v, d)) s.lunarEclipse = d; }
        else if (k == "eclipse_color") readCol(v, s.eclipseColor);
        else if (k == "rings") s.rings = v == "1";
        else if (k == "ring_intensity") { if (readNum(v, d)) s.ringIntensity = d; }
        else if (k == "ring_radius") { if (readNum(v, d)) s.ringRadius = d; }
        else if (k == "secondary_ring") s.secondaryRing = v == "1";
        else if (k == "stars") s.stars = v == "1";
        else if (k == "star_field") s.earthStars = v == "earth";
        else if (k == "star_intensity") { if (readNum(v, d)) s.starIntensity = d; }
        else if (k == "star_amount") { if (readNum(v, d)) s.starAmount = d; }
        else if (k == "star_seed") { if (readNum(v, d) && std::fabs(d) < 2e9) s.starSeed = (int)d; }
        else if (k == "star_latitude") { if (readNum(v, d)) s.starLatitude = d; }
        else if (k == "star_time") { if (readNum(v, d)) s.starTime = d; }
        else if (k == "comets") s.comets = v == "1";
        else if (k == "comet_intensity") { if (readNum(v, d)) s.cometIntensity = d; }
        else if (k == "comet_amount") { if (readNum(v, d)) s.cometAmount = d; }
        else if (k == "rainbow") s.rainbow = v == "1";
        else if (k == "rainbow_radius") { if (readNum(v, d)) s.rainbowRadius = d; }
        else if (k == "rainbow_opacity") { if (readNum(v, d)) s.rainbowOpacity = d; }
        else if (k == "secondary_bow") s.secondaryBow = v != "0";
        else if (k == "blend_sun") s.blendSun = v != "0";
        else if (k == "blend_color") { if (readNum(v, d)) s.blendColor = d; }
        else if (k == "blend_luminance") { if (readNum(v, d)) s.blendLuminance = d; }
        else if (k == "blend_haze") s.blendHaze = v != "0";
        else if (k == "blend_fog") s.blendFog = v != "0";
        else if (k == "volumetric") s.volumetric = v == "1";
        else if (k == "volumetric_quality") { if (readNum(v, d)) s.volumetricQuality = d; }
        else if (k == "volumetric_density") { if (readNum(v, d)) s.volumetricDensity = d; }
        else if (k == "color_perspective") s.colorPerspective = v == "1";
        else if (k == "color_perspective_rgb") readCol(v, s.colorPerspectiveRgb);
        else if (k == "ibl") s.ibl = v == "1";
        else if (k == "ibl_intensity") { if (readNum(v, d)) s.iblIntensity = d; }
        else if (k == "shadows") { if (readNum(v, d)) s.shadows = d; }
        else if (k == "soft_shadows") s.softShadows = v == "1";
        else if (k == "shadow_softness") { if (readNum(v, d)) s.shadowSoftness = d; }
        else if (k == "sun_diffuse") { if (readNum(v, d)) s.sunDiffuse = d; }
        else if (k == "sun_specular") { if (readNum(v, d)) s.sunSpecular = d; }
        else if (k == "sky_dome") readCol(v, s.skyDome);
        else if (k == "bake_size") { if (readNum(v, d)) s.bakeSize = (int)d; }
        else if (k == "baked_fog") s.bakedFog = v;
        else known = false;
        if (!known) s.otherFields.emplace_back(k, v);
    }
    clampSkyLab(s);
    return s;
}

void clampSkyLab(SkyLab& s) {
    auto c255 = [](Vec3& c) { c = {std::clamp(c.x, 0.0, 255.0), std::clamp(c.y, 0.0, 255.0), std::clamp(c.z, 0.0, 255.0)}; };
    s.sunAzimuth = std::fmod(s.sunAzimuth, 360.0);
    if (s.sunAzimuth < 0) s.sunAzimuth += 360;
    s.sunAltitude = std::clamp(s.sunAltitude, -90.0, 90.0);
    s.diskIntensity = std::clamp(s.diskIntensity, 0.0, 100.0);
    s.glowIntensity = std::clamp(s.glowIntensity, 0.0, 100.0);
    s.diskSize = std::clamp(s.diskSize, 0.1, 45.0);
    s.horizonIllusion = std::clamp(s.horizonIllusion, 0.0, 200.0);
    s.hazeDensity = std::clamp(s.hazeDensity, 0.0, 100.0);
    s.hazeThickness = std::clamp(s.hazeThickness, 0.0, 100.0);
    s.hazeBase = std::clamp(s.hazeBase, -100.0, 100.0);
    s.shadows = std::clamp(s.shadows, 0.0, 100.0);
    s.shadowSoftness = std::clamp(s.shadowSoftness, 0.0, 100.0);
    s.sunDiffuse = std::clamp(s.sunDiffuse, -999.0, 999.0);
    s.sunSpecular = std::clamp(s.sunSpecular, -999.0, 999.0);
    for (Vec3* c : {&s.moonColor, &s.eclipseColor, &s.colorPerspectiveRgb, &s.cumulusColor, &s.sunColor, &s.customSky, &s.customGlow, &s.customHorizon, &s.offColor, &s.hazeColor, &s.groundColor, &s.skyDome}) c255(*c);
    for (SkyLab::CloudLayer* l : {&s.stratus, &s.cumulus}) {
        l->cover = std::clamp(l->cover, 0.0, 100.0);
        l->frequency = std::clamp(l->frequency, 2.0, 150.0);
        l->amplitude = std::clamp(l->amplitude, 50.0, 500.0);
        l->turbulence = std::clamp(l->turbulence, 0.0, 100.0);
        l->complexity = std::clamp(std::round(l->complexity), 1.0, 8.0);
    }
    s.cloudHeight = std::clamp(s.cloudHeight, 0.0, 100.0);
    s.moonAzimuth = std::fmod(s.moonAzimuth, 360.0);
    if (s.moonAzimuth < 0) s.moonAzimuth += 360;
    s.moonAltitude = std::clamp(s.moonAltitude, -90.0, 90.0);
    s.moonPhase = std::clamp(s.moonPhase, 0.0, 100.0);
    s.moonSize = std::clamp(s.moonSize, 0.1, 45.0);
    for (double* v : {&s.moonIntensity, &s.moonGlow, &s.earthshine, &s.moonSoftness, &s.coronaIntensity, &s.coronaSize, &s.lunarEclipse,
                      &s.ringIntensity, &s.ringRadius, &s.starIntensity, &s.cometIntensity, &s.rainbowRadius, &s.rainbowOpacity,
                      &s.blendColor, &s.blendLuminance, &s.volumetricQuality, &s.volumetricDensity})
        *v = std::clamp(*v, 0.0, 100.0);
    s.starAmount = std::clamp(std::round(s.starAmount), 0.0, 10000.0);
    s.cometAmount = std::clamp(std::round(s.cometAmount), 0.0, 50.0);
    s.starLatitude = std::clamp(s.starLatitude, -90.0, 90.0);
    s.starTime = std::fmod(s.starTime, 24.0);
    if (s.starTime < 0) s.starTime += 24;
    s.iblIntensity = std::clamp(s.iblIntensity, 0.0, 999.0);
    if (s.moonTexture.empty()) s.moonTexture = "builtin";
    s.bakeSize = s.bakeSize >= 8192 ? 8192 : s.bakeSize >= 4096 ? 4096 : 2048;
}

Vec3 skyLabSunDirection(const SkyLab& s) {
    const double a = s.sunAltitude * kDeg, z = s.sunAzimuth * kDeg;
    return {std::cos(a) * std::cos(z), std::cos(a) * std::sin(z), std::sin(a)};
}

void sunFromLightAngles(double lightPitch, double lightYaw, double& azimuth, double& altitude) {
    altitude = -lightPitch;
    azimuth = std::fmod(lightYaw + 180.0, 360.0);
    if (azimuth < 0) azimuth += 360;
    azimuth = std::round(azimuth * 1e6) / 1e6;
    if (azimuth >= 360) azimuth -= 360;
    altitude = std::round(altitude * 1e6) / 1e6;
    if (altitude == 0) altitude = 0; // no -0
}

void lightAnglesFromSun(double azimuth, double altitude, double& lightPitch, double& lightYaw) {
    lightPitch = -altitude;
    if (lightPitch == 0) lightPitch = 0;
    lightYaw = std::fmod(azimuth + 180.0, 360.0);
    if (lightYaw < 0) lightYaw += 360;
    lightYaw = std::round(lightYaw * 1e6) / 1e6;
    if (lightYaw >= 360) lightYaw -= 360;
}

SkyLab randomSkyLab(unsigned seed) {
    Rand r{hash32(seed * 2654435761U + 12345U)};
    SkyLab s;
    s.used = true;
    const double m = r.next();
    s.mode = m < 0.45 ? SkyMode::Soft : m < 0.8 ? SkyMode::Darker : SkyMode::Custom;
    s.sunAzimuth = std::round(r.range(0, 360));
    s.sunAltitude = std::round(r.next() < 0.35 ? r.range(-4, 10) : r.range(10, 70));
    auto warm = [&](double lo) { return Vec3{255, std::round(r.range(lo, 255)), std::round(r.range(lo - 40, 245))}; };
    s.sunColor = warm(190);
    s.diskIntensity = std::round(r.range(40, 100));
    s.glowIntensity = std::round(r.range(20, 90));
    s.diskSize = std::round(r.range(1.5, 6) * 10) / 10;
    s.horizonIllusion = std::round(r.range(0, 60));
    auto any = [&]() { return Vec3{std::round(r.range(0, 255)), std::round(r.range(0, 255)), std::round(r.range(0, 255))}; };
    s.customSky = any();
    s.customGlow = warm(120);
    s.customHorizon = any();
    s.hazeOn = r.next() < 0.85;
    s.hazeColor = Vec3{std::round(r.range(170, 255)), std::round(r.range(170, 255)), std::round(r.range(170, 255))};
    s.hazeDensity = std::round(r.range(5, 60));
    s.hazeThickness = std::round(r.range(5, 60));
    s.hazeBase = 0;
    s.groundColor = Vec3{std::round(r.range(40, 140)), std::round(r.range(40, 130)), std::round(r.range(30, 110))};
    auto layer = [&](SkyLab::CloudLayer& l, double onChance) {
        l.on = r.next() < onChance;
        l.cover = std::round(r.range(10, 70));
        l.frequency = std::round(r.range(15, 90));
        l.amplitude = std::round(r.range(60, 300));
        l.turbulence = std::round(r.range(0, 70));
        l.complexity = std::round(r.range(4, 8));
    };
    layer(s.stratus, 0.5);
    layer(s.cumulus, 0.6);
    s.cloudHeight = std::round(r.range(15, 70));
    const double g = std::round(r.range(140, 220));
    s.cumulusColor = Vec3{g, g, std::min(255.0, g + 10)};
    s.sphericalClouds = r.next() < 0.3;
    s.cloudSeed = (int)(r.next() * 9999) + 1;
    clampSkyLab(s);
    return s;
}

// ---- generator

SkyGenerator::SkyGenerator(const SkyLab& s, int width, const SkyExtras& extras) : s_(s), ex_(extras) {
    clampSkyLab(s_);
    w_ = std::max(8, width - width % 2);
    h_ = w_ / 2;
    const double pix = kPi / h_; // one pixel, in radians
    sunDir_ = skyLabSunDirection(s_);
    const double alt = s_.sunAltitude * kDeg;
    sunAlt_ = alt;
    const Vec3 sc = toLinear(s_.sunColor);
    const double peak = std::max({sc.x, sc.y, sc.z, 1e-6});
    sunLight_ = sc / peak; // brightness stays with the sky; the colour only tints
    sunTransmit_ = sunTransmittance(alt);
    const Vec3 transmitN = sunTransmit_ / std::max({sunTransmit_.x, sunTransmit_.y, sunTransmit_.z, 1e-6});
    dayLevel_ = 0.02 + 0.98 * smooth(-10 * kDeg, 8 * kDeg, alt);
    diskRadius_ = s_.diskSize * 0.5 * kDeg * (1 + s_.horizonIllusion / 100.0 * (1 - smooth(0, 25 * kDeg, std::fabs(alt))));
    physical_ = s_.mode == SkyMode::Soft || s_.mode == SkyMode::Darker;
    const bool physical = physical_;

    // ---- the moon and the eclipses
    if (s_.moonVisible) {
        SkyLab m = s_;
        m.sunAzimuth = s_.moonAzimuth;
        m.sunAltitude = s_.moonAltitude;
        moonDir_ = skyLabSunDirection(m);
        const double malt = s_.moonAltitude * kDeg;
        moonRadius_ = s_.moonSize * 0.5 * kDeg * (1 + s_.horizonIllusion / 100.0 * (1 - smooth(0, 25 * kDeg, std::fabs(malt))));
        moonCosOuter_ = std::cos(std::min(kPi, moonRadius_ * 1.6 + 2 * pix));
        moonUp_ = std::fabs(moonDir_.z) < 0.999 ? normalize(Vec3{0, 0, 1} - moonDir_ * moonDir_.z) : Vec3{0, 1, 0};
        moonRight_ = cross(moonDir_, moonUp_);
        moonTint_ = toLinear(s_.moonColor);
        // The lit side faces the sun; the phase says how much of it we see.
        const double theta = s_.moonPhase / 100.0 * 2 * kPi;
        double sx = dot(sunDir_, moonRight_), sy = dot(sunDir_, moonUp_);
        const double sl = std::hypot(sx, sy);
        if (sl < 1e-6) { sx = 1; sy = 0; } else { sx /= sl; sy /= sl; }
        moonLightDir_ = Vec3{sx * std::fabs(std::sin(theta)), sy * std::fabs(std::sin(theta)), -std::cos(theta)};
        double lit = (1 - std::cos(theta)) / 2;
        // Solar eclipse: the moon in front of the sun shows its dark side.
        eclipse_ = solarEclipseCover(s_);
        const double apart = std::acos(std::clamp(dot(moonDir_, sunDir_), -1.0, 1.0));
        if (apart < moonRadius_ + diskRadius_) { moonLightDir_ = Vec3{0, 0, -1}; lit = 0; }
        if (alt < -5 * kDeg) eclipse_ = 0; // below the horizon: nothing to see
        eclipseLight_ = std::max(1 - eclipse_, 0.0015);
        moonLight_ = lit * s_.moonIntensity / 100.0 * smooth(-3 * kDeg, 5 * kDeg, malt) * (1 - 0.9 * s_.lunarEclipse / 100.0);
        // The texture, in linear light, brightened so its average is a bright grey.
        if (s_.moonTexture != "none" && ex_.moon.rgb && ex_.moon.width > 1 && ex_.moon.height > 1) {
            moonTexW_ = ex_.moon.width;
            moonTexH_ = ex_.moon.height;
            moonMap_ = moonTexW_ >= moonTexH_ * 3 / 2;
            moonTex_.resize((size_t)moonTexW_ * moonTexH_ * 3);
            double mean = 0;
            for (size_t k = 0; k < moonTex_.size(); ++k) {
                moonTex_[k] = (float)toLinear((double)ex_.moon.rgb[k]);
                mean += moonTex_[k];
            }
            mean /= (double)moonTex_.size();
            const float scale = (float)std::clamp(0.55 / std::max(mean, 1e-4), 0.5, 4.0);
            for (float& v : moonTex_) v = std::min(1.0f, v * scale);
        }
    }
    if (eclipse_ > 0) dayLevel_ *= 0.02 + 0.98 * eclipseLight_;
    // Moonlight on a dark sky (none by day).
    const double moonNight = moonLight_ * (1 - smooth(-8 * kDeg, 4 * kDeg, alt));

    offLinear_ = toLinear(s_.offColor);
    groundLinear_ = toLinear(s_.groundColor) * (physical ? std::max(dayLevel_, 0.02 + 0.06 * moonNight) : 1.0);
    // Clouds: the sunlit side takes the sun's light (red at sunset, dim at night); the
    // shaded side the sky's own colour mixed with the cumulus colour; stratus a touch of
    // the sky dome colour (Bryce: stratus respond to sunlight and sky dome colour). At
    // night the moon lights them a little.
    {
        const double low = 1 - smooth(8 * kDeg, 35 * kDeg, alt);
        const Vec3 sunTint = lerp(Vec3{1, 1, 1}, sunLight_, 0.5);
        double night = physical ? std::max(smooth(-12 * kDeg, 6 * kDeg, alt), 0.006) : 1.0;
        if (physical && eclipse_ > 0) night *= 0.02 + 0.98 * eclipseLight_;
        cloudLit_ = physical ? mul(sunTint, lerp(Vec3{1, 1, 1}, transmitN, 0.15 + 0.8 * low)) * night : sunLight_;
        const Vec3 skyAmb = s_.mode == SkyMode::Custom ? toLinear(s_.customSky) : s_.mode == SkyMode::Off ? toLinear(s_.offColor) : Vec3{0.25, 0.32, 0.45};
        cloudShade_ = mul(toLinear(s_.cumulusColor), lerp(skyAmb * 1.6, Vec3{1, 1, 1}, 0.45)) * night;
        if (physical && moonNight > 0) {
            cloudLit_ += mul(moonTint_, Vec3{0.85, 0.9, 1.0}) * (0.12 * moonNight);
            cloudShade_ += Vec3{0.02, 0.025, 0.035} * moonNight;
        }
        stratusTint_ = toLinear(s_.skyDome);
    }
    // Blend with Sun (Soft / Darker): haze and fog take the colour of the sun's light and
    // its brightness (dim at night).
    if (physical && s_.blendSun) {
        const Vec3 tint = lerp(Vec3{1, 1, 1}, transmitN, s_.blendColor / 100.0) * (1.0 + (dayLevel_ - 1.0) * s_.blendLuminance / 100.0);
        if (s_.blendHaze) hazeTint_ = tint;
        if (s_.blendFog) fogTint_ = tint;
    }
    fogColor_ = mul(toLinear(ex_.fogColor), fogTint_);
    // The disk: the sun's colour, reddened low down (Soft / Darker) but kept bright.
    diskColor_ = physical ? mul(sunLight_, transmitN) : sunLight_;
    diskCosOuter_ = std::cos(std::min(kPi, diskRadius_ + 2 * pix));
    // Per column: the direction's compass part and how far round it is from the sun.
    cosYaw_.resize((size_t)w_);
    sinYaw_.resize((size_t)w_);
    azDiff_.resize((size_t)w_);
    for (int i = 0; i < w_; ++i) {
        // Inverse of skyUV (core/sky.cpp): u = 0.5 - yaw / 2pi.
        const double yaw = (0.5 - (i + 0.5) / w_) * 2 * kPi;
        cosYaw_[(size_t)i] = std::cos(yaw);
        sinYaw_[(size_t)i] = std::sin(yaw);
        azDiff_[(size_t)i] = wrapAngle(yaw - s_.sunAzimuth * kDeg);
    }
    srgb_.resize(kSrgbSteps);
    for (int i = 0; i < kSrgbSteps; ++i) srgb_[(size_t)i] = toSrgb((double)i / (kSrgbSteps - 1));

    // ---- stars and comets: placed once, then drawn row by row
    auto rowOf = [&](const Vec3& d) { return (0.5 - std::asin(std::clamp(d.z, -1.0, 1.0)) / kPi) * h_ - 0.5; };
    // Equatorial (right ascension, declination) to our sky: +X east, +Y north, +Z up.
    const double lat = s_.starLatitude * kDeg, lst = s_.starTime * 15 * kDeg;
    auto toSky = [&](double ra, double dec) {
        const double ha = lst - ra;
        const double east = -std::cos(dec) * std::sin(ha);
        const double north = std::sin(dec) * std::cos(lat) - std::cos(dec) * std::cos(ha) * std::sin(lat);
        const double up = std::sin(dec) * std::sin(lat) + std::cos(dec) * std::cos(ha) * std::cos(lat);
        return Vec3{east, north, up};
    };
    if (s_.stars && s_.starIntensity > 0 && s_.starAmount > 0) {
        Rand r{hash32((std::uint32_t)s_.starSeed * 2246822519U + 77U)};
        const int n = (int)s_.starAmount;
        const int count = s_.earthStars ? std::min(n, kStarCatalogueSize) : n;
        stars_.reserve((size_t)count);
        for (int i = 0; i < count; ++i) {
            double ra, dec, mag, kelvin;
            if (s_.earthStars) {
                const CatalogueStar& c = kStarCatalogue[i];
                ra = c.ra * kDeg; dec = c.dec * kDeg; mag = c.magnitude; kelvin = c.kelvin;
            } else {
                // Random places; brightness and colour as the real sky has them, by rank.
                ra = r.range(0, 2 * kPi);
                dec = std::asin(r.range(-1, 1));
                const int k = std::min(i, kStarCatalogueSize - 1);
                mag = kStarCatalogue[k].magnitude + (i > k ? (i - k) / 1600.0 : 0.0);
                kelvin = kStarCatalogue[(int)(r.next() * (kStarCatalogueSize - 1))].kelvin;
            }
            const Vec3 dir = toSky(ra, dec);
            if (dir.z < -0.03) continue;
            const double flux = std::pow(10.0, -0.4 * (mag + 1.46)); // Sirius = 1
            const double k01 = std::min(1.0, std::pow(flux, 0.3));
            StarPoint p;
            p.dir = dir;
            // Colour from temperature, kept pale (stars look nearly white to the eye).
            const double t = std::clamp(kelvin, 2500.0, 30000.0);
            const Vec3 warm{1.0, 0.72, 0.45}, white{1, 1, 1}, blue{0.72, 0.82, 1.0};
            const Vec3 tint = t < 6500 ? lerp(warm, white, (t - 2500) / 4000) : lerp(white, blue, std::min(1.0, (t - 6500) / 14000));
            p.color = lerp(white, tint, 0.55) * (s_.starIntensity / 100.0 * 1.6 * std::pow(flux, 0.55));
            p.sigma = pix * (0.5 + 0.4 * k01);
            stars_.push_back(p);
        }
        // Comets: a bright head and a tail pointing away from the sun.
        if (s_.comets && s_.cometIntensity > 0) {
            Rand cr{hash32((std::uint32_t)s_.starSeed * 40503U + 0xc0deU)};
            for (int i = 0; i < (int)s_.cometAmount; ++i) {
                const double z = cr.range(0.08, 0.85), a = cr.range(0, 2 * kPi);
                const double c = std::sqrt(1 - z * z);
                Comet m;
                m.head = Vec3{c * std::cos(a), c * std::sin(a), z};
                Vec3 away = (m.head * dot(sunDir_, m.head)) - sunDir_;
                if (length(away) < 1e-4) away = cross(m.head, Vec3{0, 0, 1});
                if (length(away) < 1e-4) away = Vec3{1, 0, 0};
                m.along = normalize(away);
                m.side = cross(m.head, m.along);
                m.length = cr.range(4, 14) * kDeg;
                m.width = m.length * 0.07;
                m.color = Vec3{0.85, 0.93, 1.0} * (s_.cometIntensity / 100.0);
                comets_.push_back(m);
            }
        }
        rowStars_.assign((size_t)h_, {});
        for (size_t k = 0; k < stars_.size(); ++k) {
            const double y = rowOf(stars_[k].dir);
            const int reach = (int)std::ceil(3 * stars_[k].sigma / pix) + 1;
            for (int j = std::max(0, (int)y - reach); j <= std::min(h_ - 1, (int)y + reach); ++j) rowStars_[(size_t)j].push_back((int)k);
        }
        rowComets_.assign((size_t)h_, {});
        for (size_t k = 0; k < comets_.size(); ++k) {
            const double y = rowOf(comets_[k].head);
            const int reach = (int)std::ceil((comets_[k].length + 4 * comets_[k].width) / pix) + 1;
            for (int j = std::max(0, (int)y - reach); j <= std::min(h_ - 1, (int)y + reach); ++j) rowComets_[(size_t)j].push_back((int)k);
        }
    }

    if (physical) {
        // Table: elevation 0..90 degrees (packed toward the horizon, where colour changes
        // fastest) x azimuth difference 0..180 degrees.
        lutE_ = 96;
        lutA_ = 64;
        lut_.resize((size_t)lutE_ * lutA_);
        const double mieScale = 0.25 + s_.glowIntensity / 50.0 * 0.75;
        // The sun's own brightness, so a white sun of any altitude keeps its sky; an
        // eclipse takes it away.
        const double sunPower = 20.0 * eclipseLight_;
        for (int i = 0; i < lutE_; ++i) {
            const double t = (double)i / (lutE_ - 1);
            const double e = t * t * (kPi / 2);
            for (int j = 0; j < lutA_; ++j) {
                const double a = (double)j / (lutA_ - 1) * kPi;
                const Vec3 d{std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(std::max(e, 0.0004))};
                const Vec3 sun{std::cos(alt), 0, std::sin(alt)};
                Vec3 r, m;
                scatter(normalize(d), sun, mieScale, r, m);
                // The glow takes the sun's colour; the blue doesn't (as in Bryce).
                Vec3 c = (r + mul(m, sunLight_)) * sunPower;
                c += Vec3{0.0035, 0.0052, 0.011} * 0.06; // night: a faint deep blue, never black
                c += Vec3{0.010, 0.016, 0.034} * (0.5 * moonNight); // a moonlit night is bluer
                lut_[(size_t)i * lutA_ + j] = toneMap(c, s_.mode == SkyMode::Darker);
            }
        }
    }
}

Vec3 SkyGenerator::atmosphere(double elevation, double azDiff) const {
    const double e = std::clamp(elevation, 0.0, kPi / 2);
    const double fi = std::sqrt(e / (kPi / 2)) * (lutE_ - 1);
    const double fj = std::clamp(std::fabs(azDiff) / kPi, 0.0, 1.0) * (lutA_ - 1);
    const int i0 = std::min((int)fi, lutE_ - 2), j0 = std::min((int)fj, lutA_ - 2);
    const double ti = fi - i0, tj = fj - j0;
    auto at = [&](int i, int j) { return lut_[(size_t)i * lutA_ + j]; };
    return lerp(lerp(at(i0, j0), at(i0, j0 + 1), tj), lerp(at(i0 + 1, j0), at(i0 + 1, j0 + 1), tj), ti);
}

// Everything that depends only on the elevation is worked out once per row.
struct SkyGenerator::Row {
    double el = 0, sinEl = 0, cosEl = 1;
    double haze = 0;       // haze amount 0..1
    double ground = 0;     // 0 = sky, 1 = all ground
    double fog = 0;        // fog amount 0..1
    Vec3 hazeColor;        // linear
    Vec3 skyCustom;        // Custom: sky colour before the glow
};

SkyGenerator::Row SkyGenerator::row(double el, double pixelAngle) const {
    Row r;
    r.el = el;
    r.sinEl = std::sin(el);
    r.cosEl = std::cos(el);
    // Haze: a band of haze colour at the horizon. Thickness sets how high it reaches,
    // density how strong it is, base height moves it down or up. Below the horizon it
    // thins out quickly (the ground near the eye is clear).
    if (s_.hazeOn && s_.hazeDensity > 0) {
        // Higher clouds widen the band (as in Bryce); 35, the default height, leaves it as is.
        const double width = (1.5 + s_.hazeThickness * 0.35) * kDeg * (0.5 + s_.cloudHeight / 70.0);
        const double shifted = el - s_.hazeBase * 0.1 * kDeg;
        const double fall = shifted >= 0 ? shifted / width : -shifted / (width * 0.35);
        r.haze = std::clamp(s_.hazeDensity / 100.0 * std::exp(-fall), 0.0, 1.0);
        r.hazeColor = mul(toLinear(s_.hazeColor), hazeTint_);
    }
    // Below the horizon the horizon's colour fades into the ground colour (Off stays one
    // plain colour all round).
    if (el < 0 && s_.mode != SkyMode::Off) r.ground = smooth(0, 6 * kDeg + pixelAngle, -el + pixelAngle * 0.5);
    // Fog in the sky: the map's fog, as a layer from its base height up through its
    // thickness (the eye at height 0). How far a line of sight runs inside the layer
    // decides how fogged it is, with the fog's own start and end distances.
    if (ex_.fogOn && ex_.fogThickness > 0) {
        const double base = ex_.fogBase, top = ex_.fogBase + ex_.fogThickness, eye = 0;
        double path = 0;
        const double sn = std::sin(el);
        if (sn > 1e-6) path = eye < base ? ex_.fogThickness / sn : eye <= top ? (top - eye) / sn : 0;
        else if (sn < -1e-6) path = eye > top ? ex_.fogThickness / -sn : eye >= base ? 1e30 : 0;
        else path = eye >= base && eye <= top ? 1e30 : 0;
        const double span = std::max(1.0, ex_.fogEnd - ex_.fogStart);
        r.fog = std::clamp((path - ex_.fogStart) / span, 0.0, 1.0);
    }
    if (s_.mode == SkyMode::Custom) {
        const double up = std::pow(std::max(0.0, r.sinEl), 0.5);
        r.skyCustom = lerp(toLinear(s_.customHorizon), toLinear(s_.customSky), up);
    }
    return r;
}

namespace {
// A ring of light at angle `radius` (radians) and width `width`, red a little inside and
// blue a little outside (as ice halos split light).
Vec3 halo(double angle, double radius, double width) {
    auto g = [&](double c) { const double x = (angle - c) / (width * 0.5); return std::exp(-x * x); };
    return Vec3{g(radius - width * 0.22), g(radius), g(radius + width * 0.22)};
}
// The rainbow's colours across its band: t = 0 violet (inside) .. 1 red (outside).
Vec3 spectrum(double t) {
    if (t <= 0 || t >= 1) return {};
    const double h = (1 - t) * 4.6; // hue sector 0 (red) .. 4.6 (violet)
    const double x = 1 - std::fabs(std::fmod(h, 2.0) - 1);
    Vec3 c;
    switch ((int)h) {
        case 0: c = {1, x, 0}; break;
        case 1: c = {x, 1, 0}; break;
        case 2: c = {0, 1, x}; break;
        case 3: c = {0, x, 1}; break;
        default: c = {x, 0, 1}; break;
    }
    return c * std::sqrt(std::sin(kPi * t));
}
} // namespace

Vec3 SkyGenerator::moonAlbedo(const Vec3& n, double u, double v) const {
    if (moonTex_.empty()) return moonTint_ * 0.8;
    double tu, tv;
    if (moonMap_) { // a map: longitude 0 (the middle) faces us
        tu = 0.5 + std::atan2(n.x, n.z) / (2 * kPi);
        tv = 0.5 - std::asin(std::clamp(n.y, -1.0, 1.0)) / kPi;
    } else {        // a photo of the face
        tu = 0.5 + 0.5 * u;
        tv = 0.5 - 0.5 * v;
    }
    const double fx = std::clamp(tu * moonTexW_ - 0.5, 0.0, moonTexW_ - 1.001), fy = std::clamp(tv * moonTexH_ - 0.5, 0.0, moonTexH_ - 1.001);
    const int x0 = (int)fx, y0 = (int)fy;
    const double ax = fx - x0, ay = fy - y0;
    auto at = [&](int x, int y) {
        const float* p = &moonTex_[((size_t)y * moonTexW_ + x) * 3];
        return Vec3{p[0], p[1], p[2]};
    };
    const Vec3 c = lerp(lerp(at(x0, y0), at(x0 + 1, y0), ax), lerp(at(x0, y0 + 1), at(x0 + 1, y0 + 1), ax), ay);
    return mul(c, moonTint_);
}

Vec3 SkyGenerator::moonAt(const Vec3& d, double cosToMoon, double pixelAngle, double& cover) const {
    cover = 0;
    if (!s_.moonVisible || cosToMoon < moonCosOuter_) return {};
    const double R = std::max(moonRadius_, 1e-6);
    const double u = dot(d, moonRight_) / R, v = dot(d, moonUp_) / R;
    const double rr = std::sqrt(u * u + v * v);
    const double soft = s_.moonSoftness / 100.0 * 0.12 + pixelAngle / R * 0.7;
    cover = 1 - smooth(1 - soft, 1 + soft * 0.4, rr);
    if (cover <= 0) return {};
    const double q = std::min(rr, 0.9999);
    const double nz = std::sqrt(1 - q * q);
    const Vec3 n = rr > 1e-9 ? Vec3{u / rr * q, v / rr * q, nz} : Vec3{0, 0, 1};
    // Lit side, with a terminator as soft as the edge; earthshine lights the rest a little.
    const double k = 0.03 + s_.moonSoftness / 100.0 * 0.25;
    double light = smooth(-k, k, dot(n, moonLightDir_)) + s_.earthshine / 100.0 * 0.16;
    Vec3 c = moonAlbedo(n, u, v) * light;
    // Lunar eclipse: Earth's round shadow comes across from the east (left) side.
    if (s_.lunarEclipse > 0) {
        const double a = s_.lunarEclipse / 100.0, U = 2.6;
        const double off = (1 + U + 0.5) * (1 - a);
        const double du = u + off, dv = v - 0.12 * (1 - a);
        const double shadow = 1 - smooth(U - 0.35, U + 0.35, std::sqrt(du * du + dv * dv));
        c = lerp(c, mul(c, toLinear(s_.eclipseColor)) * 0.45, shadow);
    }
    return c * (s_.moonIntensity / 100.0 * 1.15);
}

void SkyGenerator::addStars(int j, std::vector<Vec3>& buf) const {
    if (rowStars_.empty()) return;
    const double el = (0.5 - (j + 0.5) / h_) * kPi;
    const double cosEl = std::cos(el), sinEl = std::sin(el);
    const double colAngle = 2 * kPi / w_ * std::max(cosEl, 0.02); // a column's width on the sky here
    auto pixelDir = [&](int i) { return Vec3{cosEl * cosYaw_[(size_t)i], cosEl * sinYaw_[(size_t)i], sinEl}; };
    auto columnOf = [&](const Vec3& d) { return (int)std::floor((0.5 - std::atan2(d.y, d.x) / (2 * kPi)) * w_); };
    for (int k : rowStars_[(size_t)j]) {
        const StarPoint& p = stars_[(size_t)k];
        const int c0 = columnOf(p.dir);
        const int reach = std::min(w_ / 2, (int)std::ceil(3 * p.sigma / colAngle) + 1);
        for (int di = -reach; di <= reach; ++di) {
            const int i = ((c0 + di) % w_ + w_) % w_;
            const double a2 = 2 * (1 - dot(pixelDir(i), p.dir)); // angle squared (small angles)
            const double wgt = std::exp(-a2 / (2 * p.sigma * p.sigma));
            if (wgt > 0.002) buf[(size_t)i] += p.color * wgt;
        }
    }
    for (int k : rowComets_[(size_t)j]) {
        const Comet& m = comets_[(size_t)k];
        const int c0 = columnOf(m.head);
        const int reach = std::min(w_ / 2, (int)std::ceil((m.length + 4 * m.width) / colAngle) + 1);
        for (int di = -reach; di <= reach; ++di) {
            const int i = ((c0 + di) % w_ + w_) % w_;
            const Vec3 d = pixelDir(i);
            if (dot(d, m.head) < std::cos(m.length + 4 * m.width)) continue;
            const double x = dot(d, m.along), y = dot(d, m.side) - 0.15 / m.length * x * x; // a slight curve
            const double hs = m.width * 0.35;
            double v = 1.6 * std::exp(-(x * x + y * y) / (2 * hs * hs));
            if (x > 0 && x < m.length) {
                const double wt = m.width * (0.25 + 0.75 * x / m.length);
                v += 0.6 * std::exp(-y * y / (2 * wt * wt)) * std::pow(1 - x / m.length, 1.3);
            }
            buf[(size_t)i] += m.color * v;
        }
    }
}

Vec3 SkyGenerator::pixel(const Row& r, double cosYaw, double sinYaw, double azDiff, double pixelAngle, const Vec3& star) const {
    const Vec3 d{r.cosEl * cosYaw, r.cosEl * sinYaw, r.sinEl};
    const double cosToSun = dot(d, sunDir_);
    const bool physical = physical_;

    // The sky above the horizon (at the horizon for everything below it).
    Vec3 sky;
    if (physical) {
        // Kept a little above the horizon: the plain physics gives the very horizon a
        // beige line, while Bryce's is pale; sunsets keep their colour (it comes from the
        // sun's light, not from this line).
        sky = atmosphere(std::max(r.el, 1.5 * kDeg), azDiff);
    } else if (s_.mode == SkyMode::Custom) {
        const double toSun = std::acos(std::clamp(cosToSun, -1.0, 1.0));
        const double glow = s_.glowIntensity / 100.0 * std::exp(-toSun / (0.35 + s_.glowIntensity / 100.0 * 0.6));
        sky = lerp(r.skyCustom, toLinear(s_.customGlow), std::clamp(glow, 0.0, 1.0));
    } else {
        sky = offLinear_;
    }

    // Behind the moon: the stars, the sun's disk and the corona. In front of everything
    // that is in space, the moon itself; then the clouds, rings, rainbow, haze, ground, fog.
    double moonCover = 0;
    const double cosToMoon = s_.moonVisible ? dot(d, moonDir_) : -1;
    const Vec3 moon = moonAt(d, cosToMoon, pixelAngle, moonCover);
    const double behind = 1 - moonCover;

    // Stars and comets show where the sky is dark.
    if (star.x + star.y + star.z > 0) {
        const double lum = 0.2126 * sky.x + 0.7152 * sky.y + 0.0722 * sky.z;
        sky += star * ((1 - smooth(0.01, 0.10, lum)) * behind);
    }

    // The sun's disk: soft edge one pixel wide, a little darker at its rim, added on top
    // of the sky so it stays bright at sunset. Behind the haze and the ground.
    if (s_.sunVisible && cosToSun > diskCosOuter_ - (eclipse_ > 0.85 ? 1 : 0)) {
        const double toSun = std::acos(std::clamp(cosToSun, -1.0, 1.0));
        if (s_.diskIntensity > 0 && cosToSun > diskCosOuter_) {
            const double edge = std::max(pixelAngle, diskRadius_ * 0.02);
            const double cover = 1 - smooth(diskRadius_ - edge * 0.5, diskRadius_ + edge * 0.5, toSun);
            const double rr = std::clamp(toSun / std::max(diskRadius_, 1e-6), 0.0, 1.0);
            const double limb = 0.75 + 0.25 * std::sqrt(std::max(0.0, 1 - rr * rr));
            sky += diskColor_ * (cover * limb * s_.diskIntensity / 100.0 * behind);
        }
        // A total (or nearly total) solar eclipse shows the corona round the sun.
        if (eclipse_ > 0.85 && s_.coronaIntensity > 0 && toSun > diskRadius_ * 0.9) {
            const double k = s_.coronaIntensity / 100.0 * smooth(0.85, 1.0, eclipse_);
            const double reach = diskRadius_ * (0.25 + s_.coronaSize / 100.0 * 1.6);
            sky += Vec3{0.92, 0.95, 1.0} * (k * 0.9 * std::exp(-(toSun - diskRadius_) / reach) * behind);
        }
    }
    if (moonCover > 0) sky += moon * moonCover;
    // The moon's glow.
    if (s_.moonVisible && s_.moonGlow > 0 && moonLight_ > 0) {
        const double a = std::acos(std::clamp(cosToMoon, -1.0, 1.0));
        if (a < moonRadius_ * 40) sky += moonTint_ * (s_.moonGlow / 100.0 * moonLight_ * 0.16 * std::exp(-std::max(0.0, a - moonRadius_) / (moonRadius_ * 3 + 0.02)));
    }
    sky = Vec3{std::min(sky.x, 1.0), std::min(sky.y, 1.0), std::min(sky.z, 1.0)};

    if (r.el > 0 && (s_.stratus.on || s_.cumulus.on)) sky = clouds(sky, d, cosToSun, pixelAngle);

    // Rings round the sun and the moon (ice halos).
    if (s_.rings && s_.ringIntensity > 0) {
        const double R = std::max(1.0, s_.ringRadius / 100.0 * 44) * kDeg, wdt = (0.8 + s_.ringRadius / 100.0 * 44 * 0.06) * kDeg;
        auto ringsAt = [&](double cosTo, const Vec3& light, double amp) {
            if (amp <= 0) return;
            const double a = std::acos(std::clamp(cosTo, -1.0, 1.0));
            Vec3 h = halo(a, R, wdt);
            if (s_.secondaryRing) h += halo(a, R * 2.09, wdt * 1.6) * 0.45;
            sky += mul(h, light) * amp;
        };
        if (s_.sunVisible) ringsAt(cosToSun, physical ? diskColor_ : sunLight_, s_.ringIntensity / 100.0 * 0.35 * (physical ? dayLevel_ : 1.0) * smooth(-2 * kDeg, 1 * kDeg, sunAlt_));
        if (s_.moonVisible) ringsAt(cosToMoon, moonTint_, s_.ringIntensity / 100.0 * 0.3 * moonLight_);
    }
    // The rainbow, opposite the sun, while the sun is up.
    if (s_.rainbow && s_.rainbowOpacity > 0 && r.el > 0 && sunAlt_ > 0) {
        const double a = std::acos(std::clamp(-cosToSun, -1.0, 1.0)) / kDeg;
        const double bw = 2.0 + s_.rainbowRadius / 100.0 * 9;
        Vec3 c = spectrum((a - (41.5 - bw / 2)) / bw);
        if (s_.secondaryBow) c += spectrum(1 - (a - (51.0 - bw * 0.7)) / (bw * 1.4)) * 0.43;
        const double amp = s_.rainbowOpacity / 100.0 * 1.1 * smooth(0, 3 * kDeg, sunAlt_) * (physical ? dayLevel_ : 1.0);
        sky += mul(c, physical ? diskColor_ : sunLight_) * amp;
    }
    sky = Vec3{std::min(sky.x, 1.0), std::min(sky.y, 1.0), std::min(sky.z, 1.0)};

    if (r.haze > 0) sky = lerp(sky, r.hazeColor, r.haze);
    if (r.ground > 0) sky = lerp(sky, groundLinear_, r.ground);
    if (r.fog > 0) sky = lerp(sky, fogColor_, r.fog);
    return sky;
}

Vec3 SkyGenerator::radiance(const Vec3& dir) const {
    const Vec3 d = normalize(dir);
    const double el = std::asin(std::clamp(d.z, -1.0, 1.0));
    const double yaw = std::atan2(d.y, d.x);
    const double c = std::hypot(d.x, d.y);
    return pixel(row(el, 1e-3), c > 1e-12 ? d.x / c : 1, c > 1e-12 ? d.y / c : 0, wrapAngle(yaw - s_.sunAzimuth * kDeg), 1e-3, Vec3{});
}

void SkyGenerator::rows(int y0, int y1, unsigned char* rgb) const {
    const double pixelAngle = kPi / h_;
    std::vector<Vec3> starBuf(rowStars_.empty() ? 0 : (size_t)w_);
    const Vec3 none{};
    for (int j = std::max(0, y0); j < std::min(y1, h_); ++j) {
        const Row r = row((0.5 - (j + 0.5) / h_) * kPi, pixelAngle);
        if (!starBuf.empty()) {
            std::fill(starBuf.begin(), starBuf.end(), Vec3{});
            addStars(j, starBuf);
        }
        unsigned char* p = rgb + (size_t)j * w_ * 3;
        for (int i = 0; i < w_; ++i, p += 3) {
            const Vec3 c = pixel(r, cosYaw_[i], sinYaw_[i], azDiff_[i], pixelAngle, starBuf.empty() ? none : starBuf[(size_t)i]);
            // A tiny fixed dither so smooth gradients don't band in 8 bits. Under half a
            // step even with the table's rounding, so a plain colour stays exact.
            const double n = ((hash32((std::uint32_t)(j * 7919 + i * 104729) ^ 0x5bd1e995U) >> 8) / 16777216.0 - 0.5) * 0.6;
            for (int k = 0; k < 3; ++k) {
                const int q = (int)std::lround(std::clamp(c[k], 0.0, 1.0) * (kSrgbSteps - 1));
                p[k] = (unsigned char)std::clamp((int)std::lround(srgb_[(size_t)q] * 255.0 + n), 0, 255);
            }
        }
    }
}

Vec3 SkyGenerator::clouds(const Vec3& sky, const Vec3& d, double cosToSun, double pixelAngle) const {
    Vec3 out = sky;
    // Heights in metres: cumulus 600 m .. 4.6 km, stratus two and a half times higher.
    const double cumulusH = 600 + s_.cloudHeight * 40, stratusH = cumulusH * 2.5;
    const Vec3 sunFlat = normalize(Vec3{sunDir_.x, sunDir_.y, 0} + Vec3{1e-9, 0, 0});
    auto layer = [&](const SkyLab::CloudLayer& l, double height, double scale, std::uint32_t salt, bool thick) {
        if (!l.on || l.cover <= 0) return;
        const double t = toLayer(d, height);
        const double size = scale / l.frequency; // metres per noise unit
        const std::uint32_t seed = hash32((std::uint32_t)s_.cloudSeed * 7919U + salt);
        Vec3 p{d.x * t / size, d.y * t / size, 0};
        if (!thick) {
            // Stratus are drawn out in streaks along one direction (set by the seed).
            const double a = (hash32(seed ^ 0x77U) >> 8) * (kPi / 16777216.0);
            const double u = p.x * std::cos(a) + p.y * std::sin(a), v = -p.x * std::sin(a) + p.y * std::cos(a);
            p = Vec3{u * 0.3, v, 0};
        }
        // A pixel's footprint on the layer, stretched toward the horizon.
        const double footprint = t * pixelAngle / std::sqrt(std::max(d.z, 0.004)) / size;
        const int octaves = (int)l.complexity;
        auto density = [&](double x, double y, double fp) {
            // Turbulence stirs the pattern (it bends the coordinates by more noise).
            const double warp = l.turbulence / 100.0 * 1.4;
            if (warp > 0) {
                const double wx = fbm(x * 0.7 + 31.7, y * 0.7 - 12.1, seed ^ 0xa5a5U, 3, fp * 0.7);
                const double wy = fbm(x * 0.7 - 8.3, y * 0.7 + 44.9, seed ^ 0x5a5aU, 3, fp * 0.7);
                x += wx * warp;
                y += wy * warp;
            }
            double n = 0.5 + 0.5 * fbm(x, y, seed, octaves, fp) / 0.55;
            // Spherical: rounder, denser clumps (cells blended into the noise).
            if (s_.sphericalClouds) {
                const double c = cells(x * 0.45, y * 0.45, seed ^ 0x1234U);
                const double blob = 1 - smooth(0.0, 0.75, c + (n - 0.5) * 0.5);
                n = 0.35 * n + 0.75 * blob;
            }
            // Cover sets how much passes the threshold; amplitude how hard the edge is.
            const double threshold = 0.78 - l.cover / 100.0 * 0.56;
            return std::clamp((n - threshold) * (l.amplitude / 100.0) * 2.2, 0.0, 1.0);
        };
        if (std::exp(-t / (height * 25.0)) < 0.004) return;
        double dens = density(p.x, p.y, footprint);
        // Far away the pattern averages out; fade the layer into the sky there.
        const double far = std::exp(-t / (height * 25.0));
        dens *= far * smooth(0.0, 2.5 * kDeg, std::asin(std::clamp(d.z, -1.0, 1.0)));
        if (dens <= 0) return;
        Vec3 col;
        if (thick) {
            // Lit where the cloud thins toward the sun, shaded where it thickens.
            const double step = 0.18;
            const double toward = density(p.x + sunFlat.x * step, p.y + sunFlat.y * step, footprint);
            const double sunUp = std::clamp(sunDir_.z * 2 + 0.3, 0.0, 1.0);
            const double light = std::clamp(0.62 + (dens - toward) * 1.6 * (0.4 + 0.6 * sunUp) - dens * 0.15, 0.0, 1.0);
            col = lerp(cloudShade_, cloudLit_, light);
            // A silver lining when looking toward the sun through thin edges.
            const double rim = std::pow(std::max(0.0, cosToSun), 12) * (1 - dens) * 1.2;
            col += cloudLit_ * rim;
            dens = std::min(1.0, dens * 1.15);
        } else {
            // Stratus: thin, bright, see-through.
            col = cloudLit_ * 0.97 + stratusTint_ * 0.2 + cloudShade_ * 0.08;
            col += cloudLit_ * (std::pow(std::max(0.0, cosToSun), 16) * 0.5);
            dens *= 0.85;
        }
        col = Vec3{std::min(col.x, 1.0), std::min(col.y, 1.0), std::min(col.z, 1.0)};
        out = lerp(out, col, dens);
    };
    layer(s_.stratus, stratusH, 90000, 0x51a7U, false); // higher, behind
    layer(s_.cumulus, cumulusH, 40000, 0xc0ffU, true);
    return out;
}

void SkyGenerator::rowsParallel(int y0, int y1, unsigned char* rgb, int threads) const {
    if (threads <= 0) threads = (int)std::max(1u, std::thread::hardware_concurrency());
    y0 = std::max(0, y0);
    y1 = std::min(y1, h_);
    const int n = y1 - y0;
    threads = std::max(1, std::min(threads, n / 4));
    if (threads == 1) { rows(y0, y1, rgb); return; }
    // Rows interleaved in small bands so every thread gets some sky and some ground.
    std::vector<std::thread> pool;
    for (int k = 0; k < threads; ++k)
        pool.emplace_back([=] {
            for (int y = y0 + k * 4; y < y1; y += threads * 4) rows(y, std::min(y + 4, y1), rgb);
        });
    for (std::thread& t : pool) t.join();
}

double solarEclipseCover(const SkyLab& in) {
    SkyLab s = in;
    clampSkyLab(s);
    if (!s.moonVisible) return 0;
    auto radius = [&](double size, double altDeg) {
        return size * 0.5 * kDeg * (1 + s.horizonIllusion / 100.0 * (1 - smooth(0, 25 * kDeg, std::fabs(altDeg * kDeg))));
    };
    const double rs = radius(s.diskSize, s.sunAltitude), rm = radius(s.moonSize, s.moonAltitude);
    SkyLab m = s;
    m.sunAzimuth = s.moonAzimuth;
    m.sunAltitude = s.moonAltitude;
    // The angle between them, from the chord (exact even when they nearly coincide).
    const double d = 2 * std::asin(std::min(1.0, length(skyLabSunDirection(s) - skyLabSunDirection(m)) / 2));
    if (d >= rs + rm) return 0;
    if (d <= rm - rs + 1e-9) return 1;            // the moon covers it all
    if (d <= rs - rm + 1e-9) return rm * rm / (rs * rs); // a ring of sun round a smaller moon
    // Two overlapping circles (small angles: flat).
    const double a1 = rs * rs * std::acos(std::clamp((d * d + rs * rs - rm * rm) / (2 * d * rs), -1.0, 1.0));
    const double a2 = rm * rm * std::acos(std::clamp((d * d + rm * rm - rs * rs) / (2 * d * rm), -1.0, 1.0));
    const double a3 = 0.5 * std::sqrt(std::max(0.0, (-d + rs + rm) * (d + rs - rm) * (d - rs + rm) * (d + rs + rm)));
    return std::clamp((a1 + a2 - a3) / (kPi * rs * rs), 0.0, 1.0);
}

std::vector<unsigned char> generateSky(const SkyLab& s, int width, const SkyExtras& extras) {
    SkyGenerator g(s, width, extras);
    std::vector<unsigned char> out((size_t)g.width() * g.height() * 3);
    g.rowsParallel(0, g.height(), out.data());
    return out;
}

} // namespace scal
