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
} // namespace

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

MathPrimId pickPrim(const Document& doc, const Viewport& vp, Vector2 local, double* dist) {
    if (doc.prims().empty()) return 0;
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
        return h->picked;
    }
    if (!vp.is2D()) return 0;
    // 2D: near an outline (thin shapes seen side-on, like a disc).
    for (const MathPrim& p : doc.prims())
        for (const Polyline& l : primOutline(p, 32))
            for (size_t i = 0; i + 1 < l.pts.size() + (l.closed ? 1 : 0); ++i) {
                Vector2 a, b;
                vp.project(l.pts[i], a);
                vp.project(l.pts[(i + 1) % l.pts.size()], b);
                if (segDist(local, a, b) < kOutlineTol) {
                    MathPrimId first = p.id;
                    if (p.combo)
                        for (const MathPrim* m : comboMembers(doc.prims(), p.combo)) first = std::min(first, m->id);
                    if (dist) *dist = 0;
                    return first;
                }
            }
    return 0;
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
            g.handles.push_back({primToWorld(p, l), a * 2 + s});
        }
    }
}
} // namespace

ObjectGizmo objectGizmo(const Document& doc, const Viewport& vp) {
    ObjectGizmo g;
    for (LightId id : doc.lightSelection()) {
        const Light* l = doc.findLight(id);
        if (l && l->hasDirection()) g.rings.push_back({l->pos, Scene::kRingPx * vp.worldPerPixel(l->pos), id});
    }
    primGizmo(selectedPrims(doc), vp, g);
    return g;
}

