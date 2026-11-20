#pragma once
// Rough light preview (milestone 8, View > 3D view: rough light preview). Light comes
// from core/world.hpp lightAt(): ambient + lights, no shadows. Built only while the
// preview is on; when the map changes, only the brushes an edit touched are lit again
// (all of them when a light or the environment changed).
//  - plain faces are cut into triangles of about 64 units, lit at their corners, so a
//    light in the middle of a big floor still shows;
//  - terrain cells are lit at their 4 corners (with their smooth normals).
#include "core/document.hpp"
#include "render/scene.hpp"
#include <map>
#include <vector>

namespace scal {

struct LitPoly {
    std::vector<Vec3> pts;   // plain face: triangles (3 points each); terrain cell: empty
    std::vector<Vec3> light; // a multiplier per point (terrain cell: per corner, 4)
};
using LightingMap = std::map<BrushId, std::vector<LitPoly>>; // per brush, per polygon

class LightingCache {
public:
    const LightingMap& get(const Document& doc, const std::vector<SceneBrush>& brushes);
    void clear() { revision_ = ~0ull; map_.clear(); stamps_.clear(); }
    size_t rebuiltLast() const { return rebuilt_; } // brushes the last update lit again

private:
    unsigned long long revision_ = ~0ull;
    std::uint64_t lightingStamp_ = ~0ull;
    LightingMap map_;
    std::map<BrushId, std::uint64_t> stamps_; // brush stamp each entry was lit for
    size_t rebuilt_ = 0;
};

} // namespace scal
