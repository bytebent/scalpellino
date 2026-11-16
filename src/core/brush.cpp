#include "core/brush.hpp"
#include "core/grid.hpp"
#include <algorithm>
#include <cmath>

namespace scal {

namespace {

// Two unit vectors spanning the plane, with cross(e1, e2) == n.
void planeBasis(const Vec3& n, Vec3& e1, Vec3& e2) {
    const Vec3 ref = std::fabs(n.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    e1 = normalize(cross(ref, n));
    e2 = cross(n, e1);
}

int dominantAxis(const Vec3& n) {
    const double ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
    if (az >= ax && az >= ay) return 2;
    if (ax >= ay) return 0;
    return 1;
}

} // namespace

std::vector<Vec3> clipPolygon(const std::vector<Vec3>& poly, const Plane& plane, double eps) {
    std::vector<Vec3> out;
    const size_t n = poly.size();
    if (n == 0) return out;
    out.reserve(n + 1);
    for (size_t i = 0; i < n; ++i) {
        const Vec3& a = poly[i];
        const Vec3& b = poly[(i + 1) % n];
        const double da = plane.distance(a), db = plane.distance(b);
        const bool ina = da <= eps, inb = db <= eps;
        if (ina) out.push_back(a);
        if (ina != inb && std::fabs(da) > eps && std::fabs(db) > eps) {
            const double t = da / (da - db);
            out.push_back(a + (b - a) * t);
        }
    }
    return out;
}

std::vector<Polygon> Brush::polygons() const {
    std::vector<Polygon> polys;
    const double big = kWorldHalf * 8.0;
    for (size_t i = 0; i < faces.size(); ++i) {
        const Plane pl = faces[i].plane();
        if (length(pl.n) < 0.5) continue; // degenerate face points
        Vec3 e1, e2;
        planeBasis(pl.n, e1, e2);
        const Vec3 c = pl.n * pl.d;
        // Counter-clockwise around n.
        std::vector<Vec3> poly = {c - e1 * big - e2 * big, c + e1 * big - e2 * big,
                                  c + e1 * big + e2 * big, c - e1 * big + e2 * big};
        for (size_t j = 0; j < faces.size() && poly.size() >= 3; ++j) {
            if (j == i) continue;
            const Plane other = faces[j].plane();
            // Identical plane (duplicate face): keep the earlier one only.
            if (dot(other.n, pl.n) > 1 - 1e-9 && std::fabs(other.d - pl.d) < 1e-6) {
                if (j < i) { poly.clear(); break; }
                continue;
            }
            poly = clipPolygon(poly, other);
        }
        // Clean and drop duplicate consecutive vertices.
        std::vector<Vec3> clean;
        for (const Vec3& v : poly) {
            const Vec3 cv = cleanVec(v);
            if (clean.empty() || length(cv - clean.back()) > 1e-5) clean.push_back(cv);
        }
        while (clean.size() > 1 && length(clean.front() - clean.back()) <= 1e-5) clean.pop_back();
        if (clean.size() >= 3) polys.push_back({std::move(clean), pl.n, (int)i});
    }
    return polys;
}

AABB Brush::bounds() const {
    AABB b;
    for (const Polygon& p : polygons())
        for (const Vec3& v : p.verts) b.add(v);
    return b;
}

bool Brush::valid() const {
    if (faces.size() < 4) return false;
    const auto polys = polygons();
    if (polys.size() < 4) return false;
    const AABB b = bounds();
    if (!b.valid()) return false;
    const Vec3 s = b.size();
    // Unbounded brushes clip against the huge starting quad and come out enormous.
    return s.x > 0 && s.y > 0 && s.z > 0 && s.x < kWorldHalf * 4 && s.y < kWorldHalf * 4 && s.z < kWorldHalf * 4;
}

void Brush::translate(const Vec3& delta, bool textureLock) {
    for (Face& f : faces) {
        for (Vec3& p : f.p) p += delta;
        if (textureLock) {
            // Texture coordinate = dot(pos, axis)/scale + offset; keep it constant.
            f.u.offset -= dot(delta, f.u.axis) / f.scaleU;
            f.v.offset -= dot(delta, f.v.axis) / f.scaleV;
        }
    }
}

Vec3 rotateVec(const Vec3& v, const Vec3& k, double a) {
    const double c = std::cos(a), s = std::sin(a);
    return v * c + cross(k, v) * s + k * (dot(k, v) * (1 - c));
}

void Brush::rotate(const Vec3& center, const Vec3& axis, double radians, bool textureLock) {
    for (Face& f : faces) {
        for (Vec3& p : f.p) p = cleanVec(center + rotateVec(p - center, axis, radians));
        if (textureLock) {
            // Texture coord at a rotated point must equal the original's:
            // offset' = offset + (dot(c, a) - dot(c, R a)) / scale, axis' = R a.
            const Vec3 ru = rotateVec(f.u.axis, axis, radians);
            const Vec3 rv = rotateVec(f.v.axis, axis, radians);
            f.u.offset += (dot(center, f.u.axis) - dot(center, ru)) / f.scaleU;
            f.v.offset += (dot(center, f.v.axis) - dot(center, rv)) / f.scaleV;
            f.u.axis = cleanVec(ru);
            f.v.axis = cleanVec(rv);
        }
    }
}

void Brush::remapBounds(const AABB& from, const AABB& to, bool textureLock) {
    const Vec3 fs = from.size(), ts = to.size();
    Vec3 k;
    for (int a = 0; a < 3; ++a) k[a] = fs[a] != 0 ? ts[a] / fs[a] : 1.0;
    auto map = [&](const Vec3& p) {
        Vec3 r;
        for (int a = 0; a < 3; ++a) r[a] = to.min[a] + (p[a] - from.min[a]) * k[a];
        return r;
    };
    for (Face& f : faces) {
        // Keep texture anchored at the first plane point (lock) before moving points.
        const Vec3 oldAnchor = f.p[0];
        for (Vec3& p : f.p) p = map(p);
        // Negative scale flips orientation; re-wind so the normal stays outward.
        if (k.x * k.y * k.z < 0) std::swap(f.p[1], f.p[2]);
        if (textureLock) {
            const Vec3 d = map(oldAnchor) - oldAnchor;
            f.u.offset -= dot(d, f.u.axis) / f.scaleU;
            f.v.offset -= dot(d, f.v.axis) / f.scaleV;
        }
    }
}

void defaultTexAxes(const Vec3& n, Vec3& u, Vec3& v) {
    switch (dominantAxis(n)) {
        case 2:  u = {1, 0, 0}; v = {0, -1, 0}; break; // floor / ceiling
        case 0:  u = {0, 1, 0}; v = {0, 0, -1}; break; // walls facing X
        default: u = {1, 0, 0}; v = {0, 0, -1}; break; // walls facing Y
    }
}

Face makeFace(const Vec3& n, const Vec3& point, const std::string& texture) {
    Vec3 e1, e2;
    planeBasis(n, e1, e2);
    // fromPoints uses cross(c - a, b - a) == n  ->  b - a = e2, c - a = e1 (e1 x e2 == n).
    const double s = 64.0;
    Face f;
    f.p[0] = cleanVec(point);
    f.p[1] = cleanVec(point + e2 * s);
    f.p[2] = cleanVec(point + e1 * s);
    f.texture = texture;
    defaultTexAxes(n, f.u.axis, f.v.axis);
    return f;
}

Brush makeBox(const AABB& b, const std::string& texture) {
    Brush br;
    br.faces.push_back(makeFace({ 0,  0,  1}, b.max, texture));
    br.faces.push_back(makeFace({ 0,  0, -1}, b.min, texture));
    br.faces.push_back(makeFace({ 1,  0,  0}, b.max, texture));
    br.faces.push_back(makeFace({-1,  0,  0}, b.min, texture));
    br.faces.push_back(makeFace({ 0,  1,  0}, b.max, texture));
    br.faces.push_back(makeFace({ 0, -1,  0}, b.min, texture));
    return br;
}

double rayHit(const Brush& b, const Vec3& o, const Vec3& dir) {
    double tEnter = -1e300, tExit = 1e300;
    for (const Face& f : b.faces) {
        const Plane pl = f.plane();
        const double denom = dot(pl.n, dir);
        const double dist = pl.distance(o);
        if (std::fabs(denom) < 1e-12) {
            if (dist > 0) return -1; // parallel and outside
            continue;
        }
        const double t = -dist / denom;
        if (denom < 0) tEnter = std::max(tEnter, t);
        else           tExit  = std::min(tExit, t);
        if (tEnter > tExit) return -1;
    }
    if (tExit < 0) return -1;
    return tEnter >= 0 ? tEnter : 0.0;
}

} // namespace scal
