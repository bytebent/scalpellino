#pragma once
// Selecting, moving and aiming lights, shared by the select tool and the light tool.
//  - click a light's marker (any view) to select it (brushes are deselected);
//    Cmd+click adds or removes it.
//  - drag a marker in a 2D view to move the selected lights, snapped to the grid.
//  - a selected spot or sun light has rotation rings: three in the 3D view (red = X,
//    green = Y, blue = Z), one in each 2D view (around the axis you look along). Drag a
//    ring to turn the light around that axis, in 15 degree steps.
// Every change is one undoable command on release; Esc during a drag cancels it.
#include "tools/tool.hpp"
#include <vector>

namespace scal {

// Light whose marker is under `local` (nearest to the camera in 3D), or 0.
LightId pickLight(const Document& doc, const Viewport& vp, Vector2 local);
// Ring of a selected directional light under `local`: sets `light` and returns the
// axis (0..2), or -1.
int pickRing(const Document& doc, const Viewport& vp, Vector2 local, LightId& light);

class LightEdit {
public:
    // True if the press hit a light or a ring (then it is handled here).
    bool press(ToolEnv& env, const Pointer& p);
    void drag(ToolEnv& env, const Pointer& p);
    void release(ToolEnv& env);
    bool active() const { return mode_ != Mode::None; }
    void cancel();
    // Lights being dragged replace the document's in the scene.
    void decorate(Scene& scene) const;
    const char* hint() const;

private:
    enum class Mode { None, Move, Ring } mode_ = Mode::None;
    const Viewport* view_ = nullptr;
    double u0_ = 0, v0_ = 0;
    Vector2 center_{};      // ring: light centre on screen
    double angle0_ = 0;     // ring: mouse angle at the press
    double sign_ = 1;       // ring: +1 if the axis points at the viewer
    int axis_ = -1;
    double turned_ = 0;     // ring: degrees so far (snapped)
    Vec3 moved_;            // move: offset so far (snapped)
    LightId anchor_ = 0;
    std::vector<Light> start_, now_;
    mutable char hintBuf_[120] = "";
};

} // namespace scal
