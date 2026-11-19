#include "core/skylab.hpp"
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
    put("shadows", num(s.shadows));
    put("soft_shadows", s.softShadows ? "1" : "0");
    put("shadow_softness", num(s.shadowSoftness));
    put("sun_diffuse", num(s.sunDiffuse));
    put("sun_specular", num(s.sunSpecular));
    put("sky_dome", col(s.skyDome));
    put("bake_size", std::to_string(s.bakeSize));
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
        else if (k == "shadows") { if (readNum(v, d)) s.shadows = d; }
        else if (k == "soft_shadows") s.softShadows = v == "1";
        else if (k == "shadow_softness") { if (readNum(v, d)) s.shadowSoftness = d; }
        else if (k == "sun_diffuse") { if (readNum(v, d)) s.sunDiffuse = d; }
        else if (k == "sun_specular") { if (readNum(v, d)) s.sunSpecular = d; }
        else if (k == "sky_dome") readCol(v, s.skyDome);
        else if (k == "bake_size") { if (readNum(v, d)) s.bakeSize = (int)d; }
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
    for (Vec3* c : {&s.cumulusColor, &s.sunColor, &s.customSky, &s.customGlow, &s.customHorizon, &s.offColor, &s.hazeColor, &s.groundColor, &s.skyDome}) c255(*c);
    for (SkyLab::CloudLayer* l : {&s.stratus, &s.cumulus}) {
        l->cover = std::clamp(l->cover, 0.0, 100.0);
        l->frequency = std::clamp(l->frequency, 2.0, 150.0);
        l->amplitude = std::clamp(l->amplitude, 50.0, 500.0);
        l->turbulence = std::clamp(l->turbulence, 0.0, 100.0);
        l->complexity = std::clamp(std::round(l->complexity), 1.0, 8.0);
    }
    s.cloudHeight = std::clamp(s.cloudHeight, 0.0, 100.0);
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

