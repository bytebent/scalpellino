#include "core/primitives.hpp"
#include "core/hull.hpp"
#include "core/csg.hpp"
#include <array>
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

namespace {
// Brush from faces given as 3 points each (Quake style), oriented away from `inside`.
// Round shapes are built this way: fast (no hull search) and with rounding only the
// plane points move, so the solid stays exactly convex.
std::optional<Brush> fromTriples(const std::vector<std::array<Vec3, 3>>& tris, const Vec3& inside,
                                 const std::string& texture, double texScale) {
    Brush br;
    for (const auto& t : tris) {
        if (length(cross(t[1] - t[0], t[2] - t[0])) < 1e-9) continue; // degenerate after rounding
        const Vec3 mid = (t[0] + t[1] + t[2]) / 3.0;
        Face f = faceFromPoints(t[0], t[1], t[2], mid - inside, texture);
        f.scaleU = f.scaleV = texScale;
        br.faces.push_back(f);
    }
    removeUnusedFaces(br);
    if (!br.valid()) return std::nullopt;
    return br;
}
} // namespace

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
        case Primitive::Cylinder: {
            std::vector<std::array<Vec3, 3>> tris;
            auto R = [&](Vec3 p) { if (roundToWhole) p = {std::round(p.x), std::round(p.y), std::round(p.z)}; return p; };
            for (int i = 0; i < n; ++i) {
                const double a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
                tris.push_back({R(ringPoint(a0, b.min[axis], 1)), R(ringPoint(a1, b.min[axis], 1)), R(ringPoint(a0, b.max[axis], 1))});
            }
            tris.push_back({R(ringPoint(0, b.min[axis], 1)), R(ringPoint(2.1, b.min[axis], 1)), R(ringPoint(4.2, b.min[axis], 1))});
            tris.push_back({R(ringPoint(0, b.max[axis], 1)), R(ringPoint(2.1, b.max[axis], 1)), R(ringPoint(4.2, b.max[axis], 1))});
            return fromTriples(tris, c, texture, texScale);
        }
        case Primitive::Cone: {
            std::vector<std::array<Vec3, 3>> tris;
            auto R = [&](Vec3 p) { if (roundToWhole) p = {std::round(p.x), std::round(p.y), std::round(p.z)}; return p; };
            Vec3 tip = c;
            tip[axis] = b.max[axis];
            for (int i = 0; i < n; ++i) {
                const double a0 = 2 * kPi * i / n, a1 = 2 * kPi * (i + 1) / n;
                tris.push_back({R(ringPoint(a0, b.min[axis], 1)), R(ringPoint(a1, b.min[axis], 1)), R(tip)});
            }
            tris.push_back({R(ringPoint(0, b.min[axis], 1)), R(ringPoint(2.1, b.min[axis], 1)), R(ringPoint(4.2, b.min[axis], 1))});
            Vec3 inside = c;
            inside[axis] = b.min[axis] + s[axis] / 4;
            return fromTriples(tris, inside, texture, texScale);
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
            // Rings of latitude between the poles; one face per cell, built from planes.
            const int rings = std::max(2, n / 2);
            auto R = [&](Vec3 p) { if (roundToWhole) p = {std::round(p.x), std::round(p.y), std::round(p.z)}; return p; };
            auto P = [&](int r, int i) { // r = 0 (bottom pole) .. rings (top pole)
                const double lat = -kPi / 2 + kPi * r / rings;
                const double h = c[axis] + std::sin(lat) * s[axis] / 2;
                if (r == 0 || r == rings) { Vec3 p = c; p[axis] = r == 0 ? b.min[axis] : b.max[axis]; return R(p); }
                return R(ringPoint(2 * kPi * (i % n) / n, h, std::cos(lat)));
            };
            std::vector<std::array<Vec3, 3>> tris;
            for (int r = 0; r < rings; ++r)
                for (int i = 0; i < n; ++i) {
                    if (r == 0) tris.push_back({P(0, i), P(1, i), P(1, i + 1)});
                    else if (r == rings - 1) tris.push_back({P(r, i), P(r, i + 1), P(rings, i)});
                    else tris.push_back({P(r, i), P(r, i + 1), P(r + 1, i)});
                }
            return fromTriples(tris, c, texture, texScale);
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
