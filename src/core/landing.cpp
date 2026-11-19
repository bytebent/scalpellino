#include "core/landing.hpp"
#include "core/terrain.hpp"
#include "core/hull.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
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

namespace {
// What lands: its box; `lift(dz)` raises it by dz (once, before anything is measured);
// then its surface points, and what an upward ray from a point hits of it (distance).
struct Subject {
    AABB bounds;
    std::function<void(double)> lift;
    std::function<std::vector<Vec3>()> surface;
    std::function<std::optional<double>(const Vec3&)> upHit;
};

std::optional<double> land(const Subject& sub, const std::vector<const Brush*>& brushes,
                           const std::vector<const MathPrim*>& otherPrims, const std::vector<PropRef>& props) {
    const AABB ob = sub.bounds;
    // What counts as "below": surfaces under the object's middle height. So it drops onto
    // the first thing beneath it, comes up out of what it is sunk into (up to half its
    // height), and ignores what is beside or above it: a neighbour it overlaps, a shelf or
    // roof over it (v0.10.2; before, anything overlapping its footprint lifted it on top).
    const double mid = (ob.min.z + ob.max.z) * 0.5;
    const double eps = 1e-6 * std::max(1.0, length(ob.size()));
    const double lift = mid - ob.min.z + 1;
    sub.lift(lift);
    AABB rb = ob;
    rb.min.z += lift;
    rb.max.z += lift;

    // Other primitives one shape at a time (a combination as a whole), so each is judged
    // by its own surface under the ray.
    std::vector<std::vector<MathPrim>> shapes;
    {
        std::map<std::uint32_t, std::size_t> comboAt;
        for (const MathPrim* p : otherPrims) {
            if (p->combo) {
                auto it = comboAt.find(p->combo);
                if (it == comboAt.end()) {
                    comboAt[p->combo] = shapes.size();
                    shapes.push_back({*p});
                } else {
                    shapes[it->second].push_back(*p);
                }
            } else {
                shapes.push_back({*p});
            }
        }
    }
    std::vector<AABB> shapeBox;
    for (const auto& sh : shapes) {
        AABB b;
        for (const MathPrim& m : sh) { const AABB pb = primBounds(m); b.add(pb.min); b.add(pb.max); }
        shapeBox.push_back(b);
    }

    const Vec3 down{0, 0, -1};
    double best = 1e300;
    auto take = [&](const Vec3& q, double t) {
        if (t >= 0 && q.z - t <= mid + eps) best = std::min(best, t);
    };
    // Downward from the object's surface.
    for (const Vec3& q : sub.surface()) {
        for (const Brush* b : brushes) take(q, hitBrush(*b, q, down));
        for (std::size_t k = 0; k < shapes.size(); ++k) {
            if (q.x < shapeBox[k].min.x || q.x > shapeBox[k].max.x || q.y < shapeBox[k].min.y || q.y > shapeBox[k].max.y) continue;
            if (auto h = rayHitPrims(shapes[k], q, down)) take(q, h->t);
        }
        for (const PropRef& r : props)
            if (auto h = rayHitProp(*r.prop, r.model, q, down)) take(q, h->t);
    }
    // Upward from what sticks up under it (corners of brushes, terrain points, the tops of
    // other primitives and props), so peaks between the samples count too. Only points on
    // a top surface count (a straight-down ray from just above meets that surface there):
    // the bottom corners of a wall beside the object are nothing to rest on.
    const double probe = 1e-3 * std::max(1.0, length(ob.size()));
    auto onTop = [&](const Vec3& v, auto&& hitDown) {
        const double t = hitDown(v + Vec3{0, 0, probe});
        return t >= 0 && std::fabs(t - probe) <= probe * 0.5;
    };
    auto upFrom = [&](const Vec3& v, auto&& hitDown) {
        if (v.z > mid + eps) return;
        if (v.x < rb.min.x || v.x > rb.max.x || v.y < rb.min.y || v.y > rb.max.y) return;
        if (!onTop(v, hitDown)) return;
        if (auto t = sub.upHit(v)) best = std::min(best, *t);
    };
    for (const Brush* b : brushes) {
        auto hb = [&](const Vec3& o) { return hitBrush(*b, o, down); };
        if (b->terrain && terrainValid(*b)) {
            if (auto g = terrainGrid(*b)) for (const Vec3& p : g->pos) upFrom(p, hb);
        }
        for (const Vec3& v : brushVertices(*b)) upFrom(v, hb);
    }
    for (std::size_t k = 0; k < shapes.size(); ++k) {
        auto hs = [&](const Vec3& o) { auto h = rayHitPrims(shapes[k], o, down); return h ? h->t : -1.0; };
        std::vector<const MathPrim*> ptrs;
        for (const MathPrim& m : shapes[k]) ptrs.push_back(&m);
        for (const Vec3& v : objectSurfaceSamples(ptrs, 16, 8)) upFrom(v, hs);
    }
    for (const PropRef& r : props) {
        auto hp = [&](const Vec3& o) { auto h = rayHitProp(*r.prop, r.model, o, down); return h ? h->t : -1.0; };
        for (const Vec3& v : propSurfacePoints(*r.prop, r.model, 1500)) upFrom(v, hp);
    }
    if (best >= 1e300) return std::nullopt;
    return lift - best;
}
} // namespace

std::optional<double> landingMove(const std::vector<const MathPrim*>& object, const std::vector<const Brush*>& brushes,
                                  const std::vector<const MathPrim*>& otherPrims, const std::vector<PropRef>& otherProps) {
    if (object.empty()) return std::nullopt;
    Subject sub;
    sub.bounds = boundsOf(object);
    std::vector<MathPrim> raised;
    sub.lift = [&](double dz) { raised = copies(object, dz); };
    sub.surface = [&] {
        std::vector<const MathPrim*> ptrs;
        for (const MathPrim& r : raised) ptrs.push_back(&r);
        return objectSurfaceSamples(ptrs);
    };
    sub.upHit = [&](const Vec3& v) -> std::optional<double> {
        if (auto h = rayHitPrims(raised, v, {0, 0, 1})) return h->t;
        return std::nullopt;
    };
    return land(sub, brushes, otherPrims, otherProps);
}

std::optional<double> propLandingMove(const PropRef& prop, const std::vector<const Brush*>& brushes,
                                      const std::vector<const MathPrim*>& otherPrims,
                                      const std::vector<PropRef>& otherProps) {
    if (!prop.prop) return std::nullopt;
    Subject sub;
    sub.bounds = propBounds(*prop.prop, prop.model);
    Prop raised = *prop.prop;
    sub.lift = [&](double dz) { raised.pos.z += dz; };
    sub.surface = [&] { return propSurfacePoints(raised, prop.model, 3000); };
    sub.upHit = [&](const Vec3& v) -> std::optional<double> {
        if (auto h = rayHitProp(raised, prop.model, v, {0, 0, 1})) return h->t;
        return std::nullopt;
    };
    return land(sub, brushes, otherPrims, otherProps);
}

} // namespace scal
