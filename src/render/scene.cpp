#include "render/scene.hpp"
#include "core/texture.hpp"
#include <algorithm>
#include <unordered_map>

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
        for (const TerrainLayer& l : b.terrain->layers) s.layers.push_back({l.texture, {}});
        for (int j = 0; j + 1 < n; ++j)
            for (int i = 0; i + 1 < n; ++i) {
                const int idx[4] = {j * n + i, j * n + i + 1, (j + 1) * n + i + 1, (j + 1) * n + i};
                Polygon p;
                p.face = g->face;
                for (int k : idx) {
                    p.verts.push_back(g->pos[k]);
                    // Shares -> transparency for drawing each layer over the ones below:
                    // layer l covers w_l / (what's shown up to and including l).
                    double above = 0;
                    std::vector<float> over(s.layers.size());
                    for (size_t l = s.layers.size(); l-- > 0;) {
                        const double w = b.terrain->layers[l].alpha[k];
                        const double upTo = 1 - above;
                        over[l] = upTo > 1e-9 ? (float)std::clamp(w / upTo, 0.0, 1.0) : 0.0f;
                        above += w;
                    }
                    for (size_t l = 0; l < s.layers.size(); ++l) s.layers[l].cellAlpha.push_back(over[l]);
                    s.cellNormals.push_back(vn[k]);
                }
                Vec3 cn = cross(p.verts[2] - p.verts[0], p.verts[3] - p.verts[1]);
                if (dot(cn, g->normal) < 0) cn = -cn;
                p.normal = length(cn) > 1e-12 ? normalize(cn) : g->normal;
                s.polys.push_back(std::move(p));
            }
        s.terrain = std::move(g);
    }
    for (const Polygon& p : s.polys)
        for (const Vec3& v : p.verts) s.bounds.add(v);
    return s;
}

size_t dropNoDraw(SceneBrush& b) {
    const size_t plain = b.terrain ? b.terrainFirst : b.polys.size();
    size_t dropped = 0;
    for (size_t i = 0; i < plain; ++i) {
        const Polygon& p = b.polys[i];
        if (p.face >= 0 && p.face < (int)b.faces.size() && isNoDraw(b.faces[p.face].texture)) ++dropped;
    }
    if (!dropped) return 0;
    std::vector<Polygon> kept;
    kept.reserve(b.polys.size() - dropped);
    for (size_t i = 0; i < b.polys.size(); ++i) {
        const Polygon& p = b.polys[i];
        if (i < plain && p.face >= 0 && p.face < (int)b.faces.size() && isNoDraw(b.faces[p.face].texture)) continue;
        kept.push_back(std::move(b.polys[i]));
    }
    b.polys = std::move(kept);
    if (b.terrain) b.terrainFirst -= dropped;
    return dropped;
}

std::shared_ptr<const std::vector<SceneBrush>> SceneCache::shared(const Document& doc) {
    if (doc.revision() == revision_) return list_;
    // Old entries by id. If no scene holds the old list any more, entries are moved out of
    // it; otherwise copied (the holder keeps a list that doesn't change under it).
    std::unordered_map<BrushId, size_t> old;
    old.reserve(list_->size());
    for (size_t i = 0; i < list_->size(); ++i) old.emplace((*list_)[i].id, i);
    const bool own = list_.use_count() == 1;
    auto list = std::make_shared<std::vector<SceneBrush>>();
    std::vector<std::uint64_t> stamps;
    std::vector<size_t> dropped;
    list->reserve(doc.brushes().size());
    rebuilt_ = nodraw_ = 0;
    for (const Brush& b : doc.brushes()) {
        if (opt_.visibleOnly && !doc.visible(b)) continue; // hidden: not drawn, not pickable
        const std::uint64_t stamp = doc.brushStamp(b.id);
        auto it = old.find(b.id);
        if (it != old.end() && stamps_[it->second] == stamp) {
            SceneBrush& sb = (*list_)[it->second];
            list->push_back(own ? std::move(sb) : sb);
            dropped.push_back(dropped_[it->second]);
        } else {
            list->push_back(makeSceneBrush(b));
            dropped.push_back(opt_.dropNoDraw ? dropNoDraw(list->back()) : 0);
            ++rebuilt_;
        }
        stamps.push_back(stamp);
        nodraw_ += dropped.back();
    }
    // The old list stays alive until the next update, so a `get` reference taken before
    // this edit (a tool's ToolEnv) still points at a vector, not freed memory.
    prev_ = std::move(list_);
    list_ = std::move(list);
    stamps_ = std::move(stamps);
    dropped_ = std::move(dropped);
    revision_ = doc.revision();
    return list_;
}

void SceneBrushes::swapIn(SceneBrush b) {
    if (!base_) return;
    for (size_t i = 0; i < base_->size(); ++i)
        if ((*base_)[i].id == b.id) {
            for (auto& [at, s] : swaps_)
                if (at == i) { s = std::move(b); return; }
            swaps_.emplace_back(i, std::move(b));
            return;
        }
}

const SceneBrush& SceneBrushes::operator[](size_t i) const {
    for (const auto& [at, s] : swaps_)
        if (at == i) return s;
    return (*base_)[i];
}

} // namespace scal
