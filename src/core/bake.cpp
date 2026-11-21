#include "core/bake.hpp"
#include "core/model.hpp"
#include "core/terrain.hpp"
#include "core/texture.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <thread>
#include <unordered_map>

namespace scal {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kFar = 1e6;          // "to the sky" (cm)
constexpr double kOffset = 0.3;       // rays start this far off the surface (cm)
constexpr int kSkySamples = 162;      // VRAD's NUMVERTEXNORMALS
constexpr int kSunSamples = 30;       // VRAD's NSAMPLES_SUN_AREA_LIGHT
constexpr int kMaxLuxels = 512;       // per side of one face's lightmap
constexpr int kMaxCells = 128;        // per side of one primitive's lightmap

Vec3 linColor(const Vec3& c255) {
    return {std::pow(std::clamp(c255.x, 0.0, 255.0) / 255.0, 2.2), std::pow(std::clamp(c255.y, 0.0, 255.0) / 255.0, 2.2),
            std::pow(std::clamp(c255.z, 0.0, 255.0) / 255.0, 2.2)};
}
Vec3 mul(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
double maxc(const Vec3& v) { return std::max({v.x, v.y, v.z}); }
double lengthSq(const Vec3& v) { return dot(v, v); }

// ---------------------------------------------------------------- ray tests
struct Tri { Vec3 a, e1, e2; };

class Tracer {
public:
    // `tag`: what the triangle belongs to (closestHit reports it; -1 = nothing that bounces).
    void addTri(const Vec3& a, const Vec3& b, const Vec3& c, int tag = -1) {
        if (length(cross(b - a, c - a)) < 1e-9) return;
        tris_.push_back({a, b - a, c - a});
        tags_.push_back(tag);
    }
    void addPrims(std::vector<MathPrim> members) {
        PrimObj o;
        o.members = std::move(members);
        double pad = 0;
        for (const MathPrim& m : o.members) {
            o.box.add(primBounds(m));
            pad = std::max(pad, m.blend);
        }
        o.box.min -= Vec3{1, 1, 1} * (1 + pad);
        o.box.max += Vec3{1, 1, 1} * (1 + pad);
        for (const MathPrim& m : o.members) o.ptrs.push_back(&m);
        prims_.push_back(std::move(o));
    }
    void build() {
        // pointers into members are stable now (prims_ no longer grows)
        for (PrimObj& o : prims_) {
            o.ptrs.clear();
            for (const MathPrim& m : o.members) o.ptrs.push_back(&m);
        }
        nodes_.clear();
        idx_.resize(tris_.size());
        for (size_t i = 0; i < idx_.size(); ++i) idx_[i] = (int)i;
        if (!tris_.empty()) buildNode(0, (int)tris_.size());
    }
    size_t triangles() const { return tris_.size(); }
    size_t primObjects() const { return prims_.size(); }

    // The nearest thing along the ray (d unit), up to maxT: its distance, the triangle's tag
    // (or -1) and the primitive object's index (or -1). False if nothing.
    struct Hit { double t = 0; int tag = -1; int prim = -1; };
    bool closestHit(const Vec3& o, const Vec3& d, double maxT, Hit& hit) const {
        const Vec3 inv{1.0 / (d.x != 0 ? d.x : 1e-30), 1.0 / (d.y != 0 ? d.y : 1e-30), 1.0 / (d.z != 0 ? d.z : 1e-30)};
        double best = maxT;
        bool any = false;
        if (!nodes_.empty()) {
            int stack[96];
            int sp = 0;
            stack[sp++] = 0;
            while (sp) {
                const Node& n = nodes_[stack[--sp]];
                if (!hitBox(n.box, o, inv, best)) continue;
                if (n.count) {
                    for (int i = n.first; i < n.first + n.count; ++i) {
                        double t;
                        if (hitTriT(tris_[idx_[i]], o, d, best, t)) {
                            best = t;
                            hit.t = t;
                            hit.tag = tags_[idx_[i]];
                            hit.prim = -1;
                            any = true;
                        }
                    }
                } else if (sp + 2 <= 96) {
                    stack[sp++] = n.left;
                    stack[sp++] = n.right;
                }
            }
        }
        for (size_t pi = 0; pi < prims_.size(); ++pi) {
            const PrimObj& p = prims_[pi];
            if (!hitBox(p.box, o, inv, best)) continue;
            const std::vector<RayInterval> iv = p.members.size() == 1 ? primIntervals(p.members[0], o, d) : comboIntervals(p.ptrs, o, d);
            for (const RayInterval& r : iv) {
                const double t = r.t0 > 1e-3 ? r.t0 : r.t1;
                if (t > 1e-3 && t < best) {
                    best = t;
                    hit.t = t;
                    hit.tag = -1;
                    hit.prim = (int)pi;
                    any = true;
                }
            }
        }
        return any;
    }

    // Anything between o and o + d * maxT (d unit)?
    bool occluded(const Vec3& o, const Vec3& d, double maxT) const {
        const Vec3 inv{1.0 / (d.x != 0 ? d.x : 1e-30), 1.0 / (d.y != 0 ? d.y : 1e-30), 1.0 / (d.z != 0 ? d.z : 1e-30)};
        if (!nodes_.empty()) {
            int stack[96];
            int sp = 0;
            stack[sp++] = 0;
            while (sp) {
                const Node& n = nodes_[stack[--sp]];
                if (!hitBox(n.box, o, inv, maxT)) continue;
                if (n.count) {
                    for (int i = n.first; i < n.first + n.count; ++i)
                        if (hitTri(tris_[idx_[i]], o, d, maxT)) return true;
                } else if (sp + 2 <= 96) {
                    stack[sp++] = n.left;
                    stack[sp++] = n.right;
                }
            }
        }
        for (const PrimObj& p : prims_) {
            if (!hitBox(p.box, o, inv, maxT)) continue;
            const std::vector<RayInterval> iv = p.members.size() == 1 ? primIntervals(p.members[0], o, d) : comboIntervals(p.ptrs, o, d);
            for (const RayInterval& r : iv)
                if (r.t1 > 1e-3 && r.t0 < maxT) return true;
        }
        return false;
    }

private:
    struct Node { AABB box; int left = -1, right = -1, first = 0, count = 0; };
    struct PrimObj { std::vector<MathPrim> members; std::vector<const MathPrim*> ptrs; AABB box; };
    std::vector<Tri> tris_;
    std::vector<int> tags_;
    std::vector<int> idx_;
    std::vector<Node> nodes_;
    std::vector<PrimObj> prims_;

    static bool hitBox(const AABB& b, const Vec3& o, const Vec3& inv, double maxT) {
        double t0 = 0, t1 = maxT;
        for (int a = 0; a < 3; ++a) {
            double ta = (b.min[a] - o[a]) * inv[a], tb = (b.max[a] - o[a]) * inv[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
        return true;
    }
    static bool hitTriT(const Tri& t, const Vec3& o, const Vec3& d, double maxT, double& tt) {
        const Vec3 p = cross(d, t.e2);
        const double det = dot(t.e1, p);
        if (std::fabs(det) < 1e-12) return false;
        const double inv = 1.0 / det;
        const Vec3 s = o - t.a;
        const double u = dot(s, p) * inv;
        if (u < 0 || u > 1) return false;
        const Vec3 q = cross(s, t.e1);
        const double v = dot(d, q) * inv;
        if (v < 0 || u + v > 1) return false;
        tt = dot(t.e2, q) * inv;
        return tt > 1e-4 && tt < maxT;
    }
    static bool hitTri(const Tri& t, const Vec3& o, const Vec3& d, double maxT) {
        const Vec3 p = cross(d, t.e2);
        const double det = dot(t.e1, p);
        if (std::fabs(det) < 1e-12) return false;
        const double inv = 1.0 / det;
        const Vec3 s = o - t.a;
        const double u = dot(s, p) * inv;
        if (u < 0 || u > 1) return false;
        const Vec3 q = cross(s, t.e1);
        const double v = dot(d, q) * inv;
        if (v < 0 || u + v > 1) return false;
        const double tt = dot(t.e2, q) * inv;
        return tt > 1e-4 && tt < maxT;
    }
    int buildNode(int first, int count) {
        const int me = (int)nodes_.size();
        nodes_.push_back({});
        AABB box, cb;
        for (int i = first; i < first + count; ++i) {
            const Tri& t = tris_[idx_[i]];
            box.add(t.a);
            box.add(t.a + t.e1);
            box.add(t.a + t.e2);
            cb.add(t.a + (t.e1 + t.e2) / 3.0);
        }
        nodes_[me].box = box;
        if (count <= 4) {
            nodes_[me].first = first;
            nodes_[me].count = count;
            return me;
        }
        const Vec3 sz = cb.size();
        const int axis = sz.x >= sz.y && sz.x >= sz.z ? 0 : sz.y >= sz.z ? 1 : 2;
        const int mid = first + count / 2;
        std::nth_element(idx_.begin() + first, idx_.begin() + mid, idx_.begin() + first + count, [&](int a, int b) {
            const Tri &ta = tris_[a], &tb = tris_[b];
            return (ta.a[axis] * 3 + ta.e1[axis] + ta.e2[axis]) < (tb.a[axis] * 3 + tb.e1[axis] + tb.e2[axis]);
        });
        const int l = buildNode(first, mid - first);
        const int r = buildNode(mid, first + count - mid);
        nodes_[me].left = l;
        nodes_[me].right = r;
        return me;
    }
};

// ---------------------------------------------------------------- gathering light
struct Gather {
    const Tracer* tracer = nullptr;
    std::vector<BakeLight> lights;
    std::vector<Vec3> skyDirs, skyDirsFast; // VRAD's anorms (all, a quarter)
    std::vector<Vec3> sunJitter, sunJitterFast;
    const SkyPicture* sky = nullptr;
    Vec3 skyFlat;      // sky colour without a picture
    double skyScale = 1;
    Vec3 minLight;     // flat light everywhere (option)
    bool skyOn = true;

    Vec3 skyColor(const Vec3& d) const { return sky && sky->valid() ? sky->at(d) * skyScale : skyFlat * skyScale; }

    // Light at a point `p` with shading normal `n` and geometric normal `ng`.
    Vec3 at(const Vec3& p, const Vec3& n, const Vec3& ng, bool fast) const {
        const Vec3 o = p + ng * kOffset;
        Vec3 sum = minLight;
        for (const BakeLight& L : lights) {
            if (L.kind == LightKind::Sun) {
                const double dt = -dot(n, L.dir);
                if (dt <= 0) continue;
                const std::vector<Vec3>& jit = fast ? sunJitterFast : sunJitter;
                const size_t ns = L.spreadSin > 0 ? jit.size() + 1 : 1;
                double vis = 0;
                for (size_t k = 0; k < ns; ++k) {
                    Vec3 d = -L.dir;
                    if (k > 0) d = normalize(d + jit[k - 1] * L.spreadSin);
                    if (!tracer->occluded(o, d, kFar)) vis += 1;
                }
                sum += L.intensity * (dt * vis / (double)ns);
                continue;
            }
            const Vec3 delta = L.pos - o;
            const double dist = length(delta);
            if (dist < 1e-6) continue;
            const Vec3 to = delta / dist;
            const double dt = dot(n, to);
            if (dt <= 0) continue;
            const double f = lightFalloff(L, dist, -to);
            if (f <= 0) continue;
            const Vec3 c = L.intensity * (dt * f);
            if (maxc(c) < 1e-5) continue; // VRAD's minimum: too faint to show
            if (tracer->occluded(o, to, dist - 0.5)) continue;
            sum += c;
        }
        if (skyOn) {
            const std::vector<Vec3>& dirs = fast ? skyDirsFast : skyDirs;
            double sumdot = 0;
            Vec3 acc;
            for (const Vec3& a : dirs) {
                const Vec3 d = -a; // VRAD traces "back" along each direction
                const double dt = dot(n, d);
                if (dt <= 1e-6) continue;
                sumdot += dt;
                if (!tracer->occluded(o, d, kFar)) acc += skyColor(d) * dt;
            }
            if (sumdot > 0) sum += acc / sumdot;
        }
        return sum;
    }
};

// Smooth normals: for each corner position, the normals of the lit faces meeting there.
struct SmoothNormals {
    double cosLimit = 0.7071067;
    std::unordered_map<std::uint64_t, std::vector<Vec3>> at;
    static std::uint64_t key(const Vec3& p) {
        auto q = [](double v) { return (std::uint64_t)(std::int64_t)std::llround(v * 8) & 0x1FFFFF; };
        return q(p.x) | (q(p.y) << 21) | (q(p.z) << 42);
    }
    void add(const Vec3& p, const Vec3& n) {
        std::vector<Vec3>& v = at[key(p)];
        for (const Vec3& m : v)
            if (dot(m, n) > 0.99999) return; // the same plane counts once
        v.push_back(n);
    }
    Vec3 normal(const Vec3& p, const Vec3& faceN) const {
        auto it = at.find(key(p));
        if (it == at.end()) return faceN;
        Vec3 s;
        for (const Vec3& m : it->second)
            if (dot(m, faceN) >= cosLimit) s += m;
        return length(s) > 1e-9 ? normalize(s) : faceN;
    }
};

// A plain face being baked.
struct FaceWork {
    FaceLightmap* out = nullptr;
    std::string texture;
    Vec3 n;                       // face normal
    double planeD = 0;
    std::vector<Vec3> verts;
    std::vector<Vec3> vnormals;   // smooth normal per corner
    bool smooth = false;
    std::vector<std::pair<double, double>> st; // corners in luxel coordinates
    Vec3 c0, c1, c2;              // P = (S c0 + T c1 + D c2) (inverse of [axisS; axisT; n])

    Vec3 point(double S, double T) const { return c0 * S + c1 * T + c2 * planeD; }
    void lux(const Vec3& P, double& s, double& t) const {
        s = dot(out->axisS, P) - out->s0;
        t = dot(out->axisT, P) - out->t0;
    }
    bool inside(double s, double t) const {
        const size_t m = st.size();
        int sign = 0;
        for (size_t i = 0; i < m; ++i) {
            const auto& a = st[i];
            const auto& b = st[(i + 1) % m];
            const double c = (b.first - a.first) * (t - a.second) - (b.second - a.second) * (s - a.first);
            if (std::fabs(c) < 1e-12) continue;
            const int sg = c > 0 ? 1 : -1;
            if (sign == 0) sign = sg;
            else if (sg != sign) return false;
        }
        return true;
    }
    // Moves a luxel position outside the face onto it (a little inside its edge).
    void clampInside(double& s, double& t) const {
        if (inside(s, t)) return;
        double best = 1e300, bs = s, bt = t;
        const size_t m = st.size();
        for (size_t i = 0; i < m; ++i) {
            const auto& a = st[i];
            const auto& b = st[(i + 1) % m];
            const double ex = b.first - a.first, ey = b.second - a.second;
            const double l2 = ex * ex + ey * ey;
            double u = l2 > 0 ? ((s - a.first) * ex + (t - a.second) * ey) / l2 : 0;
            u = std::clamp(u, 0.0, 1.0);
            const double px = a.first + ex * u, py = a.second + ey * u;
            const double d2 = (px - s) * (px - s) + (py - t) * (py - t);
            if (d2 < best) { best = d2; bs = px; bt = py; }
        }
        double cs = 0, ct = 0;
        for (const auto& v : st) { cs += v.first; ct += v.second; }
        cs /= (double)m;
        ct /= (double)m;
        const double dx = cs - bs, dy = ct - bt, dl = std::sqrt(dx * dx + dy * dy);
        const double nudge = std::min(0.05, dl * 0.5);
        s = dl > 1e-12 ? bs + dx / dl * nudge : bs;
        t = dl > 1e-12 ? bt + dy / dl * nudge : bt;
    }
    Vec3 shadingNormal(double s, double t) const {
        if (!smooth) return n;
        // Barycentric in the fan triangle holding (s, t) (or nearest).
        double bestMin = -1e300;
        Vec3 res = n;
        for (size_t k = 1; k + 1 < st.size(); ++k) {
            const auto &a = st[0], &b = st[k], &c = st[k + 1];
            const double det = (b.first - a.first) * (c.second - a.second) - (c.first - a.first) * (b.second - a.second);
            if (std::fabs(det) < 1e-12) continue;
            const double l1 = ((s - a.first) * (c.second - a.second) - (c.first - a.first) * (t - a.second)) / det;
            const double l2 = ((b.first - a.first) * (t - a.second) - (s - a.first) * (b.second - a.second)) / det;
            const double l0 = 1 - l1 - l2;
            const double mn = std::min({l0, l1, l2});
            if (mn > bestMin) {
                bestMin = mn;
                const double c0w = std::max(0.0, l0), c1w = std::max(0.0, l1), c2w = std::max(0.0, l2);
                const Vec3 v = vnormals[0] * c0w + vnormals[k] * c1w + vnormals[k + 1] * c2w;
                res = length(v) > 1e-9 ? normalize(v) : n;
            }
            if (mn >= 0) break;
        }
        return res;
    }
};

struct TerrainWork {
    FaceLightmap* out = nullptr;
    const Brush* brush = nullptr;
    TerrainGrid g;
    std::vector<Vec3> vn; // smooth normal per grid point
    // Position and normal at grid coordinates (u, v) in 0..n-1.
    void at(double u, double v, Vec3& p, Vec3& nn, Vec3& ng) const {
        const int n = g.n;
        const int i = std::clamp((int)std::floor(u), 0, n - 2), j = std::clamp((int)std::floor(v), 0, n - 2);
        const double fu = std::clamp(u - i, 0.0, 1.0), fv = std::clamp(v - j, 0.0, 1.0);
        const Vec3 a = g.at(i, j), b = g.at(i + 1, j), c = g.at(i + 1, j + 1), d = g.at(i, j + 1);
        // The two triangles the cell is drawn with: (a b c) and (a c d).
        if (fu >= fv) p = a + (b - a) * fu + (c - b) * fv;
        else p = a + (c - d) * fu + (d - a) * fv;
        auto N = [&](int ii, int jj) { return vn[(size_t)jj * n + ii]; };
        const Vec3 s = N(i, j) * ((1 - fu) * (1 - fv)) + N(i + 1, j) * (fu * (1 - fv)) + N(i + 1, j + 1) * (fu * fv) + N(i, j + 1) * ((1 - fu) * fv);
        nn = length(s) > 1e-9 ? normalize(s) : g.normal;
        Vec3 cn = fu >= fv ? cross(b - a, c - a) : cross(c - a, d - a);
        if (dot(cn, g.normal) < 0) cn = -cn;
        ng = length(cn) > 1e-12 ? normalize(cn) : g.normal;
    }
};

struct PrimWork {
    PrimLightmap* out = nullptr;
    std::vector<MathPrim> members;
    std::vector<const MathPrim*> ptrs;
    double dist(const Vec3& p) const { return members.size() == 1 ? primDistance(members[0], p) : comboDistance(ptrs, p); }
    // Per cell: lit (near the surface), its surface point, normal and distance to the surface.
    std::vector<char> lit;
    std::vector<Vec3> surf, nrm;
    std::vector<float> sd;
};

// A radiosity patch (VRAD's patch_t, flattened: only the leaves).
struct Patch {
    std::vector<Vec3> wind;   // its polygon (brush faces); others: a square of its area
    std::vector<std::pair<double, double>> st; // brush faces: the polygon in luxel coordinates
    Vec3 origin, normal;
    double area = 0;
    Vec3 refl;                // reflectivity (x bounce strength, < 0.99)
    Vec3 direct;              // incident direct light, the average over the patch
    Vec3 total;               // incident bounce light, summed over the bounces
    Vec3 emit, add;
    std::vector<std::pair<int, float>> transfers; // (patch it receives from, transfer)
};

// A square polygon of `area` around `o`, facing `n` (for the polygon-to-point form factor).
std::vector<Vec3> squareWinding(const Vec3& o, const Vec3& n, double area) {
    Vec3 a, b;
    defaultTexAxes(n, a, b);
    a = normalize(a - n * dot(a, n));
    b = normalize(cross(n, a));
    const double h = std::sqrt(std::max(area, 1e-9)) / 2;
    return {o - a * h - b * h, o + a * h - b * h, o + a * h + b * h, o - a * h + b * h};
}

// Splits a convex polygon by the plane x[axis] = at: below and above.
void splitPoly(const std::vector<Vec3>& p, int axis, double at, std::vector<Vec3>& lo, std::vector<Vec3>& hi) {
    lo.clear();
    hi.clear();
    for (size_t i = 0; i < p.size(); ++i) {
        const Vec3& a = p[i];
        const Vec3& b = p[(i + 1) % p.size()];
        const double da = a[axis] - at, db = b[axis] - at;
        if (da <= 0) lo.push_back(a);
        if (da >= 0) hi.push_back(a);
        if ((da < 0 && db > 0) || (da > 0 && db < 0)) {
            const Vec3 m = a + (b - a) * (da / (da - db));
            lo.push_back(m);
            hi.push_back(m);
        }
    }
}
double polyArea(const std::vector<Vec3>& p) {
    Vec3 s;
    for (size_t i = 1; i + 1 < p.size(); ++i) s += cross(p[i] - p[0], p[i + 1] - p[0]);
    return length(s) / 2;
}
Vec3 polyCentroid(const std::vector<Vec3>& p) {
    Vec3 c;
    double w = 0;
    for (size_t i = 1; i + 1 < p.size(); ++i) {
        const double a = length(cross(p[i] - p[0], p[i + 1] - p[0])) / 2;
        c += (p[0] + p[i] + p[i + 1]) * (a / 3);
        w += a;
    }
    if (w <= 0) {
        for (const Vec3& v : p) c += v;
        return c / (double)std::max<size_t>(1, p.size());
    }
    return c / w;
}

// VRAD's form factors (vrad.cpp): differential to differential, and polygon to
// differential (Dutre's formula 81; the caller multiplies by the polygon's area).
double formDiffToDiff(const Patch& from, const Patch& to) {
    Vec3 d = from.origin - to.origin;
    const double len = length(d);
    if (len < 1e-9) return 0;
    d = d / len;
    return -dot(d, from.normal) * dot(d, to.normal) / (len * len);
}
double formPolyToDiff(const Patch& poly, const Patch& diff) {
    double f = 0;
    const size_t n = poly.wind.size();
    for (size_t i = 0; i < n; ++i) {
        const Vec3 v1 = normalize(poly.wind[i] - diff.origin), v2 = normalize(poly.wind[(i + 1) % n] - diff.origin);
        Vec3 g = cross(v1, v2);
        const double s = length(g);
        if (s > 1) return 0;
        if (s < 1e-12) continue;
        g = g / s * std::asin(s);
        f += dot(g, diff.normal);
    }
    return std::fabs(f) * (0.5 / std::max(poly.area, 1e-9)); // either winding order
}

struct QualityParams {
    double scaleMul = 1;
    int superN = 0;      // supersample n x n where neighbours differ (0 = off)
    int skyMul = 1;
};
QualityParams qualityParams(BakeQuality q) {
    switch (q) {
        case BakeQuality::Draft: return {2, 0, 1};
        case BakeQuality::Normal: return {1, 3, 1};
        case BakeQuality::High: return {1, 4, 4};
    }
    return {};
}

// FNV-1a
struct Hasher {
    std::uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, size_t n) {
        const unsigned char* c = (const unsigned char*)p;
        for (size_t i = 0; i < n; ++i) { h ^= c[i]; h *= 1099511628211ull; }
    }
    void d(double v) { bytes(&v, sizeof v); }
    void i(long long v) { bytes(&v, sizeof v); }
    void v(const Vec3& x) { d(x.x); d(x.y); d(x.z); }
    void s(const std::string& x) { i((long long)x.size()); bytes(x.data(), x.size()); }
};

} // namespace

// ---------------------------------------------------------------- public pieces

double srgbToLinear(double c) {
    c = std::clamp(c, 0.0, 1.0);
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
double linearToSrgb(double l) {
    l = std::clamp(l, 0.0, 1.0);
    return l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055;
}

Vec3 SkyPicture::at(const Vec3& dir) const {
    if (!valid()) return {};
    const double len = length(dir);
    if (len < 1e-12) return {};
    const Vec3 d = dir / len;
    const double yaw = std::atan2(d.y, d.x), lat = std::asin(std::clamp(d.z, -1.0, 1.0));
    double u = 0.5 - yaw / (2 * kPi), v = 0.5 - lat / kPi;
    const double x = u * w - 0.5, y = std::clamp(v * h - 0.5, 0.0, (double)h - 1);
    const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    const double fx = x - x0, fy = y - y0;
    auto px = [&](int xi, int yi) {
        xi = ((xi % w) + w) % w;
        yi = std::clamp(yi, 0, h - 1);
        const float* p = &rgb[((size_t)yi * w + xi) * 3];
        return Vec3{p[0], p[1], p[2]};
    };
    return px(x0, y0) * ((1 - fx) * (1 - fy)) + px(x0 + 1, y0) * (fx * (1 - fy)) + px(x0, y0 + 1) * ((1 - fx) * fy) +
           px(x0 + 1, y0 + 1) * (fx * fy);
}

SkyPicture makeSkyPicture(const unsigned char* rgb, int w, int h, int outW) {
    SkyPicture s;
    if (!rgb || w <= 0 || h <= 0) return s;
    outW = std::max(8, std::min(outW, w));
    const int outH = std::max(4, outW / 2);
    s.w = outW;
    s.h = outH;
    s.rgb.assign((size_t)outW * outH * 3, 0.0f);
    float lut[256];
    for (int i = 0; i < 256; ++i) lut[i] = (float)srgbToLinear(i / 255.0);
    for (int y = 0; y < outH; ++y) {
        const int y0 = y * h / outH, y1 = std::max(y0 + 1, (y + 1) * h / outH);
        for (int x = 0; x < outW; ++x) {
            const int x0 = x * w / outW, x1 = std::max(x0 + 1, (x + 1) * w / outW);
            double acc[3] = {0, 0, 0};
            int n = 0;
            for (int yy = y0; yy < y1 && yy < h; ++yy)
                for (int xx = x0; xx < x1 && xx < w; ++xx) {
                    const unsigned char* p = &rgb[((size_t)yy * w + xx) * 3];
                    for (int c = 0; c < 3; ++c) acc[c] += lut[p[c]];
                    ++n;
                }
            for (int c = 0; c < 3; ++c) s.rgb[((size_t)y * outW + x) * 3 + c] = (float)(n ? acc[c] / n : 0);
        }
    }
    return s;
}

bool solveInverseQuadraticMonotonic(double x1, double y1, double x2, double y2, double x3, double y3, double& a, double& b, double& c) {
    auto solve = [&](double X1, double Y1, double X2, double Y2, double X3, double Y3) {
        const double det = (X1 - X2) * (X1 - X3) * (X2 - X3);
        if (det == 0.0) return false;
        a = (X3 * (-Y1 + Y2) + X2 * (Y1 - Y3) + X1 * (-Y2 + Y3)) / det;
        b = (X3 * X3 * (Y1 - Y2) + X1 * X1 * (Y2 - Y3) + X2 * X2 * (-Y1 + Y3)) / det;
        c = (X1 * X3 * (-X1 + X3) * Y2 + X2 * X2 * (X3 * Y1 - X1 * Y3) + X2 * (-(X3 * X3 * Y1) + X1 * X1 * Y3)) / det;
        return true;
    };
    if (x1 > x2) { std::swap(x1, x2); std::swap(y1, y2); }
    if (x2 > x3) { std::swap(x2, x3); std::swap(y2, y3); }
    if (x1 > x2) { std::swap(x1, x2); std::swap(y1, y2); }
    // As VRAD: when the curve wouldn't be monotonic, move the middle point toward the
    // straight line between the ends, 5% at a time (float steps, as the original).
    for (float k = 0.0f; k <= 1.0f; k += 0.05f) {
        const double lerp = y1 + (y3 - y1) * (x2 - x1) / (x3 - x1);
        const double ty2 = (1 - k) * y2 + k * lerp;
        if (!solve(x1, y1, x2, ty2, x3, y3)) return false;
        const double deriv = 2.0 * a + b;
        if (y1 < y2 && y2 < y3) {
            if (deriv >= 0.0) return true;
        } else if (y1 > y2 && y2 > y3) {
            if (deriv <= 0.0) return true;
        } else
            return true;
    }
    return true;
}

std::vector<Vec3> haltonSphere(int n) {
    auto halton = [](int elem, int base) {
        double ret = 0, inv = 1.0 / base;
        while (elem) {
            ret += (elem % base) * inv;
            inv /= base;
            elem /= base;
        }
        return ret;
    };
    std::vector<Vec3> out;
    out.reserve((size_t)n);
    for (int i = 1; i <= n; ++i) {
        const double z = 2 * halton(i, 2) - 1;
        const double phi = std::acos(std::clamp(z, -1.0, 1.0));
        const double theta = 2 * kPi * halton(i, 3);
        const double sp = std::sin(phi);
        out.push_back({std::cos(theta) * sp, std::sin(theta) * sp, z});
    }
    return out;
}

BakeLight prepareLight(const Light& l, const Environment& env) {
    BakeLight L;
    L.kind = l.kind;
    L.pos = l.pos;
    L.dir = normalize(lightDirection(l));
    L.intensity = linColor(l.color) * (std::max(0.0, l.brightness) / 255.0);
    if (l.kind == LightKind::Sun) {
        if (env.skylab.used) L.intensity = L.intensity * (env.skylab.sunDiffuse / 100.0);
        L.spreadSin = std::sin(std::clamp(l.sunSpread, 0.0, 90.0) * kDeg);
        return L;
    }
    if (l.kind == LightKind::Spot) {
        double inner = l.inner > 0 ? l.inner : 10, outer = l.outer > 0 ? l.outer : inner;
        outer = std::max(outer, inner);
        if (inner >= 180 && outer >= 180) {
            L.kind = LightKind::Point;
        } else {
            if (inner > 90 || outer > 90) L.notes.push_back("warning: a spot light's cone is wider than 90 degrees; capped at 90 (as VRAD)");
            inner = std::min(inner, 90.0);
            outer = std::min(outer, 90.0);
            L.stopdot = std::cos(inner * kDeg);
            L.stopdot2 = std::cos(outer * kDeg);
            L.exponent = l.exponent;
        }
    }
    if (l.falloff == LightFalloff::FiftyZero && l.fifty > 0) {
        const double d50 = l.fifty / kCmPerHammerUnit;
        double d0 = l.range / kCmPerHammerUnit;
        if (d0 < d50) {
            char b[200];
            std::snprintf(b, sizeof b, "warning: a light has a 50%% distance of %g but a 0%% distance of %g; using %g (as VRAD)", l.fifty, l.range,
                          2 * l.fifty);
            L.notes.push_back(b);
            d0 = 2 * d50;
        }
        double a = 0, b = 1, c = 0;
        if (!solveInverseQuadraticMonotonic(0, 1.0, d50, 2.0, d0, 256.0, a, b, c)) L.notes.push_back("warning: can't solve the falloff of a light");
        const double v50 = c + d50 * (b + d50 * a);
        const double scale = v50 != 0 ? 2.0 / v50 : 1;
        L.q = a * scale;
        L.l = b * scale;
        L.c = c * scale;
        if (l.hardFalloff) {
            L.endFade = d0;
            L.startFade = 0.75 * d0 + 0.25 * d50;
        } else if (std::fabs(L.q) > 0) {
            const double flMax = L.l / (-2.0 * L.q);
            if (flMax > 0) {
                L.capDist = flMax;
                L.startFade = flMax;
                L.endFade = 10 * flMax;
            }
        }
        return L;
    }
    if (l.falloff == LightFalloff::Custom) {
        L.c = l.constantAttn < 1e-9 ? 0 : l.constantAttn;
        L.l = l.linearAttn < 1e-9 ? 0 : l.linearAttn;
        L.q = l.quadraticAttn < 1e-9 ? 0 : l.quadraticAttn;
        if (L.c == 0 && L.l == 0 && L.q == 0) L.c = 1;
    } else {
        L.c = 0;
        L.l = 0;
        L.q = 1;
    }
    // "scale intensity for unit 100 distance"
    const double ratio = L.c + 100 * L.l + 100 * 100 * L.q;
    if (ratio > 0) L.intensity = L.intensity * ratio;
    return L;
}

double lightFalloff(const BakeLight& L, double distCm, const Vec3& toSample) {
    double dist = distCm / kCmPerHammerUnit;
    const bool fade = L.endFade > L.startFade;
    if (fade && dist > L.endFade) return 0;
    dist = std::max(dist, 1.0);
    const double e = std::min(dist, L.capDist);
    const double den = L.c + L.l * e + L.q * e * e;
    if (den <= 0) return 0;
    double f = 1.0 / den;
    if (L.kind == LightKind::Spot) {
        const double dot2 = dot(toSample, L.dir);
        if (dot2 <= L.stopdot2) return 0;
        f *= dot2;
        if (dot2 <= L.stopdot) {
            double m = L.stopdot - L.stopdot2 > 0 ? (dot2 - L.stopdot2) / (L.stopdot - L.stopdot2) : 1;
            m = std::clamp(m, 0.0, 1.0);
            if (L.exponent != 0 && L.exponent != 1) m = std::pow(m, L.exponent);
            f *= m;
        }
    }
    if (fade) {
        double t = (dist - L.startFade) / (L.endFade - L.startFade);
        t = 1 - std::clamp(t, 0.0, 1.0);
        f *= t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    }
    return f;
}

// ---------------------------------------------------------------- the bake

std::uint64_t bakeKey(const BakeInput& in) {
    Hasher h;
    h.i((long long)in.quality);
    for (const Brush& b : in.brushes) {
        h.i(b.id);
        for (const Face& f : b.faces) {
            for (const Vec3& p : f.p) h.v(p);
            h.s(f.texture);
            h.v(f.u.axis);
            h.v(f.v.axis);
            h.d(f.lightmapScale);
        }
        if (b.terrain) {
            h.i(b.terrain->face);
            h.i(b.terrain->power);
            h.v(b.terrain->start);
            for (double x : b.terrain->heights) h.d(x);
            for (const TerrainLayer& l : b.terrain->layers) {
                h.s(l.texture);
                for (float a : l.alpha) h.d(a);
            }
        }
    }
    for (const Light& l : in.lights) {
        h.i((long long)l.kind);
        h.v(l.pos); h.v(l.color); h.d(l.brightness); h.d(l.range); h.d(l.pitch); h.d(l.yaw); h.d(l.inner); h.d(l.outer);
        h.i((long long)l.falloff); h.d(l.fifty); h.i(l.hardFalloff); h.d(l.constantAttn); h.d(l.linearAttn); h.d(l.quadraticAttn);
        h.d(l.exponent); h.d(l.sunSpread);
    }
    for (const MathPrim& p : in.prims) {
        h.i(p.id); h.i((long long)p.kind); h.v(p.pos); h.v(p.rot); h.v(p.size); h.d(p.round); h.d(p.thickness); h.d(p.blend);
        h.i((long long)p.op); h.i(p.combo); h.d(p.lightmapScale);
    }
    for (const Prop& p : in.props) {
        h.s(p.model); h.v(p.pos); h.v(p.rot); h.v(p.scale); h.i(p.castShadows);
        h.i(in.models && in.models(p.model) ? 1 : 0);
    }
    h.v(in.env.ambient); h.d(in.env.skyLight); h.i(in.env.minLight); h.d(in.env.smoothAngle); h.d(in.env.bounceScale);
    h.i(in.maxBounces);
    for (const auto& [k, v] : in.reflectivity) { h.s(k); h.v(v); }
    for (const auto& [k, v] : in.modelReflectivity) { h.s(k); h.v(v); }
    for (const MathPrim& p : in.prims) { h.s(p.texture); h.v(p.color); }
    h.i(in.env.skylab.used); h.d(in.env.skylab.sunDiffuse);
    h.i(in.sky.w); h.i(in.sky.h);
    if (in.sky.valid()) h.bytes(in.sky.rgb.data(), in.sky.rgb.size() * sizeof(float));
    return h.h;
}

BakeResult bakeLight(const BakeInput& in, BakeProgress* progress, int threads) {
    const auto t0 = std::chrono::steady_clock::now();
    BakeResult res;
    const QualityParams Q = qualityParams(in.quality);
    const bool fast = in.quality == BakeQuality::Draft;

    // ---- what blocks light: every brush face (nodraw too), terrain, primitives, props.
    // Triangles are tagged with what they belong to, so a bounce ray knows which patch it hit.
    struct Owner { int kind; int a, b; }; // kind 0: lit face (face index), 1: terrain (terrain, cell), 2: prop patch
    std::vector<Owner> owners;
    Tracer tracer;
    std::vector<std::optional<TerrainGrid>> grids(in.brushes.size());
    {
        int fi = 0, ti = 0;
        for (size_t bi = 0; bi < in.brushes.size(); ++bi) {
            const Brush& b = in.brushes[bi];
            grids[bi] = terrainGrid(b);
            for (const Polygon& p : b.polygons()) {
                if (grids[bi] && p.face == grids[bi]->face) continue;
                int tag = -1;
                if (p.face >= 0 && p.face < (int)b.faces.size() && p.verts.size() >= 3 && !isNoDraw(b.faces[p.face].texture)) {
                    tag = (int)owners.size();
                    owners.push_back({0, fi++, 0});
                }
                for (size_t i = 1; i + 1 < p.verts.size(); ++i) tracer.addTri(p.verts[0], p.verts[i], p.verts[i + 1], tag);
            }
            if (grids[bi]) {
                const TerrainGrid& g = *grids[bi];
                for (int j = 0; j + 1 < g.n; ++j)
                    for (int i = 0; i + 1 < g.n; ++i) {
                        const int tag = (int)owners.size();
                        owners.push_back({1, ti, j * (g.n - 1) + i});
                        const Vec3 a = g.at(i, j), b2 = g.at(i + 1, j), c = g.at(i + 1, j + 1), d = g.at(i, j + 1);
                        tracer.addTri(a, b2, c, tag);
                        tracer.addTri(a, c, d, tag);
                    }
                ++ti;
            }
        }
    }
    std::map<std::uint32_t, std::vector<MathPrim>> combos;
    std::vector<std::vector<MathPrim>> objects;
    for (const MathPrim& p : in.prims) {
        if (p.combo) combos[p.combo].push_back(p);
        else objects.push_back({p});
    }
    for (auto& [id, m] : combos) objects.push_back(m);
    for (const auto& o : objects) tracer.addPrims(o);
    // Props: their triangles block light; grouped by place (patch-sized cells) and facing,
    // they become patches that reflect light (a difference from Source, agreed 2026-11-21).
    std::vector<Patch> patches;
    std::vector<int> propPatchIds;
    const double patchSize = kDefaultLightmapScale * 4 * Q.scaleMul;
    size_t propsCasting = 0;
    for (const Prop& p : in.props) {
        if (!p.castShadows || !in.models) continue;
        const Model* m = in.models(p.model);
        if (!m) continue;
        ++propsCasting;
        auto rit = in.modelReflectivity.find(p.model);
        const Vec3 refl = rit != in.modelReflectivity.end() ? rit->second : Vec3{kDefaultReflectivity, kDefaultReflectivity, kDefaultReflectivity};
        std::map<std::tuple<long long, long long, long long, int>, int> bucket;
        for (const ModelMesh& mesh : m->meshes) {
            auto P = [&](std::uint32_t k) {
                return propToWorld(p, {mesh.pos[3 * k], mesh.pos[3 * k + 1], mesh.pos[3 * k + 2]});
            };
            const size_t nv = mesh.pos.size() / 3;
            for (size_t t = 0; t + 2 < mesh.index.size(); t += 3) {
                if (mesh.index[t] >= nv || mesh.index[t + 1] >= nv || mesh.index[t + 2] >= nv) continue;
                const Vec3 a = P(mesh.index[t]), b = P(mesh.index[t + 1]), c = P(mesh.index[t + 2]);
                const Vec3 cr = cross(b - a, c - a);
                const double area = length(cr) / 2;
                if (area < 1e-9) continue;
                const Vec3 n = cr / (2 * area), ctr = (a + b + c) / 3.0;
                const int bin = std::fabs(n.x) >= std::fabs(n.y) && std::fabs(n.x) >= std::fabs(n.z) ? (n.x > 0 ? 0 : 1)
                                : std::fabs(n.y) >= std::fabs(n.z)                                ? (n.y > 0 ? 2 : 3)
                                                                                                   : (n.z > 0 ? 4 : 5);
                const auto key = std::make_tuple((long long)std::floor(ctr.x / patchSize), (long long)std::floor(ctr.y / patchSize),
                                                 (long long)std::floor(ctr.z / patchSize), bin);
                auto it = bucket.find(key);
                int pid;
                if (it == bucket.end()) {
                    pid = (int)patches.size();
                    bucket[key] = pid;
                    patches.push_back({});
                    patches.back().refl = refl;
                    propPatchIds.push_back(pid);
                } else
                    pid = it->second;
                Patch& pa = patches[pid];
                pa.area += area;
                pa.origin += ctr * area;
                pa.normal += n * area;
                const int tag = (int)owners.size();
                owners.push_back({2, pid, 0});
                tracer.addTri(a, b, c, tag);
            }
        }
    }
    tracer.build();
    res.triangles = tracer.triangles();

    // ---- the lights
    Gather G;
    G.tracer = &tracer;
    size_t inverseSquare = 0;
    for (const Light& l : in.lights) {
        BakeLight L = prepareLight(l, in.env);
        for (const std::string& n : L.notes) res.notes.push_back(n);
        if (l.kind != LightKind::Sun && l.falloff == LightFalloff::InverseSquare) ++inverseSquare;
        G.lights.push_back(std::move(L));
    }
    if (inverseSquare)
        res.notes.push_back(std::to_string(inverseSquare) + (inverseSquare == 1 ? " light fades" : " lights fade") +
                            " with the inverse square (Hammer's default): the reach only counts with a 50% distance");
    const int skyN = kSkySamples * Q.skyMul;
    G.skyDirs = haltonSphere(skyN);
    G.skyDirsFast = haltonSphere(std::max(8, skyN / 4));
    if (fast) G.skyDirs = G.skyDirsFast;
    {
        const std::vector<Vec3> j = haltonSphere(kSunSamples - 1);
        G.sunJitter = j;
        G.sunJitterFast.assign(j.begin(), j.begin() + std::max<size_t>(1, j.size() / 4));
        if (fast) G.sunJitter = G.sunJitterFast;
    }
    G.sky = &in.sky;
    G.skyFlat = linColor(in.env.ambient);
    G.skyScale = std::max(0.0, in.env.skyLight) / 100.0;
    G.skyOn = G.skyScale > 0;
    if (in.env.minLight) G.minLight = linColor(in.env.ambient);

    // ---- smooth normals (VRAD smooths faces less than 45 degrees apart by default)
    SmoothNormals smooth;
    smooth.cosLimit = std::cos(std::clamp(in.env.smoothAngle, 0.0, 180.0) * kDeg);
    const bool smoothing = in.env.smoothAngle > 0;

    // ---- lightmap layout
    std::vector<FaceWork> faces;
    std::vector<TerrainWork> terrains;
    size_t capped = 0;
    for (size_t bi = 0; bi < in.brushes.size(); ++bi) {
        const Brush& b = in.brushes[bi];
        for (const Polygon& p : b.polygons()) {
            if (p.face < 0 || p.face >= (int)b.faces.size() || p.verts.size() < 3) continue;
            if (grids[bi] && p.face == grids[bi]->face) continue;
            if (isNoDraw(b.faces[p.face].texture)) continue;
            FaceWork w;
            w.n = p.normal;
            w.planeD = dot(p.normal, p.verts[0]);
            w.verts = p.verts;
            faces.push_back(std::move(w));
            res.faces.push_back({});
            FaceLightmap& lm = res.faces.back();
            lm.brush = b.id;
            lm.face = p.face;
            if (smoothing)
                for (const Vec3& v : p.verts) smooth.add(v, p.normal);
        }
        if (grids[bi]) {
            TerrainWork t;
            t.g = *grids[bi];
            terrains.push_back(std::move(t));
            res.faces.push_back({});
            FaceLightmap& lm = res.faces.back();
            lm.brush = b.id;
            lm.face = grids[bi]->face;
            lm.terrain = true;
        }
    }
    // Pointers now that res.faces no longer grows.
    {
        size_t fi = 0, ti = 0;
        for (FaceLightmap& lm : res.faces) {
            if (lm.terrain) terrains[ti++].out = &lm;
            else faces[fi++].out = &lm;
        }
    }
    {
        size_t fi = 0;
        for (size_t bi = 0; bi < in.brushes.size(); ++bi) {
            const Brush& b = in.brushes[bi];
            for (const Polygon& p : b.polygons()) {
                if (p.face < 0 || p.face >= (int)b.faces.size() || p.verts.size() < 3) continue;
                if (grids[bi] && p.face == grids[bi]->face) continue;
                if (isNoDraw(b.faces[p.face].texture)) continue;
                FaceWork& w = faces[fi++];
                const Face& f = b.faces[p.face];
                w.texture = f.texture;
                double L = std::clamp(f.lightmapScale, kMinLightmapScale, kMaxLightmapScale) * Q.scaleMul;
                Vec3 aS = length(f.u.axis) > 1e-9 ? normalize(f.u.axis) : Vec3{1, 0, 0};
                Vec3 aT = length(f.v.axis) > 1e-9 ? normalize(f.v.axis) : Vec3{0, 1, 0};
                if (std::fabs(dot(cross(aS, aT), w.n)) < 0.05) defaultTexAxes(w.n, aS, aT);
                for (int tries = 0; tries < 12; ++tries) {
                    double mn[2] = {1e300, 1e300}, mx[2] = {-1e300, -1e300};
                    for (const Vec3& v : w.verts) {
                        const double s = dot(aS, v) / L, t = dot(aT, v) / L;
                        mn[0] = std::min(mn[0], s); mx[0] = std::max(mx[0], s);
                        mn[1] = std::min(mn[1], t); mx[1] = std::max(mx[1], t);
                    }
                    const double s0 = std::floor(mn[0]), t0 = std::floor(mn[1]);
                    const int ww = (int)std::ceil(mx[0]) - (int)s0 + 1, hh = (int)std::ceil(mx[1]) - (int)t0 + 1;
                    if (ww <= kMaxLuxels && hh <= kMaxLuxels) {
                        w.out->axisS = aS / L;
                        w.out->axisT = aT / L;
                        w.out->s0 = s0;
                        w.out->t0 = t0;
                        w.out->w = std::max(1, ww);
                        w.out->h = std::max(1, hh);
                        break;
                    }
                    if (tries == 0) ++capped;
                    L *= 2;
                }
                const Vec3 r0 = w.out->axisS, r1 = w.out->axisT, r2 = w.n;
                const double det = dot(r0, cross(r1, r2));
                w.c0 = cross(r1, r2) / det;
                w.c1 = cross(r2, r0) / det;
                w.c2 = cross(r0, r1) / det;
                for (const Vec3& v : w.verts) {
                    double s, t;
                    w.lux(v, s, t);
                    w.st.emplace_back(s, t);
                }
                if (smoothing) {
                    for (const Vec3& v : w.verts) {
                        const Vec3 nn = smooth.normal(v, w.n);
                        w.vnormals.push_back(nn);
                        if (dot(nn, w.n) < 0.99999) w.smooth = true;
                    }
                }
            }
        }
    }
    if (capped) res.notes.push_back(std::to_string(capped) + " large faces got coarser lightmaps (at most " + std::to_string(kMaxLuxels) + " luxels a side)");
    for (TerrainWork& t : terrains) {
        const TerrainGrid& g = t.g;
        const int n = g.n;
        t.vn.resize((size_t)n * n);
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                const Vec3 du = g.at(std::min(i + 1, n - 1), j) - g.at(std::max(i - 1, 0), j);
                const Vec3 dv = g.at(i, std::min(j + 1, n - 1)) - g.at(i, std::max(j - 1, 0));
                Vec3 nn = cross(du, dv);
                if (dot(nn, g.normal) < 0) nn = -nn;
                t.vn[(size_t)j * n + i] = length(nn) > 1e-12 ? normalize(nn) : g.normal;
            }
        const Brush* b = nullptr;
        for (const Brush& bb : in.brushes) if (bb.id == t.out->brush) b = &bb;
        t.brush = b;
        const double L = std::clamp(b ? b->faces[g.face].lightmapScale : kDefaultLightmapScale, kMinLightmapScale, kMaxLightmapScale) * Q.scaleMul;
        const double lu = length(g.corners[1] - g.corners[0]), lv = length(g.corners[3] - g.corners[0]);
        t.out->w = std::clamp((int)std::ceil(lu / L) + 1, 2, kMaxLuxels);
        t.out->h = std::clamp((int)std::ceil(lv / L) + 1, 2, kMaxLuxels);
    }

    // ---- primitive lightmaps
    std::vector<PrimWork> primWork;
    for (const auto& o : objects) {
        PrimWork w;
        w.members = o;
        primWork.push_back(std::move(w));
    }
    res.prims.resize(primWork.size());
    for (size_t i = 0; i < primWork.size(); ++i) {
        PrimWork& w = primWork[i];
        for (const MathPrim& m : w.members) w.ptrs.push_back(&m);
        w.out = &res.prims[i];
        PrimLightmap& lm = *w.out;
        lm.combo = w.members[0].combo;
        lm.prim = w.members[0].combo ? 0 : w.members[0].id;
        AABB box;
        double pad = 0;
        for (const MathPrim& m : w.members) {
            if (m.op == PrimOp::Add || w.members.size() == 1) box.add(primBounds(m));
            pad = std::max(pad, m.blend);
        }
        if (!box.valid())
            for (const MathPrim& m : w.members) box.add(primBounds(m));
        box.min -= Vec3{1, 1, 1} * pad;
        box.max += Vec3{1, 1, 1} * pad;
        const Vec3 sz = box.size();
        const double largest = std::max({sz.x, sz.y, sz.z, 1.0});
        double cell = std::clamp(w.members[0].lightmapScale, kMinLightmapScale, kMaxLightmapScale) * Q.scaleMul;
        cell = std::min(cell, largest / 4);                      // at least 4 cells across
        cell = std::max(cell, largest / (kMaxCells - 4));        // at most kMaxCells
        lm.cell = cell;
        lm.origin = box.min - Vec3{1, 1, 1} * cell;
        lm.nx = (int)std::ceil(sz.x / cell) + 3;
        lm.ny = (int)std::ceil(sz.y / cell) + 3;
        lm.nz = (int)std::ceil(sz.z / cell) + 3;
        lm.rgb.assign((size_t)lm.nx * lm.ny * lm.nz * 3, 0.0f);
    }

    long long total = 0;
    for (const FaceLightmap& f : res.faces) total += (long long)f.w * f.h;
    for (const PrimLightmap& p : res.prims) total += (long long)p.nx * p.ny * p.nz / 4;
    res.luxels = 0;
    for (const FaceLightmap& f : res.faces) res.luxels += (size_t)f.w * f.h;
    for (const PrimLightmap& p : res.prims) res.cells += (size_t)p.nx * p.ny * p.nz;
    if (progress) {
        progress->total = std::max(1LL, total);
        progress->done = 0;
    }
    auto cancelled = [&] { return progress && progress->cancel.load(); };
    auto tick = [&](long long n) { if (progress) progress->done += n; };

    // ---- jobs
    std::vector<std::function<void()>> jobs;
    for (FaceWork& w : faces)
        jobs.push_back([&, wp = &w] {
            FaceWork& w = *wp;
            FaceLightmap& lm = *w.out;
            lm.rgb.assign((size_t)lm.w * lm.h * 3, 0.0f);
            std::vector<Vec3> v((size_t)lm.w * lm.h);
            auto sampleAt = [&](double s, double t, bool f) {
                w.clampInside(s, t);
                const Vec3 P = w.point(s + lm.s0, t + lm.t0);
                return G.at(P, w.shadingNormal(s, t), w.n, f);
            };
            for (int j = 0; j < lm.h && !cancelled(); ++j) {
                for (int i = 0; i < lm.w; ++i) v[(size_t)j * lm.w + i] = sampleAt(i, j, fast);
                tick(lm.w);
            }
            if (Q.superN > 1 && !cancelled()) {
                // VRAD's -extra: luxels that differ a lot from a neighbour are sampled again
                // at several points inside them and averaged.
                std::vector<char> redo((size_t)lm.w * lm.h, 0);
                for (int j = 0; j < lm.h; ++j)
                    for (int i = 0; i < lm.w; ++i) {
                        const Vec3 a = v[(size_t)j * lm.w + i];
                        auto differs = [&](int ii, int jj) {
                            if (ii < 0 || jj < 0 || ii >= lm.w || jj >= lm.h) return false;
                            const Vec3 b = v[(size_t)jj * lm.w + ii];
                            const double d = maxc({std::fabs(a.x - b.x), std::fabs(a.y - b.y), std::fabs(a.z - b.z)});
                            return d > 0.02 + 0.1 * std::max(maxc(a), maxc(b));
                        };
                        if (differs(i - 1, j) || differs(i + 1, j) || differs(i, j - 1) || differs(i, j + 1)) redo[(size_t)j * lm.w + i] = 1;
                    }
                const int N = Q.superN;
                for (int j = 0; j < lm.h && !cancelled(); ++j)
                    for (int i = 0; i < lm.w; ++i) {
                        if (!redo[(size_t)j * lm.w + i]) continue;
                        Vec3 acc;
                        for (int b = 0; b < N; ++b)
                            for (int a = 0; a < N; ++a)
                                acc += sampleAt(i - 0.5 + (a + 0.5) / N, j - 0.5 + (b + 0.5) / N, true);
                        v[(size_t)j * lm.w + i] = acc / (double)(N * N);
                    }
            }
            for (size_t k = 0; k < v.size(); ++k) {
                lm.rgb[3 * k] = (float)v[k].x;
                lm.rgb[3 * k + 1] = (float)v[k].y;
                lm.rgb[3 * k + 2] = (float)v[k].z;
            }
        });
    for (TerrainWork& t : terrains)
        jobs.push_back([&, tp = &t] {
            TerrainWork& t = *tp;
            FaceLightmap& lm = *t.out;
            lm.rgb.assign((size_t)lm.w * lm.h * 3, 0.0f);
            const int n = t.g.n;
            for (int j = 0; j < lm.h && !cancelled(); ++j) {
                for (int i = 0; i < lm.w; ++i) {
                    // Luxels on the edge sit a little inside, so they aren't on the brush's edge.
                    const double fu = std::clamp((double)i / (lm.w - 1), 0.002, 0.998), fv = std::clamp((double)j / (lm.h - 1), 0.002, 0.998);
                    Vec3 p, nn, ng;
                    t.at(fu * (n - 1), fv * (n - 1), p, nn, ng);
                    const Vec3 c = G.at(p, nn, ng, fast);
                    const size_t k = (size_t)j * lm.w + i;
                    lm.rgb[3 * k] = (float)c.x;
                    lm.rgb[3 * k + 1] = (float)c.y;
                    lm.rgb[3 * k + 2] = (float)c.z;
                }
                tick(lm.w);
            }
        });
    for (PrimWork& w : primWork)
        jobs.push_back([&, wp = &w] {
            PrimWork& w = *wp;
            PrimLightmap& lm = *w.out;
            const double cell = lm.cell;
            const size_t ncell = (size_t)lm.nx * lm.ny * lm.nz;
            w.lit.assign(ncell, 0);
            w.surf.assign(ncell, Vec3{});
            w.nrm.assign(ncell, Vec3{});
            w.sd.assign(ncell, 0.0f);
            auto idx = [&](int i, int j, int k) { return ((size_t)k * lm.ny + j) * lm.nx + i; };
            const double hgrad = std::max(0.05, cell * 0.05);
            for (int k = 0; k < lm.nz && !cancelled(); ++k) {
                for (int j = 0; j < lm.ny; ++j)
                    for (int i = 0; i < lm.nx; ++i) {
                        const Vec3 c = lm.origin + Vec3{(double)i, (double)j, (double)k} * cell;
                        const double d = w.dist(c);
                        if (std::fabs(d) > cell * 1.75) continue;
                        Vec3 g{w.dist(c + Vec3{hgrad, 0, 0}) - w.dist(c - Vec3{hgrad, 0, 0}),
                               w.dist(c + Vec3{0, hgrad, 0}) - w.dist(c - Vec3{0, hgrad, 0}),
                               w.dist(c + Vec3{0, 0, hgrad}) - w.dist(c - Vec3{0, 0, hgrad})};
                        if (length(g) < 1e-12) continue;
                        g = normalize(g);
                        // The nearest surface point (a step or two of projection).
                        Vec3 p = c - g * d;
                        for (int it = 0; it < 2; ++it) {
                            const double dd = w.dist(p);
                            if (std::fabs(dd) < 0.01) break;
                            p = p - g * dd;
                        }
                        const Vec3 col = G.at(p, g, g, fast);
                        const size_t q = idx(i, j, k);
                        lm.rgb[3 * q] = (float)col.x;
                        lm.rgb[3 * q + 1] = (float)col.y;
                        lm.rgb[3 * q + 2] = (float)col.z;
                        w.lit[q] = 1;
                        w.surf[q] = p;
                        w.nrm[q] = g;
                        w.sd[q] = (float)d;
                    }
                tick((long long)lm.nx * lm.ny / 4);
            }
        });

    // ---- run job lists on all threads
    int nt = threads > 0 ? threads : (int)std::thread::hardware_concurrency();
    nt = std::max(1, std::min(nt, 64));
    auto runJobs = [&](std::vector<std::function<void()>>& list) {
        std::atomic<size_t> next{0};
        auto worker = [&] {
            for (;;) {
                const size_t j = next++;
                if (j >= list.size() || cancelled()) return;
                list[j]();
            }
        };
        if (nt == 1 || list.size() <= 1) worker();
        else {
            std::vector<std::thread> pool;
            for (int i = 0; i < nt; ++i) pool.emplace_back(worker);
            for (std::thread& t : pool) t.join();
        }
    };
    // Big jobs first, so one large face doesn't finish last on its own.
    {
        std::vector<std::pair<long long, size_t>> order;
        size_t ji = 0;
        for (FaceWork& w : faces) order.push_back({(long long)w.out->w * w.out->h, ji++});
        for (TerrainWork& t : terrains) order.push_back({(long long)t.out->w * t.out->h, ji++});
        for (PrimWork& w : primWork) order.push_back({(long long)w.out->nx * w.out->ny * w.out->nz / 4, ji++});
        std::stable_sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first > b.first; });
        std::vector<std::function<void()>> sorted;
        for (auto& o : order) sorted.push_back(std::move(jobs[o.second]));
        jobs.swap(sorted);
    }
    runJobs(jobs);
    auto since = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    res.secondsDirect = since();

