#include "render/scene.hpp"

namespace scal {

SceneBrush makeSceneBrush(const Brush& b) {
    SceneBrush s;
    s.id = b.id;
    s.polys = b.polygons();
    s.faces = b.faces;
    if (auto g = terrainGrid(b)) {
        // Swap the flat face for the grid cells.
        std::vector<Polygon> keep;
        for (Polygon& p : s.polys)
            if (p.face != g->face) keep.push_back(std::move(p));
        s.polys = std::move(keep);
        s.terrainFirst = s.polys.size();
        const int n = g->n;
        // Smooth normals from neighbouring points.
        std::vector<Vec3> vn((size_t)n * n);
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                const Vec3 du = g->at(std::min(i + 1, n - 1), j) - g->at(std::max(i - 1, 0), j);
                const Vec3 dv = g->at(i, std::min(j + 1, n - 1)) - g->at(i, std::max(j - 1, 0));
                Vec3 nn = cross(du, dv);
                if (dot(nn, g->normal) < 0) nn = -nn;
                vn[(size_t)j * n + i] = length(nn) > 1e-12 ? normalize(nn) : g->normal;
            }
        for (int j = 0; j + 1 < n; ++j)
            for (int i = 0; i + 1 < n; ++i) {
                const int idx[4] = {j * n + i, j * n + i + 1, (j + 1) * n + i + 1, (j + 1) * n + i};
                Polygon p;
                p.face = g->face;
                for (int k : idx) {
                    p.verts.push_back(g->pos[k]);
                    s.cellAlpha.push_back((float)g->alpha[k]);
                    s.cellNormals.push_back(vn[k]);
                }
                Vec3 cn = cross(p.verts[2] - p.verts[0], p.verts[3] - p.verts[1]);
                if (dot(cn, g->normal) < 0) cn = -cn;
                p.normal = length(cn) > 1e-12 ? normalize(cn) : g->normal;
                s.polys.push_back(std::move(p));
            }
        s.texture2 = b.terrain->texture2;
        s.terrain = std::move(g);
    }
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
