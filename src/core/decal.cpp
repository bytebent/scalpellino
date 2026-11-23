#include "core/decal.hpp"
#include "core/brush.hpp"
#include "core/terrain.hpp"
#include <algorithm>
#include <cmath>

namespace scal {

bool Decal::operator==(const Decal& o) const {
    auto same = [](const Vec3& a, const Vec3& b) { return length(a - b) < 1e-9; };
    return id == o.id && material == o.material && same(pos, o.pos) && same(normal, o.normal) && same(up, o.up) &&
           width == o.width && height == o.height && reach == o.reach && order == o.order && visgroup == o.visgroup;
}

Vec3 decalDefaultUp(const Vec3& n) {
    const Vec3 nn = normalize(n);
    const Vec3 w = std::fabs(nn.z) > 0.7 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
    const Vec3 u = normalize(w - nn * dot(w, nn));
    return length(u) > 0.5 ? u : Vec3{1, 0, 0};
}

void decalFrame(const Decal& d, Vec3& right, Vec3& up, Vec3& n) {
    n = normalize(d.normal);
    if (length(n) < 0.5) n = {0, 0, 1};
    up = d.up - n * dot(d.up, n);
    up = length(up) > 1e-9 ? normalize(up) : decalDefaultUp(n);
    right = cross(up, n);
}

double decalRotation(const Decal& d) {
    Vec3 r, u, n;
    decalFrame(d, r, u, n);
    const Vec3 u0 = decalDefaultUp(n);
    const Vec3 r0 = cross(u0, n);
    // counter-clockwise seen from the front: from up0 towards -right0
    double deg = std::atan2(-dot(u, r0), dot(u, u0)) * 180.0 / 3.14159265358979323846;
    deg = cleanNumber(deg, 1e-7);
    if (deg <= -180) deg += 360;
    return deg == -0.0 ? 0.0 : deg;
}

void setDecalRotation(Decal& d, double degrees) {
    Vec3 r, u, n;
    decalFrame(d, r, u, n);
    const Vec3 u0 = decalDefaultUp(n);
    d.up = cleanVec(rotateVec(u0, n, degrees * 3.14159265358979323846 / 180.0));
}

std::array<Vec3, 4> decalCorners(const Decal& d) {
    Vec3 r, u, n;
    decalFrame(d, r, u, n);
    const Vec3 hr = r * (d.width / 2), hu = u * (d.height / 2);
    return {d.pos - hr + hu, d.pos + hr + hu, d.pos + hr - hu, d.pos - hr - hu};
}

AABB decalBounds(const Decal& d) {
    Vec3 r, u, n;
    decalFrame(d, r, u, n);
    AABB b;
    for (const Vec3& c : decalCorners(d)) {
        b.add(c + n * d.reach);
        b.add(c - n * d.reach);
    }
    return b;
}

void decalSizeFromPicture(Decal& d, int texW, int texH, double decalScale) {
    if (!(decalScale > 0)) decalScale = 1;
    d.width = std::max(1, texW) * decalScale * kDecalUnitsPerTexel;
    d.height = std::max(1, texH) * decalScale * kDecalUnitsPerTexel;
}

std::vector<DecalVert> clipToDecal(const Decal& d, const std::vector<DecalVert>& poly) {
    Vec3 r, u, n;
    decalFrame(d, r, u, n);
    // the box's 6 sides: keep where dot(p - pos, axis) <= half
    const Vec3 axes[6] = {r, r * -1, u, u * -1, n, n * -1};
    const double half[6] = {d.width / 2, d.width / 2, d.height / 2, d.height / 2, d.reach, d.reach};
    std::vector<DecalVert> cur = poly, next;
    for (int s = 0; s < 6 && cur.size() >= 3; ++s) {
        next.clear();
        auto dist = [&](const DecalVert& v) { return dot(v.p - d.pos, axes[s]) - half[s]; };
        for (size_t i = 0; i < cur.size(); ++i) {
            const DecalVert& a = cur[i];
            const DecalVert& b = cur[(i + 1) % cur.size()];
            const double da = dist(a), db = dist(b);
            if (da <= 1e-9) next.push_back(a);
            if ((da < -1e-9 && db > 1e-9) || (da > 1e-9 && db < -1e-9)) {
                const double t = da / (da - db);
                DecalVert m;
                m.p = a.p + (b.p - a.p) * t;
                for (size_t k = 0; k < m.a.size(); ++k) m.a[k] = (float)(a.a[k] + (b.a[k] - a.a[k]) * t);
                next.push_back(m);
            }
        }
        cur.swap(next);
    }
    if (cur.size() < 3) cur.clear();
    return cur;
}

void decalUV(const Decal& d, const Vec3& p, double& u, double& v) {
    Vec3 r, up, n;
    decalFrame(d, r, up, n);
    u = dot(p - d.pos, r) / std::max(d.width, 1e-9) + 0.5;
    v = 0.5 - dot(p - d.pos, up) / std::max(d.height, 1e-9);
}

bool decalTakes(const Decal& d, const Vec3& n) { return dot(normalize(n), normalize(d.normal)) >= kDecalMinFacing; }

bool decalDrawnAt(const Decal& d, const Vec3& p, const Vec3& n) {
    if (!decalTakes(d, n)) return false;
    if (std::fabs(dot(p - d.pos, normalize(d.normal))) > d.reach + 1e-3) return false;
    double u = 0, v = 0;
    decalUV(d, p, u, v);
    constexpr double e = 1e-6;
    return u >= -e && u <= 1 + e && v >= -e && v <= 1 + e;
}

namespace {
bool overlap(const AABB& a, const AABB& b) {
    for (int k = 0; k < 3; ++k)
        if (a.max[k] < b.min[k] || b.max[k] < a.min[k]) return false;
    return true;
}
void addPiece(const Decal& d, const std::vector<Vec3>& poly, const Vec3& n, std::vector<DecalTri>& out) {
    std::vector<DecalVert> in;
    in.reserve(poly.size());
    for (const Vec3& p : poly) in.push_back({p, {}});
    const std::vector<DecalVert> c = clipToDecal(d, in);
    for (size_t i = 1; i + 1 < c.size(); ++i) {
        DecalTri t;
        const DecalVert* v[3] = {&c[0], &c[i], &c[i + 1]};
        for (int k = 0; k < 3; ++k) {
            t.p[k] = v[k]->p;
            decalUV(d, v[k]->p, t.u[k], t.v[k]);
        }
        t.n = n;
        out.push_back(t);
    }
}
} // namespace

std::vector<DecalTri> decalMesh(const Decal& d, const std::vector<Brush>& brushes, const std::function<bool(const Brush&)>& visible) {
    const AABB box = decalBounds(d);
    std::vector<const Brush*> near;
    for (const Brush& b : brushes) {
        if (visible && !visible(b)) continue;
        if (overlap(box, b.bounds())) near.push_back(&b);
    }
    return decalMeshOn(d, near);
}

std::vector<DecalTri> decalMeshOn(const Decal& d, const std::vector<const Brush*>& brushes) {
    std::vector<DecalTri> out;
    const AABB box = decalBounds(d);
    for (const Brush* bp : brushes) {
        const Brush& b = *bp;
        std::optional<TerrainGrid> g;
        if (b.terrain) g = terrainGrid(b);
        for (const Polygon& p : b.polygons()) {
            if (g && p.face == g->face) continue; // drawn as the terrain below
            if (decalTakes(d, p.normal)) addPiece(d, p.verts, p.normal, out);
        }
        if (!g) continue;
        for (int j = 0; j + 1 < g->n; ++j)
            for (int i = 0; i + 1 < g->n; ++i) {
                const Vec3 a = g->at(i, j), b1 = g->at(i + 1, j), c = g->at(i + 1, j + 1), e = g->at(i, j + 1);
                AABB cell;
                for (const Vec3& q : {a, b1, c, e}) cell.add(q);
                if (!overlap(box, cell)) continue;
                for (const auto& tri : {std::array<Vec3, 3>{a, b1, c}, std::array<Vec3, 3>{a, c, e}}) {
                    Vec3 n = normalize(cross(tri[1] - tri[0], tri[2] - tri[0]));
                    if (dot(n, g->normal) < 0) n = n * -1;
                    if (decalTakes(d, n)) {
                        // the triangle wound to face out
                        std::vector<Vec3> poly{tri[0], tri[1], tri[2]};
                        if (dot(cross(tri[1] - tri[0], tri[2] - tri[0]), n) < 0) std::swap(poly[1], poly[2]);
                        addPiece(d, poly, n, out);
                    }
                }
            }
    }
    return out;
}

void mapDecal(Decal& d, const std::function<Vec3(const Vec3&)>& f) {
    Vec3 r, u, n;
    decalFrame(d, r, u, n);
    const Vec3 o = f(d.pos);
    const Vec3 R = f(d.pos + r * (d.width / 2)) - o;
    const Vec3 U = f(d.pos + u * (d.height / 2)) - o;
    // the normal: square to the mapped rectangle, on the side the mapped surface faces
    // (a point in front stays in front)
    Vec3 nn = normalize(cross(R, U));
    const Vec3 F = f(d.pos + n) - o;
    if (dot(nn, F) < 0) nn = nn * -1;
    if (length(nn) < 0.5) return; // squashed flat: leave it
    const Vec3 upN = normalize(U - nn * dot(U, nn));
    d.pos = cleanVec(o);
    d.normal = cleanVec(nn);
    d.up = cleanVec(length(upN) > 0.5 ? upN : decalDefaultUp(nn));
    d.width = cleanNumber(2 * length(R - nn * dot(R, nn)), 1e-7);
    d.height = cleanNumber(2 * length(U - nn * dot(U, nn)), 1e-7);
    const double reachScale = length(F);
    if (reachScale > 0) d.reach = cleanNumber(d.reach * std::fabs(dot(F, nn)), 1e-7);
}

double rayHitDecal(const Decal& d, const Vec3& origin, const Vec3& dir) {
    Vec3 r, u, n;
    decalFrame(d, r, u, n);
    const double den = dot(dir, n);
    if (std::fabs(den) < 1e-9) return -1;
    const double t = dot(d.pos - origin, n) / den;
    if (t < 0) return -1;
    const Vec3 p = origin + dir * t - d.pos;
    if (std::fabs(dot(p, r)) > d.width / 2 || std::fabs(dot(p, u)) > d.height / 2) return -1;
    return t;
}

} // namespace scal
