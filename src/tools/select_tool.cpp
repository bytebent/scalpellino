#include "tools/select_tool.hpp"
#include <cmath>
#include <cstdio>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kRotStep = kPi / 12; // 15 degrees
}

namespace scal {

void SelectTool::press(ToolEnv& env, const Pointer& p) {
    Document& doc = env.doc;
    if (lights_.press(env, p)) return;
    if (!env.cmd) { doc.setLightSelection({}); doc.setPrimSelection({}); } // a plain click elsewhere drops objects
    if (!p.view->is2D()) {
        const BrushId id = pick3D(doc, *p.view, p.local);
        std::set<BrushId> sel = doc.selection();
        const std::set<BrushId> hit = id ? doc.expandToGroups({id}) : std::set<BrushId>{};
        if (env.cmd) {
            if (id && sel.count(id)) for (BrushId h : hit) sel.erase(h);
            else sel.insert(hit.begin(), hit.end());
        } else sel = hit;
        doc.setSelection(sel);
        return;
    }

    // 1. Handles / edges of the current selection box -> resize.
    const AABB selBox = doc.selectionBounds();
    if (!doc.selection().empty() && !env.cmd) {
        const BoxGrab g = grabBox(*p.view, selBox, p.local);
        if (env.alt && g.su != 0 && g.sv != 0) {
            // Option + corner handle: rotate in this view's plane around the box centre.
            mode_ = Mode::Rotating;
            const int U = p.view->axU(), V = p.view->axV();
            Vec3 eu, ev;
            eu[U] = 1;
            ev[V] = 1;
            rotAxis_ = cross(eu, ev); // counter-clockwise on screen = positive angle
            rotCenter_ = env.pivot ? *env.pivot : selBox.center();
            rotStart_ = std::atan2(p.v - rotCenter_[V], p.u - rotCenter_[U]);
            rotAngle_ = 0;
        } else if (g.resize()) {
            mode_ = Mode::Resizing;
            grab_ = g;
        }
    }

    // 2. Otherwise pick a brush.
    if (mode_ == Mode::Idle) {
        const BrushId id = pick2D(env.sceneBrushes, *p.view, p.local);
        std::set<BrushId> sel = doc.selection();
        const std::set<BrushId> hit = id ? doc.expandToGroups({id}) : std::set<BrushId>{};
        if (env.cmd) {
            if (id && sel.count(id)) for (BrushId h : hit) sel.erase(h);
            else sel.insert(hit.begin(), hit.end());
            doc.setSelection(sel);
            return;
        }
        const bool insideSel = !sel.empty() && grabBox(*p.view, selBox, p.local).move;
        pressedId_ = (id && sel.count(id) && sel.size() > hit.size()) ? id : 0;
        if (id && !sel.count(id)) {
            doc.setSelection(hit);                 // new pick replaces the selection (whole group)
        } else if (!id && !insideSel) {
            doc.setSelection({});                  // click on empty space deselects
            return;
        }
        if (doc.selection().empty()) return;
        mode_ = Mode::Moving;
        grab_ = BoxGrab{};
        grab_.move = true;
    }

    view_ = p.view;
    u0_ = p.u;
    v0_ = p.v;
    textureLock_ = env.textureLock;
    startBox_ = curBox_ = doc.selectionBounds();
    clone_ = (mode_ == Mode::Moving) && env.shift;
    ghosts_.clear();
}

void SelectTool::updateGhosts(const Document& doc) {
    ghosts_.clear();
    for (BrushId id : doc.selection()) {
        const Brush* b = doc.find(id);
        if (!b) continue;
        Brush g = *b;
        if (mode_ == Mode::Moving) g.translate(curBox_.min - startBox_.min, textureLock_);
        else if (mode_ == Mode::Rotating) g.rotate(rotCenter_, rotAxis_, rotAngle_, textureLock_);
        else g.remapBounds(startBox_, curBox_, textureLock_);
        ghosts_.push_back(std::move(g));
    }
    if (mode_ == Mode::Rotating) {
        AABB b;
        for (const Brush& g : ghosts_) b.add(g.bounds());
        curBox_ = b;
    }
}

void SelectTool::drag(ToolEnv& env, const Pointer& p) {
    if (lights_.active()) { lights_.drag(env, p); return; }
    if (mode_ == Mode::Idle || p.view != view_) return;
    if (mode_ == Mode::Rotating) {
        const int U = view_->axU(), V = view_->axV();
        double a = std::atan2(p.v - rotCenter_[V], p.u - rotCenter_[U]) - rotStart_;
        while (a > kPi) a -= 2 * kPi;
        while (a < -kPi) a += 2 * kPi;
        if (!env.shift) a = env.slopeAngles ? snapToSlope(a) : std::round(a / kRotStep) * kRotStep;
        if (a != rotAngle_) {
            rotAngle_ = a;
            updateGhosts(env.doc);
        }
        return;
    }
    const AABB nb = dragBox(*view_, startBox_, grab_, u0_, v0_, p.u, p.v, env.grid);
    if (nb.min != curBox_.min || nb.max != curBox_.max) {
        curBox_ = nb;
        updateGhosts(env.doc);
    }
}

void SelectTool::release(ToolEnv& env, const Pointer&) {
    if (lights_.active()) { lights_.release(env); return; }
    if (mode_ == Mode::Idle) return;
    const bool changed = mode_ == Mode::Rotating ? rotAngle_ != 0
                       : (curBox_.min != startBox_.min || curBox_.max != startBox_.max);
    if (!changed && pressedId_ && mode_ == Mode::Moving) env.doc.setSelection(env.doc.expandToGroups({pressedId_}));
    if (changed && !ghosts_.empty()) {
        Document& doc = env.doc;
        if (clone_) {
            for (Brush& g : ghosts_) g.id = 0; // new ids
            doc.apply(doc.makeAdd(ghosts_, "Clone"));
        } else {
            bool ok = true;
            for (const Brush& g : ghosts_) ok = ok && g.valid();
            const char* what = mode_ == Mode::Moving ? "Move" : mode_ == Mode::Rotating ? "Rotate" : "Resize";
            if (ok) doc.apply(doc.makeModify(ghosts_, what));
        }
    }
    cancel();
}

bool SelectTool::key(ToolEnv& env, int key) {
    Document& doc = env.doc;
    if (key == KEY_ESCAPE) {
        if (mode_ != Mode::Idle || lights_.active()) cancel();
        else { doc.setSelection({}); doc.setLightSelection({}); doc.setPrimSelection({}); }
        return true;
    }
    if ((key == KEY_DELETE || key == KEY_BACKSPACE) &&
        (!doc.selection().empty() || !doc.lightSelection().empty() || !doc.primSelection().empty()) &&
        mode_ == Mode::Idle && !lights_.active()) {
        Command c = doc.makeDelete(doc.selection(), "Delete");
        doc.addLightDeletes(c, doc.lightSelection());
        doc.addPrimDeletes(c, doc.primSelection());
        doc.apply(std::move(c));
        return true;
    }
    return false;
}

void SelectTool::decorate(const Document& doc, Scene& scene) const {
    lights_.decorate(doc, scene);
    if (mode_ != Mode::Idle && !ghosts_.empty()) {
        for (const Brush& g : ghosts_) scene.ghosts.push_back(makeSceneBrush(g));
        scene.hideSelected = !clone_;
        scene.handleBox = curBox_;
    } else if (!doc.selection().empty()) {
        scene.handleBox = doc.selectionBounds();
    }
}

const char* SelectTool::hint() const {
    if (const char* h = lights_.hint()) return h;
    switch (mode_) {
        case Mode::Moving:   return clone_ ? "cloning - release to place" : "moving - release to place, Esc cancels";
        case Mode::Resizing: return "resizing - release to apply, Esc cancels";
        case Mode::Rotating:
            std::snprintf(hintBuf_, sizeof hintBuf_, "rotating %.0f deg - Shift: free, Esc cancels", rotAngle_ * 180 / kPi);
            return hintBuf_;
        default:             return "click: select   Cmd+click: add/remove   drag: move   Shift+drag: clone   Option+drag corner: rotate   Del: delete";
    }
}

void SelectTool::cancel() {
    lights_.cancel();
    mode_ = Mode::Idle;
    view_ = nullptr;
    ghosts_.clear();
    clone_ = false;
    pressedId_ = 0;
}

} // namespace scal
