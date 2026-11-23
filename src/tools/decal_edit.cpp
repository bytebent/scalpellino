#include "tools/decal_edit.hpp"
#include "core/terrain.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace scal {

namespace {
constexpr float kCornerTol = 7.0f;
constexpr float kRingTol = 5.0f;
constexpr double kPi = 3.14159265358979323846;

double rayTri(const Vec3& o, const Vec3& d, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 e1 = b - a, e2 = c - a, p = cross(d, e2);
    const double det = dot(e1, p);
    if (std::fabs(det) < 1e-12) return -1;
    const double inv = 1 / det;
    const Vec3 s = o - a;
    const double u = dot(s, p) * inv;
    if (u < 0 || u > 1) return -1;
    const Vec3 q = cross(s, e1);
    const double v = dot(d, q) * inv;
    if (v < 0 || u + v > 1) return -1;
    const double t = dot(e2, q) * inv;
    return t >= 0 ? t : -1;
}

double segDist(Vector2 p, Vector2 a, Vector2 b) {
    const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

std::vector<Vec3> ringPoints(const Scene::DecalGizmo& g, int n = 64) {
    Decal tmp;
    tmp.normal = g.normal;
    tmp.up = g.corners[0] - g.corners[3];
    Vec3 r, u, nn;
    decalFrame(tmp, r, u, nn);
    std::vector<Vec3> pts;
    for (int i = 0; i < n; ++i) {
        const double a = 2 * kPi * i / n;
        pts.push_back(g.center + (r * std::cos(a) + u * std::sin(a)) * g.ringRadius);
    }
    return pts;
}

// Where the pick ray meets the decal's plane.
bool onPlane(const Viewport& vp, Vector2 local, const Decal& d, Vec3& out) {
    Vec3 o, dir;
    vp.pickRay(local, o, dir);
    const Vec3 n = normalize(d.normal);
    const double den = dot(dir, n);
    if (std::fabs(den) < 1e-9) return false;
    const double t = dot(d.pos - o, n) / den;
    if (t < 0) return false;
    out = o + dir * t;
    return true;
}

void clearOthers(Document& doc) {
    doc.setSelection({});
    doc.setLightSelection({});
    doc.setPrimSelection({});
    doc.setPropSelection({});
    doc.setCameraSelection({}); doc.setProbeSelection({});
}
} // namespace

std::optional<SurfaceHit> hitSurface(const Document& doc, const Vec3& o, const Vec3& d) {
    std::optional<SurfaceHit> best;
    for (const Brush& b : doc.brushes()) {
        if (!doc.visible(b)) continue;
        // the brush: the face it enters through
        double tEnter = -1e300, tExit = 1e300;
        int enter = -1;
        bool miss = false;
        for (size_t i = 0; i < b.faces.size(); ++i) {
            const Plane pl = b.faces[i].plane();
            const double den = dot(pl.n, d), dist = pl.distance(o);
            if (std::fabs(den) < 1e-12) {
                if (dist > 0) { miss = true; break; }
                continue;
            }
            const double t = -dist / den;
            if (den < 0) {
                if (t > tEnter) { tEnter = t; enter = (int)i; }
            } else
                tExit = std::min(tExit, t);
            if (tEnter > tExit) { miss = true; break; }
        }
        std::optional<TerrainGrid> g;
        if (b.terrain) g = terrainGrid(b);
        if (!miss && tExit >= 0 && enter >= 0 && tEnter >= 0 && !(g && enter == g->face) && (!best || tEnter < best->t))
            best = SurfaceHit{tEnter, o + d * tEnter, b.faces[enter].plane().n, b.id};
        if (!g) continue;
        for (int j = 0; j + 1 < g->n; ++j)
            for (int i = 0; i + 1 < g->n; ++i) {
                const Vec3 a = g->at(i, j), b1 = g->at(i + 1, j), c = g->at(i + 1, j + 1), e = g->at(i, j + 1);
                for (const auto& tri : {std::array<Vec3, 3>{a, b1, c}, std::array<Vec3, 3>{a, c, e}}) {
                    const double t = rayTri(o, d, tri[0], tri[1], tri[2]);
                    if (t < 0 || (best && t >= best->t)) continue;
                    Vec3 n = normalize(cross(tri[1] - tri[0], tri[2] - tri[0]));
                    if (dot(n, g->normal) < 0) n = n * -1;
                    best = SurfaceHit{t, o + d * t, n, b.id};
                }
            }
    }
    return best;
}

DecalId pickDecal(const Document& doc, const Viewport& vp, Vector2 local, double* dist) {
    if (!doc.decalsShown()) return 0;
    if (vp.is2D()) {
        DecalId best = 0;
        double bestArea = 1e300;
        for (const Brush& b : doc.brushes())
            for (const Decal& d : b.decals) {
                if (!doc.visible(b, d)) continue;
                const std::array<Vec3, 4> k = decalCorners(d);
                bool near = false;
                for (int i = 0; i < 4 && !near; ++i) {
                    Vector2 a, c;
                    if (vp.project(k[i], a) && vp.project(k[(i + 1) % 4], c) && segDist(local, a, c) < kCornerTol) near = true;
                }
                if (near && d.width * d.height < bestArea) {
                    bestArea = d.width * d.height;
                    best = d.id;
                }
            }
        return best;
    }
    Vec3 o, dir;
    vp.pickRay(local, o, dir);
    // Only where a picture shows: on the surface the ray hits (v0.12.6 review: the whole
    // rectangle used to count, also where it hangs past the edge of its surface).
    const auto surf = hitSurface(doc, o, dir);
    if (!surf) return 0;
    // the decal drawn on top there
    DecalId best = 0;
    int bestOrder = 0;
    for (const Brush& b : doc.brushes())
        for (const Decal& d : b.decals) {
            if (!doc.visible(b, d) || !decalDrawnAt(d, surf->p, surf->n)) continue;
            if (!best || d.order > bestOrder || (d.order == bestOrder && d.id > best)) {
                best = d.id;
                bestOrder = d.order;
            }
        }
    if (dist && best) *dist = surf->t;
    return best;
}

Scene::DecalGizmo decalGizmo(const Decal& d) {
    Scene::DecalGizmo g;
    g.corners = decalCorners(d);
    g.center = d.pos;
    g.normal = normalize(d.normal);
    g.ringRadius = 0.5 * std::hypot(d.width, d.height) * 1.15;
    return g;
}

bool DecalEdit::pressHandles(ToolEnv& env, const Pointer& p) {
    Document& doc = env.doc;
    const Viewport& vp = *p.view;
    mode_ = Mode::None;
    changed_ = false;
    if (!vp.is2D() && !env.cmd && doc.decalSelection().size() == 1) {
        const Document::DecalRef r = doc.findDecal(*doc.decalSelection().begin());
        if (r && doc.visible(*r.brush, *r.decal)) {
            const Scene::DecalGizmo gz = decalGizmo(*r.decal);
            const Scene::DecalGizmo* g = &gz;
            for (int i = 0; i < 4; ++i) {
                Vector2 s;
                if (vp.project(g->corners[i], s) && std::hypot(s.x - p.local.x, s.y - p.local.y) < kCornerTol) {
                    start_ = now_ = *r.decal;
                    corner_ = i;
                    view_ = p.view;
                    mode_ = Mode::Corner;
                    return true;
                }
            }
            const std::vector<Vec3> ring = ringPoints(gz);
            for (size_t i = 0; i < ring.size(); ++i) {
                Vector2 a, b;
                if (!vp.project(ring[i], a) || !vp.project(ring[(i + 1) % ring.size()], b)) continue;
                if (segDist(p.local, a, b) < kRingTol) {
                    start_ = now_ = *r.decal;
                    view_ = p.view;
                    vp.project(r.decal->pos, center_);
                    angle0_ = std::atan2(-(p.local.y - center_.y), p.local.x - center_.x);
                    rot0_ = decalRotation(*r.decal);
                    sign_ = dot(normalize(r.decal->normal), Vec3{vp.cameraPos().x, vp.cameraPos().y, vp.cameraPos().z} - r.decal->pos) > 0 ? 1 : -1;
                    mode_ = Mode::Ring;
                    return true;
                }
            }
        }
    }
    return false;
}

void DecalEdit::pressDecal(ToolEnv& env, const Pointer& p, DecalId id) {
    Document& doc = env.doc;
    const Viewport& vp = *p.view;
    mode_ = Mode::None;
    changed_ = false;
    std::set<DecalId> sel = doc.decalSelection();
    if (env.cmd) {
        if (!sel.erase(id)) sel.insert(id);
        doc.setDecalSelection(sel);
        return;
    }
    if (!sel.count(id)) {
        doc.setDecalSelection({id});
        clearOthers(doc);
    }
    if (vp.is2D()) return;
    const Document::DecalRef r = doc.findDecal(id);
    start_ = now_ = *r.decal;
    target_ = r.brush->id;
    view_ = p.view;
    Vec3 at;
    if (onPlane(vp, p.local, *r.decal, at)) {
        Vec3 rr, uu, nn;
        decalFrame(*r.decal, rr, uu, nn);
        grabX_ = dot(at - r.decal->pos, rr);
        grabY_ = dot(at - r.decal->pos, uu);
    } else
        grabX_ = grabY_ = 0;
    if (doc.decalSelection().size() == 1) mode_ = Mode::Move;
}

void DecalEdit::drag(ToolEnv& env, const Pointer& p) {
    if (mode_ == Mode::None || p.view != view_) return;
    const Viewport& vp = *p.view;
    if (mode_ == Mode::Corner) {
        Vec3 q;
        if (!onPlane(vp, p.local, start_, q)) return;
        Vec3 r, u, n;
        decalFrame(start_, r, u, n);
        // corners: 0 top left, 1 top right, 2 bottom right, 3 bottom left
        const double sx = (corner_ == 1 || corner_ == 2) ? 1 : -1, sy = (corner_ == 0 || corner_ == 1) ? 1 : -1;
        const double ox = -sx * start_.width / 2, oy = -sy * start_.height / 2; // the opposite corner stays
        double qx = dot(q - start_.pos, r), qy = dot(q - start_.pos, u);
        double w = std::max(1.0, (qx - ox) * sx), h = std::max(1.0, (qy - oy) * sy);
        if (env.shift) { // keep the proportions
            const double k = std::max(w / start_.width, h / start_.height);
            w = std::max(1.0, start_.width * k);
            h = std::max(1.0, start_.height * k);
        }
        w = cleanNumber(std::round(w * 10) / 10, 1e-9);
        h = cleanNumber(std::round(h * 10) / 10, 1e-9);
        qx = ox + sx * w;
        qy = oy + sy * h;
        now_ = start_;
        now_.width = w;
        now_.height = h;
        now_.pos = cleanVec(start_.pos + r * ((ox + qx) / 2) + u * ((oy + qy) / 2));
        changed_ = true;
        return;
    }
    if (mode_ == Mode::Ring) {
        const double a = std::atan2(-(p.local.y - center_.y), p.local.x - center_.x);
        double deg = (a - angle0_) * 180 / kPi * sign_;
        if (!env.shift) deg = std::round(deg / 15) * 15;
        now_ = start_;
        double rot = rot0_ + deg;
        while (rot > 180) rot -= 360;
        while (rot <= -180) rot += 360;
        setDecalRotation(now_, rot);
        changed_ = true;
        return;
    }
    // Move: slide over the surfaces under the cursor
    Vec3 o, dir;
    vp.pickRay(p.local, o, dir);
    const auto hit = hitSurface(env.doc, o, dir);
    if (!hit) return;
    Decal d = start_;
    const double rot = decalRotation(start_);
    d.normal = cleanVec(normalize(hit->n));
    d.up = decalDefaultUp(d.normal);
    setDecalRotation(d, rot);
    Vec3 r, u, n;
    decalFrame(d, r, u, n);
    Vec3 c = hit->p - r * grabX_ - u * grabY_;
    // snapped to the grid along the axes the surface runs along
    for (int k = 0; k < 3; ++k)
        if (std::fabs(n[k]) < 1e-6) c[k] = env.grid.snapValue(c[k]);
    d.pos = cleanVec(c);
    now_ = d;
    target_ = hit->brush;
    changed_ = true;
}

void DecalEdit::release(ToolEnv& env) {
    if (mode_ == Mode::None) return;
    const Mode m = mode_;
    mode_ = Mode::None;
    if (!changed_ || now_ == start_) return;
    Document& doc = env.doc;
    const Document::DecalRef r = doc.findDecal(now_.id);
    if (!r) return;
    if (m == Mode::Move && target_ && target_ != r.brush->id && doc.find(target_)) {
        // onto another brush: it belongs to that one now
        Command c = doc.makeModifyDecals({}, "Move decal");
        Brush from = *r.brush, to = *doc.find(target_);
        from.decals.erase(std::remove_if(from.decals.begin(), from.decals.end(), [&](const Decal& x) { return x.id == now_.id; }),
                          from.decals.end());
        to.decals.push_back(now_);
        c.changes.push_back({from.id, *r.brush, from});
        c.changes.push_back({to.id, *doc.find(target_), to});
        doc.apply(std::move(c));
        return;
    }
    doc.apply(doc.makeModifyDecals({now_}, m == Mode::Move ? "Move decal" : m == Mode::Corner ? "Size decal" : "Turn decal"));
}

void DecalEdit::decorate(const Document& doc, Scene& scene) const {
    if (mode_ != Mode::None && changed_) {
        for (Scene::SceneDecal& sd : scene.decals)
            if (sd.decal.id == now_.id) {
                sd.decal = now_;
                sd.tris = std::make_shared<std::vector<DecalTri>>(
                    decalMesh(now_, doc.brushes(), [&](const Brush& b) { return doc.visible(b); }));
            }
    }
    if (doc.decalSelection().size() != 1) return;
    for (const Scene::SceneDecal& sd : scene.decals)
        if (sd.decal.id == *doc.decalSelection().begin()) {
            scene.decalGizmo = decalGizmo(sd.decal);
            scene.decalGizmo->active = mode_ == Mode::Corner ? corner_ : mode_ == Mode::Ring ? 4 : mode_ == Mode::Move ? 5 : -1;
        }
}

const char* DecalEdit::hint() const {
    switch (mode_) {
    case Mode::Corner: return "drag: size the decal (the opposite corner stays)   Shift: keep its proportions   Esc: cancel";
    case Mode::Ring: return "drag: turn the decal in 15 degree steps   Shift: freely   Esc: cancel";
    case Mode::Move: return "drag: slide the decal over the surfaces (snapped to the grid)   Esc: cancel";
    default: return nullptr;
    }
}

} // namespace scal
