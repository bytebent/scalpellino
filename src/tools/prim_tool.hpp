#pragma once
// Primitive tool (Shift+M): math primitives (core/mathprim.hpp).
//  - pick the kind and its settings in Properties (`templ`), drag a box in a 2D view
//    (the depth comes from the last box, like the block tool); the shape fills the box.
//    Drag again to redo it. Return creates it, Esc cancels.
//  - a disc lies across the axis of the view it was drawn in.
//  - existing objects are selected, moved, turned and sized here as in the select tool.
#include "tools/object_edit.hpp"
#include "tools/tool.hpp"
#include <optional>

namespace scal {

class PrimTool : public Tool {
public:
    const char* name() const override { return "Primitive"; }
    void press(ToolEnv& env, const Pointer& p) override;
    void drag(ToolEnv& env, const Pointer& p) override;
    void release(ToolEnv& env, const Pointer& p) override;
    bool key(ToolEnv& env, int key) override;
    void decorate(const Document& doc, Scene& scene) const override;
    const char* hint() const override;
    void cancel() override;

    MathPrim templ;     // kind and settings of the next primitive
    bool flat = false;  // flat colour instead of the current texture
    bool hasPending() const { return box_.has_value(); }
    std::optional<MathPrim> pending() const;
    // Typed placement (Properties): centre and size of the pending box.
    void setPending(const Vec3& center, const Vec3& size, int viewAxis);
    // Creates the pending primitive with `texture` (Return). False if there is none.
    bool create(Document& doc, const std::string& texture, AABB* lastBox = nullptr);

private:
    ObjectEdit objects_;
    std::optional<AABB> box_;
    int axis_ = 2;           // the view's depth axis the box was drawn in
    bool drawing_ = false;
    const Viewport* view_ = nullptr;
    double au_ = 0, av_ = 0;
    std::string message_;
};

} // namespace scal
