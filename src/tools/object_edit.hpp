#pragma once
// Selecting, moving, turning and sizing objects (lights, cameras, math primitives and props),
// shared by the select tool and the object tools (light, camera, primitive, props). Brush
// tools never touch them.
//  - cameras (milestone 11) are picked by their marker like lights, moved the same way and
//    turned with the three rings about their own position, plus a yellow roll ring around
//    their view direction.
//  - click a light's marker or a primitive (any view) to select it; brushes and the
//    other kind are deselected. A primitive in a combination selects the whole
//    combination; Option+click selects just the piece under the cursor (a piece already
//    selected on its own stays that way, so it can be dragged, turned and sized alone).
//    Cmd+click adds or removes. A prop is picked on its triangles in 3D, inside its turned
//    box in a 2D view.
//  - drag in a 2D view to move what was clicked (snapped to the grid).
//  - rotation rings: around each selected spot/sun light, and one set around the
//    selected primitives and one around the selected props. Three in 3D (red = X, green = Y,
//    blue = Z), one per 2D view (the axis you look along). Drag a ring to turn in 15 degree
//    steps.
//  - size handles on a single selected primitive or prop: one on each face of its own box
//    (only the ones you can see side-on in 2D). Drag to scale EVENLY about the centre (the
//    default, v0.10.0); Shift+drag changes one axis with the opposite face staying put.
//    Changes snap to the grid (a prop's size in units, so its scale changes by that).
// Every change is one undoable command on release; Esc during a drag cancels it.
#include "tools/decal_edit.hpp"
#include "tools/tool.hpp"
#include <vector>

namespace scal {

// Light whose marker is under `local` (nearest to the camera in 3D), or 0.
LightId pickLight(const Document& doc, const Viewport& vp, Vector2 local);
// Camera whose marker is under `local` (nearest to the camera in 3D), or 0.
CameraId pickCamera(const Document& doc, const Viewport& vp, Vector2 local);
// Primitive under `local`: what a click selects (first member of a combination), or 0.
// `dist` gets the distance along the view ray in 3D; `piece` the piece itself that was
// hit. With `outlinesFirst`, an outline near the cursor wins (so a piece hidden inside
// its combination can still be picked in 2D).
MathPrimId pickPrim(const Document& doc, const Viewport& vp, Vector2 local, double* dist = nullptr,
                    MathPrimId* piece = nullptr, bool outlinesFirst = false);

// Prop under `local` (nearest to the camera in 3D, the smallest box in 2D), or 0. `dist`
// gets the distance along the view ray in 3D.
PropId pickProp(const Document& doc, const Viewport& vp, Vector2 local, const ModelFinder* models,
                double* dist = nullptr);

// Rings and size handles of the current selection, as drawn and picked.
struct ObjectGizmo {
    // light 0 and !props = the primitives
    // camera != 0: a camera's rings (and its roll ring, `radius` * Scene::kRollRingScale)
    struct Rings { Vec3 center; double radius; LightId light = 0; bool props = false; CameraId camera = 0; Vec3 rollAxis; };
    std::vector<Rings> rings;
    struct Handle { Vec3 pos; int code; bool prop; }; // size handle: axis*2 + (0 = -, 1 = +)
    std::vector<Handle> handles;
    MathPrimId handlePrim = 0;
    PropId handleProp = 0;
};
ObjectGizmo objectGizmo(const Document& doc, const Viewport& vp, const ModelFinder* models = nullptr);

class ObjectEdit {
public:
    // True if the press hit an object, ring or handle (then it is handled here).
    bool press(ToolEnv& env, const Pointer& p);
    void drag(ToolEnv& env, const Pointer& p);
    void release(ToolEnv& env);
    bool active() const { return mode_ != Mode::None || decals_.active(); }
    void cancel();
    // Objects being dragged replace the document's in the scene; rings/handles shown.
    void decorate(const Document& doc, Scene& scene) const;
    const char* hint() const;

private:
    void dragPropSize(ToolEnv& env, const Pointer& p);
    DecalEdit decals_; // decals (v0.12.5)
    enum class Mode { None, Move, Ring, Size } mode_ = Mode::None;
    const Viewport* view_ = nullptr;
    double u0_ = 0, v0_ = 0;
    Vector2 local0_{};
    Vector2 center_{};      // ring: centre on screen
    Vec3 pivot_;            // ring: world centre
    double angle0_ = 0;     // ring: mouse angle at the press
    double sign_ = 1;       // ring: +1 if the axis points at the viewer
    int axis_ = -1;
    double turned_ = 0;
    Vec3 moved_;
    Vec3 anchor_;           // move: the clicked object's position
    int handle_ = -1;       // size: axis*2 + side
    double amount_ = 0;     // size: change so far
    bool lightsMode_ = false, propsMode_ = false, sizeProp_ = false, camerasMode_ = false;
    bool perAxis_ = false;  // size: Shift held at the press (or now): one axis, not evenly
    const ModelFinder* models_ = nullptr;
    std::vector<Light> lightStart_, lightNow_;
    std::vector<MathPrim> primStart_, primNow_;
    std::vector<Prop> propStart_, propNow_;
    std::vector<Camera> cameraStart_, cameraNow_;
    mutable char hintBuf_[140] = "";
};

} // namespace scal
