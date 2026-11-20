#include "core/world.hpp"
#include "core/brush.hpp"
#include <algorithm>
#include <cmath>

namespace scal {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
double tidy(double a) { // keeps typed-looking angles: 45 stays 45, not 44.99999999
    const double r = std::round(a * 1e6) / 1e6;
    return r == 0 ? 0 : r; // no -0
}
} // namespace

const char* lightKindName(LightKind k) {
    switch (k) {
        case LightKind::Point: return "point";
        case LightKind::Spot: return "spot";
        case LightKind::Sun: return "sun";
    }
    return "point";
}

bool Light::operator==(const Light& o) const {
    return id == o.id && kind == o.kind && pos == o.pos && color == o.color && brightness == o.brightness &&
           range == o.range && pitch == o.pitch && yaw == o.yaw && inner == o.inner && outer == o.outer && otherKeys == o.otherKeys;
}

Vec3 lightDirection(double pitch, double yaw) {
    const double p = pitch * kDeg, y = yaw * kDeg;
    return {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
}

Vec3 lightDirection(const Light& l) { return lightDirection(l.pitch, l.yaw); }

void directionAngles(const Vec3& dir, double& pitch, double& yaw, double keepYaw) {
    const Vec3 d = normalize(dir);
    pitch = tidy(std::asin(std::clamp(d.z, -1.0, 1.0)) / kDeg);
    if (std::hypot(d.x, d.y) < 1e-9) { yaw = keepYaw; return; }
    double y = std::atan2(d.y, d.x) / kDeg;
    if (y < 0) y += 360;
    yaw = tidy(y);
    if (yaw >= 360) yaw -= 360;
}

void rotateLight(Light& l, int axis, double degrees) {
    Vec3 a;
    a[std::clamp(axis, 0, 2)] = 1;
    const Vec3 d = rotateVec(lightDirection(l), a, degrees * kDeg);
    directionAngles(d, l.pitch, l.yaw, l.yaw);
}

std::vector<Vec3> axisCircle(const Vec3& center, int axis, double radius, int n) {
    const int a = (axis + 1) % 3, b = (axis + 2) % 3;
    std::vector<Vec3> pts;
    pts.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double t = 2 * kPi * i / n;
        Vec3 p = center;
        p[a] += std::cos(t) * radius;
        p[b] += std::sin(t) * radius;
        pts.push_back(p);
    }
    return pts;
}

std::vector<Vec3> dirCircle(const Vec3& center, const Vec3& axis, double radius, int n) {
    const Vec3 d = normalize(axis);
    Vec3 a = normalize(cross(d, std::fabs(d.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0}));
    const Vec3 b = cross(d, a);
    std::vector<Vec3> pts;
    pts.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double t = 2 * kPi * i / n;
        pts.push_back(center + a * (std::cos(t) * radius) + b * (std::sin(t) * radius));
    }
    return pts;
}

Vec3 lightAt(const Environment& env, const std::vector<Light>& lights, const Vec3& p, const Vec3& n) {
    Vec3 c = env.ambient / 255.0;
    for (const Light& l : lights) {
        const Vec3 col = l.color / 255.0 * (l.brightness / 200.0);
        if (l.kind == LightKind::Sun) {
            c += col * std::max(0.0, dot(n, -lightDirection(l)));
            continue;
        }
        const Vec3 d = l.pos - p;
        const double dist = length(d);
        if (dist >= l.range || l.range <= 0) continue;
        const Vec3 to = dist > 1e-9 ? d / dist : n;
        const double ndl = std::max(0.0, dot(n, to));
        const double f = 1 - dist / l.range;
        double k = ndl * f * f;
        if (l.kind == LightKind::Spot) {
            const double ca = dot(-to, lightDirection(l));
            const double co = std::cos(l.outer * kDeg), ci = std::cos(std::min(l.inner, l.outer) * kDeg);
            double t = ci - co > 1e-9 ? (ca - co) / (ci - co) : (ca >= co ? 1 : 0);
            t = std::clamp(t, 0.0, 1.0);
            k *= t * t * (3 - 2 * t);
        }
        c += col * k;
    }
    return c;
}

} // namespace scal
