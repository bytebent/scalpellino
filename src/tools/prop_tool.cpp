#include "tools/prop_tool.hpp"
#include "render/model_library.hpp"
#include <algorithm>
#include <cmath>

namespace scal {

std::optional<Prop> PropTool::pending() const {
    if (!pos_ || model.empty()) return std::nullopt;
    Prop p;
    p.model = model;
    p.pos = *pos_;
    p.rot = rot;
    p.scale = {scale, scale, scale};
    return p;
}

bool PropTool::create(Document& doc) {
    auto p = pending();
    if (!p) {
        message_ = model.empty() ? "pick a model in the Props panel first" : "click in a view to put it somewhere first";
        return false;
    }
    if (!(scale > 0)) {
        message_ = "the scale must be above zero";
        return true;
    }
    doc.apply(doc.makeAddProps({*p}, "Place prop"));
    pos_.reset();
    message_.clear();
    return true;
}

// Where a click puts the prop: on the surface hit in 3D, on the grid in a 2D view.
bool PropTool::placeAt(ToolEnv& env, const Pointer& p) {
    const Viewport& vp = *p.view;
    if (vp.is2D()) {
        const int U = vp.axU(), V = vp.axV(), D = vp.axD();
        Vec3 at = pos_ ? *pos_ : Vec3{};
        if (!pos_ || depthAxis_ != D) {
            // The depth: the top of the last box in the top view (the prop stands on it),
            // the middle of it in the others; 0 when there is none.
            const AABB& from = env.lastBox;
            double d = 0;
            if (from.valid()) d = D == 2 ? from.max[D] : from.center()[D];
            at[D] = d;
        }
        at[U] = env.grid.snapValue(p.u);
        at[V] = env.grid.snapValue(p.v);
        pos_ = at;
        depthAxis_ = D;
        return true;
    }
    Vec3 o, d;
    vp.pickRay(p.local, o, d);
    double best = 1e300;
    for (const Brush& b : env.doc.brushes()) {
        if (!env.doc.visible(b)) continue;
        const double t = rayHit(b, o, d);
        if (t >= 0) best = std::min(best, t);
    }
    if (env.doc.primsShown())
        if (auto h = rayHitPrims(env.doc.prims(), o, d)) best = std::min(best, h->t);
    ModelFinder none;
    if (env.doc.propsShown())
        if (auto h = rayHitProps(env.doc.props(), env.models ? *env.models : none, o, d)) best = std::min(best, h->t);
    if (best >= 1e300 && d.z < -1e-9 && o.z > 0) best = -o.z / d.z; // the ground at z = 0
    if (best >= 1e300) {
        message_ = "nothing there to put it on: click a surface, or use a 2D view";
        return false;
    }
    pos_ = cleanVec(o + d * best);
    return true;
}

void PropTool::press(ToolEnv& env, const Pointer& p) {
    message_.clear();
    if (objects_.press(env, p)) return;
    if (model.empty()) {
        message_ = "pick a model in the Props panel first";
        return;
    }
    if (placeAt(env, p)) {
        placing_ = true;
        view_ = p.view;
        env.doc.setPropSelection({}); env.doc.setDecalSelection({});
        env.doc.setPrimSelection({});
        env.doc.setLightSelection({});
        env.doc.setCameraSelection({});
        env.doc.setSelection({});
    }
}

void PropTool::drag(ToolEnv& env, const Pointer& p) {
    if (objects_.active()) { objects_.drag(env, p); return; }
    if (!placing_ || p.view != view_) return;
    placeAt(env, p);
}

void PropTool::release(ToolEnv& env, const Pointer&) {
    if (objects_.active()) { objects_.release(env); return; }
    placing_ = false;
}

bool PropTool::key(ToolEnv& env, int key) {
    if (key == KEY_ENTER || key == KEY_KP_ENTER) {
        create(env.doc);
        return true;
    }
    if (key == KEY_ESCAPE) {
        if (objects_.active()) objects_.cancel();
        else if (pos_) pos_.reset();
        else { env.doc.setPropSelection({}); env.doc.setDecalSelection({}); }
        placing_ = false;
        message_.clear();
        return true;
    }
    if ((key == KEY_DELETE || key == KEY_BACKSPACE) && !objects_.active() &&
        (!env.doc.propSelection().empty() || !env.doc.decalSelection().empty() || !env.doc.primSelection().empty() || !env.doc.lightSelection().empty() ||
         !env.doc.cameraSelection().empty())) {
        Command c = env.doc.makeDelete({}, "Delete");
        env.doc.addLightDeletes(c, env.doc.lightSelection());
        env.doc.addPrimDeletes(c, env.doc.primSelection());
        env.doc.addPropDeletes(c, env.doc.propSelection());
        env.doc.addDecalDeletes(c, env.doc.decalSelection());
        env.doc.addCameraDeletes(c, env.doc.cameraSelection());
        env.doc.apply(std::move(c));
        return true;
    }
    return false;
}

void PropTool::decorate(const Document& doc, Scene& scene) const {
    objects_.decorate(doc, scene);
    if (auto p = pending()) {
        Scene::SceneProp sp;
        sp.prop = *p;
        const Model* m = scene.models ? scene.models->find(p->model) : nullptr;
        sp.local = propLocalBounds(m);
        sp.missing = m == nullptr;
        scene.props.push_back(sp);        // the model itself, where it will go
        scene.pendingProps.push_back(sp); // and its yellow box
    }
}

const char* PropTool::hint() const {
    if (const char* h = objects_.hint()) return h;
    if (!message_.empty()) return message_.c_str();
    if (pos_ && !model.empty()) return "Return: place it   click / drag: move it   Esc: cancel   (turn and size in Properties)";
    if (model.empty()) return "pick a model in the Props panel, then click a surface in 3D or a spot in a 2D view   click a prop: select";
    return "click a surface in 3D or a spot in a 2D view to put the prop there   click a prop: select   rings: turn   handles: scale";
}

void PropTool::cancel() {
    objects_.cancel();
    pos_.reset();
    placing_ = false;
}

} // namespace scal
