#pragma once
// Vertex tool: edit the corners of selected brushes, and the points of selected terrain.
//  - click a vertex handle in a 2D view (all corners stacked under it are taken, like
//    Hammer); Cmd+click adds/removes; click a brush to select it.
//  - drag to move the chosen corners, snapped to the grid; the brush is rebuilt live.
//    If the shape would not be a valid convex solid it is shown in magenta and the move
//    is not applied on release. A terrain face keeps its terrain as long as it is still
//    one flat face with the same corners.
//  - terrain points (small handles) of selected terrain brushes: click one in a 2D view
//    (stacked points come together) or in the 3D view; Cmd+click adds/removes. Dragging
//    raises or lowers them along the terrain's normal, snapped to the grid: in the 3D
//    view, or in a 2D view that sees the terrain from the side. Neighbouring patches
//    follow, so seams never open. Brush corners win where both sit on the same spot.
//  - moveSelected() / moveTerrainSelected(): exact typed moves
//    (Tools -> Move selected vertices...).
#include "core/terrain.hpp"
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

    size_t selectedCount() const { return sel_.size() + tsel_.size(); }
    size_t cornerCount() const { return sel_.size(); }
    size_t terrainPointCount() const { return tsel_.size(); }
    // Single distinct selected position (for "set position"), if exactly one.
    bool singlePosition(Vec3& out) const;
    // The selected terrain points' height, if they all have the same one.
    bool singleHeight(const Document& doc, double& out) const;
    // Moves the selected corners by `delta` as one undoable command. Returns false and
    // sets `error` if any affected brush would become invalid.
    bool moveSelected(Document& doc, const Vec3& delta, std::string& error);
    // Raises the selected terrain points by `amount` (or sets their height to it).
    bool moveTerrainSelected(Document& doc, double amount, bool absolute, std::string& error);
    void clearSelection() { sel_.clear(); tsel_.clear(); }

private:
    struct VRef { BrushId brush; Vec3 pos; };
    struct TRef { BrushId brush; int idx; };
    // A terrain point of a selected brush, where it is now.
    struct TPoint { BrushId brush; int idx; Vec3 pos, normal; double height; };
    bool isSelected(BrushId id, const Vec3& p) const;
    bool isTerrainSelected(BrushId id, int idx) const;
    std::vector<TPoint> terrainPoints(const Document& doc) const;
    // Rebuilds affected brushes with selected corners moved by delta.
    // Returns per-brush results (ok or not) for previews / applying.
    struct Preview { Brush brush; bool ok; std::string error; };
    std::vector<Preview> build(const Document& doc, const Vec3& delta) const;
    // Terrain brushes changed by raising the selected points (all terrain, so neighbours follow).
    std::vector<Brush> buildTerrain(const Document& doc, double amount, bool absolute) const;
    bool pressTerrain(ToolEnv& env, const Pointer& p);

    std::vector<VRef> sel_;
    std::vector<TRef> tsel_;
    bool dragging_ = false;
    bool dragTerrain_ = false;
    const Viewport* view_ = nullptr;
    double u0_ = 0, v0_ = 0;
    Vector2 local0_{};
    Vec3 anchor_;     // the vertex that follows the cursor (snapped)
    Vec3 anchorNormal_;
    double anchorHeight_ = 0;
    Vec3 delta_;
    double raise_ = 0; // terrain drag: how far the points move along the normal
    std::vector<Preview> preview_;
    std::vector<Brush> terrainPreview_;
    std::string message_;
    mutable char hintBuf_[200] = "";
};

} // namespace scal
