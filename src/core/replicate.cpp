#include "core/replicate.hpp"
#include <algorithm>
#include <cmath>

namespace scal {

std::vector<ReplicateCopy> replicate(const std::vector<Brush>& brushes, const std::vector<MathPrim>& prims,
                                     const std::vector<Light>& lights, const ReplicateSpec& spec) {
    std::vector<ReplicateCopy> out;
    if (spec.copies < 1 || (brushes.empty() && prims.empty() && lights.empty())) return out;
    AABB box;
    for (const Brush& b : brushes) { const AABB bb = b.bounds(); box.add(bb.min); box.add(bb.max); }
    for (const MathPrim& p : prims) { const AABB pb = primBounds(p); box.add(pb.min); box.add(pb.max); }
    for (const Light& l : lights) box.add(l.pos);
    Vec3 pivot = spec.pivot == ReplicateSpec::Pivot::Selection ? box.center()
                 : spec.pivot == ReplicateSpec::Pivot::Origin ? Vec3{}
                                                             : spec.point;
    const Vec3 s{spec.scale.x / 100, spec.scale.y / 100, spec.scale.z / 100};
    Vec3 grow{1, 1, 1};   // scale so far (for the offset)
    Vec3 moved;           // brushes: total offset so far
    std::vector<MathPrim> curP = prims;
    std::vector<Light> curL = lights;
    for (int i = 1; i <= spec.copies; ++i) {
        const Vec3 off = spec.scaleOffset ? Vec3{spec.offset.x * grow.x, spec.offset.y * grow.y, spec.offset.z * grow.z}
                                          : spec.offset;
        for (MathPrim& p : curP) {
            p.pos = pivot + Vec3{(p.pos.x - pivot.x) * s.x, (p.pos.y - pivot.y) * s.y, (p.pos.z - pivot.z) * s.z};
            p.size = Vec3{p.size.x * s.x, p.size.y * s.y, p.size.z * s.z};
            p.round *= std::min({s.x, s.y, s.z});
            p.blend *= std::min({s.x, s.y, s.z});
            for (int a = 0; a < 3; ++a)
                if (spec.rotate[a] != 0) rotatePrim(p, a, spec.rotate[a], pivot);
            p.pos = cleanVec(p.pos + off);
        }
        for (Light& l : curL) {
            for (int a = 0; a < 3; ++a)
                if (spec.rotate[a] != 0) {
                    const Mat3 r = axisRotation(a, spec.rotate[a]);
                    l.pos = pivot + r * (l.pos - pivot);
                    if (l.hasDirection()) rotateLight(l, a, spec.rotate[a]);
                }
            l.pos = cleanVec(l.pos + off);
        }
        moved += off;
        ReplicateCopy c;
        for (const Brush& b : brushes) {
            Brush nb = b;
            nb.translate(moved, true);
            c.brushes.push_back(std::move(nb));
        }
        c.prims = curP;
        c.lights = curL;
        out.push_back(std::move(c));
        if (spec.pivot == ReplicateSpec::Pivot::Selection) pivot += off;
        grow = Vec3{grow.x * s.x, grow.y * s.y, grow.z * s.z};
    }
    return out;
}

} // namespace scal
