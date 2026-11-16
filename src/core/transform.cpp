#include "core/transform.hpp"
#include <cmath>

namespace scal {

std::vector<Brush> applyTransform(const std::vector<Brush>& in, const TransformSpec& t,
                                  bool textureLock, std::string& error) {
    error.clear();
    std::vector<Brush> out = in;
    switch (t.kind) {
        case TransformKind::Move:
            for (Brush& b : out) b.translate(t.move, textureLock);
            break;
        case TransformKind::Rotate: {
            if (t.axis < 0 || t.axis > 2) { error = "invalid axis"; return {}; }
            Vec3 axis;
            axis[t.axis] = 1;
            const double rad = t.degrees * 3.14159265358979323846 / 180.0;
            for (Brush& b : out) b.rotate(t.pivot, axis, rad, textureLock);
            break;
        }
        case TransformKind::Scale: {
            for (int a = 0; a < 3; ++a)
                if (t.scale[a] == 0 || !std::isfinite(t.scale[a])) { error = "scale factors cannot be 0"; return {}; }
            for (Brush& b : out) {
                const AABB from = b.bounds();
                AABB to;
                for (int a = 0; a < 3; ++a) {
                    // Exact affine map p -> pivot + (p - pivot) * s; min/max may swap (mirror).
                    to.min[a] = t.pivot[a] + (from.min[a] - t.pivot[a]) * t.scale[a];
                    to.max[a] = t.pivot[a] + (from.max[a] - t.pivot[a]) * t.scale[a];
                }
                b.remapBounds(from, to, textureLock);
            }
            break;
        }
    }
    for (const Brush& b : out)
        if (!b.valid()) { error = "the result would not be a valid solid"; return {}; }
    return out;
}

} // namespace scal
