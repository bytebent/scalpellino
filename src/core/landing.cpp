#include "core/landing.hpp"
#include "core/terrain.hpp"
#include "core/hull.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

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
        // Blended combinations: the added pieces, projected onto the blended surface below.
        // Plain ones: every piece; a subtracted piece's samples that are on the outside are
        // the faces its cut made (v0.12.6 review: before, only added pieces were sampled, so
        // an object whose bottom was cut flat landed by its old bottom).
        if (object.size() > 1 && m->op != PrimOp::Add && blended) continue;
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

std::optional<double> objectBottom(const std::vector<const MathPrim*>& object) {
    std::optional<double> z;
    for (const Vec3& q : objectSurfaceSamples(object)) z = std::min(z.value_or(q.z), q.z);
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

std::optional<double> land(const Subject& sub, const std::vector<const Brush*>& allBrushes,
                           const std::vector<const MathPrim*>& allPrims, const std::vector<PropRef>& allProps) {
    const AABB ob = sub.bounds;
    // Only what is under the object's footprint can hold it up (v0.10.3: before, every
    // obstacle in the map was tested, so landing among thousands of props took minutes).
    auto under = [&](const AABB& b) {
        return b.min.x <= ob.max.x && b.max.x >= ob.min.x && b.min.y <= ob.max.y && b.max.y >= ob.min.y;
    };
    std::vector<const Brush*> brushes;
    for (const Brush* b : allBrushes) if (under(b->bounds())) brushes.push_back(b);
    std::vector<PropRef> props;
    for (const PropRef& r : allProps) if (under(propBounds(*r.prop, r.model))) props.push_back(r);
    // Primitives: whole combinations, kept if any piece is under it (a subtracted piece
    // elsewhere still shapes the combination).
    std::vector<const MathPrim*> otherPrims;
    {
        std::set<std::uint32_t> combosUnder;
        for (const MathPrim* p : allPrims) if (under(primBounds(*p))) combosUnder.insert(p->combo ? p->combo : 0);
        for (const MathPrim* p : allPrims)
            if (p->combo ? combosUnder.count(p->combo) > 0 : under(primBounds(*p))) otherPrims.push_back(p);
    }
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

namespace {
const Model* findModel(const ModelFinder& models, const std::string& name) { return models ? models(name) : nullptr; }

// The selected primitives as objects: a combination is one object.
std::vector<std::vector<const MathPrim*>> selectedObjectsOf(const Document& doc) {
    std::vector<std::vector<const MathPrim*>> out;
    std::set<std::uint32_t> combos;
    for (MathPrimId id : doc.primSelection()) {
        const MathPrim* p = doc.findPrim(id);
        if (!p) continue;
        if (!p->combo) { out.push_back({p}); continue; }
        if (combos.insert(p->combo).second) out.push_back(comboMembers(doc.prims(), p->combo));
    }
    return out;
}
} // namespace

LandingResult landSelection(const Document& doc, const ModelFinder& models) {
    LandingResult result;
    const auto objects = selectedObjectsOf(doc);
    // Several selected objects land lowest first. One lands on another selected object only
    // if it started entirely above it (a stack stays a stack); selected objects that
    // overlap side by side, like scattered copies, all go down to what is below them
    // instead of piling up on each other (v0.10.2).
    struct Item {
        bool isProp = false;
        std::set<MathPrimId> prims; // the primitive object's pieces
        PropId prop = 0;
        double bottom = 0, top = 0;  // where it started
        bool landed = false;
    };
    std::vector<Item> items;
    std::set<MathPrimId> selPrims;
    for (const auto& obj : objects) {
        Item it;
        AABB b;
        for (const MathPrim* p : obj) {
            it.prims.insert(p->id);
            selPrims.insert(p->id);
            const AABB pb = primBounds(*p);
            b.add(pb.min);
            b.add(pb.max);
        }
        it.bottom = objectBottom(obj).value_or(b.min.z); // nothing left: its box, for the order
        it.top = b.max.z;
        items.push_back(std::move(it));
    }
    for (PropId id : doc.propSelection())
        if (const Prop* q = doc.findProp(id)) {
            Item it;
            it.isProp = true;
            it.prop = id;
            const Model* mdl = findModel(models, q->model);
            it.bottom = propBottom(*q, mdl);
            it.top = propBounds(*q, mdl).max.z;
            items.push_back(std::move(it));
        }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.bottom < b.bottom; });

    std::vector<const Brush*> brushes;
    for (const Brush& b : doc.brushes())
        if (doc.visible(b)) brushes.push_back(&b);
    std::vector<MathPrim> work = doc.prims();
    std::vector<Prop> propsWork = doc.props();
    std::set<MathPrimId> movedPrims;
    std::set<PropId> movedProps;
    std::map<MathPrimId, const Item*> primItem;
    std::map<PropId, const Item*> propItem;
    for (const Item& it : items) {
        if (it.isProp) propItem[it.prop] = &it;
        else for (MathPrimId id : it.prims) primItem[id] = &it;
    }
    // Whether `other` is something `me` may land on.
    auto counts = [](const Item& me, const Item* other) {
        if (!other) return true;                      // not selected
        if (other == &me) return false;
        return other->landed && other->top <= me.bottom + 1e-6; // selected: only if it started below
    };
    int nothing = 0;
    for (Item& it : items) {
        std::vector<const MathPrim*> me, otherPrims;
        for (const MathPrim& p : work) {
            if (it.prims.count(p.id)) me.push_back(&p);
            else if (doc.primsShown()) { // hidden ones are not landed on
                auto f = primItem.find(p.id);
                if (counts(it, f == primItem.end() ? nullptr : f->second)) otherPrims.push_back(&p);
            }
        }
        std::vector<PropRef> otherProps;
        Prop* myProp = nullptr;
        for (Prop& q : propsWork) {
            if (it.isProp && q.id == it.prop) { myProp = &q; continue; }
            if (!doc.propsShown()) continue; // hidden ones are not landed on
            auto f = propItem.find(q.id);
            if (counts(it, f == propItem.end() ? nullptr : f->second)) otherProps.push_back({&q, findModel(models, q.model)});
        }
        std::optional<double> mv;
        if (it.isProp) {
            if (!myProp) continue;
            mv = propLandingMove({myProp, findModel(models, myProp->model)}, brushes, otherPrims, otherProps);
        } else {
            mv = landingMove(me, brushes, otherPrims, otherProps);
        }
        it.landed = true;
        if (!mv) { ++nothing; continue; }
        if (it.isProp) {
            myProp->pos.z = cleanNumber(myProp->pos.z + *mv, 1e-6);
            movedProps.insert(it.prop);
        } else {
            for (MathPrim& p : work)
                if (it.prims.count(p.id)) {
                    p.pos.z = cleanNumber(p.pos.z + *mv, 1e-6);
                    movedPrims.insert(p.id);
                }
        }
    }
    for (const MathPrim& p : work) if (movedPrims.count(p.id)) result.prims.push_back(p);
    for (const Prop& q : propsWork) if (movedProps.count(q.id)) result.props.push_back(q);
    result.nothingBelow = nothing;
    return result;
}

} // namespace scal
