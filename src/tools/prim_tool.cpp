#include "tools/prim_tool.hpp"
#include <algorithm>
#include <cmath>

namespace scal {

std::optional<MathPrim> PrimTool::pending() const {
    if (!box_) return std::nullopt;
    MathPrim p = templ;
    p.id = 0;
    p.combo = 0;
    p.op = PrimOp::Add;
    p.otherKeys.clear();
    p.pos = box_->center();
    const Vec3 s = box_->size();
    p.rot = {};
    p.size = s;
    if (p.kind == PrimKind::Disc) {
        // Lies across the view's axis: its own Z points along it.
        if (axis_ == 0) { p.rot = {0, 90, 0}; p.size = {s.z, s.y, 0}; }
        else if (axis_ == 1) { p.rot = {-90, 0, 0}; p.size = {s.x, s.z, 0}; }
        else p.size = {s.x, s.y, 0};
    }
    return p;
}

void PrimTool::setPending(const Vec3& c, const Vec3& size, int viewAxis) {
    AABB b;
    b.add(c - size * 0.5);
    b.add(c + size * 0.5);
    box_ = b;
    axis_ = viewAxis;
}

bool PrimTool::create(Document& doc, const std::string& texture, AABB* lastBox) {
    auto p = pending();
    if (!p) return false;
    if (std::min({std::fabs(p->size.x), std::fabs(p->size.y), p->kind == PrimKind::Disc ? 1.0 : std::fabs(p->size.z)}) < 1) {
        message_ = "the box is flat: draw it in another view too, or type its size in Properties";
        return true;
    }
    p->texture = flat ? "" : texture;
    if (lastBox) *lastBox = primBounds(*p);
    doc.apply(doc.makeAddPrims({*p}, std::string("Create ") + primKindName(p->kind)));
    box_.reset();
    return true;
}

void PrimTool::press(ToolEnv& env, const Pointer& p) {
    message_.clear();
    if (objects_.press(env, p)) return;
    if (!p.view->is2D()) {
        message_ = "draw the box in a 2D view";
        return;
    }
    const int U = p.view->axU(), V = p.view->axV(), D = p.view->axD();
    au_ = env.grid.snapValue(p.u);
    av_ = env.grid.snapValue(p.v);
    // Depth: the pending box's, else the last box's (like the block tool).
    const AABB& from = box_ ? *box_ : env.lastBox;
    AABB b;
    Vec3 lo, hi;
    lo[U] = hi[U] = au_;
    lo[V] = hi[V] = av_;
    lo[D] = from.valid() ? from.min[D] : 0;
    hi[D] = from.valid() ? from.max[D] : 64;
    if (hi[D] - lo[D] < 1) hi[D] = lo[D] + 64;
    b.add(lo);
    b.add(hi);
    box_ = b;
    axis_ = D;
    drawing_ = true;
    view_ = p.view;
    env.doc.setPrimSelection({});
    env.doc.setLightSelection({});
}

void PrimTool::drag(ToolEnv& env, const Pointer& p) {
    if (objects_.active()) { objects_.drag(env, p); return; }
    if (!drawing_ || p.view != view_ || !box_) return;
    const int U = view_->axU(), V = view_->axV();
    const double u = env.grid.snapValue(p.u), v = env.grid.snapValue(p.v);
    box_->min[U] = std::min(au_, u);
    box_->max[U] = std::max(au_, u);
    box_->min[V] = std::min(av_, v);
    box_->max[V] = std::max(av_, v);
}

void PrimTool::release(ToolEnv& env, const Pointer&) {
    if (objects_.active()) { objects_.release(env); return; }
    if (!drawing_) return;
    drawing_ = false;
    const int U = view_->axU(), V = view_->axV();
    if (box_ && (box_->size()[U] < 1e-9 || box_->size()[V] < 1e-9)) box_.reset(); // a click, not a box
}

bool PrimTool::key(ToolEnv& env, int key) {
    if (key == KEY_ENTER || key == KEY_KP_ENTER) {
        if (!create(env.doc, env.currentTexture ? *env.currentTexture : "", &env.lastBox))
            message_ = "drag a box in a 2D view first";
        return true;
    }
    if (key == KEY_ESCAPE) {
        if (objects_.active()) objects_.cancel();
        else if (box_) box_.reset();
        else env.doc.setPrimSelection({});
        drawing_ = false;
        return true;
    }
    if ((key == KEY_DELETE || key == KEY_BACKSPACE) && !objects_.active() &&
        (!env.doc.primSelection().empty() || !env.doc.lightSelection().empty())) {
        Command c = env.doc.makeDelete({}, "Delete");
        env.doc.addLightDeletes(c, env.doc.lightSelection());
        env.doc.addPrimDeletes(c, env.doc.primSelection());
        env.doc.apply(std::move(c));
        return true;
    }
    return false;
}

void PrimTool::decorate(const Document& doc, Scene& scene) const {
    objects_.decorate(doc, scene);
    if (auto p = pending()) scene.pendingPrims.push_back(*p);
}

const char* PrimTool::hint() const {
    if (const char* h = objects_.hint()) return h;
    if (!message_.empty()) return message_.c_str();
    if (box_) return "Return: create it   drag again: redraw   Esc: cancel   (kind and settings in Properties)";
    return "drag a box in a 2D view for a new primitive (kind in Properties)   click a primitive: select   rings: turn   handles: size";
}

void PrimTool::cancel() {
    objects_.cancel();
    box_.reset();
    drawing_ = false;
}

} // namespace scal
