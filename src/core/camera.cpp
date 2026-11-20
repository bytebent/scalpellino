#include "core/camera.hpp"
#include "core/world.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace scal {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
double tidy(double a) { // typed-looking angles stay as typed: 45, not 44.99999999
    const double r = std::round(a * 1e6) / 1e6;
    return r == 0 ? 0 : r;
}
double wrap180(double a) { // -180 < a <= 180
    a = std::fmod(a, 360.0);
    if (a > 180) a -= 360;
    if (a <= -180) a += 360;
    return tidy(a);
}
double wrap360(double a) { // 0 <= a < 360
    a = std::fmod(a, 360.0);
    if (a < 0) a += 360;
    a = tidy(a);
    return a >= 360 ? 0 : a;
}
double finiteOr(double v, double keep) { return std::isfinite(v) ? v : keep; }
} // namespace

bool Camera::operator==(const Camera& o) const {
    return id == o.id && name == o.name && pos == o.pos && pitch == o.pitch && yaw == o.yaw && roll == o.roll &&
           fov == o.fov && nearDist == o.nearDist && farDist == o.farDist && width == o.width && height == o.height &&
           ortho == o.ortho && orthoSize == o.orthoSize && exposure == o.exposure && color == o.color &&
           active == o.active && otherKeys == o.otherKeys;
}

void clampCamera(Camera& c) {
    const Camera d;
    c.pos = {finiteOr(c.pos.x, 0), finiteOr(c.pos.y, 0), finiteOr(c.pos.z, 0)};
    c.pitch = tidy(std::clamp(finiteOr(c.pitch, d.pitch), -90.0, 90.0)); // tidy: no -0
    c.yaw = wrap360(finiteOr(c.yaw, d.yaw));
    c.roll = wrap180(finiteOr(c.roll, d.roll));
    c.fov = std::clamp(finiteOr(c.fov, d.fov), 1.0, 170.0);
    c.nearDist = std::clamp(finiteOr(c.nearDist, d.nearDist), 0.1, 1e7);
    c.farDist = std::clamp(finiteOr(c.farDist, d.farDist), c.nearDist + 1, 1e8);
    c.width = std::clamp(c.width, 16, 16384);
    c.height = std::clamp(c.height, 16, 16384);
    c.orthoSize = std::clamp(finiteOr(c.orthoSize, d.orthoSize), 1.0, 1e7);
    c.exposure = std::clamp(finiteOr(c.exposure, d.exposure), -20.0, 20.0);
    for (int i = 0; i < 3; ++i) c.color[i] = std::clamp(finiteOr(c.color[i], d.color[i]), 0.0, 255.0);
    if (c.name.empty()) c.name = d.name;
}

double lensToFov(double mm) { return tidy(2 * std::atan(18.0 / std::max(mm, 0.1)) / kDeg); }
double fovToLens(double fovDeg) {
    const double t = std::tan(std::clamp(fovDeg, 0.01, 179.0) * 0.5 * kDeg);
    return std::round(18.0 / t * 1e3) / 1e3;
}

const std::vector<LensPreset>& lensPresets() {
    static const std::vector<LensPreset> p{
        {"ultra-wide 14 mm", 14}, {"wide 24 mm", 24}, {"normal 50 mm", 50}, {"portrait 85 mm", 85}, {"telephoto 135 mm", 135}};
    return p;
}

const std::vector<SizePreset>& sizePresets() {
    static const std::vector<SizePreset> p{
        {"1280 x 720 (HD)", 1280, 720},        {"1920 x 1080 (Full HD)", 1920, 1080}, {"3840 x 2160 (4K)", 3840, 2160},
        {"1080 x 1080 (square)", 1080, 1080},  {"1080 x 1350 (portrait 4:5)", 1080, 1350},
        {"1620 x 1080 (3:2)", 1620, 1080},     {"1440 x 1080 (4:3)", 1440, 1080}};
    return p;
}

Mat3 cameraBasis(const Camera& c) { return rotationMatrix({c.roll, -c.pitch, c.yaw}); }

void setCameraBasis(Camera& c, const Mat3& m) {
    // rotationAngles keeps its middle angle (here -pitch) within -90..90, as pitch must be.
    const Vec3 a = rotationAngles(m);
    c.roll = wrap180(a.x);
    c.pitch = tidy(-a.y);
    c.yaw = wrap360(a.z);
}

Vec3 cameraForward(const Camera& c) { return lightDirection(c.pitch, c.yaw); }

void rotateCamera(Camera& c, int axis, double degrees) {
    setCameraBasis(c, axisRotation(std::clamp(axis, 0, 2), degrees) * cameraBasis(c));
}

void rollCamera(Camera& c, double degrees) { c.roll = wrap180(c.roll + degrees); }

