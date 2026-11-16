#include "core/hull.hpp"
#include <algorithm>
#include <cmath>

namespace scal {

namespace {
constexpr double kEps = 1e-5;

void addUnique(std::vector<Vec3>& pts, const Vec3& p) {
    for (const Vec3& q : pts)
        if (length(q - p) < 1e-6) return;
    pts.push_back(p);
}
} // namespace

std::vector<Vec3> brushVertices(const Brush& b) {
    std::vector<Vec3> pts;
    for (const Polygon& p : b.polygons())
        for (const Vec3& v : p.verts) addUnique(pts, cleanVec(v));
    return pts;
}

HullResult brushFromVertices(const std::vector<Vec3>& input, const Brush& source, bool requireAllCorners) {
    HullResult r;
    std::vector<Vec3> pts;
    for (const Vec3& p : input) addUnique(pts, cleanVec(p));
    if (pts.size() < 4) { r.error = "not enough corners for a solid"; return r; }

    // Brute-force hull: a plane through 3 points is a hull face if every point is on
    // one side of it. Brushes have few vertices, so O(n^4) is fine and very robust.
    std::vector<Plane> planes;
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            for (size_t k = j + 1; k < n; ++k) {
                Vec3 nn = cross(pts[j] - pts[i], pts[k] - pts[i]);
                if (length(nn) < 1e-9) continue; // collinear
                nn = normalize(nn);
                double d = dot(nn, pts[i]);
                double mx = -1e300, mn = 1e300;
                for (const Vec3& p : pts) {
                    const double s = dot(nn, p) - d;
                    mx = std::max(mx, s);
                    mn = std::min(mn, s);
                }
                if (mx <= kEps) { /* outward already */ }
                else if (mn >= -kEps) { nn = -nn; d = -d; }
                else continue;
                bool dup = false;
                for (const Plane& q : planes)
                    if (dot(q.n, nn) > 1 - 1e-9 && std::fabs(q.d - d) < kEps) { dup = true; break; }
                if (!dup) planes.push_back({nn, d});
            }
    if (planes.size() < 4) { r.error = "the corners are flat (no volume)"; return r; }

    // Every requested corner must be on the surface; one inside would silently vanish.
    // Every requested corner must stay a corner (or at least on an edge) of the solid.
    // On 0 faces = inside the solid; on exactly 1 face = in the middle of a face. Either
    // way that corner would silently disappear, so the shape is not what was asked for.
    for (const Vec3& p : (requireAllCorners ? pts : std::vector<Vec3>{})) {
        int faces = 0;
        for (const Plane& pl : planes)
            if (std::fabs(pl.distance(p)) <= kEps) ++faces;
        if (faces == 0) { r.error = "shape would not be convex (a corner ends up inside)"; return r; }
        if (faces == 1) { r.error = "shape would not be convex (a corner ends up flat inside a face)"; return r; }
    }

    Brush out;
    out.id = source.id;
    for (const Plane& pl : planes) {
        std::vector<Vec3> on;
        for (const Vec3& p : pts)
            if (std::fabs(pl.distance(p)) <= kEps) on.push_back(p);
        if (on.size() < 3) continue;
        // Most stable triple (largest triangle) for the plane points.
        Vec3 a = on[0], b = on[1], c = on[2];
        double best = -1;
        for (size_t i = 0; i < on.size(); ++i)
            for (size_t j = i + 1; j < on.size(); ++j)
                for (size_t k = j + 1; k < on.size(); ++k) {
                    const double area = length(cross(on[j] - on[i], on[k] - on[i]));
                    if (area > best) { best = area; a = on[i]; b = on[j]; c = on[k]; }
                }
        // Convention: n = normalize(cross(c - a, b - a)) must point outward.
        if (dot(cross(c - a, b - a), pl.n) < 0) std::swap(b, c);

        // Texture from the source face facing the most similar way.
        Face f;
        const Face* tmpl = nullptr;
        double bestDot = -2;
        for (const Face& sf : source.faces) {
            const double dd = dot(sf.plane().n, pl.n);
            if (dd > bestDot) { bestDot = dd; tmpl = &sf; }
        }
        if (tmpl) f = *tmpl;
        else defaultTexAxes(pl.n, f.u.axis, f.v.axis);
        f.p[0] = a; f.p[1] = b; f.p[2] = c;
        out.faces.push_back(f);
    }
    if (!out.valid()) { r.error = "the result is not a valid solid"; return r; }
    r.ok = true;
    r.brush = std::move(out);
    return r;
}

HullResult snapBrushToGrid(const Brush& b, double grid) {
    std::vector<Vec3> pts = brushVertices(b);
    for (Vec3& p : pts)
        for (int a = 0; a < 3; ++a) p[a] = std::round(p[a] / grid) * grid;
    HullResult r = brushFromVertices(pts, b);
    if (r.ok && r.brush.faces.size() > b.polygons().size()) {
        r.ok = false;
        r.error = "snapping would bend faces (they would split); try another grid size or the vertex tool";
    }
    return r;
}

bool brushOnGrid(const Brush& b, double grid) {
    for (const Vec3& p : brushVertices(b))
        for (int a = 0; a < 3; ++a)
            if (std::fabs(p[a] / grid - std::round(p[a] / grid)) > 1e-6) return false;
    return true;
}

} // namespace scal
