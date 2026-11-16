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

bool VertexTool::singlePosition(Vec3& out) const {
    if (sel_.empty()) return false;
    for (const VRef& r : sel_)
        if (!samePos(r.pos, sel_[0].pos)) return false;
    out = sel_[0].pos;
    return true;
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
        out.push_back(std::move(p));
    }
    return out;
}

void VertexTool::press(ToolEnv& env, const Pointer& p) {
    Document& doc = env.doc;
    message_.clear();
    if (!p.view->is2D()) {
        const BrushId id = pick3D(doc, *p.view, p.local);
        if (id) { doc.setSelection({id}); sel_.clear(); }
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
        view_ = p.view;
        u0_ = p.u;
        v0_ = p.v;
        anchor_ = hitPos;
        delta_ = {};
        preview_.clear();
        return;
    }

    // 2. Otherwise pick a brush to edit.
    const BrushId id = pick2D(env.sceneBrushes, *p.view, p.local);
    std::set<BrushId> s = doc.selection();
    if (env.cmd) { if (id) { if (!s.erase(id)) s.insert(id); } }
    else { s.clear(); if (id) s.insert(id); sel_.clear(); }
    doc.setSelection(s);
}

void VertexTool::drag(ToolEnv& env, const Pointer& p) {
    if (!dragging_ || p.view != view_) return;
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

bool VertexTool::key(ToolEnv&, int key) {
    if (key == KEY_ESCAPE) {
        if (dragging_) { dragging_ = false; preview_.clear(); delta_ = {}; }
        else sel_.clear();
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
    // Handles for every corner of the selected brushes (moved ones at their new spot).
    for (BrushId id : doc.selection()) {
        const Brush* b = doc.find(id);
        if (!b) continue;
        for (const Vec3& v : brushVertices(*b)) {
            const bool s = isSelected(id, v);
            scene.vertexHandles.push_back({s && dragging_ ? v + delta_ : v, s});
        }
    }
}

const char* VertexTool::hint() const {
    if (dragging_) {
        bool bad = false;
        for (const Preview& p : preview_) bad = bad || !p.ok;
        std::snprintf(hintBuf_, sizeof hintBuf_, "moving %zu corner(s) by %g %g %g%s", sel_.size(),
                      delta_.x, delta_.y, delta_.z, bad ? "   INVALID SHAPE - won't apply" : "");
        return hintBuf_;
    }
    if (!message_.empty()) return message_.c_str();
    return "click a corner (stacked corners move together)   Cmd+click: add   drag: move   click a brush: edit it";
}

void VertexTool::cancel() {
    dragging_ = false;
    preview_.clear();
    delta_ = {};
    view_ = nullptr;
}

} // namespace scal
