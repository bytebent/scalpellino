#include "tools/vertex_tool.hpp"
#include "core/hull.hpp"
#include <cmath>
#include <cstdio>

namespace scal {

namespace {
constexpr double kSame = 1e-4;
bool samePos(const Vec3& a, const Vec3& b) { return length(a - b) < kSame; }
} // namespace

bool VertexTool::isSelected(BrushId id, const Vec3& p) const {
    for (const VRef& r : sel_)
        if (r.brush == id && samePos(r.pos, p)) return true;
    return false;
}

bool VertexTool::isTerrainSelected(BrushId id, int idx) const {
    for (const TRef& r : tsel_)
        if (r.brush == id && r.idx == idx) return true;
    return false;
}

bool VertexTool::singlePosition(Vec3& out) const {
    if (sel_.empty()) return false;
    for (const VRef& r : sel_)
        if (!samePos(r.pos, sel_[0].pos)) return false;
    out = sel_[0].pos;
    return true;
}

std::vector<VertexTool::TPoint> VertexTool::terrainPoints(const Document& doc) const {
    std::vector<TPoint> out;
    for (BrushId id : doc.selection()) {
        const Brush* b = doc.find(id);
        if (!b || !doc.visible(*b)) continue;
        const auto g = terrainGrid(*b);
        if (!g) continue;
        for (int k = 0; k < (int)g->pos.size(); ++k) out.push_back({id, k, g->pos[k], g->normal, b->terrain->heights[k]});
    }
    return out;
}

bool VertexTool::singleHeight(const Document& doc, double& out) const {
    bool first = true;
    for (const TRef& r : tsel_) {
        const Brush* b = doc.find(r.brush);
        if (!b || !b->terrain || r.idx >= (int)b->terrain->heights.size()) continue;
        const double h = b->terrain->heights[r.idx];
        if (first) { out = h; first = false; }
        else if (std::fabs(h - out) > 1e-9) return false;
    }
    return !first;
}

std::vector<VertexTool::Preview> VertexTool::build(const Document& doc, const Vec3& delta) const {
    std::vector<Preview> out;
    for (BrushId id : doc.selection()) {
        const Brush* b = doc.find(id);
        if (!b) continue;
        std::vector<Vec3> verts = brushVertices(*b);
        bool touched = false;
        for (Vec3& v : verts)
            if (isSelected(id, v)) { v += delta; touched = true; }
        if (!touched) continue;
        HullResult h = brushFromVertices(verts, *b);
        Preview p;
        p.ok = h.ok;
        p.error = h.error;
        if (h.ok) p.brush = std::move(h.brush);
        else {
            // Invalid: show the solid you'd get (corners that end up inside are lost).
            HullResult loose = brushFromVertices(verts, *b, false);
            p.brush = loose.ok ? loose.brush : *b;
        }
        // Terrain stays on its face if that face kept its corners (they may have moved).
        if (b->terrain && p.brush.faces.size())
            carryTerrain(*b, p.brush, [&](const Vec3& v) { return isSelected(id, v) ? v + delta : v; });
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<Brush> VertexTool::buildTerrain(const Document& doc, double amount, bool absolute) const {
    std::vector<Brush> all;
    std::vector<TerrainPoint> pts;
    for (const Brush& b : doc.brushes()) {
        if (!b.terrain || !terrainValid(b)) continue;
        for (const TRef& r : tsel_)
            if (r.brush == b.id) pts.push_back({all.size(), r.idx});
        all.push_back(b);
    }
    std::vector<Brush> out;
    if (pts.empty() || !moveTerrainPoints(all, pts, amount, absolute)) return out;
    for (Brush& b : all) {
        const Brush* old = doc.find(b.id);
        if (old && old->terrain && *old->terrain != *b.terrain) out.push_back(std::move(b));
    }
    return out;
}

bool VertexTool::pressTerrain(ToolEnv& env, const Pointer& p) {
    const Document& doc = env.doc;
    const std::vector<TPoint> pts = terrainPoints(doc);
    if (pts.empty()) return false;
    const bool flat = p.view->is2D();
    const int U = flat ? p.view->axU() : 0, V = flat ? p.view->axV() : 1;
    auto screen = [&](const Vec3& w, Vector2& s) {
        if (flat) { s = p.view->worldToLocal2D(w[U], w[V]); return true; }
        return p.view->worldToLocal3D(w, s);
    };
    // Nearest handle on screen (3D: the one nearest the camera among close ones).
    const float tol = 6.0f;
    int best = -1;
    double bestKey = 1e300;
    Vec3 cam;
    if (!flat) {
        const Vector3 c = p.view->cameraPos();
        cam = {c.x, c.y, c.z};
    }
    for (int k = 0; k < (int)pts.size(); ++k) {
        Vector2 s;
        if (!screen(pts[k].pos, s)) continue;
        const double d = std::hypot(s.x - p.local.x, s.y - p.local.y);
        if (d > tol) continue;
        const double key = flat ? d : length(pts[k].pos - cam) + d * 1e-3;
        if (key < bestKey) { bestKey = key; best = k; }
    }
    if (best < 0) return false;
    // 2D: every point stacked under it on terrain facing the same way comes too (like
    // corners). 3D: just that one.
    std::vector<TRef> stacked;
    for (const TPoint& t : pts) {
        const bool same = flat ? std::fabs(t.pos[U] - pts[best].pos[U]) < kSame && std::fabs(t.pos[V] - pts[best].pos[V]) < kSame &&
                                     length(t.normal - pts[best].normal) < 1e-3
                               : (t.brush == pts[best].brush && t.idx == pts[best].idx);
        if (same) stacked.push_back({t.brush, t.idx});
    }
    bool allSelected = true;
    for (const TRef& h : stacked) allSelected = allSelected && isTerrainSelected(h.brush, h.idx);
    message_.clear();
    if (env.cmd) {
        sel_.clear(); // corners and terrain points are moved separately
        if (allSelected) {
            std::vector<TRef> keep;
            for (const TRef& r : tsel_) {
                bool drop = false;
                for (const TRef& h : stacked) drop = drop || (h.brush == r.brush && h.idx == r.idx);
                if (!drop) keep.push_back(r);
            }
            tsel_ = keep;
        } else {
            for (const TRef& h : stacked)
                if (!isTerrainSelected(h.brush, h.idx)) tsel_.push_back(h);
        }
        return true;
    }
    sel_.clear();
    if (!allSelected) tsel_ = stacked;
    dragging_ = true;
    dragTerrain_ = true;
    view_ = p.view;
    u0_ = p.u;
    v0_ = p.v;
    local0_ = p.local;
    anchor_ = pts[best].pos;
    anchorNormal_ = pts[best].normal;
    anchorHeight_ = pts[best].height;
    raise_ = 0;
    terrainPreview_.clear();
    if (flat && std::fabs(anchorNormal_[U]) < 1e-6 && std::fabs(anchorNormal_[V]) < 1e-6)
        message_ = "this view looks straight at the terrain: drag in a side view or the 3D view (or Cmd+Shift+T)";
    return true;
}

void VertexTool::press(ToolEnv& env, const Pointer& p) {
    Document& doc = env.doc;
    message_.clear();
    if (!p.view->is2D()) {
        if (pressTerrain(env, p)) return;
        const BrushId id = pick3D(doc, *p.view, p.local);
        if (env.cmd) {
            std::set<BrushId> s = doc.selection();
            if (id && !s.erase(id)) s.insert(id);
            doc.setSelection(s);
        } else if (id) {
            doc.setSelection({id});
            sel_.clear();
            tsel_.clear();
        }
        return;
    }
    const int U = p.view->axU(), V = p.view->axV();

    // 1. Vertex handles of selected brushes under the cursor. Every corner that projects
    //    onto the same spot (stacked along the view direction) is taken together.
    float bestDist = 7.0f;
    bool found = false;
    Vec3 hitPos;
    for (BrushId id : doc.selection()) {
        const Brush* b = doc.find(id);
        if (!b) continue;
        for (const Vec3& v : brushVertices(*b)) {
            const Vector2 s = p.view->worldToLocal2D(v[U], v[V]);
            const float d = std::hypot(s.x - p.local.x, s.y - p.local.y);
            if (d < bestDist) { bestDist = d; hitPos = v; found = true; }
        }
    }
    std::vector<VRef> stacked;
    if (found)
        for (BrushId id : doc.selection())
            if (const Brush* b = doc.find(id))
                for (const Vec3& v : brushVertices(*b))
                    if (std::fabs(v[U] - hitPos[U]) < kSame && std::fabs(v[V] - hitPos[V]) < kSame)
                        stacked.push_back({id, v});

    if (!stacked.empty()) {
        tsel_.clear(); // corners and terrain points are moved separately
        bool allSelected = true;
        for (const VRef& h : stacked) allSelected = allSelected && isSelected(h.brush, h.pos);
        if (env.cmd) {
            if (allSelected) {
                std::vector<VRef> keep;
                for (const VRef& r : sel_) {
                    bool drop = false;
                    for (const VRef& h : stacked) drop = drop || (h.brush == r.brush && samePos(h.pos, r.pos));
                    if (!drop) keep.push_back(r);
                }
                sel_ = keep;
            } else {
                for (const VRef& h : stacked)
                    if (!isSelected(h.brush, h.pos)) sel_.push_back(h);
            }
            return;
        }
        if (!allSelected) sel_ = stacked;
        dragging_ = true;
        dragTerrain_ = false;
        view_ = p.view;
        u0_ = p.u;
        v0_ = p.v;
        anchor_ = hitPos;
        delta_ = {};
        preview_.clear();
        return;
    }

    // 2. Terrain points of selected terrain.
    if (pressTerrain(env, p)) return;

    // 3. Otherwise pick a brush to edit.
    const BrushId id = pick2D(env.sceneBrushes, *p.view, p.local);
    std::set<BrushId> s = doc.selection();
    if (env.cmd) { if (id) { if (!s.erase(id)) s.insert(id); } }
    else { s.clear(); if (id) s.insert(id); sel_.clear(); tsel_.clear(); }
    doc.setSelection(s);
}

void VertexTool::drag(ToolEnv& env, const Pointer& p) {
    if (!dragging_ || p.view != view_) return;
    if (dragTerrain_) {
        // How far along the normal: the mouse movement measured along the normal as it
        // appears on screen.
        double raw = 0;
        if (view_->is2D()) {
            Vec3 d;
            d[view_->axU()] = p.u - u0_;
            d[view_->axV()] = p.v - v0_;
            raw = dot(d, anchorNormal_);
        } else {
            Vector2 a, b;
            const double dx = p.local.x - local0_.x, dy = p.local.y - local0_.y;
            if (view_->worldToLocal3D(anchor_, a) && view_->worldToLocal3D(anchor_ + anchorNormal_ * 64.0, b)) {
                const double sx = (b.x - a.x) / 64.0, sy = (b.y - a.y) / 64.0; // pixels per unit
                const double s2 = sx * sx + sy * sy;
                if (s2 > 0.04 * 0.04) raw = (dx * sx + dy * sy) / s2;
                else {
                    // Looking along the normal: up on screen = out of the surface.
                    const Vector3 c = view_->cameraPos();
                    raw = -dy * length(anchor_ - Vec3{c.x, c.y, c.z}) / std::max(1, view_->height());
                }
            }
        }
        const double r = env.grid.snapValue(anchorHeight_ + raw) - anchorHeight_;
        if (std::fabs(r - raise_) > 1e-12) {
            raise_ = r;
            terrainPreview_ = r == 0 ? std::vector<Brush>{} : buildTerrain(env.doc, r, false);
        }
        return;
    }
    const int U = view_->axU(), V = view_->axV();
    Vec3 d;
    d[U] = env.grid.snapValue(anchor_[U] + (p.u - u0_)) - anchor_[U];
    d[V] = env.grid.snapValue(anchor_[V] + (p.v - v0_)) - anchor_[V];
    if (d != delta_) {
        delta_ = d;
        preview_ = (d == Vec3{}) ? std::vector<Preview>{} : build(env.doc, d);
    }
}

void VertexTool::release(ToolEnv& env, const Pointer&) {
    if (!dragging_) return;
    dragging_ = false;
    if (dragTerrain_) {
        dragTerrain_ = false;
        if (raise_ != 0) {
            std::string err;
            moveTerrainSelected(env.doc, raise_, false, err);
        }
        terrainPreview_.clear();
        raise_ = 0;
        return;
    }
    if (delta_ != Vec3{}) {
        std::string err;
        if (!moveSelected(env.doc, delta_, err)) message_ = "not applied: " + err;
    }
    preview_.clear();
    delta_ = {};
}

bool VertexTool::moveSelected(Document& doc, const Vec3& delta, std::string& error) {
    if (sel_.empty()) { error = "no vertices selected"; return false; }
    const std::vector<Preview> pv = build(doc, delta);
    std::vector<Brush> out;
    for (const Preview& p : pv) {
        if (!p.ok) { error = p.error; return false; }
        out.push_back(p.brush);
    }
    if (out.empty()) { error = "no vertices selected"; return false; }
    doc.apply(doc.makeModify(out, "Move vertices"));
    for (VRef& r : sel_) r.pos += delta;
    return true;
}

bool VertexTool::moveTerrainSelected(Document& doc, double amount, bool absolute, std::string& error) {
    if (tsel_.empty()) { error = "no terrain points selected"; return false; }
    std::vector<Brush> out = buildTerrain(doc, amount, absolute);
    if (out.empty()) return true; // already there
    doc.apply(doc.makeModify(out, absolute ? "Set terrain height" : "Move terrain points"));
    return true;
}

bool VertexTool::key(ToolEnv&, int key) {
    if (key == KEY_ESCAPE) {
        if (dragging_) {
            dragging_ = dragTerrain_ = false;
            preview_.clear();
            terrainPreview_.clear();
            delta_ = {};
            raise_ = 0;
        } else {
            sel_.clear();
            tsel_.clear();
        }
        return true;
    }
    return false;
}

void VertexTool::decorate(const Document& doc, Scene& scene) const {
    bool anyInvalid = false;
    std::set<BrushId> previewed;
    for (const Preview& p : preview_) {
        previewed.insert(p.brush.id);
        if (p.ok) scene.ghosts.push_back(makeSceneBrush(p.brush));
        else { anyInvalid = true; scene.invalidGhosts.push_back(makeSceneBrush(p.brush)); }
    }
    scene.hideSelected = !preview_.empty();
    scene.invalid = anyInvalid;
    // Terrain being raised: drawn in place of the originals (neighbours included).
    for (const Brush& b : terrainPreview_) scene.replaced.push_back(makeSceneBrush(b));
    // Terrain points of the selected terrain (moved ones at their new spot), then every
    // corner of the selected brushes on top.
    for (const TPoint& t : terrainPoints(doc)) {
        const bool s = isTerrainSelected(t.brush, t.idx);
        scene.vertexHandles.push_back({s && dragTerrain_ ? t.pos + t.normal * raise_ : t.pos, s, true});
    }
    for (BrushId id : doc.selection()) {
        const Brush* b = doc.find(id);
        if (!b) continue;
        for (const Vec3& v : brushVertices(*b)) {
            const bool s = isSelected(id, v);
            scene.vertexHandles.push_back({s && dragging_ && !dragTerrain_ ? v + delta_ : v, s, false});
        }
    }
}

const char* VertexTool::hint() const {
    if (dragging_ && dragTerrain_) {
        if (!message_.empty()) return message_.c_str();
        std::snprintf(hintBuf_, sizeof hintBuf_, "raising %zu terrain point(s) by %g", tsel_.size(), raise_);
        return hintBuf_;
    }
    if (dragging_) {
        bool bad = false;
        for (const Preview& p : preview_) bad = bad || !p.ok;
        std::snprintf(hintBuf_, sizeof hintBuf_, "moving %zu corner(s) by %g %g %g%s", sel_.size(),
                      delta_.x, delta_.y, delta_.z, bad ? "   INVALID SHAPE - won't apply" : "");
        return hintBuf_;
    }
    if (!message_.empty()) return message_.c_str();
    if (!tsel_.empty()) {
        std::snprintf(hintBuf_, sizeof hintBuf_,
                      "%zu terrain point(s): drag in 3D or a side view to raise/lower   Cmd+click: add   Cmd+Shift+T: type a height",
                      tsel_.size());
        return hintBuf_;
    }
    return "click a corner (stacked corners move together) or a terrain point   Cmd+click: add   drag: move   click a brush: edit it";
}

void VertexTool::cancel() {
    dragging_ = false;
    dragTerrain_ = false;
    preview_.clear();
    terrainPreview_.clear();
    delta_ = {};
    raise_ = 0;
    view_ = nullptr;
}

} // namespace scal
