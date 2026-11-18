#include "core/landing.hpp"
#include "core/terrain.hpp"
#include "core/hull.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace scal {

namespace {
constexpr double kPi = 3.14159265358979323846;

// Where a ray first hits brush `b`, skipping a terrain brush's flat terrain face (the
// terrain surface is hit instead). -1 = no hit.
double hitBrush(const Brush& b, const Vec3& o, const Vec3& d) {
    double best = -1;
    if (b.terrain && terrainValid(b)) {
        best = rayHitTerrain(b, o, d);
        const double t = rayHit(b, o, d);
        if (t >= 0) {
            const Plane pl = b.faces[b.terrain->face].plane();
            if (std::fabs(pl.distance(o + d * t)) > 1e-6 && (best < 0 || t < best)) best = t;
        }
        return best;
    }
    return rayHit(b, o, d);
}

std::vector<MathPrim> copies(const std::vector<const MathPrim*>& ps, double dz) {
    std::vector<MathPrim> out;
    for (const MathPrim* p : ps) {
        MathPrim c = *p;
        c.pos.z += dz;
        out.push_back(c);
    }
    return out;
}

AABB boundsOf(const std::vector<const MathPrim*>& ps) {
    AABB b;
    for (const MathPrim* p : ps) {
        const AABB pb = primBounds(*p);
        b.add(pb.min);
        b.add(pb.max);
    }
    return b;
}

bool overlapsXY(const AABB& a, const AABB& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y;
}
} // namespace

std::vector<Vec3> objectSurfaceSamples(const std::vector<const MathPrim*>& object, int around, int up) {
    std::vector<Vec3> out;
    if (object.empty()) return out;
    const bool blended = comboBlended(object);
    const AABB box = boundsOf(object);
    const double tol = 1e-4 * std::max(1.0, length(box.size()));
    for (const MathPrim* m : object) {
        if (object.size() > 1 && m->op != PrimOp::Add) continue;
        const Vec3 c = m->pos;
        for (int j = 0; j <= up; ++j) {
            const double lat = -kPi / 2 + kPi * j / up;
            for (int i = 0; i < (j == 0 || j == up ? 1 : around); ++i) {
                const double lon = 2 * kPi * i / around;
                // A direction in the primitive's own (stretched) space, so samples spread over it.
                const Vec3 local{std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
                Vec3 dir = primToWorld(*m, local) - c;
                if (length(dir) < 1e-12) continue;
                dir = normalize(dir);
                // The way out of this piece along that direction.
                const auto iv = primIntervals(*m, c - dir * (length(box.size()) + 10), dir);
                if (iv.empty()) continue;
                Vec3 q = c - dir * (length(box.size()) + 10) + dir * iv.back().t1;
                if (blended) { // onto the blended surface
                    for (int k = 0; k < 6; ++k) {
                        const double dd = comboDistance(object, q);
                        const double e = 1e-3 * std::max(1.0, length(box.size()));
                        Vec3 g;
                        for (int a = 0; a < 3; ++a) {
                            Vec3 dp;
                            dp[a] = e;
                            g[a] = comboDistance(object, q + dp) - comboDistance(object, q - dp);
                        }
                        if (length(g) < 1e-12) break;
                        q -= normalize(g) * dd;
                    }
                } else if (object.size() > 1 && std::fabs(comboDistance(object, q)) > tol * 10) {
                    continue; // inside another piece or cut away: not on the outside
                }
                out.push_back(q);
            }
        }
    }
    return out;
}

double objectBottom(const std::vector<const MathPrim*>& object) {
    double z = 1e300;
    for (const Vec3& q : objectSurfaceSamples(object)) z = std::min(z, q.z);
    return z;
}

std::optional<double> landingMove(const std::vector<const MathPrim*>& object, const std::vector<const Brush*>& brushes,
                                  const std::vector<const MathPrim*>& otherPrims) {
    if (object.empty()) return std::nullopt;
    const AABB ob = boundsOf(object);
    // Lift it clear of everything under its footprint first, so an object that is partly
    // sunk in comes up to rest on top.
    double top = -1e300;
    for (const Brush* b : brushes) {
        const AABB bb = b->bounds();
        if (overlapsXY(bb, ob)) top = std::max(top, bb.max.z);
        if (auto g = terrainGrid(*b))
            for (const Vec3& p : g->pos) top = std::max(top, p.z);
    }
    std::vector<MathPrim> others;
    for (const MathPrim* p : otherPrims) {
        others.push_back(*p);
        const AABB pb = primBounds(*p);
        if (overlapsXY(pb, ob)) top = std::max(top, pb.max.z);
    }
    if (top == -1e300) return std::nullopt;
    const double lift = std::max(0.0, top - ob.min.z) + 1;
    std::vector<const MathPrim*> raisedPtrs;
    const std::vector<MathPrim> raised = copies(object, lift);
    for (const MathPrim& r : raised) raisedPtrs.push_back(&r);
    const AABB rb = boundsOf(raisedPtrs);

    const Vec3 down{0, 0, -1}, upDir{0, 0, 1};
    double best = 1e300;
    // Downward from the object's surface.
    for (const Vec3& q : objectSurfaceSamples(raisedPtrs)) {
        for (const Brush* b : brushes) {
            const double t = hitBrush(*b, q, down);
            if (t >= 0) best = std::min(best, t);
        }
        if (!others.empty())
            if (auto h = rayHitPrims(others, q, down)) best = std::min(best, h->t);
    }
    // Upward from what sticks up under it (corners of brushes, terrain points, the tops of
    // other primitives), so peaks between the samples count too.
    auto upFrom = [&](const Vec3& v) {
        if (v.x < rb.min.x || v.x > rb.max.x || v.y < rb.min.y || v.y > rb.max.y) return;
        if (auto h = rayHitPrims(raised, v, upDir)) best = std::min(best, h->t);
    };
    for (const Brush* b : brushes) {
        if (b->terrain && terrainValid(*b)) {
            if (auto g = terrainGrid(*b)) for (const Vec3& p : g->pos) upFrom(p);
        }
        for (const Vec3& v : brushVertices(*b)) upFrom(v);
    }
    for (const MathPrim* p : otherPrims)
        for (const Vec3& v : objectSurfaceSamples({p}, 16, 8)) upFrom(v);
    if (best >= 1e300) return std::nullopt;
    return lift - best;
}

} // namespace scal
