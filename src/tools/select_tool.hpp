#pragma once
// Select tool: click to select (Cmd+click toggles), drag to move (snapped),
// drag handles/edges to resize, Shift+drag to clone, Option+drag a corner to rotate
// (15-degree steps; add Shift for free rotation). Delete removes, Esc deselects.
#include "tools/tool.hpp"
#include <optional>

namespace scal {

class SelectTool : public Tool {
public:
    const char* name() const override { return "Select"; }
    void press(ToolEnv& env, const Pointer& p) override;
    void drag(ToolEnv& env, const Pointer& p) override;
    void release(ToolEnv& env, const Pointer& p) override;
    bool key(ToolEnv& env, int key) override;
    void decorate(const Document& doc, Scene& scene) const override;
    const char* hint() const override;
    void cancel() override;

private:
    enum class Mode { Idle, Moving, Resizing, Rotating };
    void updateGhosts(const Document& doc);

    Mode mode_ = Mode::Idle;
    const Viewport* view_ = nullptr;
    BoxGrab grab_;
    double u0_ = 0, v0_ = 0;
    AABB startBox_, curBox_;
    bool clone_ = false;
    BrushId pressedId_ = 0; // brush clicked inside a multi-selection (narrow to it on plain click)
    std::vector<Brush> ghosts_;
    Vec3 rotCenter_, rotAxis_;
    double rotStart_ = 0, rotAngle_ = 0;
    mutable char hintBuf_[96] = "";
};

} // namespace scal
