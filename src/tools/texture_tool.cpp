#include "tools/texture_tool.hpp"
#include "core/texture.hpp"

namespace scal {

FaceRef pickFace3D(const Document& doc, const Viewport& vp, Vector2 local) {
    Vec3 o, d;
    vp.pickRay(local, o, d);
    FaceRef best{0, -1};
    double bestT = 1e300;
    for (const Brush& b : doc.brushes()) {
        if (!doc.visible(b)) continue;
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
        if (env.alt && env.shift) { wrapOnto(env.doc, hit); return; }
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
            primary_ = hit;
        } else if (env.shift) {
            // Shift+click: add / remove single faces.
            if (!faces_.erase(hit)) { faces_.insert(hit); if (primary_.second < 0) primary_ = hit; }
        } else {
            faces_ = {hit};
            primary_ = hit;
        }
        return;
    }
    const BrushId id = pick2D(env.sceneBrushes, *p.view, p.local);
    if (!env.shift) faces_.clear();
    if (const Brush* b = doc.find(id))
        for (int i = 0; i < (int)b->faces.size(); ++i) faces_.insert({b->id, i});
}

FaceRef TextureTool::primary(const Document& doc) const {
    const Brush* b = doc.find(primary_.first);
    if (!b || primary_.second < 0 || primary_.second >= (int)b->faces.size()) return {0, -1};
    return primary_;
}

void TextureTool::wrapOnto(Document& doc, const FaceRef& target) {
    const FaceRef src = primary(doc);
    if (src.second < 0) { message_ = "click a face first, then Option+Shift+click its neighbour"; return; }
    if (src == target) return;
    const Brush* sb = doc.find(src.first);
    const Brush* tb = doc.find(target.first);
    if (!sb || !tb) return;
    auto polyOf = [](const Brush& b, int face) {
        for (const Polygon& p : b.polygons()) if (p.face == face) return p;
        return Polygon{};
    };
    Brush out = *tb;
    if (!wrapTexture(sb->faces[src.second], polyOf(*sb, src.second), out.faces[target.second], polyOf(*tb, target.second))) {
        message_ = "those faces don't share an edge";
        return;
    }
    doc.apply(doc.makeModify({out}, "Wrap texture"));
    primary_ = target;
    faces_ = {target};
    message_ = "wrapped - Option+Shift+click the next face to keep going";
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
    return "3D: click face  Shift+click add/remove  Cmd+click whole brush  Option+click pick texture  Option+Shift+click wrap onto face   Shift+T: apply";
}

} // namespace scal
