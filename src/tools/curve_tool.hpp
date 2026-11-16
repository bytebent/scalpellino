#pragma once
// Curve tool (Shift+C): arches, rings, curved walls, spiral stairs and arched openings.
//  - pick a shape in the Properties panel (right), then drag a box in a 2D view
//    (curved wall: drag from start to end; a bend handle appears);
//  - adjust: drag the box handles in any 2D view (incl. depth in the other views),
//    drag the curved wall's points 1 (start), 2 (end), 3 (bend), or type exact values in
//    the panel (segments, thickness, angles, steps, box, depth...);
//  - Return generates, Esc cancels. Generated shapes stay editable (Tools -> Edit curve)
//    until baked into plain brushes (Tools -> Bake curve).
// No mouse-wheel parameters (project rule): typed fields and handles only.
#include "core/curves.hpp"
#include "tools/tool.hpp"

namespace scal {

class CurveTool : public Tool {
public:
    const char* name() const override { return "Curve"; }
    void press(ToolEnv& env, const Pointer& p) override;
    void drag(ToolEnv& env, const Pointer& p) override;
    void release(ToolEnv& env, const Pointer& p) override;
    bool key(ToolEnv& env, int key) override;
    void decorate(const Document& doc, Scene& scene) const override;
    const char* hint() const override;
    void cancel() override;

    CurveParams params;          // edited by the panel too
    bool hasShape() const { return hasShape_; }
    GroupId editingGroup() const { return editing_; }
    void startEditing(const CurveParams& p, GroupId g) { params = p; hasShape_ = true; editing_ = g; message_.clear(); }
    void finished() { hasShape_ = false; editing_ = 0; dragging_ = Drag::None; }
    void setMessage(std::string m) { message_ = std::move(m); }
    // Preview result for the current parameters (cached).
    const CurveResult& preview() const;
    // The 3D box of box-based shapes.
    AABB box3D() const;
    void setBox3D(const AABB& b);

private:
    enum class Drag { None, Drawing, Box, Point };
    bool uprightShape() const {
        return params.shape == CurveShape::SpiralStairs || params.shape == CurveShape::HelixRamp || params.shape == CurveShape::Dome;
    }
    bool viewMatches(const Viewport& v) const { return v.axU() == params.axU && v.axV() == params.axV; }

    bool hasShape_ = false;
    GroupId editing_ = 0;
    Drag dragging_ = Drag::None;
    const Viewport* view_ = nullptr;
    double au_ = 0, av_ = 0, u0_ = 0, v0_ = 0;
    BoxGrab grab_;
    AABB startBox_;
    int point_ = -1;
    std::string message_;
    mutable std::string cacheKey_;
    mutable CurveResult cache_;
    mutable char hintBuf_[200] = "";
};

} // namespace scal
