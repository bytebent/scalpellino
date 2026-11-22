#pragma once
// Decals (v0.12.5) in the select tool and every object tool, like props:
//  - click a decal to select it (3D: on its picture, unless something is in front; 2D: on
//    its outline); Cmd+click adds or removes; brushes and other objects are deselected.
//  - 3D, the one selected decal: drag a corner to size it (the opposite corner stays; Shift
//    keeps its proportions), drag the ring in its plane to turn it (15 degree steps; Shift:
//    free), drag the decal to slide it over the surfaces (snapped to the grid; it moves to
//    the brush it ends up on).
// Every change is one undoable command on release; Esc during a drag cancels it.
#include "tools/tool.hpp"
#include <optional>

namespace scal {

// What a ray from the 3D view hits first among the shown brushes (terrain included): the
// point, the surface's outward normal there and the brush.
struct SurfaceHit {
    double t = 0;
    Vec3 p, n;
    BrushId brush = 0;
};
std::optional<SurfaceHit> hitSurface(const Document& doc, const Vec3& origin, const Vec3& dir);

// The decal under the cursor (shown ones only), or 0; `dist`: along the view ray (3D).
DecalId pickDecal(const Document& doc, const Viewport& vp, Vector2 local, double* dist = nullptr);

// The selected decal's handles as drawn (one decal selected; 3D only).
Scene::DecalGizmo decalGizmo(const Decal& d);

class DecalEdit {
public:
    // A handle of the one selected decal (3D)? Then it is handled here.
    bool pressHandles(ToolEnv& env, const Pointer& p);
    // A click on decal `id` (from pickDecal): select it and, in 3D, start sliding it.
    void pressDecal(ToolEnv& env, const Pointer& p, DecalId id);
    void drag(ToolEnv& env, const Pointer& p);
    void release(ToolEnv& env);
    bool active() const { return mode_ != Mode::None; }
    void cancel() { mode_ = Mode::None; }
    // The decal being changed in place of the document's; its handles.
    void decorate(const Document& doc, Scene& scene) const;
    const char* hint() const;

private:
    enum class Mode { None, Move, Corner, Ring } mode_ = Mode::None;
    const Viewport* view_ = nullptr;
    Decal start_, now_;
    BrushId target_ = 0;      // the brush it ends up on (Move)
    int corner_ = -1;
    double grabX_ = 0, grabY_ = 0; // Move: the grabbed point in the decal's frame
    double angle0_ = 0, rot0_ = 0, sign_ = 1;
    Vector2 center_{};
    bool changed_ = false;
};

} // namespace scal
