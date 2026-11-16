#pragma once
// Texture tool (Shift+A): pick faces to texture and adjust.
//  3D view: click a face; Shift+click adds/removes single faces; Cmd+click = every face
//           of that brush (Cmd+Shift+click adds that brush's faces);
//           Option+click = eyedropper (that face's texture becomes the current texture);
//           Option+Shift+click = wrap: continue the last clicked face's texture onto
//           this neighbouring face (folded around the shared edge); repeat to go around.
//  2D views: click a brush = all its faces (Shift+click adds).
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
    void clear() { faces_.clear(); primary_ = {0, -1}; }
    // The face clicked last (start for wrapping), if it still exists.
    FaceRef primary(const Document& doc) const;

private:
    void wrapOnto(Document& doc, const FaceRef& target);
    std::set<FaceRef> faces_;
    FaceRef primary_{0, -1};
    std::string message_;
};

// Nearest face under the cursor in the 3D view, or {0, -1}.
FaceRef pickFace3D(const Document& doc, const Viewport& vp, Vector2 local);

} // namespace scal