    // ======== bounces: VRAD's radiosity (vrad.cpp MakeTransfer, GatherLight, CollectLight)
    const int maxBounces = in.env.bounceScale <= 0 ? 0 : in.maxBounces >= 0 ? in.maxBounces : (fast ? 10 : 100); // strength 0: none
    const Vec3 minL = G.minLight;
    auto clampRefl = [&](Vec3 r) {
        r = r * std::max(0.0, in.env.bounceScale);
        return Vec3{std::clamp(r.x, 0.0, 0.99), std::clamp(r.y, 0.0, 0.99), std::clamp(r.z, 0.0, 0.99)};
    };
    auto texRefl = [&](const std::string& t) {
        auto it = in.reflectivity.find(t);
        return it != in.reflectivity.end() ? it->second : Vec3{kDefaultReflectivity, kDefaultReflectivity, kDefaultReflectivity};
    };
    std::vector<std::vector<int>> facePatches(faces.size());
    struct TerrainBlocks { int k = 1, nb = 1, first = 0; };
    std::vector<TerrainBlocks> terrainBlocks(terrains.size());
    std::vector<std::vector<int>> primPatches(primWork.size());
    auto insideSt = [](const std::vector<std::pair<double, double>>& st, double s, double t) {
        int sign = 0;
        for (size_t i = 0; i < st.size(); ++i) {
            const auto& a = st[i];
            const auto& b = st[(i + 1) % st.size()];
            const double c = (b.first - a.first) * (t - a.second) - (b.second - a.second) * (s - a.first);
            if (std::fabs(c) < 1e-12) continue;
            const int sg = c > 0 ? 1 : -1;
            if (sign == 0) sign = sg;
            else if (sg != sign) return false;
        }
        return true;
    };
    // The patch of face `fi` holding world point P (or the nearest).
    auto facePatchAt = [&](int fi, const Vec3& P) {
        const std::vector<int>& list = facePatches[fi];
        if (list.empty()) return -1;
        double s, t;
        faces[fi].lux(P, s, t);
        for (int id : list)
            if (insideSt(patches[id].st, s, t)) return id;
        int best = list[0];
        double bd = 1e300;
        for (int id : list) {
            const double d = lengthSq(patches[id].origin - P);
            if (d < bd) { bd = d; best = id; }
        }
        return best;
    };
    auto patchOfHit = [&](const Tracer::Hit& h, const Vec3& P) -> int {
        if (h.prim >= 0) {
            const std::vector<int>& list = primPatches[h.prim];
            int best = -1;
            double bd = 1e300;
            for (int id : list) {
                const double d = lengthSq(patches[id].origin - P);
                if (d < bd) { bd = d; best = id; }
            }
            return best;
        }
        if (h.tag < 0 || h.tag >= (int)owners.size()) return -1;
        const Owner& o = owners[h.tag];
        if (o.kind == 0) return facePatchAt(o.a, P);
        if (o.kind == 2) return o.a;
        const TerrainBlocks& tb = terrainBlocks[o.a];
        const int n1 = terrains[o.a].g.n - 1;
        const int ci = o.b % n1, cj = o.b / n1;
        return tb.first + (cj / tb.k) * tb.nb + (ci / tb.k);
    };

