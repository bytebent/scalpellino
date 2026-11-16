#include "core/texture.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace scal {

namespace {
constexpr double kPi = 3.14159265358979323846;

void ranges(const Face& f, const Polygon& poly, double& smin, double& smax, double& tmin, double& tmax) {
    smin = tmin = 1e300;
    smax = tmax = -1e300;
    for (const Vec3& p : poly.verts) {
        const double s = dot(p, f.u.axis), t = dot(p, f.v.axis);
        smin = std::min(smin, s); smax = std::max(smax, s);
        tmin = std::min(tmin, t); tmax = std::max(tmax, t);
    }
}

double wrap(double v, double m) {
    if (m <= 0) return v;
    double r = std::fmod(v, m);
    if (r < 0) r += m;
    if (std::fabs(r - m) < 1e-9) r = 0;
    r = cleanNumber(r, 1e-9);
    return r == 0 ? 0.0 : r; // never show "-0" in the UI
}
} // namespace

UV texCoord(const Face& f, const Vec3& p, TexSize ts) {
    UV uv;
    uv.u = (dot(p, f.u.axis) / f.scaleU + f.u.offset) / ts.w;
    uv.v = (dot(p, f.v.axis) / f.scaleV + f.v.offset) / ts.h;
    return uv;
}

void alignTextureToWorld(Face& f, const Vec3& normal) {
    defaultTexAxes(normal, f.u.axis, f.v.axis);
    f.u.offset = f.v.offset = 0;
    f.rotation = 0;
}

void alignTextureToFace(Face& f, const Vec3& n) {
    Vec3 u0, v0;
    defaultTexAxes(n, u0, v0);
    Vec3 u = u0 - n * dot(u0, n);
    if (length(u) < 1e-6) u = cross(n, v0);
    u = normalize(u);
    Vec3 v = v0 - n * dot(v0, n) - u * dot(v0, u);
    if (length(v) < 1e-6) v = cross(n, u);
    v = normalize(v);
    f.u.axis = cleanVec(u);
    f.v.axis = cleanVec(v);
    f.u.offset = f.v.offset = 0;
    f.rotation = 0;
}

void rotateTexture(Face& f, const Vec3& n, double degrees) {
    const double r = degrees * kPi / 180.0;
    f.u.axis = cleanVec(rotateVec(f.u.axis, n, r));
    f.v.axis = cleanVec(rotateVec(f.v.axis, n, r));
    f.rotation = std::fmod(f.rotation + degrees, 360.0);
}

void fitTexture(Face& f, const Polygon& poly, TexSize ts, int timesU, int timesV) {
    double smin, smax, tmin, tmax;
    ranges(f, poly, smin, smax, tmin, tmax);
    if (smax - smin > 1e-9) {
        f.scaleU = (smax - smin) / (ts.w * std::max(1, timesU));
        f.u.offset = -smin / f.scaleU;
    }
    if (tmax - tmin > 1e-9) {
        f.scaleV = (tmax - tmin) / (ts.h * std::max(1, timesV));
        f.v.offset = -tmin / f.scaleV;
    }
    normalizeTextureOffsets(f, ts);
}

void justifyTexture(Face& f, const Polygon& poly, TexSize ts, Justify j) {
    double smin, smax, tmin, tmax;
    ranges(f, poly, smin, smax, tmin, tmax);
    switch (j) {
        case Justify::Left:   f.u.offset = -smin / f.scaleU; break;
        case Justify::Right:  f.u.offset = -smax / f.scaleU; break;
        case Justify::Top:    f.v.offset = -tmin / f.scaleV; break;
        case Justify::Bottom: f.v.offset = -tmax / f.scaleV; break;
        case Justify::Center:
            f.u.offset = ts.w * 0.5 - (smin + smax) * 0.5 / f.scaleU;
            f.v.offset = ts.h * 0.5 - (tmin + tmax) * 0.5 / f.scaleV;
            break;
    }
    normalizeTextureOffsets(f, ts);
}

bool wrapTexture(const Face& from, const Polygon& fp, Face& to, const Polygon& tp) {
    // Shared edge: two corners present in both polygons.
    std::vector<Vec3> shared;
    for (const Vec3& a : fp.verts)
        for (const Vec3& b : tp.verts)
            if (length(a - b) < 1e-4) { shared.push_back(a); break; }
    if (shared.size() < 2) return false;
    const Vec3 q = shared[0];
    const Vec3 e = normalize(shared[1] - shared[0]);
    const Vec3 na = from.plane().n, nb = to.plane().n;
    // Angle that folds face A's plane onto face B's, about the edge.
    const double ang = std::atan2(dot(cross(na, nb), e), dot(na, nb));
    const Vec3 ru = rotateVec(from.u.axis, e, ang);
    const Vec3 rv = rotateVec(from.v.axis, e, ang);
    to.texture = from.texture;
    to.scaleU = from.scaleU;
    to.scaleV = from.scaleV;
    to.rotation = from.rotation;
    // Keep texel coordinates equal on the edge (pivot q lies on it; e is fixed by the
    // rotation, so the whole edge matches).
    to.u.offset = from.u.offset + (dot(q, from.u.axis) - dot(q, ru)) / from.scaleU;
    to.v.offset = from.v.offset + (dot(q, from.v.axis) - dot(q, rv)) / from.scaleV;
    to.u.axis = cleanVec(ru);
    to.v.axis = cleanVec(rv);
    return true;
}

void normalizeTextureOffsets(Face& f, TexSize ts) {
    f.u.offset = wrap(f.u.offset, ts.w);
    f.v.offset = wrap(f.v.offset, ts.h);
}

bool isNoDraw(const std::string& name) {
    std::string n;
    for (char c : name) n += (char)std::tolower((unsigned char)c);
    // Ours, plus the names other tools use for the same idea.
    return n == "nodraw" || n == "tools/toolsnodraw" || n == "toolsnodraw" || n == "common/nodraw";
}

int rayHitFace(const Brush& b, const Vec3& o, const Vec3& dir, double* tOut) {
    double tEnter = -1e300, tExit = 1e300;
    int enterFace = -1;
    for (size_t i = 0; i < b.faces.size(); ++i) {
        const Plane pl = b.faces[i].plane();
        const double denom = dot(pl.n, dir);
        const double dist = pl.distance(o);
        if (std::fabs(denom) < 1e-12) {
            if (dist > 0) return -1;
            continue;
        }
        const double t = -dist / denom;
        if (denom < 0) { if (t > tEnter) { tEnter = t; enterFace = (int)i; } }
        else tExit = std::min(tExit, t);
        if (tEnter > tExit) return -1;
    }
    if (tExit < 0 || tEnter < 0) return -1; // behind, or starting inside
    if (tOut) *tOut = tEnter;
    return enterFace;
}

} // namespace scal
