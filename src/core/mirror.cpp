#include "core/mirror.hpp"
#include <algorithm>
#include <cctype>
#include <array>
#include <cmath>

namespace scal {

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool partOn(const Material& m, const char* key) {
    if (!m.has(key)) return true; // Source's default: the render target
    const std::string v = lower(m.str(key));
    return v.rfind("_rt_", 0) == 0;
}

// Sutherland-Hodgman against a half-plane a*x + b*y + c >= 0 (2D points).
std::vector<std::array<double, 2>> clip2(const std::vector<std::array<double, 2>>& in, double a, double b, double c) {
    std::vector<std::array<double, 2>> out;
    for (size_t i = 0; i < in.size(); ++i) {
        const auto& p = in[i];
        const auto& q = in[(i + 1) % in.size()];
        const double dp = a * p[0] + b * p[1] + c, dq = a * q[0] + b * q[1] + c;
        if (dp >= 0) out.push_back(p);
        if ((dp >= 0) != (dq >= 0)) {
            const double t = dp / (dp - dq);
            out.push_back({p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t});
        }
    }
    return out;
}
} // namespace

bool isReflectiveMaterial(const Material& m) { return lower(m.shader) == "lightmappedreflective"; }

int mirrorParts(const Material& m) {
    if (!isReflectiveMaterial(m)) return 0;
    int parts = 0;
    if (partOn(m, "$reflecttexture")) parts |= kMirrorReflect;
    if (partOn(m, "$refracttexture") && m.num("$minreflectivity", 0) < 1) parts |= kMirrorRefract;
    if (m.has("$basetexture") && !m.str("$basetexture").empty()) parts |= kMirrorBase;
    return parts;
}

Camera mirrorCamera(const Camera& c, const Vec3& n0, double d) {
    const Vec3 n = normalize(n0);
    Camera m = c;
    m.pos = c.pos - n * (2 * (dot(n, c.pos) - d));
    const Mat3 b = cameraBasis(c);
    const Vec3 f = b.col[0] - n * (2 * dot(b.col[0], n));
    const Vec3 u = b.col[2] - n * (2 * dot(b.col[2], n));
    Mat3 mb;
    mb.col = {normalize(f), normalize(cross(u, f)), normalize(u)}; // forward, left, up: a rotation again
    setCameraBasis(m, mb);
    return m;
}

double pictureCover(const std::vector<Vec3>& verts, const Camera& c) {
    if (verts.size() < 3) return 0;
    const Mat3 b = cameraBasis(c);
    // camera space: x forward, y left, z up
    std::vector<Vec3> cs;
    for (const Vec3& v : verts) {
        const Vec3 r = v - c.pos;
        cs.push_back({dot(r, b.col[0]), dot(r, b.col[1]), dot(r, b.col[2])});
    }
    double tx = 1, ty = 1;
    std::vector<std::array<double, 2>> pts;
    if (c.ortho) {
        cameraHalfSize(c, tx, ty);
        for (const Vec3& p : cs)
            if (p.x > c.nearDist) pts.push_back({-p.y / tx, p.z / ty});
        if (pts.size() < 3) return 0; // (partly behind an orthographic camera: rare, counted as not in view)
    } else {
        cameraHalfTangents(c, tx, ty);
        // clip against the near plane first (x >= near)
        const double nearX = std::max(c.nearDist, 1e-3);
        std::vector<Vec3> kept;
        for (size_t i = 0; i < cs.size(); ++i) {
            const Vec3& p = cs[i];
            const Vec3& q = cs[(i + 1) % cs.size()];
            if (p.x >= nearX) kept.push_back(p);
            if ((p.x >= nearX) != (q.x >= nearX)) {
                const double t = (p.x - nearX) / (p.x - q.x);
                kept.push_back(p + (q - p) * t);
            }
        }
        if (kept.size() < 3) return 0;
        for (const Vec3& p : kept) pts.push_back({-p.y / (p.x * tx), p.z / (p.x * ty)});
    }
    // the picture is -1..1 both ways
    pts = clip2(pts, 1, 0, 1);
    if (pts.size() >= 3) pts = clip2(pts, -1, 0, 1);
    if (pts.size() >= 3) pts = clip2(pts, 0, 1, 1);
    if (pts.size() >= 3) pts = clip2(pts, 0, -1, 1);
    if (pts.size() < 3) return 0;
    double a = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const auto& p = pts[i];
        const auto& q = pts[(i + 1) % pts.size()];
        a += p[0] * q[1] - q[0] * p[1];
    }
    return std::fabs(a) * 0.5 / 4.0;
}

bool onMirrorPlane(const MirrorPlane& p, const Vec3& n, double d) {
    return dot(normalize(n), p.n) > 1 - 1e-4 && std::fabs(d - p.d) < 0.5;
}

std::vector<MirrorPlane> mirrorPlanes(const std::vector<MirrorFace>& faces, const Camera& c, size_t limit, size_t* dropped) {
    std::vector<MirrorPlane> planes;
    for (const MirrorFace& f : faces) {
        if (!(f.parts & (kMirrorReflect | kMirrorRefract)) || f.verts.size() < 3) continue;
        const Vec3 n = normalize(f.n);
        if (c.ortho) {
            if (dot(cameraForward(c), n) > -1e-6) continue; // seen from behind or edge-on
        } else if (dot(n, c.pos) - f.d <= 0.1) continue;      // the camera is behind the mirror
        const double cover = pictureCover(f.verts, c);
        if (cover <= 0) continue;
        MirrorPlane* into = nullptr;
        for (MirrorPlane& p : planes)
            if (onMirrorPlane(p, n, f.d)) { into = &p; break; }
        if (!into) {
            planes.push_back({n, f.d, 0, 0});
            into = &planes.back();
        }
        into->parts |= f.parts & (kMirrorReflect | kMirrorRefract);
        into->cover += cover;
    }
    // planes too small to see (a glass pane's edges) reflect the probes instead
    planes.erase(std::remove_if(planes.begin(), planes.end(),
                                [](const MirrorPlane& p) { return !(p.parts & (kMirrorReflect | kMirrorRefract)) || p.cover < kMirrorMinCover; }),
                 planes.end());
    std::stable_sort(planes.begin(), planes.end(), [](const MirrorPlane& a, const MirrorPlane& b) { return a.cover > b.cover; });
    if (dropped) *dropped = planes.size() > limit ? planes.size() - limit : 0;
    if (planes.size() > limit) planes.resize(limit);
    return planes;
}

} // namespace scal