    if (maxBounces > 0 && !cancelled()) {
        // ---- patches on brush faces: VRAD's split along the widest world axis until each is
        // under 4 luxels (maxchop = minchop = 4)
        for (size_t fi = 0; fi < faces.size(); ++fi) {
            FaceWork& w = faces[fi];
            const double luxscale = (length(w.out->axisS) + length(w.out->axisT)) / 2; // luxels per cm
            const Vec3 refl = clampRefl(texRefl(w.texture));
            std::vector<std::vector<Vec3>> stack{w.verts};
            while (!stack.empty()) {
                std::vector<Vec3> poly = std::move(stack.back());
                stack.pop_back();
                AABB box;
                for (const Vec3& v : poly) box.add(v);
                const Vec3 total = box.size() * luxscale;
                const int axis = total.x >= total.y && total.x >= total.z ? 0 : total.y >= total.z ? 1 : 2;
                if (total[axis] >= 4 && poly.size() >= 3) {
                    std::vector<Vec3> lo, hi;
                    splitPoly(poly, axis, (box.min[axis] + box.max[axis]) / 2, lo, hi);
                    if (lo.size() >= 3 && hi.size() >= 3 && polyArea(lo) > 1e-6 && polyArea(hi) > 1e-6) {
                        stack.push_back(std::move(lo));
                        stack.push_back(std::move(hi));
                        continue;
                    }
                }
                Patch pa;
                pa.area = polyArea(poly);
                if (pa.area <= 1e-6) continue;
                pa.origin = polyCentroid(poly);
                double s, t;
                w.lux(pa.origin, s, t);
                pa.normal = w.shadingNormal(s, t);
                for (const Vec3& v : poly) {
                    double a, b;
                    w.lux(v, a, b);
                    pa.st.emplace_back(a, b);
                }
                pa.wind = std::move(poly);
                pa.refl = refl;
                facePatches[fi].push_back((int)patches.size());
                patches.push_back(std::move(pa));
            }
            // the patches' direct light: the average of the luxels on them (VRAD's AddSampleToPatch)
            const FaceLightmap& lm = *w.out;
            std::vector<Vec3> sum(facePatches[fi].size());
            std::vector<int> cnt(facePatches[fi].size(), 0);
            for (int j = 0; j < lm.h; ++j)
                for (int i = 0; i < lm.w; ++i) {
                    double s = i, t = j;
                    w.clampInside(s, t);
                    const Vec3 P = w.point(s + lm.s0, t + lm.t0);
                    const int id = facePatchAt((int)fi, P);
                    if (id < 0) continue;
                    const size_t k = (size_t)(std::find(facePatches[fi].begin(), facePatches[fi].end(), id) - facePatches[fi].begin());
                    const size_t q = ((size_t)j * lm.w + i) * 3;
                    sum[k] += Vec3{lm.rgb[q], lm.rgb[q + 1], lm.rgb[q + 2]} - minL;
                    ++cnt[k];
                }
            for (size_t k = 0; k < facePatches[fi].size(); ++k) {
                Patch& pa = patches[facePatches[fi][k]];
                pa.direct = cnt[k] ? sum[k] / (double)cnt[k] : G.at(pa.origin, pa.normal, w.n, fast) - minL;
            }
        }
        // ---- terrain: blocks of cells about 4 luxels wide
        for (size_t ti = 0; ti < terrains.size(); ++ti) {
            TerrainWork& t = terrains[ti];
            const TerrainGrid& g = t.g;
            const int n1 = g.n - 1;
            const double cellSize = std::max(1e-6, length(g.corners[1] - g.corners[0]) / n1);
            const double L = std::clamp(t.brush ? t.brush->faces[g.face].lightmapScale : kDefaultLightmapScale, kMinLightmapScale, kMaxLightmapScale) * Q.scaleMul;
            TerrainBlocks& tb = terrainBlocks[ti];
            tb.k = std::max(1, (int)std::lround(4 * L / cellSize));
            tb.nb = (n1 + tb.k - 1) / tb.k;
            tb.first = (int)patches.size();
            const Vec3 base = t.brush ? texRefl(t.brush->faces[g.face].texture) : Vec3{kDefaultReflectivity, kDefaultReflectivity, kDefaultReflectivity};
            for (int by = 0; by < tb.nb; ++by)
                for (int bx = 0; bx < tb.nb; ++bx) {
                    Patch pa;
                    Vec3 refl = base;
                    for (int cj = by * tb.k; cj < std::min(n1, (by + 1) * tb.k); ++cj)
                        for (int ci = bx * tb.k; ci < std::min(n1, (bx + 1) * tb.k); ++ci) {
                            const Vec3 a = g.at(ci, cj), b = g.at(ci + 1, cj), c = g.at(ci + 1, cj + 1), d = g.at(ci, cj + 1);
                            for (const auto& tri : {std::array<Vec3, 3>{a, b, c}, std::array<Vec3, 3>{a, c, d}}) {
                                Vec3 cr = cross(tri[1] - tri[0], tri[2] - tri[0]);
                                if (dot(cr, g.normal) < 0) cr = -cr;
                                const double ar = length(cr) / 2;
                                pa.area += ar;
                                pa.origin += (tri[0] + tri[1] + tri[2]) * (ar / 3);
                                pa.normal += cr / 2;
                            }
                        }
                    // painted layers over the base, by their average strength in the block
                    if (t.brush && t.brush->terrain)
                        for (const TerrainLayer& lay : t.brush->terrain->layers) {
                            double al = 0;
                            int an = 0;
                            for (int cj = by * tb.k; cj <= std::min(n1, (by + 1) * tb.k); ++cj)
                                for (int ci = bx * tb.k; ci <= std::min(n1, (bx + 1) * tb.k); ++ci) {
                                    const size_t k = (size_t)cj * g.n + ci;
                                    if (k < lay.alpha.size()) { al += lay.alpha[k]; ++an; }
                                }
                            if (an) refl = refl + (texRefl(lay.texture) - refl) * std::clamp(al / an, 0.0, 1.0);
                        }
                    if (pa.area > 1e-9) {
                        pa.origin = pa.origin / pa.area;
                        pa.normal = length(pa.normal) > 1e-12 ? normalize(pa.normal) : g.normal;
                    } else {
                        pa.origin = g.at(std::min(n1, bx * tb.k), std::min(n1, by * tb.k));
                        pa.normal = g.normal;
                    }
                    pa.refl = clampRefl(refl);
                    patches.push_back(std::move(pa));
                }
            const FaceLightmap& lm = *t.out;
            std::vector<Vec3> sum((size_t)tb.nb * tb.nb);
            std::vector<int> cnt((size_t)tb.nb * tb.nb, 0);
            for (int j = 0; j < lm.h; ++j)
                for (int i = 0; i < lm.w; ++i) {
                    const double u = (double)i / (lm.w - 1) * n1, v = (double)j / (lm.h - 1) * n1;
                    const int bx = std::min(tb.nb - 1, (int)(u / tb.k)), by = std::min(tb.nb - 1, (int)(v / tb.k));
                    const size_t q = ((size_t)j * lm.w + i) * 3;
                    sum[(size_t)by * tb.nb + bx] += Vec3{lm.rgb[q], lm.rgb[q + 1], lm.rgb[q + 2]} - minL;
                    ++cnt[(size_t)by * tb.nb + bx];
                }
            for (int k = 0; k < tb.nb * tb.nb; ++k) {
                Patch& pa = patches[tb.first + k];
                pa.direct = cnt[k] ? sum[k] / (double)cnt[k] : G.at(pa.origin, pa.normal, pa.normal, fast) - minL;
            }
        }
        // ---- primitives: their surface cells, in blocks of 4 cells and by facing
        for (size_t oi = 0; oi < primWork.size(); ++oi) {
            PrimWork& w = primWork[oi];
            const PrimLightmap& lm = *w.out;
            if (w.lit.empty()) continue;
            struct Acc { double area = 0; Vec3 o, n, d; int c = 0; };
            std::map<std::tuple<int, int, int, int>, Acc> blocks;
            for (int k = 0; k < lm.nz; ++k)
                for (int j = 0; j < lm.ny; ++j)
                    for (int i = 0; i < lm.nx; ++i) {
                        const size_t q = ((size_t)k * lm.ny + j) * lm.nx + i;
                        if (!w.lit[q] || std::fabs(w.sd[q]) > lm.cell * 0.5) continue;
                        const Vec3& n = w.nrm[q];
                        const int bin = std::fabs(n.x) >= std::fabs(n.y) && std::fabs(n.x) >= std::fabs(n.z) ? (n.x > 0 ? 0 : 1)
                                        : std::fabs(n.y) >= std::fabs(n.z)                                ? (n.y > 0 ? 2 : 3)
                                                                                                           : (n.z > 0 ? 4 : 5);
                        Acc& a = blocks[{i / 4, j / 4, k / 4, bin}];
                        a.area += lm.cell * lm.cell;
                        a.o += w.surf[q];
                        a.n += n;
                        a.d += Vec3{lm.rgb[3 * q], lm.rgb[3 * q + 1], lm.rgb[3 * q + 2]} - minL;
                        ++a.c;
                    }
            for (auto& [key, a] : blocks) {
                if (a.c == 0 || length(a.n) < 1e-9) continue;
                Patch pa;
                pa.area = a.area;
                pa.origin = a.o / (double)a.c;
                pa.normal = normalize(a.n);
                pa.direct = a.d / (double)a.c;
                // the colour of the piece nearest to it
                const MathPrim* near = &w.members[0];
                double bd = 1e300;
                for (const MathPrim& m : w.members) {
                    if (m.op != PrimOp::Add && w.members.size() > 1) continue;
                    const double d = std::fabs(primDistance(m, pa.origin));
                    if (d < bd) { bd = d; near = &m; }
                }
                Vec3 col = linColor(near->color);
                if (!near->texture.empty()) col = mul(col, texRefl(near->texture));
                pa.refl = clampRefl(col);
                primPatches[oi].push_back((int)patches.size());
                patches.push_back(std::move(pa));
            }
        }
        // ---- props' patches (made with their triangles above)
        for (int id : propPatchIds) {
            Patch& pa = patches[id];
            if (pa.area <= 1e-9 || length(pa.normal) < 1e-12) { pa.area = 0; continue; }
            pa.origin = pa.origin / pa.area;
            pa.normal = normalize(pa.normal);
            pa.refl = clampRefl(pa.refl);
            pa.direct = G.at(pa.origin, pa.normal, pa.normal, fast) - minL;
        }
        for (Patch& pa : patches) {
            if (pa.wind.empty() && pa.area > 0) pa.wind = squareWinding(pa.origin, pa.normal, pa.area);
            pa.direct = Vec3{std::max(0.0, pa.direct.x), std::max(0.0, pa.direct.y), std::max(0.0, pa.direct.z)};
        }
        res.patches = patches.size();

        // ---- transfers: every pair that faces each other and sees each other (no VVIS:
        // a ray per pair), tiny ones dropped; per receiver normalized to pi as VRAD
        const double kTransferMin = 1e-5 * kPi;
        if (progress) progress->total = progress->done.load() + (long long)patches.size() + 1;
        std::vector<std::function<void()>> tjobs;
        const size_t NP = patches.size();
        const size_t chunk = 16;
        // Only patches near enough can pass the minimum: a transfer is at most
        // area / distance^2, so patches of area A matter within sqrt(A / min). Patches are
        // sorted into classes by area (powers of 2), each in a grid; a receiver looks only at
        // the cells within reach of each class (the same transfers, much quicker).
        struct AreaClass { double reach = 0, cellSize = 1; std::unordered_map<std::uint64_t, std::vector<int>> cells; std::vector<int> ids; bool everywhere = false; };
        std::map<int, AreaClass> classes;
        auto cellKey = [](long long x, long long y, long long z) {
            return ((std::uint64_t)(x & 0x1FFFFF)) | ((std::uint64_t)(y & 0x1FFFFF) << 21) | ((std::uint64_t)(z & 0x1FFFFF) << 42);
        };
        for (size_t j = 0; j < NP; ++j) {
            const Patch& pj = patches[j];
            if (pj.area <= 0) continue;
            const int c = (int)std::ceil(std::log2(std::max(pj.area, 1e-6)));
            AreaClass& ac = classes[c];
            if (ac.reach == 0) {
                ac.reach = std::sqrt(std::pow(2.0, c) / kTransferMin) + 1;
                ac.cellSize = std::max(ac.reach / 4, 64.0);
            }
            const Vec3& o = pj.origin;
            ac.ids.push_back((int)j);
            ac.cells[cellKey((long long)std::floor(o.x / ac.cellSize), (long long)std::floor(o.y / ac.cellSize), (long long)std::floor(o.z / ac.cellSize))]
                .push_back((int)j);
        }
        AABB all;
        for (const Patch& pa : patches) all.add(pa.origin);
        const double extent = length(all.size());
        bool allEverywhere = true;
        for (auto& [c, ac] : classes) {
            ac.everywhere = ac.reach >= extent; // reaches across the whole map
            allEverywhere = allEverywhere && ac.everywhere;
        }
        // Each pair is looked at once (i < j): one ray for both directions.
        struct PairT { int i, j; float tij, tji; };
        std::vector<std::vector<PairT>> found((NP + chunk - 1) / chunk);
        auto transfer = [&](const Patch& recv, const Patch& send, double r2) -> double {
            if (recv.area <= 0 || send.area <= 0 || maxc(send.refl) <= 0) return 0;
            double scale = formDiffToDiff(send, recv);
            if (scale <= 0) return 0;
            if (kPi * 0.04 * r2 < send.area) {
                scale = formPolyToDiff(send, recv);
                if (scale <= 0) return 0;
            }
            const double t = send.area * scale;
            return t > kTransferMin ? t : 0;
        };
        for (size_t start = 0; start < NP; start += chunk)
            tjobs.push_back([&, start] {
                std::vector<PairT>& out = found[start / chunk];
                std::vector<int> near;
                for (size_t i = start; i < std::min(NP, start + chunk) && !cancelled(); ++i) {
                    const Patch& pi = patches[i];
                    if (pi.area <= 0) { tick(1); continue; }
                    const Vec3 oi = pi.origin + pi.normal * 1.0;
                    const double ownReach = std::sqrt(pi.area / kTransferMin) + 1; // as a sender
                    near.clear();
                    if (allEverywhere || ownReach >= extent) {
                        for (size_t j = i + 1; j < NP; ++j) near.push_back((int)j);
                    } else
                        for (const auto& [c, ac] : classes) {
                            const double reach = std::max(ac.reach, ownReach);
                            if (reach >= extent) {
                                for (int j : ac.ids) if ((size_t)j > i) near.push_back(j);
                                continue;
                            }
                            const Vec3 lo = pi.origin - Vec3{1, 1, 1} * reach, hi = pi.origin + Vec3{1, 1, 1} * reach;
                            const long long x0 = (long long)std::floor(std::max(lo.x, all.min.x) / ac.cellSize), x1 = (long long)std::floor(std::min(hi.x, all.max.x) / ac.cellSize);
                            const long long y0 = (long long)std::floor(std::max(lo.y, all.min.y) / ac.cellSize), y1 = (long long)std::floor(std::min(hi.y, all.max.y) / ac.cellSize);
                            const long long z0 = (long long)std::floor(std::max(lo.z, all.min.z) / ac.cellSize), z1 = (long long)std::floor(std::min(hi.z, all.max.z) / ac.cellSize);
                            for (long long x = x0; x <= x1; ++x)
                                for (long long y = y0; y <= y1; ++y)
                                    for (long long z = z0; z <= z1; ++z) {
                                        auto it = ac.cells.find(cellKey(x, y, z));
                                        if (it == ac.cells.end()) continue;
                                        for (int j : it->second) if ((size_t)j > i) near.push_back(j);
                                    }
                        }
                    for (int jj : near) {
                        const Patch& pj = patches[jj];
                        const Vec3 d = pj.origin - pi.origin;
                        const double r2 = lengthSq(d);
                        if (r2 < 1e-6) continue;
                        // each must face the other (VRAD leaves this to visibility)
                        if (dot(d, pi.normal) <= 0 || dot(d, pj.normal) >= 0) continue;
                        const double tij = transfer(pi, pj, r2), tji = transfer(pj, pi, r2);
                        if (tij <= 0 && tji <= 0) continue;
                        const Vec3 oj = pj.origin + pj.normal * 1.0;
                        const Vec3 dd = oj - oi;
                        const double dl = length(dd);
                        if (dl > 1e-6 && tracer.occluded(oi, dd / dl, dl - 1.0)) continue;
                        out.push_back({(int)i, jj, (float)tij, (float)tji});
                    }
                    tick(1);
                }
            });
        runJobs(tjobs);
        for (const std::vector<PairT>& list : found)
            for (const PairT& p : list) {
                if (p.tij > 0) patches[p.i].transfers.push_back({p.j, p.tij});
                if (p.tji > 0) patches[p.j].transfers.push_back({p.i, p.tji});
            }
        found.clear();
        // per receiver normalized to pi (VRAD's MakeScales: "the total transfer should be PI")
        for (Patch& pa : patches) {
            double total = 0;
            for (auto& t : pa.transfers) total += t.second;
            const double norm = total > kPi ? 1.0 / total : 1.0 / kPi;
            for (auto& t : pa.transfers) t.second = (float)(t.second * norm);
        }
        for (const Patch& pa : patches) res.transfers += pa.transfers.size();
        res.secondsTransfers = since() - res.secondsDirect;

        // ---- bounce: gather from the patches each patch sees, until a bounce adds less
        // than 1 (in VRAD's 0..255) over all the patches together
        for (Patch& pa : patches) {
            pa.emit = pa.direct;
            pa.total = {};
        }
        for (int b = 0; b < maxBounces && !cancelled(); ++b) {
            std::vector<std::function<void()>> bjobs;
            for (size_t start = 0; start < NP; start += 64)
                bjobs.push_back([&, start] {
                    for (size_t i = start; i < std::min(NP, start + 64); ++i) {
                        Vec3 sum;
                        for (const auto& [j, t] : patches[i].transfers) sum += mul(patches[j].emit, patches[j].refl) * (double)t;
                        patches[i].add = sum;
                    }
                });
            runJobs(bjobs);
            Vec3 added;
            for (Patch& pa : patches) {
                pa.total += pa.add;
                pa.emit = pa.add;
                added += pa.add;
            }
            added = added * 255.0;
            char line[96];
            std::snprintf(line, sizeof line, "Bounce #%d added RGB(%.0f, %.0f, %.0f)", b + 1, added.x, added.y, added.z);
            res.bounces.push_back(line);
            if (added.x < 1.0 && added.y < 1.0 && added.z < 1.0) break;
        }

        // ---- bounce light onto the lightmaps: brush faces (smooth blend of the face's
        // patches, as VRAD's radial blending), terrain (between its blocks), primitives
        // (a gather from the patches, as VRAD's ambient cubes)
        std::vector<std::function<void()>> fjobs;
        for (size_t fi = 0; fi < faces.size(); ++fi)
            fjobs.push_back([&, fi] {
                FaceWork& w = faces[fi];
                FaceLightmap& lm = *w.out;
                const std::vector<int>& list = facePatches[fi];
                if (list.empty()) return;
                const double R = 4.0 / std::max(1e-9, (length(w.out->axisS) + length(w.out->axisT)) / 2) * 1.25;
                for (int j = 0; j < lm.h; ++j)
                    for (int i = 0; i < lm.w; ++i) {
                        double s = i, t = j;
                        w.clampInside(s, t);
                        const Vec3 P = w.point(s + lm.s0, t + lm.t0);
                        Vec3 acc;
                        double ws = 0;
                        int nearest = list[0];
                        double nd = 1e300;
                        for (int id : list) {
                            const double d = length(patches[id].origin - P);
                            if (d < nd) { nd = d; nearest = id; }
                            const double wgt = 1 - d / R;
                            if (wgt <= 0) continue;
                            acc += patches[id].total * wgt;
                            ws += wgt;
                        }
                        const Vec3 bounce = ws > 0 ? acc / ws : patches[nearest].total;
                        const size_t q = ((size_t)j * lm.w + i) * 3;
                        lm.rgb[q] += (float)bounce.x;
                        lm.rgb[q + 1] += (float)bounce.y;
                        lm.rgb[q + 2] += (float)bounce.z;
                    }
            });
        for (size_t ti = 0; ti < terrains.size(); ++ti)
            fjobs.push_back([&, ti] {
                TerrainWork& t = terrains[ti];
                FaceLightmap& lm = *t.out;
                const TerrainBlocks& tb = terrainBlocks[ti];
                const int n1 = t.g.n - 1;
                auto P = [&](int bx, int by) {
                    bx = std::clamp(bx, 0, tb.nb - 1);
                    by = std::clamp(by, 0, tb.nb - 1);
                    return patches[tb.first + by * tb.nb + bx].total;
                };
                for (int j = 0; j < lm.h; ++j)
                    for (int i = 0; i < lm.w; ++i) {
                        const double u = (double)i / (lm.w - 1) * n1 / tb.k - 0.5, v = (double)j / (lm.h - 1) * n1 / tb.k - 0.5;
                        const int bx = (int)std::floor(u), by = (int)std::floor(v);
                        const double fu = u - bx, fv = v - by;
                        const Vec3 b = P(bx, by) * ((1 - fu) * (1 - fv)) + P(bx + 1, by) * (fu * (1 - fv)) + P(bx, by + 1) * ((1 - fu) * fv) +
                                       P(bx + 1, by + 1) * (fu * fv);
                        const size_t q = ((size_t)j * lm.w + i) * 3;
                        lm.rgb[q] += (float)b.x;
                        lm.rgb[q + 1] += (float)b.y;
                        lm.rgb[q + 2] += (float)b.z;
                    }
            });
        const std::vector<Vec3>& gdirs = fast ? G.skyDirsFast : haltonSphere(kSkySamples);
        for (size_t oi = 0; oi < primWork.size(); ++oi)
            fjobs.push_back([&, oi] {
                PrimWork& w = primWork[oi];
                PrimLightmap& lm = *w.out;
                for (size_t q = 0; q < w.lit.size() && !cancelled(); ++q) {
                    if (!w.lit[q]) continue;
                    const Vec3 n = w.nrm[q];
                    const Vec3 o = w.surf[q] + n * kOffset;
                    Vec3 acc;
                    double sumdot = 0;
                    for (const Vec3& d : gdirs) {
                        const double c = dot(n, d);
                        if (c <= 1e-6) continue;
                        sumdot += c;
                        Tracer::Hit h;
                        if (!tracer.closestHit(o, d, kFar, h)) continue;
                        const int id = patchOfHit(h, o + d * h.t);
                        if (id < 0) continue;
                        const Patch& pa = patches[id];
                        if (dot(pa.normal, d) >= 0) continue; // its back
                        acc += mul(pa.direct + pa.total, pa.refl) * c;
                    }
                    if (sumdot > 0) {
                        const Vec3 b = acc / sumdot;
                        lm.rgb[3 * q] += (float)b.x;
                        lm.rgb[3 * q + 1] += (float)b.y;
                        lm.rgb[3 * q + 2] += (float)b.z;
                    }
                }
            });
        runJobs(fjobs);
        res.secondsBounce = since() - res.secondsDirect - res.secondsTransfers;
    }

