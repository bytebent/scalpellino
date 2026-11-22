#include "tools/decal_tool.hpp"
#include "tools/decal_edit.hpp"
#include <algorithm>
#include <cmath>

namespace scal {

bool DecalTool::placeOn(ToolEnv& env, const Vec3& origin, const Vec3& dir) {
    const std::string material = env.currentTexture ? *env.currentTexture : "";
    if (material.empty()) {
        message_ = "pick a picture in the Textures panel first";
        return false;
    }
    const auto hit = hitSurface(env.doc, origin, normalize(dir));
    if (!hit) {
        message_ = "nothing there to put it on: click a brush or terrain in the 3D view";
        return false;
    }
    Decal d = pending_ ? *pending_ : Decal{};
    const bool sameMaterial = pending_ && pending_->material == material;
    d.material = material;
    if (!sameMaterial) {
        int w = 64, h = 64;
        double scale = 1;
        if (env.decalPicture && *env.decalPicture && (*env.decalPicture)(material, w, h, scale)) decalSizeFromPicture(d, w, h, scale);
        else decalSizeFromPicture(d, w, h, 1);
        d.reach = kDefaultDecalReach;
        d.order = 0;
    }
    const double rot = pending_ ? decalRotation(*pending_) : rotation;
    d.normal = cleanVec(normalize(hit->n));
    d.up = decalDefaultUp(d.normal);
    setDecalRotation(d, rot);
    Vec3 c = hit->p;
    for (int k = 0; k < 3; ++k)
        if (std::fabs(d.normal[k]) < 1e-6) c[k] = env.grid.snapValue(c[k]);
    d.pos = cleanVec(c);
    pending_ = d;
    brush_ = hit->brush;
    message_.clear();
    return true;
}

bool DecalTool::create(Document& doc) {
    if (!pending_) {
        message_ = "click a surface in the 3D view to put the decal there first";
        return false;
    }
    if (!(pending_->width > 0) || !(pending_->height > 0)) {
        message_ = "the decal's width and height must be above zero";
        return true;
    }
    if (!doc.find(brush_)) {
        message_ = "the surface it was on is gone: click a surface again";
        pending_.reset();
        return true;
    }
    rotation = decalRotation(*pending_);
    doc.apply(doc.makeAddDecal(*pending_, brush_, "Place decal"));
    pending_.reset();
    message_.clear();
    return true;
}

void DecalTool::press(ToolEnv& env, const Pointer& p) {
    message_.clear();
    if (!env.alt && objects_.press(env, p)) return; // a decal or object: select / edit it
    if (p.view->is2D()) {
        message_ = "decals are put on surfaces in the 3D view (2D views: click one to select it)";
        return;
    }
    Vec3 o, d;
    p.view->pickRay(p.local, o, d);
    if (placeOn(env, o, d)) {
        placing_ = true;
        view_ = p.view;
        env.doc.setDecalSelection({});
        env.doc.setPropSelection({});
        env.doc.setPrimSelection({});
        env.doc.setLightSelection({});
        env.doc.setCameraSelection({}); env.doc.setProbeSelection({});
        env.doc.setSelection({});
    }
}

void DecalTool::drag(ToolEnv& env, const Pointer& p) {
    if (objects_.active()) { objects_.drag(env, p); return; }
    if (!placing_ || p.view != view_) return;
    Vec3 o, d;
    p.view->pickRay(p.local, o, d);
    placeOn(env, o, d);
}

void DecalTool::release(ToolEnv& env, const Pointer&) {
    if (objects_.active()) { objects_.release(env); return; }
    placing_ = false;
}

bool DecalTool::key(ToolEnv& env, int key) {
    if (key == KEY_ENTER || key == KEY_KP_ENTER) {
        create(env.doc);
        return true;
    }
    if (key == KEY_ESCAPE) {
        if (objects_.active()) objects_.cancel();
        else if (pending_) pending_.reset();
        else env.doc.setDecalSelection({});
        placing_ = false;
        message_.clear();
        return true;
    }
    if ((key == KEY_DELETE || key == KEY_BACKSPACE) && !objects_.active() &&
        (!env.doc.decalSelection().empty() || !env.doc.propSelection().empty() || !env.doc.primSelection().empty() ||
         !env.doc.lightSelection().empty() || !env.doc.cameraSelection().empty())) {
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

void DecalTool::decorate(const Document& doc, Scene& scene) const {
    objects_.decorate(doc, scene);
    if (pending_) {
        Scene::SceneDecal sd;
        sd.decal = *pending_;
        sd.pending = true;
        sd.tris = std::make_shared<std::vector<DecalTri>>(decalMesh(*pending_, doc.brushes(), [&](const Brush& b) { return doc.visible(b); }));
        scene.decals.push_back(std::move(sd));
    }
}

const char* DecalTool::hint() const {
    if (const char* h = objects_.hint()) return h;
    if (!message_.empty()) return message_.c_str();
    if (pending_) return "Return: place the decal   click / drag: move it   Esc: cancel   (size, turn and order in Properties)";
    return "click a surface in the 3D view to put the current texture there as a decal   click a decal: select   Option+click: a new one over it";
}

void DecalTool::cancel() {
    objects_.cancel();
    pending_.reset();
    placing_ = false;
}

} // namespace scal
