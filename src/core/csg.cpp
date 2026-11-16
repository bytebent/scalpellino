#include "core/csg.hpp"
#include <algorithm>
#include <cmath>

namespace scal {

namespace {
constexpr double kSliver = 1e-3; // pieces thinner than this are dropped

// True if the brush is a real solid (not a sliver).
bool solid(const Brush& b) {
    if (!b.valid()) return false;
    const Vec3 s = b.bounds().size();
    if (std::min({s.x, s.y, s.z}) < kSliver) return false;
    return brushVolume(b) > kSliver;
}

Face capFace(const Plane& pl, const Vec3& a, const Vec3& b, const Vec3& c, const std::string& tex) {
    return faceFromPoints(a, b, c, pl.n, tex);
}

Plane flipped(const Plane& p) { return {-p.n, -p.d}; }
} // namespace

double brushVolume(const Brush& b) {
    double v = 0;
    for (const Polygon& p : b.polygons())
        for (size_t i = 1; i + 1 < p.verts.size(); ++i)
            v += dot(p.verts[0], cross(p.verts[i], p.verts[i + 1]));
    return v / 6.0;
}

void removeUnusedFaces(Brush& b) {
    const auto polys = b.polygons();
    std::vector<Face> keep;
    for (size_t i = 0; i < b.faces.size(); ++i) {
        bool used = false;
        for (const Polygon& p : polys) used = used || p.face == (int)i;
        if (used) keep.push_back(b.faces[i]);
    }
    b.faces = std::move(keep);
}

Face faceFromPoints(Vec3 a, Vec3 b, Vec3 c, const Vec3& outward, const std::string& texture) {
    // Convention: normal = normalize(cross(c - a, b - a)).
    if (dot(cross(c - a, b - a), outward) < 0) std::swap(b, c);
    Face f;
    f.p[0] = a; f.p[1] = b; f.p[2] = c;
    f.texture = texture;
    defaultTexAxes(f.plane().n, f.u.axis, f.v.axis);
    return f;
}

SplitResult splitBrush(const Brush& b, const Vec3& a, const Vec3& bp, const Vec3& c, const std::string& capTexture) {
    SplitResult r;
    const Plane pl = Plane::fromPoints(a, bp, c);
    if (length(pl.n) < 0.5) return r; // degenerate (collinear) points

    // Is the brush entirely on one side?
    double mn = 1e300, mx = -1e300;
    for (const Polygon& p : b.polygons())
        for (const Vec3& v : p.verts) { const double d = pl.distance(v); mn = std::min(mn, d); mx = std::max(mx, d); }
    if (mx <= kSliver) { r.back = b; return r; }
    if (mn >= -kSliver) { r.front = b; return r; }

    // Back part: keep where distance <= 0 -> add a face with outward normal pl.n.
    Brush back = b;
    back.faces.push_back(capFace(pl, a, bp, c, capTexture));
    // Front part: outward normal -pl.n.
    Brush front = b;
    front.faces.push_back(capFace(flipped(pl), a, bp, c, capTexture));
    removeUnusedFaces(back);
    removeUnusedFaces(front);
    if (solid(back)) r.back = std::move(back);
    if (solid(front)) r.front = std::move(front);
    return r;
}

std::vector<Brush> carveBrush(const Brush& b, const Brush& cutter) {
    // Quick reject on bounds.
    const AABB bb = b.bounds(), cb = cutter.bounds();
    for (int a = 0; a < 3; ++a)
        if (bb.max[a] <= cb.min[a] + kSliver || cb.max[a] <= bb.min[a] + kSliver) return {b};

    std::vector<Brush> pieces;
    Brush rest = b;
    bool touched = false;
    for (const Face& cf : cutter.faces) {
        // Outside of this cutter face = in front of its plane: that part survives.
        const SplitResult s = splitBrush(rest, cf.p[0], cf.p[1], cf.p[2], cf.texture);
        if (s.front && s.back) {
            pieces.push_back(*s.front);
            rest = *s.back;
            touched = true;
        } else if (s.front && !s.back) {
            return {b}; // b lies fully outside this cutter plane: no overlap at all
        } else if (!s.back) {
            return {b};
        }
    }
    // Whatever is left is inside the cutter and is removed.
    (void)touched;
    return pieces;
}

namespace {
bool intersection(const Brush& a, const Brush& b, Brush& out) {
    out.faces = a.faces;
    out.faces.insert(out.faces.end(), b.faces.begin(), b.faces.end());
    removeUnusedFaces(out);
    return solid(out);
}
} // namespace

double overlapVolume(const Brush& a, const Brush& b) {
    const AABB ab = a.bounds(), bb = b.bounds();
    for (int k = 0; k < 3; ++k)
        if (ab.max[k] <= bb.min[k] + kSliver || bb.max[k] <= ab.min[k] + kSliver) return 0;
    Brush i;
    return intersection(a, b, i) ? brushVolume(i) : 0;
}

std::vector<Overlap> findOverlaps(const std::vector<Brush>& brushes, double minVolume) {
    std::vector<AABB> boxes;
    boxes.reserve(brushes.size());
    for (const Brush& b : brushes) boxes.push_back(b.bounds());
    std::vector<Overlap> out;
    for (size_t i = 0; i < brushes.size(); ++i)
        for (size_t j = i + 1; j < brushes.size(); ++j) {
            bool apart = false;
            for (int k = 0; k < 3 && !apart; ++k)
                apart = boxes[i].max[k] <= boxes[j].min[k] + kSliver || boxes[j].max[k] <= boxes[i].min[k] + kSliver;
            if (apart) continue;
            Brush x;
            if (!intersection(brushes[i], brushes[j], x)) continue;
            const double v = brushVolume(x);
            if (v > minVolume) out.push_back({brushes[i].id, brushes[j].id, v, x.bounds()});
        }
    return out;
}

std::vector<Brush> hollowBrush(const Brush& b, double t, std::string& error) {
    error.clear();
    if (t <= 0) { error = "wall thickness must be positive"; return {}; }
    // Inner brush: every face moved inward by t.
    Brush inner = b;
    for (Face& f : inner.faces) {
        const Vec3 n = f.plane().n;
        for (Vec3& p : f.p) p = p - n * t;
    }
    if (!solid(inner)) { error = "the brush is too thin for that wall thickness"; return {}; }
    std::vector<Brush> walls = carveBrush(b, inner);
    if (walls.empty()) { error = "hollowing failed"; return {}; }
    return walls;
}

} // namespace scal
