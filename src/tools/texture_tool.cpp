#include "tools/texture_tool.hpp"
#include "core/texture.hpp"

namespace scal {

FaceRef pickFace3D(const Document& doc, const Viewport& vp, Vector2 local) {
    Vec3 o, d;
    vp.pickRay(local, o, d);
    FaceRef best{0, -1};
    double bestT = 1e300;
    for (const Brush& b : doc.brushes()) {
        double t = 0;
        const int f = rayHitFace(b, o, d, &t);
        if (f >= 0 && t < bestT) { bestT = t; best = {b.id, f}; }
    }
    return best;
}

void TextureTool::press(ToolEnv& env, const Pointer& p) {
    const Document& doc = env.doc;
    message_.clear();
    if (!p.view->is2D()) {
        const FaceRef hit = pickFace3D(doc, *p.view, p.local);
        if (hit.second < 0) { if (!env.shift) faces_.clear(); return; }
        const Brush* b = doc.find(hit.first);
        if (env.alt) { // eyedropper
            if (b && env.currentTexture) {
                *env.currentTexture = b->faces[hit.second].texture;
                message_ = "picked texture " + *env.currentTexture;
            }
            return;
        }
        if (env.cmd && b) {
            // Cmd+click: the whole brush (Cmd+Shift+click adds it to the picked faces).
            if (!env.shift) faces_.clear();
            for (int i = 0; i < (int)b->faces.size(); ++i) faces_.insert({b->id, i});
        } else if (env.shift) {
            // Shift+click: add / remove single faces.
            if (!faces_.erase(hit)) faces_.insert(hit);
        } else {
            faces_ = {hit};
        }
        return;
    }
    const BrushId id = pick2D(env.sceneBrushes, *p.view, p.local);
    if (!env.shift) faces_.clear();
    if (const Brush* b = doc.find(id))
        for (int i = 0; i < (int)b->faces.size(); ++i) faces_.insert({b->id, i});
}

bool TextureTool::key(ToolEnv&, int key) {
    if (key == KEY_ESCAPE) { faces_.clear(); message_.clear(); return true; }
    return false;
}

std::vector<FaceRef> TextureTool::faces(const Document& doc) const {
    std::vector<FaceRef> out;
    for (const FaceRef& f : faces_)
        if (const Brush* b = doc.find(f.first); b && f.second >= 0 && f.second < (int)b->faces.size())
            out.push_back(f);
    return out;
}

void TextureTool::decorate(const Document& doc, Scene& scene) const {
    for (const FaceRef& f : faces(doc)) scene.selectedFaces.insert(f);
}

const char* TextureTool::hint() const {
    if (!message_.empty()) return message_.c_str();
    return "3D: click face  Shift+click add/remove  Cmd+click whole brush  Option+click pick texture   Shift+T: apply current texture";
}

} // namespace scal
