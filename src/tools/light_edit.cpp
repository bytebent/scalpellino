#include "tools/light_edit.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace scal {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr float kMarkerTol = 8.0f; // pixels
constexpr float kRingTol = 5.0f;

double segDist(Vector2 p, Vector2 a, Vector2 b) {
    const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

// Axes that get a ring in this view.
std::vector<int> ringAxes(const Viewport& vp) {
    if (vp.is2D()) return {vp.axD()};
    return {0, 1, 2};
}

Vec3 cameraPos(const Viewport& vp) {
    const Vector3 c = vp.cameraPos();
    return {c.x, c.y, c.z};
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

int pickRing(const Document& doc, const Viewport& vp, Vector2 local, LightId& light) {
    int bestAxis = -1;
    double best = kRingTol;
    for (LightId id : doc.lightSelection()) {
        const Light* l = doc.findLight(id);
        if (!l || !l->hasDirection()) continue;
        const double r = Scene::kRingPx * vp.worldPerPixel(l->pos);
        for (int axis : ringAxes(vp)) {
            const std::vector<Vec3> pts = axisCircle(l->pos, axis, r);
            for (size_t i = 0; i < pts.size(); ++i) {
                Vector2 a, b;
                if (!vp.project(pts[i], a) || !vp.project(pts[(i + 1) % pts.size()], b)) continue;
                const double d = segDist(local, a, b);
                if (d < best) { best = d; bestAxis = axis; light = id; }
            }
        }
    }
    return bestAxis;
}

bool LightEdit::press(ToolEnv& env, const Pointer& p) {
    Document& doc = env.doc;
    // 1. A ring of a selected light: turn it.
    LightId ringLight = 0;
    const int axis = pickRing(doc, *p.view, p.local, ringLight);
    if (axis >= 0 && !env.cmd) {
        mode_ = Mode::Ring;
        axis_ = axis;
        const Light* l = doc.findLight(ringLight);
        p.view->project(l->pos, center_);
        angle0_ = std::atan2(-(p.local.y - center_.y), p.local.x - center_.x);
        // Turning counter-clockwise on screen is a positive turn about an axis that
        // points at the viewer.
        Vec3 a;
        a[axis] = 1;
        if (p.view->is2D()) {
            Vec3 eu, ev;
            eu[p.view->axU()] = 1;
            ev[p.view->axV()] = 1;
            sign_ = dot(a, cross(eu, ev)) > 0 ? 1 : -1;
        } else {
            sign_ = dot(a, cameraPos(*p.view) - l->pos) > 0 ? 1 : -1;
        }
    } else {
        // 2. A marker: select it (and get ready to move in 2D).
        const LightId id = pickLight(doc, *p.view, p.local);
        if (!id) return false;
        std::set<LightId> sel = doc.lightSelection();
        if (env.cmd) {
            if (!sel.erase(id)) sel.insert(id);
            doc.setLightSelection(sel);
            return true;
        }
        if (!sel.count(id)) {
            doc.setLightSelection({id});
            doc.setSelection({});
        }
        if (!p.view->is2D()) return true; // 3D: select only
        mode_ = Mode::Move;
        anchor_ = id;
    }
    view_ = p.view;
    u0_ = p.u;
    v0_ = p.v;
    turned_ = 0;
    moved_ = {};
    start_.clear();
    for (LightId id : doc.lightSelection())
        if (const Light* l = doc.findLight(id)) start_.push_back(*l);
    now_ = start_;
    return true;
}

void LightEdit::drag(ToolEnv& env, const Pointer& p) {
    if (mode_ == Mode::None || p.view != view_) return;
    if (mode_ == Mode::Move) {
        const Light* a = nullptr;
        for (const Light& l : start_) if (l.id == anchor_) a = &l;
        if (!a) return;
        const int U = view_->axU(), V = view_->axV();
        Vec3 d;
        d[U] = env.grid.snapValue(a->pos[U] + (p.u - u0_)) - a->pos[U];
        d[V] = env.grid.snapValue(a->pos[V] + (p.v - v0_)) - a->pos[V];
        if (d == moved_) return;
        moved_ = d;
        now_ = start_;
        for (Light& l : now_) l.pos += d;
        return;
    }
    double ang = std::atan2(-(p.local.y - center_.y), p.local.x - center_.x) - angle0_;
    while (ang > kPi) ang -= 2 * kPi;
    while (ang < -kPi) ang += 2 * kPi;
    double deg = std::round(ang * 180 / kPi / 15) * 15 * sign_;
    if (deg == turned_) return;
    turned_ = deg;
    now_ = start_;
    for (Light& l : now_)
        if (l.hasDirection()) rotateLight(l, axis_, deg);
}

void LightEdit::release(ToolEnv& env) {
    if (mode_ == Mode::None) return;
    const bool ring = mode_ == Mode::Ring;
    mode_ = Mode::None;
    bool changed = false;
    for (size_t i = 0; i < now_.size(); ++i) changed = changed || now_[i] != start_[i];
    if (changed) env.doc.apply(env.doc.makeModifyLights(now_, ring ? "Turn light" : "Move light"));
    start_.clear();
    now_.clear();
}

void LightEdit::cancel() {
    mode_ = Mode::None;
    start_.clear();
    now_.clear();
    view_ = nullptr;
}

void LightEdit::decorate(Scene& scene) const {
    if (mode_ == Mode::Ring) scene.activeRing = axis_;
    for (const Light& l : now_)
        for (Scene::SceneLight& s : scene.lights)
            if (s.light.id == l.id) s.light = l;
}

const char* LightEdit::hint() const {
    if (mode_ == Mode::Move) {
        std::snprintf(hintBuf_, sizeof hintBuf_, "moving %zu light(s) by %g %g %g", now_.size(), moved_.x, moved_.y, moved_.z);
        return hintBuf_;
    }
    if (mode_ == Mode::Ring) {
        static const char* names[] = {"X", "Y", "Z"};
        std::snprintf(hintBuf_, sizeof hintBuf_, "turning around %s by %g degrees (15 degree steps)", names[axis_], turned_);
        return hintBuf_;
    }
    return nullptr;
}

} // namespace scal
