#pragma once
// Selecting, moving, turning and sizing objects (lights and math primitives), shared by
// the select tool, the light tool and the primitive tool. Brush tools never touch them.
//  - click a light's marker or a primitive (any view) to select it; brushes and the
//    other kind are deselected. A primitive in a combination selects the whole
//    combination. Cmd+click adds or removes.
//  - drag in a 2D view to move what was clicked (snapped to the grid).
//  - rotation rings: around each selected spot/sun light, and one set around the
//    selected primitives. Three in 3D (red = X, green = Y, blue = Z), one per 2D view
//    (the axis you look along). Drag a ring to turn in 15 degree steps.
//  - size handles on a single selected primitive: one on each face of its own box
//    (only the ones you can see side-on in 2D). Drag to resize with the opposite face
//    staying put; Shift+drag scales evenly about the centre. Sizes snap to the grid.
// Every change is one undoable command on release; Esc during a drag cancels it.
#include "tools/tool.hpp"
#include <vector>

namespace scal {

// Light whose marker is under `local` (nearest to the camera in 3D), or 0.
LightId pickLight(const Document& doc, const Viewport& vp, Vector2 local);
// Primitive under `local`: what a click selects (first member of a combination), or 0.
// `dist` gets the distance along the view ray in 3D.
MathPrimId pickPrim(const Document& doc, const Viewport& vp, Vector2 local, double* dist = nullptr);

// Rings and size handles of the current selection, as drawn and picked.
struct ObjectGizmo {
    struct Rings { Vec3 center; double radius; LightId light = 0; }; // light 0 = the primitives
    std::vector<Rings> rings;
    std::vector<std::pair<Vec3, int>> handles; // size handles: position, axis*2 + (0 = -, 1 = +)
    MathPrimId handlePrim = 0;
};
ObjectGizmo objectGizmo(const Document& doc, const Viewport& vp);

class ObjectEdit {
public:
    // True if the press hit an object, ring or handle (then it is handled here).
    bool press(ToolEnv& env, const Pointer& p);
    void drag(ToolEnv& env, const Pointer& p);
    void release(ToolEnv& env);
    bool active() const { return mode_ != Mode::None; }
    void cancel();
    // Objects being dragged replace the document's in the scene; rings/handles shown.
    void decorate(const Document& doc, Scene& scene) const;
    const char* hint() const;

private:
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
    bool lightsMode_ = false;
    bool even_ = false;     // size: Shift held at the press (or now): scale evenly
    std::vector<Light> lightStart_, lightNow_;
    std::vector<MathPrim> primStart_, primNow_;
    mutable char hintBuf_[140] = "";
};

} // namespace scal
