#include "core/primitives.hpp"
#include "core/hull.hpp"
#include <cmath>
#include <vector>

namespace scal {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

const char* primitiveName(Primitive p) {
    switch (p) {
        case Primitive::Box: return "Box";
        case Primitive::Wedge: return "Wedge";
        case Primitive::Cylinder: return "Cylinder";
        case Primitive::Cone: return "Cone";
        case Primitive::Pyramid: return "Pyramid";
        case Primitive::Sphere: return "Sphere";
    }
    return "Box";
}

std::optional<Brush> makePrimitive(Primitive kind, const AABB& b, int axis, int slopeAxis, int sides,
                                   bool roundToWhole, const std::string& texture, double texScale) {
    if (!b.valid()) return std::nullopt;
    const Vec3 s = b.size();
    if (s.x <= 0 || s.y <= 0 || s.z <= 0) return std::nullopt;
    if (kind == Primitive::Box) {
        Brush br = makeBox(b, texture);
        for (Face& f : br.faces) f.scaleU = f.scaleV = texScale;
        return br;
    }
    // The two axes across `axis`.
    const int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
    const Vec3 c = b.center();
    std::vector<Vec3> pts;
    auto add = [&](Vec3 p) {
        if (roundToWhole) p = {std::round(p.x), std::round(p.y), std::round(p.z)};
        pts.push_back(p);
    };
    auto ringPoint = [&](double ang, double h, double scale) {
        Vec3 p;
        p[a1] = c[a1] + std::cos(ang) * s[a1] / 2 * scale;
        p[a2] = c[a2] + std::sin(ang) * s[a2] / 2 * scale;
        p[axis] = h;
        return p;
    };
    const int n = std::max(3, sides);
    switch (kind) {
        case Primitive::Wedge: {
            // Triangle across (slopeAxis, axis): full height at the min side of slopeAxis,
            // zero at the max side; straight along the third axis.
            const int t = 3 - axis - slopeAxis;
            if (t < 0 || t > 2 || axis == slopeAxis) return std::nullopt;
            for (double tv : {b.min[t], b.max[t]}) {
                Vec3 p;
                p[t] = tv;
                p[slopeAxis] = b.min[slopeAxis]; p[axis] = b.min[axis]; add(p);
                p[slopeAxis] = b.max[slopeAxis]; p[axis] = b.min[axis]; add(p);
                p[slopeAxis] = b.min[slopeAxis]; p[axis] = b.max[axis]; add(p);
            }
            break;
        }
        case Primitive::Cylinder:
            for (int i = 0; i < n; ++i) {
                const double ang = 2 * kPi * i / n;
                add(ringPoint(ang, b.min[axis], 1));
                add(ringPoint(ang, b.max[axis], 1));
            }
            break;
        case Primitive::Cone: {
            for (int i = 0; i < n; ++i) add(ringPoint(2 * kPi * i / n, b.min[axis], 1));
            Vec3 tip = c;
            tip[axis] = b.max[axis];
            add(tip);
            break;
        }
        case Primitive::Pyramid: {
            for (double u : {b.min[a1], b.max[a1]})
                for (double v : {b.min[a2], b.max[a2]}) {
                    Vec3 p; p[a1] = u; p[a2] = v; p[axis] = b.min[axis]; add(p);
                }
            Vec3 tip = c;
            tip[axis] = b.max[axis];
            add(tip);
            break;
        }
        case Primitive::Sphere: {
            // Rings of latitude between the poles; `sides` around, sides/2 rings.
            const int rings = std::max(2, n / 2);
            Vec3 lo = c, hi = c;
            lo[axis] = b.min[axis];
            hi[axis] = b.max[axis];
            add(lo);
            add(hi);
            for (int r = 1; r < rings; ++r) {
                const double lat = -kPi / 2 + kPi * r / rings;
                const double h = c[axis] + std::sin(lat) * s[axis] / 2;
                for (int i = 0; i < n; ++i) add(ringPoint(2 * kPi * i / n, h, std::cos(lat)));
            }
            break;
        }
        case Primitive::Box: break;
    }
    Brush tmpl;
    HullResult h = brushFromVertices(pts, tmpl, false); // rounding may merge a few points
    if (!h.ok) return std::nullopt;
    for (Face& f : h.brush.faces) {
        f.texture = texture;
        f.scaleU = f.scaleV = texScale;
        defaultTexAxes(f.plane().n, f.u.axis, f.v.axis);
        f.u.offset = f.v.offset = 0;
        f.rotation = 0;
    }
    h.brush.id = 0;
    return h.brush;
}

} // namespace scal