SkyGenerator::SkyGenerator(const SkyLab& s, int width) : s_(s) {
    clampSkyLab(s_);
    w_ = std::max(8, width - width % 2);
    h_ = w_ / 2;
    sunDir_ = skyLabSunDirection(s_);
    const double alt = s_.sunAltitude * kDeg;
    const Vec3 sc = toLinear(s_.sunColor);
    const double peak = std::max({sc.x, sc.y, sc.z, 1e-6});
    sunLight_ = sc / peak; // brightness stays with the sky; the colour only tints
    sunTransmit_ = sunTransmittance(alt);
    dayLevel_ = 0.02 + 0.98 * smooth(-10 * kDeg, 8 * kDeg, alt);
    diskRadius_ = s_.diskSize * 0.5 * kDeg * (1 + s_.horizonIllusion / 100.0 * (1 - smooth(0, 25 * kDeg, std::fabs(alt))));

    const bool physical = s_.mode == SkyMode::Soft || s_.mode == SkyMode::Darker;
    offLinear_ = toLinear(s_.offColor);
    groundLinear_ = toLinear(s_.groundColor) * (physical ? dayLevel_ : 1.0);
    // Clouds: the sunlit side takes the sun's light (red at sunset, dim at night); the
    // shaded side the sky's own colour mixed with the cumulus colour; stratus a touch of
    // the sky dome colour (Bryce: stratus respond to sunlight and sky dome colour).
    {
        const double m = std::max({sunTransmit_.x, sunTransmit_.y, sunTransmit_.z, 1e-6});
        // White at midday, the sunset's colours when the sun is low.
        const double low = 1 - smooth(8 * kDeg, 35 * kDeg, alt);
        const Vec3 sunTint = lerp(Vec3{1, 1, 1}, sunLight_, 0.5);
        // At night (until the moon comes) clouds are dark shapes, a shade over the sky.
        const double night = physical ? std::max(smooth(-12 * kDeg, 6 * kDeg, alt), 0.006) : 1.0;
        cloudLit_ = physical ? mul(sunTint, lerp(Vec3{1, 1, 1}, sunTransmit_ / m, 0.15 + 0.8 * low)) * night : sunLight_;
        const Vec3 skyAmb = s_.mode == SkyMode::Custom ? toLinear(s_.customSky) : s_.mode == SkyMode::Off ? toLinear(s_.offColor) : Vec3{0.25, 0.32, 0.45};
        cloudShade_ = mul(toLinear(s_.cumulusColor), lerp(skyAmb * 1.6, Vec3{1, 1, 1}, 0.45)) * night;
        stratusTint_ = toLinear(s_.skyDome);
    }
    // The disk: the sun's colour, reddened low down (Soft / Darker) but kept bright.
    diskColor_ = sunLight_;
    if (physical) diskColor_ = mul(sunLight_, sunTransmit_ / std::max({sunTransmit_.x, sunTransmit_.y, sunTransmit_.z, 1e-6}));
    diskCosOuter_ = std::cos(std::min(kPi, diskRadius_ + 2 * kPi / h_));
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

    if (physical) {
        // Table: elevation 0..90 degrees (packed toward the horizon, where colour changes
        // fastest) x azimuth difference 0..180 degrees.
        lutE_ = 96;
        lutA_ = 64;
        lut_.resize((size_t)lutE_ * lutA_);
        const double mieScale = 0.25 + s_.glowIntensity / 50.0 * 0.75;
        // The sun's own brightness, so a white sun of any altitude keeps its sky.
        const double sunPower = 20.0;
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
    Vec3 hazeColor;        // linear
    Vec3 skyCustom;        // Custom: sky colour before the glow
};

SkyGenerator::Row SkyGenerator::row(double el, double pixelAngle) const {
    Row r;
    r.el = el;
    r.sinEl = std::sin(el);
    r.cosEl = std::cos(el);
    const bool physical = s_.mode == SkyMode::Soft || s_.mode == SkyMode::Darker;
    // Haze: a band of haze colour at the horizon. Thickness sets how high it reaches,
    // density how strong it is, base height moves it down or up. Below the horizon it
    // thins out quickly (the ground near the eye is clear).
    if (s_.hazeOn && s_.hazeDensity > 0) {
        // Higher clouds widen the band (as in Bryce); 35, the default height, leaves it as is.
        const double width = (1.5 + s_.hazeThickness * 0.35) * kDeg * (0.5 + s_.cloudHeight / 70.0);
        const double shifted = el - s_.hazeBase * 0.1 * kDeg;
        const double fall = shifted >= 0 ? shifted / width : -shifted / (width * 0.35);
        r.haze = std::clamp(s_.hazeDensity / 100.0 * std::exp(-fall), 0.0, 1.0);
        r.hazeColor = toLinear(s_.hazeColor);
        if (physical) {
            const double m = std::max({sunTransmit_.x, sunTransmit_.y, sunTransmit_.z, 1e-6});
            r.hazeColor = mul(r.hazeColor, lerp(Vec3{1, 1, 1}, sunTransmit_ / m, 0.6)) * dayLevel_;
        }
    }
    // Below the horizon the horizon's colour fades into the ground colour (Off stays one
    // plain colour all round).
    if (el < 0 && s_.mode != SkyMode::Off) r.ground = smooth(0, 6 * kDeg + pixelAngle, -el + pixelAngle * 0.5);
    if (s_.mode == SkyMode::Custom) {
        const double up = std::pow(std::max(0.0, r.sinEl), 0.5);
        r.skyCustom = lerp(toLinear(s_.customHorizon), toLinear(s_.customSky), up);
    }
    return r;
}

Vec3 SkyGenerator::pixel(const Row& r, double cosYaw, double sinYaw, double azDiff, double pixelAngle) const {
    const Vec3 d{r.cosEl * cosYaw, r.cosEl * sinYaw, r.sinEl};
    const double cosToSun = dot(d, sunDir_);
    const bool physical = s_.mode == SkyMode::Soft || s_.mode == SkyMode::Darker;

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

    // The sun's disk: soft edge one pixel wide, a little darker at its rim, added on top
    // of the sky so it stays bright at sunset. Behind the haze and the ground.
    if (s_.sunVisible && s_.diskIntensity > 0 && cosToSun > diskCosOuter_) {
        const double toSun = std::acos(std::clamp(cosToSun, -1.0, 1.0));
        const double edge = std::max(pixelAngle, diskRadius_ * 0.02);
        const double cover = 1 - smooth(diskRadius_ - edge * 0.5, diskRadius_ + edge * 0.5, toSun);
        const double rr = std::clamp(toSun / std::max(diskRadius_, 1e-6), 0.0, 1.0);
        const double limb = 0.75 + 0.25 * std::sqrt(std::max(0.0, 1 - rr * rr));
        sky += diskColor_ * (cover * limb * s_.diskIntensity / 100.0);
        sky = Vec3{std::min(sky.x, 1.0), std::min(sky.y, 1.0), std::min(sky.z, 1.0)};
    }

    if (r.el > 0 && (s_.stratus.on || s_.cumulus.on)) sky = clouds(sky, d, cosToSun, pixelAngle);

    if (r.haze > 0) sky = lerp(sky, r.hazeColor, r.haze);
    if (r.ground > 0) sky = lerp(sky, groundLinear_, r.ground);
    return sky;
}

Vec3 SkyGenerator::radiance(const Vec3& dir) const {
    const Vec3 d = normalize(dir);
    const double el = std::asin(std::clamp(d.z, -1.0, 1.0));
    const double yaw = std::atan2(d.y, d.x);
    const double c = std::hypot(d.x, d.y);
    return pixel(row(el, 1e-3), c > 1e-12 ? d.x / c : 1, c > 1e-12 ? d.y / c : 0, wrapAngle(yaw - s_.sunAzimuth * kDeg), 1e-3);
}

void SkyGenerator::rows(int y0, int y1, unsigned char* rgb) const {
    const double pixelAngle = kPi / h_;
    for (int j = std::max(0, y0); j < std::min(y1, h_); ++j) {
        const Row r = row((0.5 - (j + 0.5) / h_) * kPi, pixelAngle);
        unsigned char* p = rgb + (size_t)j * w_ * 3;
        for (int i = 0; i < w_; ++i, p += 3) {
            const Vec3 c = pixel(r, cosYaw_[i], sinYaw_[i], azDiff_[i], pixelAngle);
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

std::vector<unsigned char> generateSky(const SkyLab& s, int width) {
    SkyGenerator g(s, width);
    std::vector<unsigned char> out((size_t)g.width() * g.height() * 3);
    g.rowsParallel(0, g.height(), out.data());
    return out;
}

} // namespace scal
