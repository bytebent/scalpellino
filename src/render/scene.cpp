#include "render/scene.hpp"

namespace scal {

SceneBrush makeSceneBrush(const Brush& b) {
    SceneBrush s;
    s.id = b.id;
    s.polys = b.polygons();
    s.faces = b.faces;
    for (const Polygon& p : s.polys)
        for (const Vec3& v : p.verts) s.bounds.add(v);
    return s;
}

const std::vector<SceneBrush>& SceneCache::get(const Document& doc) {
    if (doc.revision() != revision_) {
        brushes_.clear();
        brushes_.reserve(doc.brushes().size());
        for (const Brush& b : doc.brushes())
            if (doc.visible(b)) brushes_.push_back(makeSceneBrush(b)); // hidden: not drawn, not pickable
        revision_ = doc.revision();
    }
    return brushes_;
}

} // namespace scal
