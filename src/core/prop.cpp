#include "core/prop.hpp"
#include <algorithm>
#include <cmath>

namespace scal {

bool Prop::operator==(const Prop& o) const {
    return id == o.id && model == o.model && pos == o.pos && rot == o.rot && scale == o.scale &&
           otherKeys == o.otherKeys;
}

AABB propLocalBounds(const Model* m) {
    if (m && m->bounds.valid()) return m->bounds;
    return placeholderBounds();
}

Vec3 propToWorld(const Prop& p, const Vec3& l) {
    return p.pos + rotationMatrix(p.rot) * Vec3{l.x * p.scale.x, l.y * p.scale.y, l.z * p.scale.z};
}

Vec3 propToLocal(const Prop& p, const Vec3& w) {
    const Vec3 r = rotationMatrix(p.rot).transposeMul(w - p.pos);
    auto div = [](double v, double s) { return std::fabs(s) > 1e-12 ? v / s : 0.0; };
    return {div(r.x, p.scale.x), div(r.y, p.scale.y), div(r.z, p.scale.z)};
}

std::vector<Vec3> propCorners(const Prop& p, const Model* m) {
    const AABB b = propLocalBounds(m);
    std::vector<Vec3> out;
    for (int i = 0; i < 8; ++i)
        out.push_back(propToWorld(p, {i & 1 ? b.max.x : b.min.x, i & 2 ? b.max.y : b.min.y, i & 4 ? b.max.z : b.min.z}));
    return out;
}

AABB propBounds(const Prop& p, const Model* m) {
    AABB b;
    for (const Vec3& c : propCorners(p, m)) b.add(c);
    return b;
}

std::vector<Vec3> propSurfacePoints(const Prop& p, const Model* m, size_t limit) {
    std::vector<Vec3> out;
    if (!m || m->meshes.empty()) return propCorners(p, nullptr);
    size_t total = 0;
    for (const ModelMesh& mesh : m->meshes) total += mesh.vertices();
    const size_t stride = std::max<size_t>(1, total / std::max<size_t>(limit, 1));
    size_t n = 0;
    for (const ModelMesh& mesh : m->meshes)
        for (size_t i = 0; i < mesh.vertices(); ++i, ++n)
            if (n % stride == 0)
                out.push_back(propToWorld(p, {mesh.pos[3 * i], mesh.pos[3 * i + 1], mesh.pos[3 * i + 2]}));
    // The corners of the box too, so thin or sparse models still have their extremes.
    for (const Vec3& c : propCorners(p, m)) out.push_back(c);
    return out;
}

double propBottom(const Prop& p, const Model* m) {
    double z = 1e300;
    if (m && !m->meshes.empty()) {
        // Every vertex, not a thinned set: the lowest point must be exact.
        for (const ModelMesh& mesh : m->meshes)
            for (size_t i = 0; i < mesh.vertices(); ++i)
                z = std::min(z, propToWorld(p, {mesh.pos[3 * i], mesh.pos[3 * i + 1], mesh.pos[3 * i + 2]}).z);
        return z;
    }
    for (const Vec3& c : propCorners(p, nullptr)) z = std::min(z, c.z);
    return z;
}

std::optional<PropHit> rayHitProp(const Prop& p, const Model* m, const Vec3& o, const Vec3& d) {
    // The ray in the model's space: same t, since the direction is only transformed (not
    // normalised).
    const Mat3 R = rotationMatrix(p.rot);
    auto div = [](double v, double s) { return std::fabs(s) > 1e-12 ? v / s : 0.0; };
    const Vec3 lo = propToLocal(p, o);
    const Vec3 rd = R.transposeMul(d);
    const Vec3 ld{div(rd.x, p.scale.x), div(rd.y, p.scale.y), div(rd.z, p.scale.z)};
    double t = 0;
    Vec3 n;
    const bool hit = (m && !m->meshes.empty()) ? rayHitModel(*m, lo, ld, t, n) : rayHitBox(placeholderBounds(), lo, ld, t, n);
    if (!hit) return std::nullopt;
    // Normals turn with the inverse transpose of the scale.
    const Vec3 sn{div(n.x, p.scale.x), div(n.y, p.scale.y), div(n.z, p.scale.z)};
    Vec3 wn = normalize(R * sn);
    if (dot(wn, d) > 0) wn = -wn;
    return PropHit{t, wn, p.id};
}

std::optional<PropHit> rayHitProps(const std::vector<Prop>& props, const ModelFinder& models, const Vec3& o,
                                   const Vec3& d) {
    std::optional<PropHit> best;
    for (const Prop& p : props)
        if (auto h = rayHitProp(p, models ? models(p.model) : nullptr, o, d))
            if (!best || h->t < best->t) best = h;
    return best;
}

void rotateProp(Prop& p, int axis, double degrees, const Vec3& pivot) {
    const Mat3 r = axisRotation(axis, degrees);
    p.rot = rotationAngles(r * rotationMatrix(p.rot));
    p.pos = cleanVec(pivot + r * (p.pos - pivot));
}

void rotateProp(Prop& p, int axis, double degrees) { rotateProp(p, axis, degrees, p.pos); }

} // namespace scal