bool aimCamera(Camera& c, const Vec3& target) {
    const Vec3 d = target - c.pos;
    if (length(d) < 1e-9) return false;
    directionAngles(d, c.pitch, c.yaw, c.yaw);
    return true;
}

void cameraHalfTangents(const Camera& c, double& tx, double& ty) {
    const double t = std::tan(std::clamp(c.fov, 1.0, 170.0) * 0.5 * kDeg);
    const double w = std::max(1, c.width), h = std::max(1, c.height);
    if (w >= h) { tx = t; ty = t * h / w; }
    else { ty = t; tx = t * w / h; }
}

void cameraHalfSize(const Camera& c, double& hx, double& hy) {
    const double s = std::max(c.orthoSize, 1.0) * 0.5;
    const double w = std::max(1, c.width), h = std::max(1, c.height);
    if (w >= h) { hx = s; hy = s * h / w; }
    else { hy = s; hx = s * w / h; }
}

std::array<Vec3, 4> cameraFrame(const Camera& c, double dist) {
    const Mat3 b = cameraBasis(c);
    const Vec3 f = b.col[0], right = -b.col[1], up = b.col[2];
    double hx, hy;
    if (c.ortho) cameraHalfSize(c, hx, hy);
    else { cameraHalfTangents(c, hx, hy); hx *= dist; hy *= dist; }
    const Vec3 m = c.pos + f * dist;
    return {m - right * hx + up * hy, m + right * hx + up * hy, m + right * hx - up * hy, m - right * hx - up * hy};
}

std::vector<std::pair<Vec3, Vec3>> cameraShape(const Camera& c, double len) {
    std::vector<std::pair<Vec3, Vec3>> s;
    const std::array<Vec3, 4> far = cameraFrame(c, len);
    for (int i = 0; i < 4; ++i) s.push_back({far[i], far[(i + 1) % 4]});
    if (c.ortho) {
        const std::array<Vec3, 4> at = cameraFrame(c, 0);
        for (int i = 0; i < 4; ++i) {
            s.push_back({at[i], at[(i + 1) % 4]});
            s.push_back({at[i], far[i]});
        }
    } else {
        for (int i = 0; i < 4; ++i) s.push_back({c.pos, far[i]});
    }
    // "Up": a small triangle standing on the top edge (top-left .. top-right).
    const Vec3 up = cameraBasis(c).col[2];
    const Vec3 a = far[0] + (far[1] - far[0]) * 0.3, b = far[0] + (far[1] - far[0]) * 0.7;
    const Vec3 tip = (a + b) * 0.5 + up * (length(b - a) * 0.5);
    s.push_back({a, tip});
    s.push_back({tip, b});
    return s;
}

std::string cameraPresetToString(const Camera& c) {
    auto num = [](double v) {
        char b[48];
        std::snprintf(b, sizeof b, "%.10g", v);
        return std::string(b);
    };
    std::string s;
    s += "fov " + num(c.fov) + "\n";
    s += "near " + num(c.nearDist) + "\n";
    s += "far " + num(c.farDist) + "\n";
    s += "size " + std::to_string(c.width) + " " + std::to_string(c.height) + "\n";
    s += std::string("ortho ") + (c.ortho ? "1" : "0") + "\n";
    s += "ortho_size " + num(c.orthoSize) + "\n";
    s += "exposure " + num(c.exposure) + "\n";
    return s;
}

bool applyCameraPreset(Camera& c, const std::string& text) {
    std::istringstream in(text);
    std::string line;
    Camera r = c;
    int read = 0;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string k;
        if (!(l >> k)) continue;
        double a = 0, b = 0;
        if (!(l >> a) || !std::isfinite(a)) continue;
        if (k == "fov") r.fov = a;
        else if (k == "near") r.nearDist = a;
        else if (k == "far") r.farDist = a;
        else if (k == "size") {
            if (!(l >> b) || !std::isfinite(b) || a < 1 || b < 1 || a > 1e6 || b > 1e6) continue;
            r.width = (int)std::lround(a);
            r.height = (int)std::lround(b);
        } else if (k == "ortho") r.ortho = a != 0;
        else if (k == "ortho_size") r.orthoSize = a;
        else if (k == "exposure") r.exposure = a;
        else continue;
        ++read;
    }
    if (!read) return false;
    clampCamera(r);
    c = r;
    return true;
}

std::vector<CameraId> fixActiveCamera(std::vector<Camera>& cams) {
    std::vector<CameraId> changed;
    bool seen = false;
    for (Camera& c : cams) {
        if (c.active && seen) { c.active = false; changed.push_back(c.id); }
        seen = seen || c.active;
    }
    if (!seen && !cams.empty()) { cams[0].active = true; changed.push_back(cams[0].id); }
    return changed;
}

} // namespace scal
