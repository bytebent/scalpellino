#pragma once
// Terrain tool (Shift+G): sculpt and paint terrain (see core/terrain.hpp).
// Drag over a terrain in the 3D view, or in a 2D view looking at it (top view for
// ground). Each drag is one undo step; Esc during a drag cancels it. The circle shows
// the radius. Mode, radius, amount, strength and falloff are typed in the Properties
// panel (no mouse wheel). Paint blends in the current texture.
//
// Making terrain (Terrain > Make terrain): the tool picks faces instead. Click any face
// of any brush in the 3D view (Shift+click adds or removes), or click a brush in a 2D
// view to pick its face toward you. Return makes the picked faces terrain (the editor
// does that, with the grid size from the Properties panel); Esc stops picking.
#include "core/terrain.hpp"
#include "tools/tool.hpp"
#include <optional>
#include <set>

namespace scal {

class TerrainTool : public Tool {
public:
    const char* name() const override { return "Terrain"; }
    void press(ToolEnv& env, const Pointer& p) override;
    void drag(ToolEnv& env, const Pointer& p) override;
    void release(ToolEnv& env, const Pointer& p) override;
    void hover(ToolEnv& env, const Pointer& p) override;
    bool key(ToolEnv& env, int key) override;
    void decorate(const Document& doc, Scene& scene) const override;
    const char* hint() const override;
    void cancel() override;
    bool stroking() const { return stroking_; }

    // Face picking for "make terrain".
    bool picking() const { return picking_; }
    void startPicking() { cancel(); picking_ = true; picked_.clear(); message_.clear(); }
    void stopPicking() { picking_ = false; picked_.clear(); }
    std::vector<FaceRef> pickedFaces(const Document& doc) const;

    // Options (Properties panel, Terrain menu, script `terrainset`).
    SculptMode mode = SculptMode::Raise;
    Falloff falloff = Falloff::Soft;
    double radius = 128;   // units, measured along the surface
    double amount = 8;     // units per dab: raise, lower, noise
    double strength = 50;  // percent per dab: smooth, flatten, paint, erase
    std::string paintTexture = "default";

private:
    // Where the pointer's ray meets a terrain (the stroke's copies while dragging).
    bool hit(const ToolEnv& env, const Pointer& p, Vec3& point, Vec3& normal) const;
    void dab(const Vec3& at);
    // Cursor at `at`, its circle laid onto the terrain surface below it.
    void setCursor(const ToolEnv& env, const Vec3& at, const Vec3& n);

    bool picking_ = false;
    std::set<FaceRef> picked_;
    void pickPress(ToolEnv& env, const Pointer& p);

    bool stroking_ = false;
    bool changed_ = false;
    std::vector<Brush> work_;   // every visible terrain brush, edited during a stroke
    Vec3 lastDab_, flatten_;
    unsigned seed_ = 1;
    unsigned long long rev_ = 0; // bumps with each dab (drawing cache key)
    std::optional<Scene::BrushCursor> cursor_;
    std::string message_;
    mutable unsigned long long drawnRev_ = ~0ull;
    mutable std::vector<SceneBrush> drawn_;
};

} // namespace scal
