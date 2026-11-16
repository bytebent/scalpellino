#pragma once
// Tool interface. Tools turn pointer/key input into document commands and draw
// their own previews into the Scene. They never change the document directly
// except through Document::apply (undoable) or selection changes.
#include "core/document.hpp"
#include "core/grid.hpp"
#include "render/scene.hpp"
#include "render/viewport.hpp"
#include <vector>

namespace scal {

// Pointer position in one viewport.
struct Pointer {
    const Viewport* view = nullptr;
    Vector2 local{};    // pixels inside the viewport
    double u = 0, v = 0; // raw world coords on the 2D view plane (2D views only)
};

struct ToolEnv {
    Document& doc;
    const Grid& grid;
    const std::vector<SceneBrush>& sceneBrushes; // cached polygons, same order as doc
    AABB& lastBox;                               // last created/selected bounds (depth defaults)
    bool cmd = false;                            // Cmd on macOS, Ctrl elsewhere
    bool shift = false;
    bool alt = false;                            // Option on macOS
};

class Tool {
public:
    virtual ~Tool() = default;
    virtual const char* name() const = 0;
    virtual void press(ToolEnv&, const Pointer&) {}
    virtual void drag(ToolEnv&, const Pointer&) {}
    virtual void release(ToolEnv&, const Pointer&) {}
    // Return/Enter, Esc, Delete... Returns true if handled.
    virtual bool key(ToolEnv&, int key) { return false; }
    virtual void decorate(const Document&, Scene&) const {}
    virtual const char* hint() const { return ""; }
    // Abort any in-progress drag or preview (tool switch, undo, load...).
    virtual void cancel() {}
};

// ---- shared box handle logic (2D views)

struct BoxGrab {
    int su = 0, sv = 0;  // side being dragged on the view's U / V axis: -1 min, +1 max
    bool move = false;   // dragging the whole box
    bool none() const { return su == 0 && sv == 0 && !move; }
    bool resize() const { return su != 0 || sv != 0; }
};

// What part of box `b` is under `local` in view `vp` (handles and edges first, then inside).
BoxGrab grabBox(const Viewport& vp, const AABB& b, Vector2 local, float tolPx = 6.0f);

// New box after dragging `grab` from (u0,v0) to (u,v), snapped to the grid.
AABB dragBox(const Viewport& vp, const AABB& start, const BoxGrab& grab,
             double u0, double v0, double u, double v, const Grid& grid);

// Brush under the cursor in a 2D view (smallest one wins), or 0.
BrushId pick2D(const std::vector<SceneBrush>& brushes, const Viewport& vp, Vector2 local);

// Brush hit first by a ray from the 3D view, or 0.
BrushId pick3D(const Document& doc, const Viewport& vp, Vector2 local);

} // namespace scal
