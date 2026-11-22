#include "tools/camera_tool.hpp"
#include "core/terrain.hpp"
#include <set>

namespace scal {

namespace {
// The nearest brush surface (terrain included) along a ray, or -1.
double hitBrushes(const Document& doc, const Vec3& o, const Vec3& d) {
    double best = -1;
    for (const Brush& b : doc.brushes()) {
        if (!doc.visible(b)) continue;
        for (double t : {rayHit(b, o, d), rayHitTerrain(b, o, d)})
            if (t >= 0 && (best < 0 || t < best)) best = t;
    }
    return best;
}
} // namespace

std::string freeCameraName(const Document& doc, const std::vector<Camera>& alsoTaken) {
    std::set<std::string> used;
    for (const Camera& c : doc.cameras()) used.insert(c.name);
    for (const Camera& c : alsoTaken) used.insert(c.name);
    for (int n = 1;; ++n) {
        const std::string name = "camera " + std::to_string(n);
        if (!used.count(name)) return name;
    }
}

bool CameraTool::aimSelected(Document& doc, const Vec3& target, int depthAxis) {
    std::vector<Camera> mod;
    for (CameraId id : doc.cameraSelection())
        if (const Camera* c = doc.findCamera(id)) {
            Camera a = *c;
            Vec3 t = target;
            if (depthAxis >= 0) t[depthAxis] = a.pos[depthAxis]; // a 2D click: at the camera's own depth
            if (aimCamera(a, t) && a != *c) mod.push_back(a);
        }
    if (doc.cameraSelection().empty()) return false;
    if (!mod.empty()) doc.apply(doc.makeModifyCameras(mod, "Aim camera"));
    return true;
}

void CameraTool::press(ToolEnv& env, const Pointer& p) {
    Document& doc = env.doc;
    if (aiming_) {
        aiming_ = false;
        bool ok;
        if (p.view->is2D()) {
            Vec3 t;
            t[p.view->axU()] = env.grid.snapValue(p.u);
            t[p.view->axV()] = env.grid.snapValue(p.v);
            ok = aimSelected(doc, t, p.view->axD());
        } else {
            Vec3 o, d;
            p.view->pickRay(p.local, o, d);
            const double t = hitBrushes(doc, o, d);
            ok = aimSelected(doc, o + d * (t >= 0 ? t : 1000.0));
        }
        message_ = ok ? "" : "select a camera first, then aim it";
        return;
    }
    message_.clear();
    if (objects_.press(env, p)) return;
    Vec3 at = pending ? *pending : env.lastBox.center();
    if (p.view->is2D()) {
        at[p.view->axU()] = env.grid.snapValue(p.u);
        at[p.view->axV()] = env.grid.snapValue(p.v);
    } else {
        // On the surface under the cursor, 32 units back toward the view.
        Vec3 o, d;
        p.view->pickRay(p.local, o, d);
        const double best = hitBrushes(doc, o, d);
        const double t = best >= 0 ? std::max(0.0, best - 32) : 512;
        const Vec3 h = o + d * t;
        for (int a = 0; a < 3; ++a) at[a] = env.grid.snapValue(h[a]);
    }
    pending = at;
    doc.setCameraSelection({});
    doc.setLightSelection({});
    doc.setPrimSelection({});
    doc.setPropSelection({}); doc.setDecalSelection({});
}

void CameraTool::drag(ToolEnv& env, const Pointer& p) {
    if (objects_.active()) objects_.drag(env, p);
}

void CameraTool::release(ToolEnv& env, const Pointer&) {
    if (objects_.active()) objects_.release(env);
}

bool CameraTool::create(Document& doc) {
    if (!pending) return false;
    Camera c = templ;
    c.id = 0;
    c.pos = *pending;
    c.otherKeys.clear();
    c.active = false; // the first camera of a map becomes active by itself
    if (c.name.empty() || c.name == Camera{}.name) c.name = freeCameraName(doc);
    clampCamera(c);
    doc.apply(doc.makeAddCameras({c}, "Create camera"));
    pending.reset();
    return true;
}

bool CameraTool::key(ToolEnv& env, int key) {
    if (key == KEY_ENTER || key == KEY_KP_ENTER) {
        if (aiming_) return true;
        if (create(env.doc)) return true;
        message_ = "click in a view first to place the camera";
        return true;
    }
    if (key == KEY_ESCAPE) {
        if (objects_.active()) objects_.cancel();
        else if (aiming_) aiming_ = false;
        else if (pending) pending.reset();
        else env.doc.setCameraSelection({});
        message_.clear();
        return true;
    }
    if ((key == KEY_DELETE || key == KEY_BACKSPACE) && !env.doc.cameraSelection().empty() && !objects_.active()) {
        Command c = env.doc.makeDelete({}, "Delete camera");
        env.doc.addCameraDeletes(c, env.doc.cameraSelection());
        env.doc.apply(std::move(c));
        return true;
    }
    return false;
}

void CameraTool::decorate(const Document& doc, Scene& scene) const {
    objects_.decorate(doc, scene);
    if (pending) {
        Camera c = templ;
        c.pos = *pending;
        c.active = doc.cameras().empty();
        scene.pendingCamera = c;
    }
}

const char* CameraTool::hint() const {
    if (aiming_) return "click the point to aim the selected cameras at (a surface in 3D, a spot in a 2D view)   Esc: stop";
    if (const char* h = objects_.hint()) return h;
    if (!message_.empty()) return message_.c_str();
    if (pending) return "Return: create the camera here   click: put it somewhere else   Esc: cancel   (lens, size... in Properties)";
    return "click in a view to place a camera (settings in Properties)   click a camera: select   drag a ring: turn (yellow: roll)";
}

void CameraTool::cancel() {
    objects_.cancel();
    pending.reset();
    aiming_ = false;
}

} // namespace scal