    // ---- primitives: cells far from the surface take their neighbours' light, so reading
    // between cells near the surface never mixes in black
    for (PrimWork& w : primWork) {
        PrimLightmap& lm = *w.out;
        if (w.lit.empty()) continue;
        std::vector<char> have = w.lit;
        auto idx = [&](int i, int j, int k) { return ((size_t)k * lm.ny + j) * lm.nx + i; };
        for (int pass = 0; pass < 4; ++pass) {
            std::vector<char> nh = have;
            std::vector<float> nr = lm.rgb;
            bool any = false;
            for (int k = 0; k < lm.nz; ++k)
                for (int j = 0; j < lm.ny; ++j)
                    for (int i = 0; i < lm.nx; ++i) {
                        if (have[idx(i, j, k)]) continue;
                        float acc[3] = {0, 0, 0};
                        int cnt = 0;
                        for (int dk = -1; dk <= 1; ++dk)
                            for (int dj = -1; dj <= 1; ++dj)
                                for (int di = -1; di <= 1; ++di) {
                                    const int a = i + di, b = j + dj, c = k + dk;
                                    if (a < 0 || b < 0 || c < 0 || a >= lm.nx || b >= lm.ny || c >= lm.nz) continue;
                                    const size_t q = idx(a, b, c);
                                    if (!have[q]) continue;
                                    for (int ch = 0; ch < 3; ++ch) acc[ch] += lm.rgb[3 * q + ch];
                                    ++cnt;
                                }
                        if (!cnt) continue;
                        const size_t q = idx(i, j, k);
                        for (int ch = 0; ch < 3; ++ch) nr[3 * q + ch] = acc[ch] / cnt;
                        nh[q] = 1;
                        any = true;
                    }
            have.swap(nh);
            lm.rgb.swap(nr);
            if (!any) break;
        }
    }

    res.cancelled = cancelled();
    if (progress && !res.cancelled) progress->done = progress->total.load();
    if (propsCasting) res.notes.push_back(std::to_string(propsCasting) + (propsCasting == 1 ? " prop casts" : " props cast") + " shadows");
    res.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

} // namespace scal
