#pragma once
// Decal tool (Shift+D, v0.12.5): putting decals on surfaces (core/decal.hpp).
//  - pick the picture in the Textures panel (the current texture); click a surface in the
//    3D view: the decal shows there, at its picture's size (texels x $decalscale, as
//    Source), facing the surface; drag to slide it. Return places it, Esc cancels. Size,
//    turn, reach and order are typed in Properties (or Decals > Decal...).
//  - click an existing decal to select it (Option+click places a new one over it); its
//    corners size it, its ring turns it, dragging slides it (decal_edit.hpp).
#include "tools/object_edit.hpp"
#include "tools/tool.hpp"
#include <optional>

namespace scal {

class DecalTool : public Tool {
public:
    const char* name() const override { return "Decal"; }
    void press(ToolEnv& env, const Pointer& p) override;
    void drag(ToolEnv& env, const Pointer& p) override;
    void release(ToolEnv& env, const Pointer& p) override;
    bool key(ToolEnv& env, int key) override;
    void decorate(const Document& doc, Scene& scene) const override;
    const char* hint() const override;
    void cancel() override;

    // The next decal's turn (degrees); its size comes from the picture.
    double rotation = 0;

    bool hasPending() const { return pending_.has_value(); }
    // The pending decal, to be changed in Properties (null if none).
    Decal* pending() { return pending_ ? &*pending_ : nullptr; }
    const Decal* pending() const { return pending_ ? &*pending_ : nullptr; }
    // Places the pending decal (Return); false if there is none. Sets the hint if it can't.
    bool create(Document& doc);
    // Puts the pending decal at a surface point (scripts, typed placement).
    bool placeOn(ToolEnv& env, const Vec3& origin, const Vec3& dir);

private:
    ObjectEdit objects_;
    std::optional<Decal> pending_;
    BrushId brush_ = 0;
    bool placing_ = false;
    const Viewport* view_ = nullptr;
    std::string message_;
};

} // namespace scal
