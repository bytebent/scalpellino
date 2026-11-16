#include "tools/clip_tool.hpp"
#include "core/csg.hpp"
#include <cmath>
#include <cstdio>

namespace scal {

void ClipTool::setPoints(const Vec3& a, const Vec3& b, const Vec3& c) {
    p_[0] = a; p_[1] = b; p_[2] = c;
    active_ = length(cross(c - a, b - a)) > 1e-9;
}

void ClipTool::press(ToolEnv& env, const Pointer& p) {
    message_.clear();
    if (!p.view->is2D()) return;
    const int U = p.view->axU(), V = p.view->axV();
    view_ = p.view;
    // 1. Grab an existing point handle (nearest within 8 px; point 3 wins ties).
    if (active_) {
        int best = -1;
        float bestD = 8.0f;
        for (int i = 2; i >= 0; --i) {
            const Vector2 s = p.view->worldToLocal2D(p_[i][U], p_[i][V]);
            const float d = std::hypot(s.x - p.local.x, s.y - p.local.y);
            if (d < bestD) { bestD = d; best = i; }
        }
        if (best >= 0) { dragging_ = best; return; }
    }
    // 2. Start a new line. Depth: middle of the selection (doesn't matter for a plane
    //    going straight into the screen, but keeps the handles near the brushes).
    const AABB sb = env.doc.selectionBounds();
    const int D = p.view->axD();
    Vec3 a;
    a[U] = env.grid.snapValue(p.u);
    a[V] = env.grid.snapValue(p.v);
    a[D] = sb.valid() ? sb.center()[D] : 0;
    p_[0] = p_[1] = a;
    p_[2] = a;
    p_[2][D] += 64; // straight into the screen
    active_ = false;
    dragging_ = 3;
}

void ClipTool::drag(ToolEnv& env, const Pointer& p) {
    if (dragging_ < 0 || p.view != view_) return;
    const int U = view_->axU(), V = view_->axV();
    const double u = env.grid.snapValue(p.u), v = env.grid.snapValue(p.v);
    if (dragging_ == 3) {
        p_[1][U] = u;
        p_[1][V] = v;
        active_ = length(cross(p_[2] - p_[0], p_[1] - p_[0])) > 1e-9;
    } else {
        Vec3 np = p_[dragging_];
        np[U] = u;
        np[V] = v;
        Vec3 q[3] = {p_[0], p_[1], p_[2]};
        q[dragging_] = np;
        // Ignore moves that would make the three points collinear (no plane).
        if (length(cross(q[2] - q[0], q[1] - q[0])) > 1e-9) p_[dragging_] = np;
    }
}

void ClipTool::release(ToolEnv&, const Pointer&) {
    dragging_ = -1;
    view_ = nullptr;
}

std::vector<Brush> ClipTool::keptPieces(const Document& doc) const {
    if (cacheRev_ == doc.revision() && cacheKeep_ == keep && cacheP_[0] == p_[0] && cacheP_[1] == p_[1] && cacheP_[2] == p_[2])
        return cache_;
    cache_.clear();
    for (BrushId id : doc.selection()) {
        const Brush* b = doc.find(id);
        if (!b) continue;
        const std::string cap = b->faces.empty() ? std::string("default") : b->faces[0].texture;
        const SplitResult r = splitBrush(*b, p_[0], p_[1], p_[2], cap);
        if (r.front && keep != Keep::Back) cache_.push_back(*r.front);
        if (r.back && keep != Keep::Front) cache_.push_back(*r.back);
    }
    cacheRev_ = doc.revision();
    cacheKeep_ = keep;
    for (int i = 0; i < 3; ++i) cacheP_[i] = p_[i];
    return cache_;
}

bool ClipTool::apply(Document& doc, std::string& message) {
    if (!active_) { message = "draw a clip line first"; return false; }
    if (doc.selection().empty()) { message = "select brushes to clip first"; return false; }
    std::vector<Brush> modified, added;
    std::set<BrushId> deleted, selectAfter;
    for (BrushId id : doc.selection()) {
        const Brush* b = doc.find(id);
        if (!b) continue;
        const std::string cap = b->faces.empty() ? std::string("default") : b->faces[0].texture;
        const SplitResult r = splitBrush(*b, p_[0], p_[1], p_[2], cap);
        std::vector<Brush> keepPieces;
        if (r.front && keep != Keep::Back) keepPieces.push_back(*r.front);
        if (r.back && keep != Keep::Front) keepPieces.push_back(*r.back);
        if (keepPieces.empty()) { deleted.insert(id); continue; }
        keepPieces[0].id = id;
        modified.push_back(keepPieces[0]);
        selectAfter.insert(id);
        for (size_t i = 1; i < keepPieces.size(); ++i) added.push_back(keepPieces[i]);
    }
    Command c = doc.makeChange(modified, std::move(added), deleted, "Clip");
    // Select everything that came out of the clip.
    for (const BrushChange& ch : c.changes)
        if (ch.after) c.selectionAfter.insert(ch.id);
    doc.apply(std::move(c));
    cancel();
    return true;
}

bool ClipTool::key(ToolEnv& env, int key) {
    if (key == KEY_ESCAPE) {
        if (!active_ && dragging_ < 0) return false;
        cancel();
        return true;
    }
    if ((key == KEY_ENTER || key == KEY_KP_ENTER) && active_ && dragging_ < 0) {
        if (!apply(env.doc, message_)) return true;
        return true;
    }
    return false;
}

void ClipTool::decorate(const Document& doc, Scene& scene) const {
    if (dragging_ == 3 || active_) {
        scene.clipPoints = {p_[0], p_[1], p_[2]};
        scene.clipActive = active_;
    }
    if (!active_) return;
    for (const Brush& b : keptPieces(doc)) scene.ghosts.push_back(makeSceneBrush(b));
    scene.hideSelected = true;
}

const char* ClipTool::hint() const {
    if (!message_.empty()) return message_.c_str();
    const char* k = keep == Keep::Both ? "both" : keep == Keep::Front ? "front" : "back";
    if (active_) {
        std::snprintf(hintBuf_, sizeof hintBuf_,
                      "keeping %s (Shift+X cycles)   drag points 1-3 in any 2D view (3 tilts the plane)   Return: clip   Esc: cancel", k);
        return hintBuf_;
    }
    return "select brushes, then drag a line in a 2D view";
}

void ClipTool::cancel() {
    active_ = false;
    dragging_ = -1;
    view_ = nullptr;
}

} // namespace scal
