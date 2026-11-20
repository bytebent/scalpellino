#include "render/lighting.hpp"
#include <algorithm>
#include <set>
#include <cmath>

namespace scal {

namespace {
constexpr double kPiece = 64; // preview triangles are about this big
constexpr int kMaxSplit = 32;
} // namespace

const LightingMap& LightingCache::get(const Document& doc, const std::vector<SceneBrush>& brushes) {
    if (doc.revision() == revision_) return map_;
    if (doc.lightingStamp() != lightingStamp_) { map_.clear(); stamps_.clear(); } // all lit again
    lightingStamp_ = doc.lightingStamp();
    rebuilt_ = 0;
    // Entries of brushes no longer listed go.
    {
        std::set<BrushId> listed;
        for (const SceneBrush& b : brushes) listed.insert(b.id);
        for (auto it = map_.begin(); it != map_.end();)
            if (!listed.count(it->first)) { stamps_.erase(it->first); it = map_.erase(it); }
            else ++it;
    }
    const Environment& env = doc.environment();
    const std::vector<Light>& lights = doc.lights();
    // A little off the surface, so a light right on it still counts.
    auto at = [&](const Vec3& p, const Vec3& n) { return lightAt(env, lights, p + n * 0.5, n); };
    for (const SceneBrush& b : brushes) {
        const std::uint64_t stamp = doc.brushStamp(b.id);
        auto st = stamps_.find(b.id);
        if (st != stamps_.end() && st->second == stamp && map_.count(b.id)) continue; // unchanged
        stamps_[b.id] = stamp;
        ++rebuilt_;
        std::vector<LitPoly>& polys = map_[b.id];
        polys.clear();
        polys.resize(b.polys.size());
        for (size_t pi = 0; pi < b.polys.size(); ++pi) {
            const Polygon& p = b.polys[pi];
            LitPoly& out = polys[pi];
            if (b.terrain && pi >= b.terrainFirst) {
                for (size_t k = 0; k < p.verts.size(); ++k)
                    out.light.push_back(at(p.verts[k], b.cellNormals[4 * (pi - b.terrainFirst) + k]));
                continue;
            }
            for (size_t i = 1; i + 1 < p.verts.size(); ++i) {
                const Vec3 &a = p.verts[0], &e1 = p.verts[i], &e2 = p.verts[i + 1];
                const double longest = std::max({length(e1 - a), length(e2 - a), length(e2 - e1)});
                const int k = std::clamp((int)std::ceil(longest / kPiece), 1, kMaxSplit);
                // Points (s, t) with s + t <= k, lit once each.
                auto idx = [k](int s, int t) { return s * (k + 1) - s * (s - 1) / 2 + t; };
                std::vector<Vec3> pts, lit;
                for (int s = 0; s <= k; ++s)
                    for (int t = 0; t + s <= k; ++t) {
                        const Vec3 q = a + (e1 - a) * ((double)s / k) + (e2 - a) * ((double)t / k);
                        pts.push_back(q);
                        lit.push_back(at(q, p.normal));
                    }
                auto tri = [&](int i0, int i1, int i2) {
                    for (int j : {i0, i1, i2}) { out.pts.push_back(pts[j]); out.light.push_back(lit[j]); }
                };
                for (int s = 0; s < k; ++s)
                    for (int t = 0; t + s < k; ++t) {
                        tri(idx(s, t), idx(s + 1, t), idx(s, t + 1));
                        if (t + s + 1 < k) tri(idx(s + 1, t), idx(s + 1, t + 1), idx(s, t + 1));
                    }
            }
        }
    }
    revision_ = doc.revision();
    return map_;
}

} // namespace scal
