#include "tools/tool.hpp"
#include <algorithm>
#include <cmath>

namespace scal {

BoxGrab grabBox(const Viewport& vp, const AABB& b, Vector2 p, float tol) {
    BoxGrab g;
    if (!b.valid()) return g;
    const int U = vp.axU(), V = vp.axV();
    const Vector2 tl = vp.worldToLocal2D(b.min[U], b.max[V]);
    const Vector2 br = vp.worldToLocal2D(b.max[U], b.min[V]);
    const bool inX = p.x >= tl.x - tol && p.x <= br.x + tol;
    const bool inY = p.y >= tl.y - tol && p.y <= br.y + tol;
    if (!inX || !inY) return g;

    const float mx = (tl.x + br.x) * 0.5f, my = (tl.y + br.y) * 0.5f;
    auto nearv = [&](float a, float b) { return std::fabs(a - b) <= tol; };
    // Handles (corners + edge middles).
    if (nearv(p.x, tl.x)) g.su = -1;
    else if (nearv(p.x, br.x)) g.su = +1;
    if (nearv(p.y, tl.y)) g.sv = +1;
    else if (nearv(p.y, br.y)) g.sv = -1;
    // Edge-middle handles also grab when the box is tiny on screen.
    if (g.su == 0 && g.sv == 0) {
        if (nearv(p.x, mx) && nearv(p.y, tl.y)) g.sv = +1;
        else if (nearv(p.x, mx) && nearv(p.y, br.y)) g.sv = -1;
        else if (nearv(p.y, my) && nearv(p.x, tl.x)) g.su = -1;
        else if (nearv(p.y, my) && nearv(p.x, br.x)) g.su = +1;
    }
    if (g.su == 0 && g.sv == 0) g.move = p.x > tl.x && p.x < br.x && p.y > tl.y && p.y < br.y;
    return g;
}

AABB dragBox(const Viewport& vp, const AABB& start, const BoxGrab& g,
             double u0, double v0, double u, double v, const Grid& grid) {
    const int U = vp.axU(), V = vp.axV();
    AABB r = start;
    const double minSize = grid.snap ? grid.size : 1.0;
    if (g.move) {
        const double nu = grid.snapValue(start.min[U] + (u - u0));
        const double nv = grid.snapValue(start.min[V] + (v - v0));
        r.min[U] = nu; r.max[U] = nu + (start.max[U] - start.min[U]);
        r.min[V] = nv; r.max[V] = nv + (start.max[V] - start.min[V]);
        return r;
    }
    const double su = grid.snapValue(u), sv = grid.snapValue(v);
    if (g.su > 0) r.max[U] = std::max(su, start.min[U] + minSize);
    if (g.su < 0) r.min[U] = std::min(su, start.max[U] - minSize);
    if (g.sv > 0) r.max[V] = std::max(sv, start.min[V] + minSize);
    if (g.sv < 0) r.min[V] = std::min(sv, start.max[V] - minSize);
    return r;
}

namespace {
bool insideConvex(const std::vector<Vector2>& poly, Vector2 p) {
    int sign = 0;
    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const Vector2 a = poly[i], b = poly[(i + 1) % n];
        const float c = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        if (std::fabs(c) < 1e-4f) continue;
        const int s = c > 0 ? 1 : -1;
        if (sign == 0) sign = s;
        else if (s != sign) return false;
    }
    return sign != 0;
}

float distToSegment(Vector2 p, Vector2 a, Vector2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0;
    t = std::clamp(t, 0.0f, 1.0f);
    const float ex = a.x + t * dx - p.x, ey = a.y + t * dy - p.y;
    return std::sqrt(ex * ex + ey * ey);
}
} // namespace

BrushId pick2D(const std::vector<SceneBrush>& brushes, const Viewport& vp, Vector2 local) {
    const int U = vp.axU(), V = vp.axV(), D = vp.axD();
    BrushId best = 0;
    double bestArea = 1e300;
    for (const SceneBrush& b : brushes) {
        bool hit = false;
        for (const Polygon& p : b.polys) {
            std::vector<Vector2> proj;
            proj.reserve(p.verts.size());
            for (const Vec3& v : p.verts) proj.push_back(vp.worldToLocal2D(v[U], v[V]));
            for (size_t i = 0; i < proj.size() && !hit; ++i)
                if (distToSegment(local, proj[i], proj[(i + 1) % proj.size()]) <= 3.0f) hit = true;
            if (!hit && std::fabs(p.normal[D]) > 1e-6 && insideConvex(proj, local)) hit = true;
            if (hit) break;
        }
        if (!hit) continue;
        const Vec3 s = b.bounds.size();
        const double area = s[U] * s[V];
        if (area < bestArea) { bestArea = area; best = b.id; }
    }
    return best;
}

BrushId pick3D(const Document& doc, const Viewport& vp, Vector2 local) {
    Vec3 o, d;
    vp.pickRay(local, o, d);
    BrushId best = 0;
    double bestT = 1e300;
    for (const Brush& b : doc.brushes()) {
        const double t = rayHit(b, o, d);
        if (t >= 0 && t < bestT) { bestT = t; best = b.id; }
    }
    return best;
}

} // namespace scal
