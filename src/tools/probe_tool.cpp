#include "tools/probe_tool.hpp"
#include "tools/decal_edit.hpp" // hitSurface

namespace scal {

void ProbeTool::press(ToolEnv& env, const Pointer& p) {
    Document& doc = env.doc;
    message_.clear();
    if (objects_.press(env, p)) return;
    Vec3 at = pending ? *pending : env.lastBox.center();
    if (p.view->is2D()) {
        at[p.view->axU()] = env.grid.snapValue(p.u);
        at[p.view->axV()] = env.grid.snapValue(p.v);
    } else {
        Vec3 o, d;
        p.view->pickRay(p.local, o, d);
        const auto hit = hitSurface(doc, o, normalize(d));
        if (!hit) {
            message_ = "click a surface in the 3D view (or a spot in a 2D view)";
            return;
        }
        const Vec3 q = probePlacement(hit->p, hit->n);
        // snapped to the grid along the surface; the height above a floor / distance from a wall exact
        const Vec3 n = normalize(hit->n);
        const int keep = n.z > 0.7 ? 2 : (std::fabs(n.x) >= std::fabs(n.y) && std::fabs(n.x) >= std::fabs(n.z) ? 0 : std::fabs(n.y) >= std::fabs(n.z) ? 1 : 2);
        for (int a = 0; a < 3; ++a) at[a] = a == keep ? q[a] : env.grid.snapValue(q[a]);
    }
    pending = at;
    doc.setSelection({});
    doc.setCameraSelection({});
    doc.setProbeSelection({});
    doc.setLightSelection({});
    doc.setPrimSelection({});
    doc.setPropSelection({});
    doc.setDecalSelection({});
}

void ProbeTool::drag(ToolEnv& env, const Pointer& p) {
    if (objects_.active()) objects_.drag(env, p);
}

void ProbeTool::release(ToolEnv& env, const Pointer&) {
    if (objects_.active()) objects_.release(env);
}

bool ProbeTool::create(Document& doc) {
    if (!pending) return false;
    Probe r = templ;
    r.id = 0;
    r.pos = *pending;
    r.otherKeys.clear();
    doc.apply(doc.makeAddProbes({r}, "Create reflection probe"));
    pending.reset();
    return true;
}

bool ProbeTool::key(ToolEnv& env, int key) {
    if (key == KEY_ENTER || key == KEY_KP_ENTER) {
        if (create(env.doc)) return true;
        message_ = "click in a view first to place the reflection probe";
        return true;
    }
    if (key == KEY_ESCAPE) {
        if (objects_.active()) objects_.cancel();
        else if (pending) pending.reset();
        else env.doc.setProbeSelection({});
        message_.clear();
        return true;
    }
    if ((key == KEY_DELETE || key == KEY_BACKSPACE) && !env.doc.probeSelection().empty() && !objects_.active()) {
        Command c = env.doc.makeDelete({}, "Delete reflection probe");
        env.doc.addProbeDeletes(c, env.doc.probeSelection());
        env.doc.apply(std::move(c));
        return true;
    }
    return false;
}

void ProbeTool::decorate(const Document& doc, Scene& scene) const {
    objects_.decorate(doc, scene);
    if (pending) {
        Probe r = templ;
        r.pos = *pending;
        scene.pendingProbe = r;
    }
}

const char* ProbeTool::hint() const {
    if (const char* h = objects_.hint()) return h;
    if (!message_.empty()) return message_.c_str();
    if (pending) return "Return: create the reflection probe here   click: put it somewhere else   Esc: cancel   (size in Properties)";
    return "click a floor or wall in 3D (170 cm above a floor, 81 cm from a wall) or a spot in a 2D view   click a probe: select";
}

void ProbeTool::cancel() {
    objects_.cancel();
    pending.reset();
}

} // namespace scal
