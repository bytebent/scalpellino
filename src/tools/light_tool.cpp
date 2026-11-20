#include "tools/light_tool.hpp"
#include "core/terrain.hpp"

namespace scal {

void LightTool::press(ToolEnv& env, const Pointer& p) {
    message_.clear();
    if (lights_.press(env, p)) return;
    Document& doc = env.doc;
    Vec3 at = pending ? *pending : env.lastBox.center();
    if (p.view->is2D()) {
        at[p.view->axU()] = env.grid.snapValue(p.u);
        at[p.view->axV()] = env.grid.snapValue(p.v);
    } else {
        // On the surface under the cursor, 32 units back toward the camera.
        Vec3 o, d;
        p.view->pickRay(p.local, o, d);
        double best = -1;
        for (const Brush& b : doc.brushes()) {
            if (!doc.visible(b)) continue;
            for (double t : {rayHit(b, o, d), rayHitTerrain(b, o, d)})
                if (t >= 0 && (best < 0 || t < best)) best = t;
        }
        const double t = best >= 0 ? std::max(0.0, best - 32) : 512;
        const Vec3 h = o + d * t;
        for (int a = 0; a < 3; ++a) at[a] = env.grid.snapValue(h[a]);
    }
    pending = at;
    doc.setLightSelection({});
    doc.setPrimSelection({});
    doc.setPropSelection({});
    doc.setCameraSelection({});
}

void LightTool::drag(ToolEnv& env, const Pointer& p) {
    if (lights_.active()) lights_.drag(env, p);
}

void LightTool::release(ToolEnv& env, const Pointer&) {
    if (lights_.active()) lights_.release(env);
}

bool LightTool::create(Document& doc) {
    if (!pending) return false;
    Light l = templ;
    l.id = 0;
    l.pos = *pending;
    l.otherKeys.clear();
    doc.apply(doc.makeAddLights({l}, std::string("Create ") + lightKindName(l.kind) + " light"));
    pending.reset();
    return true;
}

bool LightTool::key(ToolEnv& env, int key) {
    if (key == KEY_ENTER || key == KEY_KP_ENTER) {
        if (create(env.doc)) return true;
        message_ = "click in a view first to place the light";
        return true;
    }
    if (key == KEY_ESCAPE) {
        if (lights_.active()) lights_.cancel();
        else if (pending) pending.reset();
        else env.doc.setLightSelection({});
        return true;
    }
    if ((key == KEY_DELETE || key == KEY_BACKSPACE) && !env.doc.lightSelection().empty() && !lights_.active()) {
        Command c = env.doc.makeDelete({}, "Delete light");
        env.doc.addLightDeletes(c, env.doc.lightSelection());
        env.doc.apply(std::move(c));
        return true;
    }
    return false;
}

void LightTool::decorate(const Document& doc, Scene& scene) const {
    lights_.decorate(doc, scene);
    if (pending) {
        Light l = templ;
        l.pos = *pending;
        scene.pendingLight = l;
    }
}

const char* LightTool::hint() const {
    if (const char* h = lights_.hint()) return h;
    if (!message_.empty()) return message_.c_str();
    if (pending) return "Return: create the light here   click: put it somewhere else   Esc: cancel   (settings in Properties)";
    return "click in a view to place a light (kind and settings in Properties)   click a light: select   drag a ring: aim";
}

void LightTool::cancel() {
    lights_.cancel();
    pending.reset();
}

} // namespace scal
