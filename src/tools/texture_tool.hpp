#pragma once
// Texture tool (Shift+A): pick faces to texture and adjust.
//  3D view: click a face; Cmd+click adds/removes; Shift+click = every face of that brush;
//           Option+click = eyedropper (that face's texture becomes the current texture).
//  2D views: click a brush = all its faces.
// The Face panel and Texture menu act on the picked faces.
#include "tools/tool.hpp"
#include <set>

namespace scal {

class TextureTool : public Tool {
public:
    const char* name() const override { return "Texture"; }
    void press(ToolEnv& env, const Pointer& p) override;
    bool key(ToolEnv& env, int key) override;
    void decorate(const Document& doc, Scene& scene) const override;
    const char* hint() const override;

    // Picked faces that still exist in `doc`.
    std::vector<FaceRef> faces(const Document& doc) const;
    void setFaces(std::set<FaceRef> f) { faces_ = std::move(f); }
    void clear() { faces_.clear(); }

private:
    std::set<FaceRef> faces_;
    std::string message_;
};

// Nearest face under the cursor in the 3D view, or {0, -1}.
FaceRef pickFace3D(const Document& doc, const Viewport& vp, Vector2 local);

} // namespace scal
