#pragma once
// Camera tool (Shift+K, milestone 11): place cameras, select, move and aim them.
//  - click in a 2D view to put the new camera there (the depth comes from the last box,
//    as for lights), or on a surface in the 3D view (32 units out from it). Click again to
//    adjust; Return creates it, Esc cancels. Its settings (lens, picture size...) are typed
//    in Properties beforehand (`templ`). The first camera of a map becomes the active one.
//  - cameras are selected, moved and turned here as in the select tool: three rings about
//    their position plus a roll ring around the view direction.
//  - Aim at a clicked point (`startAim`): the next click aims the selected cameras at the
//    point clicked: a surface in the 3D view, or a spot in a 2D view (at each camera's own
//    depth there, so the top view aims level). One undo step; Esc stops.
#include "tools/object_edit.hpp"
#include "tools/tool.hpp"
#include <optional>

namespace scal {

class CameraTool : public Tool {
public:
    const char* name() const override { return "Camera"; }
    void press(ToolEnv& env, const Pointer& p) override;
    void drag(ToolEnv& env, const Pointer& p) override;
    void release(ToolEnv& env, const Pointer& p) override;
    bool key(ToolEnv& env, int key) override;
    void decorate(const Document& doc, Scene& scene) const override;
    const char* hint() const override;
    void cancel() override;

    Camera templ;                   // what the next camera will be like
    std::optional<Vec3> pending;    // where it will go (Return creates it)
    // Creates the pending camera (Return). False if there is none.
    bool create(Document& doc);
    void startAim() { aiming_ = true; pending.reset(); message_.clear(); }
    bool aiming() const { return aiming_; }
    // Aims the selected cameras at `target` (one undo step). False if none is selected.
    static bool aimSelected(Document& doc, const Vec3& target, int depthAxis = -1);

private:
    ObjectEdit objects_;
    bool aiming_ = false;
    std::string message_;
};

// A free name: "camera 1", "camera 2"... (the lowest number not used by the map's cameras
// or by `alsoTaken`).
std::string freeCameraName(const Document& doc, const std::vector<Camera>& alsoTaken = {});

} // namespace scal
