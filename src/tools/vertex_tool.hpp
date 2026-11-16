#pragma once
// Vertex tool: edit the corners of selected brushes.
//  - click a vertex handle in a 2D view (all corners stacked under it are taken, like
//    Hammer); Cmd+click adds/removes; click a brush to select it.
//  - drag to move the chosen corners, snapped to the grid; the brush is rebuilt live.
//    If the shape would not be a valid convex solid it is shown in magenta and the move
//    is not applied on release.
//  - moveSelected(): exact typed moves (Tools -> Move selected vertices...).
#include "tools/tool.hpp"
#include <string>
#include <vector>

namespace scal {

class VertexTool : public Tool {
public:
    const char* name() const override { return "Vertex"; }
    void press(ToolEnv& env, const Pointer& p) override;
    void drag(ToolEnv& env, const Pointer& p) override;
    void release(ToolEnv& env, const Pointer& p) override;
    bool key(ToolEnv& env, int key) override;
    void decorate(const Document& doc, Scene& scene) const override;
    const char* hint() const override;
    void cancel() override;

    size_t selectedCount() const { return sel_.size(); }
    // Single distinct selected position (for "set position"), if exactly one.
    bool singlePosition(Vec3& out) const;
    // Moves the selected corners by `delta` as one undoable command. Returns false and
    // sets `error` if any affected brush would become invalid.
    bool moveSelected(Document& doc, const Vec3& delta, std::string& error);
    void clearSelection() { sel_.clear(); }

private:
    struct VRef { BrushId brush; Vec3 pos; };
    bool isSelected(BrushId id, const Vec3& p) const;
    // Rebuilds affected brushes with selected corners moved by delta.
    // Returns per-brush results (ok or not) for previews / applying.
    struct Preview { Brush brush; bool ok; std::string error; };
    std::vector<Preview> build(const Document& doc, const Vec3& delta) const;

    std::vector<VRef> sel_;
    bool dragging_ = false;
    const Viewport* view_ = nullptr;
    double u0_ = 0, v0_ = 0;
    Vec3 anchor_;     // the vertex that follows the cursor (snapped)
    Vec3 delta_;
    std::vector<Preview> preview_;
    std::string message_;
    mutable char hintBuf_[160] = "";
};

} // namespace scal
