#include "tools/block_tool.hpp"
#include <algorithm>

namespace scal {

void BlockTool::press(ToolEnv& env, const Pointer& p) {
    if (!p.view->is2D()) return;
    view_ = p.view;
    if (box_) {
        const BoxGrab g = grabBox(*p.view, *box_, p.local);
        if (!g.none()) {
            mode_ = Mode::Editing;
            grab_ = g;
            u0_ = p.u;
            v0_ = p.v;
            startBox_ = *box_;
            return;
        }
    }
    // Start a new rectangle (replaces any uncommitted preview).
    mode_ = Mode::Drawing;
    axU_ = p.view->axU(); axV_ = p.view->axV(); axD_ = p.view->axD();
    box_.reset();
    au_ = env.grid.snapValue(p.u);
    av_ = env.grid.snapValue(p.v);
}

void BlockTool::drag(ToolEnv& env, const Pointer& p) {
    if (p.view != view_) return;
    if (mode_ == Mode::Drawing) {
        const double cu = env.grid.snapValue(p.u), cv = env.grid.snapValue(p.v);
        if (cu == au_ || cv == av_) { box_.reset(); return; }
        const int U = view_->axU(), V = view_->axV(), D = view_->axD();
        AABB b;
        b.min[U] = std::min(au_, cu); b.max[U] = std::max(au_, cu);
        b.min[V] = std::min(av_, cv); b.max[V] = std::max(av_, cv);
        // Depth from the last brush, like Hammer.
        b.min[D] = env.lastBox.min[D];
        b.max[D] = env.lastBox.max[D];
        if (b.max[D] <= b.min[D]) { b.min[D] = 0; b.max[D] = 64; }
        box_ = b;
    } else if (mode_ == Mode::Editing && box_) {
        box_ = dragBox(*view_, startBox_, grab_, u0_, v0_, p.u, p.v, env.grid);
    }
}

void BlockTool::release(ToolEnv&, const Pointer&) {
    mode_ = Mode::Idle;
    view_ = nullptr;
}

bool BlockTool::key(ToolEnv& env, int key) {
    if (key == KEY_ESCAPE) {
        if (!box_ && mode_ == Mode::Idle) return false;
        cancel();
        return true;
    }
    if ((key == KEY_ENTER || key == KEY_KP_ENTER) && box_ && mode_ == Mode::Idle) {
        Document& doc = env.doc;
        std::optional<Brush> b = previewBrush();
        if (b && b->valid()) {
            doc.apply(doc.makeAdd({*b}, shape == Primitive::Box ? "Create block" : std::string("Create ") + primitiveName(shape)));
            env.lastBox = *box_;
        }
        box_.reset();
        return true;
    }
    return false;
}

std::optional<Brush> BlockTool::previewBrush() const {
    if (!box_) return std::nullopt;
    // Wedge: its triangle faces the view it was drawn in (slopes along the view's
    // horizontal axis, rises along its vertical axis). Round shapes stand along the
    // view's depth axis (drawn in the top view = upright).
    const int axis = shape == Primitive::Wedge ? axV_ : axD_;
    return makePrimitive(shape, *box_, axis, axU_, sides, roundToWhole, texture, textureScale);
}

void BlockTool::decorate(const Document&, Scene& scene) const {
    if (box_) {
        scene.previewBox = box_;
        scene.handleBox = box_;
        if (shape != Primitive::Box)
            if (auto b = previewBrush()) scene.ghosts.push_back(makeSceneBrush(*b));
    }
}

const char* BlockTool::hint() const {
    if (mode_ == Mode::Drawing) return "drag to size the block";
    if (box_) return "Return: create   Esc: cancel   drag handles/inside to adjust (any 2D view)";
    return "drag in a 2D view to draw a block";
}

void BlockTool::cancel() {
    mode_ = Mode::Idle;
    view_ = nullptr;
    box_.reset();
}

} // namespace scal
