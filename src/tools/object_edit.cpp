#include "tools/object_edit.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace scal {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kMarkerTol = 8.0f;
constexpr float kRingTol = 5.0f;
constexpr float kHandleTol = 7.0f;
constexpr float kOutlineTol = 5.0f;

double segDist(Vector2 p, Vector2 a, Vector2 b) {
    const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

std::vector<int> ringAxes(const Viewport& vp) {
    if (vp.is2D()) return {vp.axD()};
    return {0, 1, 2};
}

Vec3 cameraPos(const Viewport& vp) {
    const Vector3 c = vp.cameraPos();
    return {c.x, c.y, c.z};
}

Vec3 towardViewer(const Viewport& vp) {
    Vec3 eu, ev;
    eu[vp.axU()] = 1;
    ev[vp.axV()] = 1;
    return cross(eu, ev);
}

std::vector<MathPrim> selectedPrims(const Document& doc) {
    std::vector<MathPrim> out;
    for (MathPrimId id : doc.primSelection())
        if (const MathPrim* p = doc.findPrim(id)) out.push_back(*p);
    return out;
}

AABB areaBox(const std::vector<const MathPrim*>& ps) {
    AABB b;
    for (const MathPrim* p : ps) {
        const AABB pb = primBounds(*p);
        b.add(pb.min);
        b.add(pb.max);
    }
    return b;
}
std::vector<Prop> selectedProps(const Document& doc) {
    std::vector<Prop> out;
    for (PropId id : doc.propSelection())
        if (const Prop* p = doc.findProp(id)) out.push_back(*p);
    return out;
}

// A prop's world box from its model box (the Document's models are not needed once the box is known).
AABB boundsOfLocal(const Prop& p, const AABB& local) {
    AABB b;
    for (int i = 0; i < 8; ++i)
        b.add(propToWorld(p, {i & 1 ? local.max.x : local.min.x, i & 2 ? local.max.y : local.min.y, i & 4 ? local.max.z : local.min.z}));
    return b;
}

// Centre of the model box face on axis `a`, `side` +1 = the max face.
Vec3 faceCentre(const AABB& lb, int a, int side) {
    Vec3 c = lb.center();
    c[a] = side > 0 ? lb.max[a] : lb.min[a];
    return c;
}

// The convex hull of points (Andrew's monotone chain).
std::vector<Vector2> hull(std::vector<Vector2> p) {
    std::sort(p.begin(), p.end(), [](Vector2 a, Vector2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    p.erase(std::unique(p.begin(), p.end(), [](Vector2 a, Vector2 b) { return a.x == b.x && a.y == b.y; }), p.end());
    if (p.size() < 3) return p;
    auto cr = [](Vector2 o, Vector2 a, Vector2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
    std::vector<Vector2> h(2 * p.size());
    size_t k = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        while (k >= 2 && cr(h[k - 2], h[k - 1], p[i]) <= 0) --k;
        h[k++] = p[i];
    }
    for (size_t i = p.size() - 1, t = k + 1; i > 0; --i) {
        while (k >= t && cr(h[k - 2], h[k - 1], p[i - 1]) <= 0) --k;
        h[k++] = p[i - 1];
    }
    h.resize(k - 1);
    return h;
}

double polyArea(const std::vector<Vector2>& h) {
    double a = 0;
    for (size_t i = 0; i < h.size(); ++i) {
        const Vector2 &p = h[i], &q = h[(i + 1) % h.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return std::fabs(a) * 0.5;
}
} // namespace

PropId pickProp(const Document& doc, const Viewport& vp, Vector2 local, const ModelFinder* models, double* dist) {
    if (doc.props().empty() || !doc.propsShown()) return 0;
    if (!vp.is2D()) {
        Vec3 o, d;
        vp.pickRay(local, o, d);
        ModelFinder none;
        if (auto h = rayHitProps(doc.props(), models ? *models : none, o, d)) {
            if (dist) *dist = h->t;
            return h->prop;
        }
        return 0;
    }
    // 2D: inside the turned box (or near its outline); the smallest box wins.
    PropId best = 0;
    double bestArea = 1e300;
    for (const Prop& p : doc.props()) {
        std::vector<Vector2> pts;
        for (const Vec3& c : propCorners(p, findModelOf(models, p.model))) {
            Vector2 s;
            if (vp.project(c, s)) pts.push_back(s);
        }
        if (pts.size() < 3) continue;
        const std::vector<Vector2> h = hull(pts);
        if (h.size() < 3) continue;
        bool inside = true;
        double edge = 1e300;
        for (size_t i = 0; i < h.size(); ++i) {
            const Vector2 &a = h[i], &b = h[(i + 1) % h.size()];
            if ((b.x - a.x) * (local.y - a.y) - (b.y - a.y) * (local.x - a.x) < 0) inside = false;
            edge = std::min(edge, segDist(local, a, b));
        }
        if (!inside && edge > kOutlineTol) continue;
        const double area = polyArea(h);
        if (area < bestArea) { bestArea = area; best = p.id; }
    }
    if (best && dist) *dist = 0;
    return best;
}

CameraId pickCamera(const Document& doc, const Viewport& vp, Vector2 local) {
    CameraId best = 0;
    double bestKey = 1e300;
    for (const Camera& c : doc.cameras()) {
        Vector2 s;
        if (!vp.project(c.pos, s)) continue;
        const double d = std::hypot(s.x - local.x, s.y - local.y);
        if (d > kMarkerTol) continue;
        const double key = vp.is2D() ? d : length(c.pos - cameraPos(vp)) + d;
        if (key < bestKey) { bestKey = key; best = c.id; }
    }
    return best;
}

LightId pickLight(const Document& doc, const Viewport& vp, Vector2 local) {
    LightId best = 0;
    double bestKey = 1e300;
    for (const Light& l : doc.lights()) {
        Vector2 s;
        if (!vp.project(l.pos, s)) continue;
        const double d = std::hypot(s.x - local.x, s.y - local.y);
        if (d > kMarkerTol) continue;
        const double key = vp.is2D() ? d : length(l.pos - cameraPos(vp)) + d;
        if (key < bestKey) { bestKey = key; best = l.id; }
    }
    return best;
}

MathPrimId pickPrim(const Document& doc, const Viewport& vp, Vector2 local, double* dist, MathPrimId* piece,
                    bool outlinesFirst) {
    if (doc.prims().empty() || !doc.primsShown()) return 0;
    auto firstOf = [&](const MathPrim& p) {
        MathPrimId first = p.id;
        if (p.combo)
            for (const MathPrim* m : comboMembers(doc.prims(), p.combo)) first = std::min(first, m->id);
        return first;
    };
    // 2D: near an outline (thin shapes seen side-on, like a disc, or a piece inside others).
    auto byOutline = [&]() -> MathPrimId {
        if (!vp.is2D()) return 0;
        const MathPrim* best = nullptr;
        double bestD = kOutlineTol;
        for (const MathPrim& p : doc.prims())
            for (const Polyline& l : primOutline(p, 32))
                for (size_t i = 0; i + 1 < l.pts.size() + (l.closed ? 1 : 0); ++i) {
                    Vector2 a, b;
                    vp.project(l.pts[i], a);
                    vp.project(l.pts[(i + 1) % l.pts.size()], b);
                    const double d = segDist(local, a, b);
                    if (d < bestD) { bestD = d; best = &p; }
                }
        if (!best) return 0;
        if (dist) *dist = 0;
        if (piece) *piece = best->id;
        return firstOf(*best);
    };
    if (outlinesFirst)
        if (MathPrimId id = byOutline()) return id;
    Vec3 o, d;
    if (vp.is2D()) {
        double u = 0, v = 0;
        vp.screenToWorld2D(local, u, v);
        const Vec3 tv = towardViewer(vp);
        o[vp.axU()] = u;
        o[vp.axV()] = v;
        o = o + tv * 1e6;
        d = -tv;
    } else {
        vp.pickRay(local, o, d);
    }
    if (auto h = rayHitPrims(doc.prims(), o, d)) {
        if (dist) *dist = h->t;
        if (piece) *piece = h->prim;
        return h->picked;
    }
    return outlinesFirst ? 0 : byOutline();
}

namespace {
// Rings and handles for a set of primitives (world sizes; views apply their minimum).
void primGizmo(const std::vector<MathPrim>& sel, const Viewport& vp, ObjectGizmo& g) {
    if (sel.empty()) return;
    std::vector<const MathPrim*> ptrs;
    for (const MathPrim& p : sel) ptrs.push_back(&p);
    const AABB b = areaBox(ptrs);
    const Vec3 c = b.center();
    const double r = std::max(0.5 * length(b.size()) * 1.05, Scene::kRingPx * vp.worldPerPixel(c));
    g.rings.push_back({c, r, 0});
    if (sel.size() != 1) return;
    const MathPrim& p = sel[0];
    const Mat3 R = rotationMatrix(p.rot);
    g.handlePrim = p.id;
    for (int a = 0; a < 3; ++a) {
        if (p.kind == PrimKind::Disc && a == 2) continue;
        const Vec3 u = R.col[a];
        if (vp.is2D() && std::hypot(u[vp.axU()], u[vp.axV()]) < 0.3) continue; // seen end-on
        for (int s = 0; s < 2; ++s) {
            Vec3 l;
            l[a] = s ? 1 : -1;
            g.handles.push_back({primToWorld(p, l), a * 2 + s, false});
        }
    }
}

// Rings and handles for a set of props.
void propGizmo(const std::vector<Prop>& sel, const Viewport& vp, const ModelFinder* models, ObjectGizmo& g) {
    if (sel.empty()) return;
    AABB b;
    for (const Prop& p : sel) b.add(propBounds(p, findModelOf(models, p.model)));
    const Vec3 c = b.center();
    const double r = std::max(0.5 * length(b.size()) * 1.05, Scene::kRingPx * vp.worldPerPixel(c));
    g.rings.push_back({c, r, 0, true});
    if (sel.size() != 1) return;
    const Prop& p = sel[0];
    const AABB lb = propLocalBounds(findModelOf(models, p.model));
    const Mat3 R = rotationMatrix(p.rot);
    g.handleProp = p.id;
    for (int a = 0; a < 3; ++a) {
        if (lb.max[a] - lb.min[a] < 1e-6) continue; // a flat model has no handle across itself
        const Vec3 u = R.col[a];
        if (vp.is2D() && std::hypot(u[vp.axU()], u[vp.axV()]) < 0.3) continue; // seen end-on
        for (int s = 0; s < 2; ++s) g.handles.push_back({propToWorld(p, faceCentre(lb, a, s ? 1 : -1)), a * 2 + s, true});
    }
}
} // namespace

ObjectGizmo objectGizmo(const Document& doc, const Viewport& vp, const ModelFinder* models) {
    ObjectGizmo g;
    for (LightId id : doc.lightSelection()) {
        const Light* l = doc.findLight(id);
        if (l && l->hasDirection()) g.rings.push_back({l->pos, Scene::kRingPx * vp.worldPerPixel(l->pos), id});
    }
    for (CameraId id : doc.cameraSelection())
        if (const Camera* c = doc.findCamera(id)) {
            ObjectGizmo::Rings r{c->pos, Scene::kRingPx * vp.worldPerPixel(c->pos), 0};
            r.camera = id;
            r.rollAxis = cameraForward(*c);
            g.rings.push_back(r);
        }
    primGizmo(selectedPrims(doc), vp, g);
    propGizmo(selectedProps(doc), vp, models, g);
    return g;
}

bool ObjectEdit::press(ToolEnv& env, const Pointer& p) {
    Document& doc = env.doc;
    const Viewport& vp = *p.view;
    models_ = env.models;
    const ObjectGizmo g = objectGizmo(doc, vp, env.models);
    auto begin = [&](Mode m) {
        mode_ = m;
        view_ = p.view;
        u0_ = p.u;
        v0_ = p.v;
        local0_ = p.local;
        turned_ = 0;
        amount_ = 0;
        moved_ = {};
        lightNow_ = lightStart_;
        primNow_ = primStart_;
        propNow_ = propStart_;
        cameraNow_ = cameraStart_;
    };
    if (decals_.pressHandles(env, p)) return true; // the selected decal's corners and ring
    lightStart_.clear();
    primStart_.clear();
    propStart_.clear();
    cameraStart_.clear();
    propsMode_ = false;
    camerasMode_ = false;
    sizeProp_ = false;
    if (!env.cmd) {
        // 1. Size handles of a single selected primitive.
        int bestH = -1;
        bool bestProp = false;
        double bestD = kHandleTol;
        for (const auto& h : g.handles) {
            Vector2 s;
            if (!vp.project(h.pos, s)) continue;
            const double d = std::hypot(s.x - p.local.x, s.y - p.local.y);
            if (d < bestD) { bestD = d; bestH = h.code; bestProp = h.prop; }
        }
        if (bestH >= 0) {
            handle_ = bestH;
            perAxis_ = env.shift;
            lightsMode_ = false;
            sizeProp_ = bestProp;
            if (bestProp) propStart_ = {*doc.findProp(g.handleProp)};
            else primStart_ = {*doc.findPrim(g.handlePrim)};
            begin(Mode::Size);
            return true;
        }
        // 2. Rings.
        const ObjectGizmo::Rings* hitRing = nullptr;
        int hitAxis = -1;
        double bestR = kRingTol;
        for (const auto& ring : g.rings)
            for (int axis : ringAxes(vp)) {
                const std::vector<Vec3> pts = axisCircle(ring.center, axis, ring.radius);
                for (size_t i = 0; i < pts.size(); ++i) {
                    Vector2 a, b;
                    if (!vp.project(pts[i], a) || !vp.project(pts[(i + 1) % pts.size()], b)) continue;
                    const double d = segDist(p.local, a, b);
                    if (d < bestR) { bestR = d; hitRing = &ring; hitAxis = axis; }
                }
            }
        for (const auto& ring : g.rings) { // cameras' roll rings (axis 3)
            if (!ring.camera) continue;
            if (vp.is2D() && std::fabs(ring.rollAxis[vp.axD()]) <= 0.2) continue; // edge-on: not drawn
            const std::vector<Vec3> pts = dirCircle(ring.center, ring.rollAxis, ring.radius * Scene::kRollRingScale);
            for (size_t i = 0; i < pts.size(); ++i) {
                Vector2 a, b;
                if (!vp.project(pts[i], a) || !vp.project(pts[(i + 1) % pts.size()], b)) continue;
                const double d = segDist(p.local, a, b);
                if (d < bestR) { bestR = d; hitRing = &ring; hitAxis = 3; }
            }
        }
        if (hitRing) {
            axis_ = hitAxis;
            pivot_ = hitRing->center;
            vp.project(pivot_, center_);
            angle0_ = std::atan2(-(p.local.y - center_.y), p.local.x - center_.x);
            Vec3 ax;
            if (hitAxis < 3) ax[hitAxis] = 1;
            else ax = hitRing->rollAxis;
            sign_ = (vp.is2D() ? dot(ax, towardViewer(vp)) : dot(ax, cameraPos(vp) - pivot_)) > 0 ? 1 : -1;
            lightsMode_ = hitRing->light != 0;
            propsMode_ = hitRing->props;
            camerasMode_ = hitRing->camera != 0;
            if (camerasMode_) {
                for (CameraId id : doc.cameraSelection())
                    if (const Camera* c = doc.findCamera(id)) cameraStart_.push_back(*c);
            } else if (lightsMode_) {
                for (LightId id : doc.lightSelection())
                    if (const Light* l = doc.findLight(id)) lightStart_.push_back(*l);
            } else if (propsMode_) {
                propStart_ = selectedProps(doc);
            } else {
                primStart_ = selectedPrims(doc);
            }
            begin(Mode::Ring);
            return true;
        }
    }
    // 3. A camera's marker, then a light's.
    if (const CameraId id = pickCamera(doc, vp, p.local)) {
        std::set<CameraId> sel = doc.cameraSelection();
        if (env.cmd) {
            if (!sel.erase(id)) sel.insert(id);
            doc.setCameraSelection(sel);
            return true;
        }
        if (!sel.count(id)) {
            doc.setCameraSelection({id});
            doc.setDecalSelection({});
            doc.setSelection({});
            doc.setLightSelection({});
            doc.setPrimSelection({});
            doc.setPropSelection({}); doc.setDecalSelection({});
        }
        if (!vp.is2D()) return true;
        lightsMode_ = false;
        camerasMode_ = true;
        for (CameraId c : doc.cameraSelection())
            if (const Camera* x = doc.findCamera(c)) cameraStart_.push_back(*x);
        anchor_ = doc.findCamera(id)->pos;
        begin(Mode::Move);
        return true;
    }
    if (const LightId id = pickLight(doc, vp, p.local)) {
        std::set<LightId> sel = doc.lightSelection();
        if (env.cmd) {
            if (!sel.erase(id)) sel.insert(id);
            doc.setLightSelection(sel);
            return true;
        }
        if (!sel.count(id)) {
            doc.setLightSelection({id});
            doc.setDecalSelection({});
            doc.setSelection({});
            doc.setPrimSelection({});
            doc.setPropSelection({}); doc.setDecalSelection({});
            doc.setCameraSelection({});
        }
        if (!vp.is2D()) return true;
        lightsMode_ = true;
        for (LightId l : doc.lightSelection())
            if (const Light* x = doc.findLight(l)) lightStart_.push_back(*x);
        anchor_ = doc.findLight(id)->pos;
        begin(Mode::Move);
        return true;
    }
    // 4. A primitive (unless a brush is in front of it / smaller than it).
    if (vp.is2D() && !doc.selection().empty() && !env.cmd) {
        // Inside the selected brushes' box: leave it to the brush tools (move, handles).
        AABB sb;
        for (const SceneBrush& b : env.sceneBrushes)
            if (doc.selection().count(b.id)) { sb.add(b.bounds.min); sb.add(b.bounds.max); }
        const double pad = 6 * vp.worldPerPixel({});
        if (sb.valid() && p.u >= sb.min[vp.axU()] - pad && p.u <= sb.max[vp.axU()] + pad &&
            p.v >= sb.min[vp.axV()] - pad && p.v <= sb.max[vp.axV()] + pad)
            return false;
    }
    // A prop was hit (`tProp` = distance in 3D): a brush in front of it (or smaller, in 2D)
    // takes the click; else select it and, in 2D, start dragging.
    auto pressPropHit = [&](PropId prid, double tProp) -> bool {
        const Prop* pr = doc.findProp(prid);
        if (vp.is2D()) {
            if (const BrushId bid = pick2D(env.sceneBrushes, vp, p.local)) {
                const AABB pb = propBounds(*pr, findModelOf(env.models, pr->model));
                for (const SceneBrush& b : env.sceneBrushes)
                    if (b.id == bid) {
                        const int U = vp.axU(), V = vp.axV();
                        const double ab = (b.bounds.max[U] - b.bounds.min[U]) * (b.bounds.max[V] - b.bounds.min[V]);
                        const double ap = (pb.max[U] - pb.min[U]) * (pb.max[V] - pb.min[V]);
                        if (ab < ap) return false; // the smaller one wins, like brushes among themselves
                    }
            }
        } else {
            Vec3 o, d;
            vp.pickRay(p.local, o, d);
            for (const Brush& b : doc.brushes()) {
                if (!doc.visible(b)) continue;
                const double t = rayHit(b, o, d);
                if (t >= 0 && t < tProp) return false; // a brush is in front
            }
        }
        std::set<PropId> sel = doc.propSelection();
        if (env.cmd) {
            if (!sel.erase(prid)) sel.insert(prid);
            doc.setPropSelection(sel);
            return true;
        }
        if (!sel.count(prid)) {
            doc.setPropSelection({prid});
            doc.setSelection({});
            doc.setLightSelection({});
            doc.setPrimSelection({});
            doc.setCameraSelection({});
            doc.setDecalSelection({});
        }
        if (!vp.is2D()) return true;
        lightsMode_ = false;
        propsMode_ = true;
        propStart_ = selectedProps(doc);
        anchor_ = doc.findProp(prid)->pos;
        begin(Mode::Move);
        return true;
    };
    double tPrim = 0, tProp = 0;
    MathPrimId piece = 0;
    const MathPrimId pid = pickPrim(doc, vp, p.local, &tPrim, &piece, env.alt);
    const PropId prid = pickProp(doc, vp, p.local, env.models, &tProp);
    // A decal (v0.12.5): unless a primitive or prop is in front of it.
    double tDecal = 0;
    if (const DecalId did = pickDecal(doc, vp, p.local, &tDecal)) {
        const bool hidden = !vp.is2D() && ((pid && tPrim < tDecal - 1) || (prid && tProp < tDecal - 1));
        if (!hidden) {
            decals_.pressDecal(env, p, did);
            return true;
        }
    }
    if (!pid && !prid) return false;
    if (prid && pid) {
        // Both under the cursor: the nearer one in 3D, the smaller one in a 2D view.
        bool propWins;
        if (vp.is2D()) {
            const AABB bp = propBounds(*doc.findProp(prid), findModelOf(env.models, doc.findProp(prid)->model));
            std::vector<const MathPrim*> ms;
            for (MathPrimId m : doc.withCombos({pid})) ms.push_back(doc.findPrim(m));
            const AABB bq = areaBox(ms);
            const int U = vp.axU(), V = vp.axV();
            propWins = (bp.max[U] - bp.min[U]) * (bp.max[V] - bp.min[V]) <= (bq.max[U] - bq.min[U]) * (bq.max[V] - bq.min[V]);
        } else {
            propWins = tProp <= tPrim;
        }
        if (propWins) return pressPropHit(prid, tProp);
    } else if (prid) {
        return pressPropHit(prid, tProp);
    }
    // A plain click takes the whole combination; Option+click just the piece.
    const std::set<MathPrimId> group = env.alt ? std::set<MathPrimId>{piece} : doc.withCombos({pid});
    if (vp.is2D()) {
        if (const BrushId bid = pick2D(env.sceneBrushes, vp, p.local)) {
            std::vector<const MathPrim*> ms;
            for (MathPrimId m : group) ms.push_back(doc.findPrim(m));
            const AABB pb = areaBox(ms);
            for (const SceneBrush& b : env.sceneBrushes)
                if (b.id == bid) {
                    const int U = vp.axU(), V = vp.axV();
                    const double ab = (b.bounds.max[U] - b.bounds.min[U]) * (b.bounds.max[V] - b.bounds.min[V]);
                    const double ap = (pb.max[U] - pb.min[U]) * (pb.max[V] - pb.min[V]);
                    if (ab < ap) return false; // the smaller one wins, like brushes among themselves
                }
        }
    } else {
        Vec3 o, d;
        vp.pickRay(p.local, o, d);
        for (const Brush& b : doc.brushes()) {
            if (!doc.visible(b)) continue;
            const double t = rayHit(b, o, d);
            if (t >= 0 && t < tPrim) return false; // a brush is in front
        }
    }
    std::set<MathPrimId> sel = doc.primSelection();
    if (env.cmd) {
        bool all = true;
        for (MathPrimId m : group) all = all && sel.count(m);
        for (MathPrimId m : group) { if (all) sel.erase(m); else sel.insert(m); }
        doc.setPrimSelection(sel);
        return true;
    }
    // Already selected (the whole combination, or this piece on its own): keep it, so a
    // piece picked from Properties or with Option+click can be dragged by itself.
    if (!sel.count(piece) || (env.alt && sel != group)) {
        doc.setPrimSelection(group);
        doc.setSelection({});
        doc.setLightSelection({});
        doc.setPropSelection({}); doc.setDecalSelection({});
        doc.setCameraSelection({});
    }
    if (!vp.is2D()) return true;
    lightsMode_ = false;
    primStart_ = selectedPrims(doc);
    anchor_ = doc.findPrim(piece)->pos;
    begin(Mode::Move);
    return true;
}

void ObjectEdit::drag(ToolEnv& env, const Pointer& p) {
    if (decals_.active()) { decals_.drag(env, p); return; }
    if (mode_ == Mode::None || p.view != view_) return;
    const Viewport& vp = *view_;
    if (mode_ == Mode::Move) {
        const int U = vp.axU(), V = vp.axV();
        Vec3 d;
        d[U] = env.grid.snapValue(anchor_[U] + (p.u - u0_)) - anchor_[U];
        d[V] = env.grid.snapValue(anchor_[V] + (p.v - v0_)) - anchor_[V];
        if (d == moved_) return;
        moved_ = d;
        lightNow_ = lightStart_;
        for (Light& l : lightNow_) l.pos += d;
        primNow_ = primStart_;
        for (MathPrim& m : primNow_) m.pos += d;
        propNow_ = propStart_;
        for (Prop& m : propNow_) m.pos += d;
        cameraNow_ = cameraStart_;
        for (Camera& c : cameraNow_) c.pos += d;
        return;
    }
    if (mode_ == Mode::Ring) {
        double ang = std::atan2(-(p.local.y - center_.y), p.local.x - center_.x) - angle0_;
        while (ang > kPi) ang -= 2 * kPi;
        while (ang < -kPi) ang += 2 * kPi;
        const double deg = std::round(ang * 180 / kPi / 15) * 15 * sign_;
        if (deg == turned_) return;
        turned_ = deg;
        lightNow_ = lightStart_;
        for (Light& l : lightNow_)
            if (l.hasDirection()) rotateLight(l, axis_, deg);
        primNow_ = primStart_;
        for (MathPrim& m : primNow_) rotatePrim(m, axis_, deg, pivot_);
        propNow_ = propStart_;
        for (Prop& m : propNow_) rotateProp(m, axis_, deg, pivot_);
        cameraNow_ = cameraStart_;
        for (Camera& c : cameraNow_) {
            if (axis_ == 3) rollCamera(c, deg);
            else rotateCamera(c, axis_, deg);
        }
        return;
    }
    if (sizeProp_) { dragPropSize(env, p); return; }
    // Size: how far the mouse went along the handle's axis (as seen on screen).
    const MathPrim& p0 = primStart_[0];
    const int a = handle_ / 2;
    const double side = handle_ % 2 ? 1 : -1;
    const Vec3 u = rotationMatrix(p0.rot).col[a];
    double s = 0;
    if (vp.is2D()) {
        const double wu = p.u - u0_, wv = p.v - v0_;
        const double iu = u[vp.axU()], iv = u[vp.axV()];
        const double l2 = iu * iu + iv * iv;
        if (l2 > 1e-6) s = (wu * iu + wv * iv) / l2;
    } else {
        Vector2 sa, sb;
        const Vec3 at = primToWorld(p0, [&] { Vec3 l; l[a] = side; return l; }());
        if (vp.project(at, sa) && vp.project(at + u * 64.0, sb)) {
            const double sx = (sb.x - sa.x) / 64, sy = (sb.y - sa.y) / 64, l2 = sx * sx + sy * sy;
            if (l2 > 1e-6) s = ((p.local.x - local0_.x) * sx + (p.local.y - local0_.y) * sy) / l2;
        }
    }
    const double size0 = std::fabs(p0.size[a]);
    const bool even = !(env.shift || perAxis_); // evenly by default; Shift = one axis
    // The handle follows the mouse: with the centre fixed (evenly) the size grows twice as much.
    // The change snaps to the grid (not the size), so an odd size stays as it is until
    // the handle actually moves.
    const double newSize = std::max(1.0, size0 + env.grid.snapValue(s * side * (even ? 2 : 1)));
    if (newSize - size0 == amount_) return;
    amount_ = newSize - size0;
    MathPrim m = p0;
    if (even) {
        const double f = newSize / std::max(size0, 1e-9);
        m.size = p0.size * f;
        if (m.kind == PrimKind::Disc) m.size.z = p0.size.z;
    } else {
        m.size[a] = newSize;
        m.pos = cleanVec(p0.pos + u * (side * amount_ * 0.5));
    }
    primNow_ = {m};
}

void ObjectEdit::dragPropSize(ToolEnv& env, const Pointer& p) {
    const Viewport& vp = *view_;
    const Prop& p0 = propStart_[0];
    const Model* model = findModelOf(models_, p0.model);
    const AABB lb = propLocalBounds(model);
    const int a = handle_ / 2;
    const int side = handle_ % 2 ? 1 : -1;
    const double ext = lb.max[a] - lb.min[a];
    const double sgn = p0.scale[a] < 0 ? -1 : 1; // a mirrored axis has its max face on the other side
    const Mat3 R = rotationMatrix(p0.rot);
    const Vec3 u = R.col[a];
    const double size0 = std::fabs(ext * p0.scale[a]);
    if (size0 < 1e-9) return;
    // How far the mouse went along the handle's axis (as seen on screen).
    double s = 0;
    if (vp.is2D()) {
        const double wu = p.u - u0_, wv = p.v - v0_;
        const double iu = u[vp.axU()], iv = u[vp.axV()];
        const double l2 = iu * iu + iv * iv;
        if (l2 > 1e-6) s = (wu * iu + wv * iv) / l2;
    } else {
        Vector2 sa, sb;
        const Vec3 at = propToWorld(p0, faceCentre(lb, a, side));
        if (vp.project(at, sa) && vp.project(at + u * 64.0, sb)) {
            const double sx = (sb.x - sa.x) / 64, sy = (sb.y - sa.y) / 64, l2 = sx * sx + sy * sy;
            if (l2 > 1e-6) s = ((p.local.x - local0_.x) * sx + (p.local.y - local0_.y) * sy) / l2;
        }
    }
    const bool even = !(env.shift || perAxis_);
    // The change snaps to the grid (not the size), so a size that is off the grid stays as it
    // is until the handle actually moves. Evenly about the centre the size grows twice as much.
    const double newSize = std::max(1.0, size0 + env.grid.snapValue(s * side * sgn * (even ? 2 : 1)));
    if (newSize - size0 == amount_) return;
    amount_ = newSize - size0;
    auto tidy = [](double v) { return std::round(v * 1e4) / 1e4; };
    Prop m = p0;
    const double f = newSize / size0;
    if (even) {
        const Vec3 cl = lb.center();
        const Vec3 cw = propToWorld(p0, cl);
        m.scale = {tidy(p0.scale.x * f), tidy(p0.scale.y * f), tidy(p0.scale.z * f)};
        const Vec3 sl{cl.x * m.scale.x, cl.y * m.scale.y, cl.z * m.scale.z};
        m.pos = cleanVec(cw - R * sl);
    } else {
        m.scale[a] = tidy(p0.scale[a] * f);
        // the opposite face stays where it is
        const Vec3 opp = faceCentre(lb, a, -side);
        m.pos = cleanVec(p0.pos + u * ((p0.scale[a] - m.scale[a]) * opp[a]));
    }
    propNow_ = {m};
}

void ObjectEdit::release(ToolEnv& env) {
    if (decals_.active()) { decals_.release(env); return; }
    if (mode_ == Mode::None) return;
    const char* name = mode_ == Mode::Ring ? "Turn" : mode_ == Mode::Size ? (sizeProp_ ? "Resize prop" : "Resize primitive") : "Move";
    mode_ = Mode::None;
    bool lc = false, pc = false, qc = false, cc = false;
    for (size_t i = 0; i < cameraNow_.size(); ++i) cc = cc || cameraNow_[i] != cameraStart_[i];
    if (cc) env.doc.apply(env.doc.makeModifyCameras(cameraNow_, std::string(name) == "Turn" ? "Turn camera" : "Move camera"));
    for (size_t i = 0; i < lightNow_.size(); ++i) lc = lc || lightNow_[i] != lightStart_[i];
    for (size_t i = 0; i < primNow_.size(); ++i) pc = pc || primNow_[i] != primStart_[i];
    for (size_t i = 0; i < propNow_.size(); ++i) qc = qc || propNow_[i] != propStart_[i];
    if (lc) env.doc.apply(env.doc.makeModifyLights(lightNow_, name));
    if (pc) env.doc.apply(env.doc.makeModifyPrims(primNow_, name));
    if (qc) env.doc.apply(env.doc.makeModifyProps(propNow_, name));
    lightStart_.clear();
    lightNow_.clear();
    primStart_.clear();
    primNow_.clear();
    propStart_.clear();
    propNow_.clear();
    cameraStart_.clear();
    cameraNow_.clear();
}

void ObjectEdit::cancel() {
    decals_.cancel();
    mode_ = Mode::None;
    lightStart_.clear();
    lightNow_.clear();
    primStart_.clear();
    primNow_.clear();
    propStart_.clear();
    propNow_.clear();
    cameraStart_.clear();
    cameraNow_.clear();
    view_ = nullptr;
}

void ObjectEdit::decorate(const Document& doc, Scene& scene) const {
    decals_.decorate(doc, scene);
    if (mode_ == Mode::Ring) scene.activeRing = axis_;
    for (const Light& l : lightNow_)
        for (Scene::SceneLight& s : scene.lights)
            if (s.light.id == l.id) s.light = l;
    for (const MathPrim& m : primNow_)
        for (Scene::ScenePrim& s : scene.prims)
            if (s.prim.id == m.id) s.prim = m;
    for (const Prop& m : propNow_)
        for (Scene::SceneProp& s : scene.props)
            if (s.prop.id == m.id) s.prop = m;
    for (const Camera& c : cameraNow_)
        for (Scene::SceneCamera& s : scene.cameras)
            if (s.cam.id == c.id) s.cam = c;
    // The primitives' rings and handles follow what is being dragged.
    std::vector<MathPrim> sel;
    for (const Scene::ScenePrim& s : scene.prims)
        if (doc.primSelection().count(s.prim.id)) sel.push_back(s.prim);
    if (!sel.empty()) {
        Scene::PrimGizmo gz;
        AABB b;
        for (const MathPrim& m : sel) { const AABB pb = primBounds(m); b.add(pb.min); b.add(pb.max); }
        gz.center = b.center();
        gz.radius = 0.5 * length(b.size()) * 1.05;
        if (sel.size() == 1) {
            const MathPrim& p = sel[0];
            const Mat3 R = rotationMatrix(p.rot);
            for (int a = 0; a < 3; ++a) {
                if (p.kind == PrimKind::Disc && a == 2) continue;
                for (int s = 0; s < 2; ++s) {
                    Vec3 l;
                    l[a] = s ? 1 : -1;
                    gz.handles.push_back({primToWorld(p, l), R.col[a]});
                }
            }
        }
        scene.gizmos.push_back(std::move(gz));
    }
    // ... and the props' (their model boxes come with the scene's props).
    std::vector<const Scene::SceneProp*> psel;
    for (const Scene::SceneProp& s : scene.props)
        if (doc.propSelection().count(s.prop.id)) psel.push_back(&s);
    if (!psel.empty()) {
        Scene::PrimGizmo gz;
        AABB b;
        for (const Scene::SceneProp* sp : psel) b.add(boundsOfLocal(sp->prop, sp->local));
        gz.center = b.center();
        gz.radius = 0.5 * length(b.size()) * 1.05;
        if (psel.size() == 1) {
            const Prop& p = psel[0]->prop;
            const AABB& lb = psel[0]->local;
            const Mat3 R = rotationMatrix(p.rot);
            for (int a = 0; a < 3; ++a) {
                if (lb.max[a] - lb.min[a] < 1e-6) continue;
                for (int s = 0; s < 2; ++s) gz.handles.push_back({propToWorld(p, faceCentre(lb, a, s ? 1 : -1)), R.col[a]});
            }
        }
        scene.gizmos.push_back(std::move(gz));
    }
}

const char* ObjectEdit::hint() const {
    if (const char* h = decals_.hint()) return h;
    switch (mode_) {
        case Mode::Move:
            std::snprintf(hintBuf_, sizeof hintBuf_, "moving by %g %g %g", moved_.x, moved_.y, moved_.z);
            return hintBuf_;
        case Mode::Ring: {
            if (axis_ == 3) {
                std::snprintf(hintBuf_, sizeof hintBuf_, "rolling the camera by %g degrees (15 degree steps)", turned_);
                return hintBuf_;
            }
            static const char* names[] = {"X", "Y", "Z"};
            std::snprintf(hintBuf_, sizeof hintBuf_, "turning around %s by %g degrees (15 degree steps)", names[axis_], turned_);
            return hintBuf_;
        }
        case Mode::Size:
            if (sizeProp_ && !propNow_.empty())
                std::snprintf(hintBuf_, sizeof hintBuf_, "scale %g x %g x %g   (evenly; Shift: one axis)", propNow_[0].scale.x,
                              propNow_[0].scale.y, propNow_[0].scale.z);
            else if (!primNow_.empty())
                std::snprintf(hintBuf_, sizeof hintBuf_, "size %g x %g x %g   (evenly; Shift: one axis)", primNow_[0].size.x,
                              primNow_[0].size.y, primNow_[0].size.z);
            return hintBuf_;
        default: return nullptr;
    }
}

} // namespace scal
