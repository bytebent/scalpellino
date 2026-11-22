#include "core/hull.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace scal {

namespace {
constexpr double kEps = 1e-5;

void addUnique(std::vector<Vec3>& pts, const Vec3& p) {
    for (const Vec3& q : pts)
        if (length(q - p) < 1e-6) return;
    pts.push_back(p);
}

// Brute-force hull: a plane through 3 points is a hull face if every point is on one
// side of it. O(n^4) but very robust; the fallback for the fast search below.
std::vector<Plane> hullPlanesBrute(const std::vector<Vec3>& pts) {
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
                    if (mx > kEps && mn < -kEps) break; // points on both sides: not a face
                }
                if (mx <= kEps) { /* outward already */ }
                else if (mn >= -kEps) { nn = -nn; d = -d; }
                else continue;
                bool dup = false;
                for (const Plane& q : planes)
                    if (dot(q.n, nn) > 1 - 1e-9 && std::fabs(q.d - d) < kEps) { dup = true; break; }
                if (!dup) planes.push_back({nn, d});
            }
    return planes;
}

// Plane through a, b, c facing away from g; nullopt if (nearly) collinear.
std::optional<Plane> planeAwayFrom(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& g) {
    Vec3 nn = cross(b - a, c - a);
    if (length(nn) < 1e-9) return std::nullopt;
    nn = normalize(nn);
    double d = dot(nn, a);
    if (dot(nn, g) - d > 0) { nn = -nn; d = -d; }
    return Plane{nn, d};
}

bool allBehind(const Plane& pl, const std::vector<Vec3>& pts) {
    for (const Vec3& p : pts)
        if (pl.distance(p) > kEps) return false;
    return true;
}

// Gift wrapping: find one face, then for every edge of every face found, pivot a plane
// around that edge to the neighbouring face. About O(n * faces) instead of O(n^4).
// Returns nullopt if anything looks degenerate; the caller then uses the brute force.
std::optional<std::vector<Plane>> hullPlanesFast(const std::vector<Vec3>& pts) {
    const size_t n = pts.size();
    Vec3 g;
    for (const Vec3& p : pts) g += p;
    g = g / (double)n;
    // Start: a face through the lowest point (any face will do).
    size_t lo = 0;
    for (size_t i = 1; i < n; ++i)
        if (pts[i].x < pts[lo].x || (pts[i].x == pts[lo].x && (pts[i].y < pts[lo].y || (pts[i].y == pts[lo].y && pts[i].z < pts[lo].z)))) lo = i;
    std::optional<Plane> first;
    for (size_t j = 0; j < n && !first; ++j)
        for (size_t k = j + 1; k < n && !first; ++k) {
            if (j == lo || k == lo) continue;
            auto pl = planeAwayFrom(pts[lo], pts[j], pts[k], g);
            if (pl && pl->distance(g) < -kEps && allBehind(*pl, pts)) first = pl;
        }
    if (!first) return std::nullopt;

    std::vector<Plane> planes{*first};
    for (size_t f = 0; f < planes.size(); ++f) {
        if (planes.size() > 4 * n + 8) return std::nullopt; // runaway: give up
        const Plane cur = planes[f];
        // The face's points, as a convex polygon (2D hull in the plane, collinear dropped).
        std::vector<size_t> on;
        for (size_t i = 0; i < n; ++i)
            if (std::fabs(cur.distance(pts[i])) <= kEps) on.push_back(i);
        if (on.size() < 3) return std::nullopt;
        Vec3 ax, ay;
        defaultTexAxes(cur.n, ax, ay);
        ax = normalize(ax - cur.n * dot(ax, cur.n));
        ay = cross(cur.n, ax);
        std::sort(on.begin(), on.end(), [&](size_t a, size_t b) {
            const double ua = dot(pts[a], ax), ub = dot(pts[b], ax); // exact compare: a strict order
            if (ua != ub) return ua < ub;
            return dot(pts[a], ay) < dot(pts[b], ay);
        });
        auto turn = [&](size_t o, size_t a, size_t b) {
            const Vec3 e1 = pts[a] - pts[o], e2 = pts[b] - pts[o];
            return dot(e1, ax) * dot(e2, ay) - dot(e1, ay) * dot(e2, ax);
        };
        std::vector<size_t> poly;
        for (int pass = 0; pass < 2; ++pass) {
            const size_t start = poly.size();
            for (size_t t = 0; t < on.size(); ++t) {
                const size_t i = pass == 0 ? on[t] : on[on.size() - 1 - t];
                while (poly.size() >= start + 2 && turn(poly[poly.size() - 2], poly.back(), i) <= 1e-9) poly.pop_back();
                poly.push_back(i);
            }
            poly.pop_back();
        }
        if (poly.size() < 3) return std::nullopt;
        // Pivot around each edge.
        for (size_t e = 0; e < poly.size(); ++e) {
            const Vec3& a = pts[poly[e]];
            const Vec3& b = pts[poly[(e + 1) % poly.size()]];
            // Looking along the edge, the neighbouring face is the one folded least away
            // from the edge's outward direction w (within this face): the point with the
            // smallest angle from w, measured towards the inside.
            const Vec3 t = normalize(b - a);
            const Vec3 w = cross(t, cur.n);
            std::optional<size_t> c;
            double best = 1e300;
            for (size_t i = 0; i < n; ++i) {
                Vec3 r = pts[i] - a;
                r = r - t * dot(r, t);
                if (length(r) < 1e-7) continue; // on the edge line
                // max(): never -0.0 (atan2 would give -pi for points on this face)
                const double phi = std::atan2(std::max(0.0, -dot(r, cur.n)), dot(r, w));
                if (phi < best) { best = phi; c = i; }
            }
            if (!c) return std::nullopt;
            auto pl = planeAwayFrom(a, b, pts[*c], g);
            if (!pl) return std::nullopt;
            if (pl->distance(g) >= -kEps || !allBehind(*pl, pts)) return std::nullopt;
            bool dup = false;
            for (const Plane& q : planes)
                if (dot(q.n, pl->n) > 1 - 1e-9 && std::fabs(q.d - pl->d) < kEps) { dup = true; break; }
            if (!dup) planes.push_back(*pl);
        }
    }
    return planes;
}

std::vector<Plane> hullPlanes(const std::vector<Vec3>& pts) {
    if (auto fast = hullPlanesFast(pts)) return *fast;
    return hullPlanesBrute(pts);
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

    std::vector<Plane> planes = hullPlanes(pts);
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
    out.decals = source.decals; // they stay where they are (v0.12.5)
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
