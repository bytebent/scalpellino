#pragma once
// The Editor's member templates (used from several editor_*.cpp files).
#include "app/editor.hpp"

namespace scal {

template <class Fn> void Editor::editFaces(const char* name, Fn fn) {
    const std::vector<FaceRef> faces = textureTool_.faces(doc_);
    if (faces.empty()) return;
    std::vector<Brush> out;
    for (const FaceRef& fr : faces) {
        auto it = std::find_if(out.begin(), out.end(), [&](const Brush& b) { return b.id == fr.first; });
        if (it == out.end()) {
            out.push_back(*doc_.find(fr.first));
            it = out.end() - 1;
        }
        Face& f = it->faces[fr.second];
        Polygon poly;
        for (const Polygon& p : it->polygons()) if (p.face == fr.second) { poly = p; break; }
        const Vec3 n = f.plane().n;
        fn(f, poly, textures_.size(f.texture), n);
    }
    doc_.apply(doc_.makeModify(out, name));
}

template <class Fn> void Editor::editLights(const char* name, Fn fn) {
    std::vector<Light> out;
    for (LightId id : doc_.lightSelection())
        if (const Light* l = doc_.findLight(id)) {
            Light c = *l;
            fn(c);
            if (c != *l) out.push_back(std::move(c));
        }
    if (!out.empty()) doc_.apply(doc_.makeModifyLights(out, name));
}

} // namespace scal