bool ObjectEdit::press(ToolEnv& env, const Pointer& p) {
    Document& doc = env.doc;
    const Viewport& vp = *p.view;
    const ObjectGizmo g = objectGizmo(doc, vp);
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
    };
    lightStart_.clear();
    primStart_.clear();
    if (!env.cmd) {
        // 1. Size handles of a single selected primitive.
        int bestH = -1;
        double bestD = kHandleTol;
        for (const auto& [pos, code] : g.handles) {
            Vector2 s;
            if (!vp.project(pos, s)) continue;
            const double d = std::hypot(s.x - p.local.x, s.y - p.local.y);
            if (d < bestD) { bestD = d; bestH = code; }
        }
        if (bestH >= 0) {
            handle_ = bestH;
            even_ = env.shift;
            lightsMode_ = false;
            primStart_ = {*doc.findPrim(g.handlePrim)};
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
        if (hitRing) {
            axis_ = hitAxis;
            pivot_ = hitRing->center;
            vp.project(pivot_, center_);
            angle0_ = std::atan2(-(p.local.y - center_.y), p.local.x - center_.x);
            Vec3 ax;
            ax[hitAxis] = 1;
            sign_ = (vp.is2D() ? dot(ax, towardViewer(vp)) : dot(ax, cameraPos(vp) - pivot_)) > 0 ? 1 : -1;
            lightsMode_ = hitRing->light != 0;
            if (lightsMode_) {
                for (LightId id : doc.lightSelection())
                    if (const Light* l = doc.findLight(id)) lightStart_.push_back(*l);
            } else {
                primStart_ = selectedPrims(doc);
            }
            begin(Mode::Ring);
            return true;
        }
    }
    // 3. A light's marker.
    if (const LightId id = pickLight(doc, vp, p.local)) {
        std::set<LightId> sel = doc.lightSelection();
        if (env.cmd) {
            if (!sel.erase(id)) sel.insert(id);
            doc.setLightSelection(sel);
            return true;
        }
        if (!sel.count(id)) {
            doc.setLightSelection({id});
            doc.setSelection({});
            doc.setPrimSelection({});
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
    double tPrim = 0;
    const MathPrimId pid = pickPrim(doc, vp, p.local, &tPrim);
    if (!pid) return false;
    const std::set<MathPrimId> group = doc.withCombos({pid});
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
    if (!sel.count(pid)) {
        doc.setPrimSelection(group);
        doc.setSelection({});
        doc.setLightSelection({});
    }
    if (!vp.is2D()) return true;
    lightsMode_ = false;
    primStart_ = selectedPrims(doc);
    anchor_ = doc.findPrim(pid)->pos;
    begin(Mode::Move);
    return true;
}

void ObjectEdit::drag(ToolEnv& env, const Pointer& p) {
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
        return;
    }
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
    const bool even = env.shift || even_;
    // The handle follows the mouse: with the centre fixed (evenly) the size grows twice as much.
    const double newSize = std::max(1.0, env.grid.snapValue(size0 + s * side * (even ? 2 : 1)));
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

void ObjectEdit::release(ToolEnv& env) {
    if (mode_ == Mode::None) return;
    const char* name = mode_ == Mode::Ring ? "Turn" : mode_ == Mode::Size ? "Resize primitive" : "Move";
    mode_ = Mode::None;
    bool lc = false, pc = false;
    for (size_t i = 0; i < lightNow_.size(); ++i) lc = lc || lightNow_[i] != lightStart_[i];
    for (size_t i = 0; i < primNow_.size(); ++i) pc = pc || primNow_[i] != primStart_[i];
    if (lc) env.doc.apply(env.doc.makeModifyLights(lightNow_, name));
    if (pc) env.doc.apply(env.doc.makeModifyPrims(primNow_, name));
    lightStart_.clear();
    lightNow_.clear();
    primStart_.clear();
    primNow_.clear();
}

void ObjectEdit::cancel() {
    mode_ = Mode::None;
    lightStart_.clear();
    lightNow_.clear();
    primStart_.clear();
    primNow_.clear();
    view_ = nullptr;
}

void ObjectEdit::decorate(const Document& doc, Scene& scene) const {
    if (mode_ == Mode::Ring) scene.activeRing = axis_;
    for (const Light& l : lightNow_)
        for (Scene::SceneLight& s : scene.lights)
            if (s.light.id == l.id) s.light = l;
    for (const MathPrim& m : primNow_)
        for (Scene::ScenePrim& s : scene.prims)
            if (s.prim.id == m.id) s.prim = m;
    // The primitives' rings and handles follow what is being dragged.
    std::vector<MathPrim> sel;
    for (const Scene::ScenePrim& s : scene.prims)
        if (doc.primSelection().count(s.prim.id)) sel.push_back(s.prim);
    if (!sel.empty()) {
        scene.primGizmo = Scene::PrimGizmo{};
        AABB b;
        for (const MathPrim& m : sel) { const AABB pb = primBounds(m); b.add(pb.min); b.add(pb.max); }
        scene.primGizmo->center = b.center();
        scene.primGizmo->radius = 0.5 * length(b.size()) * 1.05;
        if (sel.size() == 1) {
            const MathPrim& p = sel[0];
            const Mat3 R = rotationMatrix(p.rot);
            for (int a = 0; a < 3; ++a) {
                if (p.kind == PrimKind::Disc && a == 2) continue;
                for (int s = 0; s < 2; ++s) {
                    Vec3 l;
                    l[a] = s ? 1 : -1;
                    scene.primGizmo->handles.push_back({primToWorld(p, l), R.col[a]});
                }
            }
        }
    }
}

const char* ObjectEdit::hint() const {
    switch (mode_) {
        case Mode::Move:
            std::snprintf(hintBuf_, sizeof hintBuf_, "moving by %g %g %g", moved_.x, moved_.y, moved_.z);
            return hintBuf_;
        case Mode::Ring: {
            static const char* names[] = {"X", "Y", "Z"};
            std::snprintf(hintBuf_, sizeof hintBuf_, "turning around %s by %g degrees (15 degree steps)", names[axis_], turned_);
            return hintBuf_;
        }
        case Mode::Size:
            std::snprintf(hintBuf_, sizeof hintBuf_, "size %g x %g x %g   (Shift: evenly)", primNow_[0].size.x,
                          primNow_[0].size.y, primNow_[0].size.z);
            return hintBuf_;
        default: return nullptr;
    }
}

} // namespace scal
