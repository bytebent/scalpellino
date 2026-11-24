#include "core/plane.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace scal {

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
// The same integer hash as the shaders' (32-bit unsigned arithmetic wraps the same way).
double hash01(std::int64_t ix, std::int64_t iy, int seed) {
    std::uint32_t h = (std::uint32_t)ix * 374761393u + (std::uint32_t)iy * 668265263u + (std::uint32_t)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (double)(h & 0xffffffu) / 16777215.0;
}
} // namespace

bool EndlessPlane::operator==(const EndlessPlane& o) const {
    return id == o.id && z == o.z && face.texture == o.face.texture && face.u.axis == o.face.u.axis && face.u.offset == o.face.u.offset &&
           face.v.axis == o.face.v.axis && face.v.offset == o.face.v.offset && face.rotation == o.face.rotation &&
           face.scaleU == o.face.scaleU && face.scaleV == o.face.scaleV && otherKeys == o.otherKeys;
}

EndlessPlane makePlane(double z, const std::string& mat) {
    EndlessPlane p;
    p.z = z;
    p.face.texture = mat;
    p.face.u = {{1, 0, 0}, 0};
    p.face.v = {{0, -1, 0}, 0};
    p.face.scaleU = p.face.scaleV = 0.25;
    p.face = planeFace(p);
    return p;
}

Face planeFace(const EndlessPlane& p) {
    Face f = p.face;
    // plane points counter-clockwise seen from above (the normal points up)
    f.p[0] = {0, 0, p.z};
    f.p[1] = {0, 100, p.z};
    f.p[2] = {100, 0, p.z};
    return f;
}

PlaneKind planeKind(const Material* m) {
    if (!m) return PlaneKind::Ground;
    const std::string s = lower(m->shader);
    if (s == "water" || s.rfind("water_dx", 0) == 0) return PlaneKind::Water;
    if (s == "clouds") return PlaneKind::Clouds;
    return PlaneKind::Ground;
}

const char* planeKindName(PlaneKind k) {
    switch (k) {
    case PlaneKind::Water: return "water";
    case PlaneKind::Clouds: return "clouds";
    default: return "ground";
    }
}

const char* starterPlaneMaterial(PlaneKind k) {
    switch (k) {
    case PlaneKind::Water: return "textures/starter/water";
    case PlaneKind::Clouds: return "textures/starter/clouds";
    default: return "textures/starter/ground";
    }
}

std::vector<Vec3> planeSquare(double z, double cx, double cy, double half) {
    // counter-clockwise seen from above
    return {{cx - half, cy - half, z}, {cx + half, cy - half, z}, {cx + half, cy + half, z}, {cx - half, cy + half, z}};
}

CloudParams cloudParams(const Material& m) {
    CloudParams c;
    c.cover = std::clamp(m.num("$cover", c.cover), 0.0, 100.0);
    c.size = std::max(10.0, m.num("$cloudsize", c.size));
    c.softness = std::clamp(m.num("$softness", c.softness), 0.0, 100.0);
    c.opacity = std::clamp(m.num("$opacity", c.opacity), 0.0, 1.0);
    c.complexity = std::clamp((int)std::lround(m.num("$complexity", c.complexity)), 1, 8);
    c.seed = (int)std::lround(m.num("$seed", c.seed));
    return c;
}

double cloudNoise(double x, double y, int seed) {
    const double fx = std::floor(x), fy = std::floor(y);
    const std::int64_t ix = (std::int64_t)fx, iy = (std::int64_t)fy;
    double tx = x - fx, ty = y - fy;
    tx = tx * tx * (3 - 2 * tx);
    ty = ty * ty * (3 - 2 * ty);
    const double a = hash01(ix, iy, seed), b = hash01(ix + 1, iy, seed);
    const double c = hash01(ix, iy + 1, seed), d = hash01(ix + 1, iy + 1, seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

double cloudThickness(const CloudParams& c, double x, double y) {
    double px = x / c.size, py = y / c.size;
    double sum = 0, amp = 1, norm = 0;
    for (int o = 0; o < c.complexity; ++o) {
        sum += amp * cloudNoise(px + 17.31 * o, py - 9.73 * o, c.seed + o);
        norm += amp;
        amp *= 0.5;
        px *= 2.03;
        py *= 2.03;
    }
    const double f = sum / norm;
    if (c.cover <= 0) return 0;
    // The noise is about normal, mean 0.496, spread 0.134 (measured): the edge sits where
    // `cover` % of the plane is above it (the inverse normal by its logistic approximation).
    const double q = std::clamp(1 - c.cover / 100, 0.003, 0.997);
    const double m = 0.496 + 0.134 * std::log(q / (1 - q)) / 1.702;
    const double w = 0.01 + 0.10 * c.softness / 100;
    double t = std::clamp((f - m + w) / (2 * w), 0.0, 1.0);
    t = t * t * (3 - 2 * t);
    return t * (0.55 + 0.45 * std::clamp((f - m) / 0.15, 0.0, 1.0)); // thicker inside
}

double cloudTransmit(const CloudParams& c, double x, double y) { return 1 - c.opacity * cloudThickness(c, x, y); }

} // namespace scal
