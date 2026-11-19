#include "core/model.hpp"
#include <algorithm>
#include <cmath>

namespace scal {

AABB placeholderBounds() {
    AABB b;
    b.add({-32, -32, 0});
    b.add({32, 32, 64});
    return b;
}

bool rayHitBox(const AABB& b, const Vec3& o, const Vec3& d, double& tOut, Vec3& n) {
    double t0 = 0, t1 = 1e300;
    int axis = -1;
    double side = 0;
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) < 1e-12) {
            if (o[a] < b.min[a] || o[a] > b.max[a]) return false;
            continue;
        }
        double ta = (b.min[a] - o[a]) / d[a], tb = (b.max[a] - o[a]) / d[a];
        double s = -1;
        if (ta > tb) { std::swap(ta, tb); s = 1; }
        if (ta > t0) { t0 = ta; axis = a; side = s; }
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    tOut = t0;
    n = {};
    if (axis >= 0) n[axis] = side;
    else n = normalize(-d);
    return true;
}

bool rayHitModel(const Model& m, const Vec3& o, const Vec3& d, double& tOut, Vec3& nOut) {
    double bt;
    Vec3 bn;
    if (!m.bounds.valid() || !rayHitBox(m.bounds, o, d, bt, bn)) {
        // The ray may start inside the box (rayHitBox then gives t = 0): that is a hit of the
        // box, handled by the slab test; a miss really is a miss.
        return false;
    }
    double best = 1e300;
    Vec3 bestN;
    for (const ModelMesh& mesh : m.meshes) {
        const float* P = mesh.pos.data();
        for (size_t i = 0; i + 2 < mesh.index.size(); i += 3) {
            const float* a = P + 3 * (size_t)mesh.index[i];
            const float* b = P + 3 * (size_t)mesh.index[i + 1];
            const float* c = P + 3 * (size_t)mesh.index[i + 2];
            const Vec3 va{a[0], a[1], a[2]}, vb{b[0], b[1], b[2]}, vc{c[0], c[1], c[2]};
            const Vec3 e1 = vb - va, e2 = vc - va;
            const Vec3 p = cross(d, e2);
            const double det = dot(e1, p);
            if (std::fabs(det) < 1e-18) continue;
            const double inv = 1.0 / det;
            const Vec3 s = o - va;
            const double u = dot(s, p) * inv;
            if (u < 0 || u > 1) continue;
            const Vec3 q = cross(s, e1);
            const double v = dot(d, q) * inv;
            if (v < 0 || u + v > 1) continue;
            const double t = dot(e2, q) * inv;
            if (t < 0 || t >= best) continue;
            best = t;
            bestN = cross(e1, e2);
        }
    }
    if (best >= 1e300) return false;
    tOut = best;
    nOut = normalize(bestN);
    if (dot(nOut, d) > 0) nOut = -nOut;
    return true;
}

} // namespace scal
