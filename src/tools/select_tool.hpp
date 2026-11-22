#pragma once
// Select tool: click to select (Cmd+click toggles), drag to move (snapped),
// drag handles/edges to resize, Shift+drag to clone, Option+drag a corner to rotate
// (15-degree steps; add Shift for free rotation). Delete removes, Esc deselects.
// Lights: click a marker to select it, drag it (2D) to move, drag a ring to aim it
// (see tools/light_edit.hpp).
// 3D view (v0.10.2): drag a rectangle to select everything that lies wholly inside it
// (brushes, primitives, props, lights; hidden ones never); Cmd+drag adds to the selection.
#include "tools/object_edit.hpp"
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
    void beginRect(const Document& doc, const Pointer& p, bool add);
    void applyRect(ToolEnv& env);

    Mode mode_ = Mode::Idle;
    ObjectEdit lights_;
    const Viewport* view_ = nullptr;
    BoxGrab grab_;
    double u0_ = 0, v0_ = 0;
    AABB startBox_, curBox_;
    bool clone_ = false;
    bool textureLock_ = true;
    BrushId pressedId_ = 0; // brush clicked inside a multi-selection (narrow to it on plain click)
    std::vector<Brush> ghosts_;
    Vec3 rotCenter_, rotAxis_;
    double rotStart_ = 0, rotAngle_ = 0;
    mutable char hintBuf_[96] = "";
    // 3D rectangle select: armed on press, drawn once the mouse has moved a few pixels.
    bool rectArmed_ = false, rectOn_ = false, rectAdd_ = false;
    const Viewport* rectView_ = nullptr;
    Vector2 rectA_{}, rectB_{};
    std::set<BrushId> sel0_;
    std::set<MathPrimId> prim0_;
    std::set<PropId> prop0_;
    std::set<DecalId> decal0_;
    std::set<LightId> light0_;
    std::set<CameraId> camera0_;
};

} // namespace scal
