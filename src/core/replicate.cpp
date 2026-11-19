#include "core/replicate.hpp"
#include <algorithm>
#include <cmath>

namespace scal {

std::vector<ReplicateCopy> replicate(const std::vector<Brush>& brushes, const std::vector<MathPrim>& prims,
                                     const std::vector<Light>& lights, const ReplicateSpec& spec,
                                     const std::vector<Prop>& props, const ModelFinder& models) {
    std::vector<ReplicateCopy> out;
    if (spec.copies < 1 || (brushes.empty() && prims.empty() && lights.empty() && props.empty())) return out;
    AABB box;
    for (const Brush& b : brushes) { const AABB bb = b.bounds(); box.add(bb.min); box.add(bb.max); }
    for (const MathPrim& p : prims) { const AABB pb = primBounds(p); box.add(pb.min); box.add(pb.max); }
    for (const Light& l : lights) box.add(l.pos);
    for (const Prop& p : props) { const AABB pb = propBounds(p, models ? models(p.model) : nullptr); box.add(pb.min); box.add(pb.max); }
    Vec3 pivot = spec.pivot == ReplicateSpec::Pivot::Selection ? box.center()
                 : spec.pivot == ReplicateSpec::Pivot::Origin ? Vec3{}
                                                             : spec.point;
    const Vec3 s{spec.scale.x / 100, spec.scale.y / 100, spec.scale.z / 100};
    Vec3 grow{1, 1, 1};   // scale so far (for the offset)
    Vec3 moved;           // brushes: total offset so far
    std::vector<MathPrim> curP = prims;
    std::vector<Light> curL = lights;
    std::vector<Prop> curQ = props;
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
        for (Prop& p : curQ) {
            p.pos = pivot + Vec3{(p.pos.x - pivot.x) * s.x, (p.pos.y - pivot.y) * s.y, (p.pos.z - pivot.z) * s.z};
            p.scale = Vec3{p.scale.x * s.x, p.scale.y * s.y, p.scale.z * s.z};
            for (int a = 0; a < 3; ++a)
                if (spec.rotate[a] != 0) rotateProp(p, a, spec.rotate[a], pivot);
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
        c.props = curQ;
        out.push_back(std::move(c));
        if (spec.pivot == ReplicateSpec::Pivot::Selection) pivot += off;
        grow = Vec3{grow.x * s.x, grow.y * s.y, grow.z * s.z};
    }
    return out;
}

namespace {

AABB selectionBox(const std::vector<Brush>& brushes, const std::vector<MathPrim>& prims, const std::vector<Light>& lights,
                  const std::vector<Prop>& props, const ModelFinder& models) {
    AABB box;
    for (const Brush& b : brushes) { const AABB bb = b.bounds(); box.add(bb.min); box.add(bb.max); }
    for (const MathPrim& p : prims) { const AABB pb = primBounds(p); box.add(pb.min); box.add(pb.max); }
    for (const Light& l : lights) box.add(l.pos);
    for (const Prop& p : props) { const AABB pb = propBounds(p, models ? models(p.model) : nullptr); box.add(pb.min); box.add(pb.max); }
    return box;
}

// SplitMix64: tiny, and the same numbers on every compiler and platform (the standard
// library's distributions are not), so a saved seed gives the same scatter everywhere.
struct SplitMix {
    std::uint64_t s;
    std::uint64_t next() {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double unit() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); } // [0, 1)
    double sym() { return 2.0 * unit() - 1.0; }                                     // [-1, 1)
};

} // namespace

std::vector<ReplicateCopy> randomReplicate(const std::vector<Brush>& brushes, const std::vector<MathPrim>& prims,
                                           const std::vector<Light>& lights, const RandomReplicateSpec& spec,
                                           const std::vector<Prop>& props, const ModelFinder& models) {
    std::vector<ReplicateCopy> out;
    if (spec.copies < 1 || (brushes.empty() && prims.empty() && lights.empty() && props.empty())) return out;
    const Vec3 pivot = selectionBox(brushes, prims, lights, props, models).center();
    double lo = std::max(1.0, std::min(spec.sizeMin, spec.sizeMax)), hi = std::max(1.0, std::max(spec.sizeMin, spec.sizeMax));
    out.reserve(spec.copies);
    for (int i = 0; i < spec.copies; ++i) {
        SplitMix rng{((std::uint64_t)spec.seed << 32) ^ (std::uint64_t)(std::uint32_t)i};
        rng.next(); // stir: neighbouring seeds and copies start far apart
        // Always nine draws in this order, used or not, so ranges don't affect each other.
        double d[9];
        for (double& v : d) v = rng.sym();
        Vec3 off{d[0] * spec.spread.x, d[1] * spec.spread.y, d[2] * spec.spread.z};
        const Vec3 turn{d[3] * spec.turn.x, d[4] * spec.turn.y, d[5] * spec.turn.z};
        auto size = [&](double r) { return (lo + (r + 1.0) * 0.5 * (hi - lo)) / 100.0; };
        const Vec3 s = spec.uniform ? Vec3{size(d[6]), size(d[6]), size(d[6])} : Vec3{size(d[6]), size(d[7]), size(d[8])};
        const double smin = std::min({s.x, s.y, s.z});
        off = cleanVec(off);

        ReplicateCopy c;
        Vec3 brushOff = off;
        if (spec.brushGrid > 0)
            for (int a = 0; a < 3; ++a) brushOff[a] = std::round(off[a] / spec.brushGrid) * spec.brushGrid;
        for (const Brush& b : brushes) {
            Brush nb = b;
            nb.translate(cleanVec(brushOff), true);
            c.brushes.push_back(std::move(nb));
        }
        for (MathPrim p : prims) {
            // Spacing about the centre grows with the size; the shape grows along its own axes.
            p.pos = pivot + Vec3{(p.pos.x - pivot.x) * s.x, (p.pos.y - pivot.y) * s.y, (p.pos.z - pivot.z) * s.z};
            p.size = Vec3{p.size.x * s.x, p.size.y * s.y, p.size.z * s.z};
            p.round *= smin;
            p.blend *= smin;
            for (int a = 0; a < 3; ++a)
                if (turn[a] != 0) rotatePrim(p, a, turn[a], pivot);
            p.pos = cleanVec(p.pos + off);
            c.prims.push_back(std::move(p));
        }
        for (Prop p : props) {
            p.pos = pivot + Vec3{(p.pos.x - pivot.x) * s.x, (p.pos.y - pivot.y) * s.y, (p.pos.z - pivot.z) * s.z};
            p.scale = Vec3{p.scale.x * s.x, p.scale.y * s.y, p.scale.z * s.z};
            for (int a = 0; a < 3; ++a)
                if (turn[a] != 0) rotateProp(p, a, turn[a], pivot);
            p.pos = cleanVec(p.pos + off);
            c.props.push_back(std::move(p));
        }
        for (Light l : lights) {
            for (int a = 0; a < 3; ++a)
                if (turn[a] != 0) {
                    l.pos = pivot + axisRotation(a, turn[a]) * (l.pos - pivot);
                    if (l.hasDirection()) rotateLight(l, a, turn[a]);
                }
            l.pos = cleanVec(l.pos + off);
            c.lights.push_back(std::move(l));
        }
        out.push_back(std::move(c));
    }
    return out;
}

} // namespace scal
