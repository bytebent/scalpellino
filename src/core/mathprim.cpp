#include "core/mathprim.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace scal {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kInf = 1e30;
constexpr double kEps = 1e-12;

double tidy(double a) {
    const double r = std::round(a * 1e6) / 1e6;
    return r == 0 ? 0 : r;
}

Vec3 halfSize(const MathPrim& p) {
    Vec3 h = p.size * 0.5;
    for (int a = 0; a < 3; ++a) h[a] = std::max(std::fabs(h[a]), 1e-6);
    return h;
}

// One stretch in the shape's own space; normals in that space (not unit length).
struct LIv {
    double t0, t1;
    Vec3 n0, n1;
};

// {t : a t^2 + b t + c <= 0} as up to two stretches (open ends at +-kInf).
std::vector<std::pair<double, double>> quadNonPos(double a, double b, double c) {
    std::vector<std::pair<double, double>> out;
    if (std::fabs(a) < 1e-14) {
        if (std::fabs(b) < 1e-14) {
            if (c <= 0) out.push_back({-kInf, kInf});
        } else {
            const double r = -c / b;
            if (b > 0) out.push_back({-kInf, r});
            else out.push_back({r, kInf});
        }
        return out;
    }
    const double disc = b * b - 4 * a * c;
    if (disc < 0) {
        if (a < 0) out.push_back({-kInf, kInf});
        return out;
    }
    const double s = std::sqrt(disc);
    // Stable roots.
    const double q = -0.5 * (b + (b >= 0 ? s : -s));
    double r1 = q / a, r2 = std::fabs(q) > 1e-300 ? c / q : r1;
    if (r1 > r2) std::swap(r1, r2);
    if (a > 0) out.push_back({r1, r2});
    else {
        out.push_back({-kInf, r1});
        out.push_back({r2, kInf});
    }
    return out;
}

// Ray stretch inside lo <= o[axis] + t d[axis] <= hi, with outward normals.
std::optional<LIv> slab(int axis, double lo, double hi, const Vec3& o, const Vec3& d) {
    Vec3 nLo, nHi;
    nLo[axis] = -1;
    nHi[axis] = 1;
    if (std::fabs(d[axis]) < kEps) {
        if (o[axis] < lo || o[axis] > hi) return std::nullopt;
        return LIv{-kInf, kInf, {}, {}};
    }
    double ta = (lo - o[axis]) / d[axis], tb = (hi - o[axis]) / d[axis];
    if (d[axis] > 0) return LIv{ta, tb, nLo, nHi};
    return LIv{tb, ta, nHi, nLo};
}

std::vector<LIv> intersectL(const std::vector<LIv>& a, const std::vector<LIv>& b) {
    std::vector<LIv> out;
    for (const LIv& x : a)
        for (const LIv& y : b) {
            LIv r;
            if (x.t0 >= y.t0) { r.t0 = x.t0; r.n0 = x.n0; } else { r.t0 = y.t0; r.n0 = y.n0; }
            if (x.t1 <= y.t1) { r.t1 = x.t1; r.n1 = x.n1; } else { r.t1 = y.t1; r.n1 = y.n1; }
            if (r.t0 <= r.t1) out.push_back(r);
        }
    return out;
}

// Quadric region {Q <= 0} with gradient `grad` for normals.
template <class Grad>
std::vector<LIv> quadric(double a, double b, double c, const Vec3& o, const Vec3& d, Grad grad) {
    std::vector<LIv> out;
    for (auto [t0, t1] : quadNonPos(a, b, c)) {
        LIv iv{t0, t1, {}, {}};
        if (t0 > -kInf) iv.n0 = grad(o + d * t0);
        if (t1 < kInf) iv.n1 = grad(o + d * t1);
        out.push_back(iv);
    }
    return out;
}

std::vector<LIv> unitBox(const Vec3& o, const Vec3& d) {
    std::vector<LIv> r{{-kInf, kInf, {}, {}}};
    for (int a = 0; a < 3; ++a) {
        auto s = slab(a, -1, 1, o, d);
        if (!s) return {};
        r = intersectL(r, {*s});
        if (r.empty()) return {};
    }
    return r;
}

std::vector<LIv> sphereL(const Vec3& o, const Vec3& d) {
    return quadric(dot(d, d), 2 * dot(o, d), dot(o, o) - 1, o, d, [](const Vec3& p) { return p; });
}

std::vector<LIv> cylinderL(const Vec3& o, const Vec3& d, double zHalf = 1) {
    auto side = quadric(d.x * d.x + d.y * d.y, 2 * (o.x * d.x + o.y * d.y), o.x * o.x + o.y * o.y - 1, o, d,
                        [](const Vec3& p) { return Vec3{p.x, p.y, 0}; });
    auto s = slab(2, -zHalf, zHalf, o, d);
    if (!s) return {};
    return intersectL(side, {*s});
}

std::vector<LIv> coneL(const Vec3& o, const Vec3& d) {
    // x^2 + y^2 <= ((1 - z) / 2)^2, apex at z = 1, base radius 1 at z = -1.
    const double w0 = 1 - o.z;
    auto side = quadric(d.x * d.x + d.y * d.y - d.z * d.z / 4, 2 * (o.x * d.x + o.y * d.y) + w0 * d.z / 2,
                        o.x * o.x + o.y * o.y - w0 * w0 / 4, o, d,
                        [](const Vec3& p) { return Vec3{2 * p.x, 2 * p.y, (1 - p.z) / 2}; });
    auto s = slab(2, -1, 1, o, d);
    if (!s) return {};
    return intersectL(side, {*s});
}

std::vector<LIv> ellipsoidL(const Vec3& o, const Vec3& d, double zc, double c) {
    // x^2 + y^2 + ((z - zc) / c)^2 <= 1
    const double oz = (o.z - zc) / c, dz = d.z / c;
    return quadric(d.x * d.x + d.y * d.y + dz * dz, 2 * (o.x * d.x + o.y * d.y + oz * dz),
                   o.x * o.x + o.y * o.y + oz * oz - 1, o, d,
                   [zc, c](const Vec3& p) { return Vec3{p.x, p.y, (p.z - zc) / (c * c)}; });
}

// Convex pieces whose union is convex: one stretch from the first entry to the last exit.
std::vector<LIv> convexUnion(const std::vector<std::vector<LIv>>& parts) {
    bool any = false;
    LIv r{kInf, -kInf, {}, {}};
    for (const auto& part : parts)
        for (const LIv& x : part) {
            any = true;
            if (x.t0 < r.t0) { r.t0 = x.t0; r.n0 = x.n0; }
            if (x.t1 > r.t1) { r.t1 = x.t1; r.n1 = x.n1; }
        }
    if (!any) return {};
    return {r};
}

std::vector<LIv> capsuleL(const Vec3& o, const Vec3& d, double c) {
    const double zc = 1 - c;
    std::vector<std::vector<LIv>> parts{ellipsoidL(o, d, zc, c), ellipsoidL(o, d, -zc, c)};
    if (zc > 1e-9) parts.push_back(cylinderL(o, d, zc));
    return convexUnion(parts);
}

std::vector<LIv> discL(const Vec3& o, const Vec3& d) {
    if (std::fabs(d.z) < kEps) return {};
    const double t = -o.z / d.z;
    const Vec3 p = o + d * t;
    if (p.x * p.x + p.y * p.y > 1) return {};
    const Vec3 n{0, 0, d.z > 0 ? -1.0 : 1.0}; // faces the ray (two-sided)
    return {{t, t, n, n}};
}

// Torus in its own space: ring in the XY plane, tube radius th (the shape's z is
// stretched so the tube fills z = -1..1).
std::vector<LIv> torusL(const Vec3& o, const Vec3& d, double th) {
    const double R = 1 - th;
    auto f = [&](double t) {
        const Vec3 p = o + d * t;
        const double rho = std::sqrt(p.x * p.x + p.y * p.y), qz = p.z * th;
        return (rho - R) * (rho - R) + qz * qz - th * th;
    };
    auto grad = [&](const Vec3& p) {
        const double rho = std::max(1e-12, std::sqrt(p.x * p.x + p.y * p.y));
        const double k = 1 - R / rho;
        return Vec3{p.x * k, p.y * k, p.z * th * th};
    };
    const auto box = unitBox(o, d);
    if (box.empty()) return {};
    const double a = box[0].t0, b = box[0].t1;
    constexpr int N = 160;
    std::vector<double> roots;
    double prevT = a, prevF = f(a);
    const bool startInside = prevF < 0;
    for (int i = 1; i <= N; ++i) {
        const double t = a + (b - a) * i / N, ft = f(t);
        if ((ft < 0) != (prevF < 0)) {
            double lo = prevT, hi = t, flo = prevF;
            for (int k = 0; k < 50; ++k) {
                const double m = 0.5 * (lo + hi), fm = f(m);
                if ((fm < 0) == (flo < 0)) { lo = m; flo = fm; } else hi = m;
            }
            roots.push_back(0.5 * (lo + hi));
        }
        prevT = t;
        prevF = ft;
    }
    std::vector<LIv> out;
    size_t i = 0;
    if (startInside) { // starts inside (on the box edge: can't happen for a real torus, but be safe)
        LIv iv{a, roots.empty() ? b : roots[0], {}, {}};
        if (!roots.empty()) iv.n1 = grad(o + d * roots[0]);
        out.push_back(iv);
        i = 1;
    }
    for (; i + 1 < roots.size(); i += 2)
        out.push_back({roots[i], roots[i + 1], grad(o + d * roots[i]), grad(o + d * roots[i + 1])});
    if (i < roots.size()) out.push_back({roots[i], b, grad(o + d * roots[i]), {}});
    return out;
}

// Rounded box (in the rotated, unscaled frame): exact distance field, sphere traced.
double roundBoxSdf(const Vec3& p, const Vec3& h, double r) {
    Vec3 q;
    for (int a = 0; a < 3; ++a) q[a] = std::fabs(p[a]) - (h[a] - r);
    const Vec3 mq{std::max(q.x, 0.0), std::max(q.y, 0.0), std::max(q.z, 0.0)};
    return length(mq) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0) - r;
}

Vec3 roundBoxNormal(const Vec3& p, const Vec3& h, double r) {
    const double e = 1e-4 * std::max({h.x, h.y, h.z, 1.0});
    Vec3 n;
    for (int a = 0; a < 3; ++a) {
        Vec3 dp;
        dp[a] = e;
        n[a] = roundBoxSdf(p + dp, h, r) - roundBoxSdf(p - dp, h, r);
    }
    return normalize(n);
}

// `o`, `d` (unit) in the rotated frame.
std::vector<LIv> cubeR(const Vec3& o, const Vec3& d, const Vec3& h, double r) {
    std::vector<LIv> box{{-kInf, kInf, {}, {}}};
    for (int a = 0; a < 3; ++a) {
        auto s = slab(a, -h[a], h[a], o, d);
        if (!s) return {};
        box = intersectL(box, {*s});
        if (box.empty()) return {};
    }
    if (r <= 1e-9) return box;
    const double a = box[0].t0, b = box[0].t1;
    const double tol = 1e-7 * std::max({h.x, h.y, h.z, 1.0});
    auto trace = [&](double from, double dir, double limit, double& hit) {
        double t = from;
        for (int i = 0; i < 200; ++i) {
            const double s = roundBoxSdf(o + d * t, h, r);
            if (s < tol) { hit = t; return true; }
            t += dir * s;
            if ((dir > 0 && t > limit) || (dir < 0 && t < limit)) return false;
        }
        return false;
    };
    double t0 = 0, t1 = 0;
    if (!trace(a, 1, b, t0) || !trace(b, -1, a, t1)) return {};
    return {{t0, t1, roundBoxNormal(o + d * t0, h, r), roundBoxNormal(o + d * t1, h, r)}};
}

double capsuleCap(const MathPrim& p) {
    const Vec3 h = halfSize(p);
    return std::clamp(std::min(h.x, h.y) / h.z, 1e-3, 1.0);
}

double torusThickness(const MathPrim& p) { return std::clamp(p.thickness, 0.02, 0.5); }

std::vector<RayInterval> unionIntervals(std::vector<RayInterval> v) {
    std::sort(v.begin(), v.end(), [](const RayInterval& a, const RayInterval& b) { return a.t0 < b.t0; });
    std::vector<RayInterval> out;
    for (const RayInterval& x : v) {
        if (!out.empty() && x.t0 <= out.back().t1) {
            if (x.t1 > out.back().t1) { out.back().t1 = x.t1; out.back().n1 = x.n1; out.back().prim1 = x.prim1; }
        } else out.push_back(x);
    }
    return out;
}

std::vector<RayInterval> intersectIntervals(const std::vector<RayInterval>& a, const std::vector<RayInterval>& b) {
    std::vector<RayInterval> out;
    for (const RayInterval& x : a)
        for (const RayInterval& y : b) {
            RayInterval r = x;
            if (y.t0 > x.t0) { r.t0 = y.t0; r.n0 = y.n0; r.prim0 = y.prim0; }
            if (y.t1 < x.t1) { r.t1 = y.t1; r.n1 = y.n1; r.prim1 = y.prim1; }
            if (r.t0 <= r.t1) out.push_back(r);
        }
    return out;
}

std::vector<RayInterval> subtractIntervals(const std::vector<RayInterval>& a, const std::vector<RayInterval>& cut) {
    std::vector<RayInterval> cur = a;
    for (const RayInterval& c : cut) {
        if (c.t1 <= c.t0) continue; // zero thickness cuts nothing
        std::vector<RayInterval> next;
        for (const RayInterval& x : cur) {
            if (c.t1 <= x.t0 || c.t0 >= x.t1) { next.push_back(x); continue; }
            if (c.t0 > x.t0) {
                RayInterval l = x;
                l.t1 = c.t0;
                l.n1 = -c.n0;
                l.prim1 = c.prim0;
                next.push_back(l);
            }
            if (c.t1 < x.t1) {
                RayInterval r = x;
                r.t0 = c.t1;
                r.n0 = -c.n1;
                r.prim0 = c.prim1;
                next.push_back(r);
            }
        }
        cur = std::move(next);
    }
    return cur;
}
} // namespace

const char* primKindName(PrimKind k) {
    static const char* names[] = {"sphere", "cube", "cylinder", "cone", "torus", "capsule", "disc"};
    return names[(int)k];
}

std::optional<PrimKind> primKindFromName(const std::string& s) {
    for (int i = 0; i < kPrimKinds; ++i)
        if (s == primKindName((PrimKind)i)) return (PrimKind)i;
    return std::nullopt;
}

const char* primOpName(PrimOp o) {
    switch (o) {
        case PrimOp::Add: return "add";
        case PrimOp::Subtract: return "subtract";
        case PrimOp::Intersect: return "intersect";
    }
    return "add";
}

bool MathPrim::operator==(const MathPrim& o) const {
    return id == o.id && kind == o.kind && pos == o.pos && rot == o.rot && size == o.size && round == o.round &&
           thickness == o.thickness && op == o.op && combo == o.combo && texture == o.texture && color == o.color &&
           texScale == o.texScale && otherKeys == o.otherKeys;
}

Mat3 operator*(const Mat3& a, const Mat3& b) {
    Mat3 r;
    for (int c = 0; c < 3; ++c) r.col[c] = a * b.col[c];
    return r;
}

Mat3 axisRotation(int axis, double degrees) {
    const double c = std::cos(degrees * kDeg), s = std::sin(degrees * kDeg);
    Mat3 m;
    const int a = (axis + 1) % 3, b = (axis + 2) % 3;
    // Turning a toward b.
    m.col[a] = Vec3{};
    m.col[b] = Vec3{};
    m.col[a][a] = c;
    m.col[a][b] = s;
    m.col[b][a] = -s;
    m.col[b][b] = c;
    return m;
}

Mat3 rotationMatrix(const Vec3& r) { return axisRotation(2, r.z) * axisRotation(1, r.y) * axisRotation(0, r.x); }

Vec3 rotationAngles(const Mat3& m) {
    // m = Rz(c) Ry(b) Rx(a); element (row, col) = m.col[col][row].
    auto e = [&](int row, int col) { return m.col[col][row]; };
    const double sb = std::clamp(-e(2, 0), -1.0, 1.0);
    const double b = std::asin(sb);
    double a, c;
    if (std::fabs(std::cos(b)) > 1e-9) {
        a = std::atan2(e(2, 1), e(2, 2));
        c = std::atan2(e(1, 0), e(0, 0));
    } else { // looking straight along Y: X and Z turn the same way, keep it in X
        c = 0;
        a = std::atan2(-e(1, 2), e(1, 1));
    }
    auto angle = [](double rad) {
        double v = tidy(rad / kDeg);
        if (v <= -180) v += 360; // a half turn reads 180, not -180
        return v;
    };
    return {angle(a), angle(b), angle(c)};
}

void rotatePrim(MathPrim& p, int axis, double degrees, const Vec3& pivot) {
    const Mat3 r = axisRotation(axis, degrees);
    p.rot = rotationAngles(r * rotationMatrix(p.rot));
    p.pos = cleanVec(pivot + r * (p.pos - pivot));
}

void rotatePrim(MathPrim& p, int axis, double degrees) { rotatePrim(p, axis, degrees, p.pos); }

Vec3 primToWorld(const MathPrim& p, const Vec3& local) {
    const Vec3 h = halfSize(p);
    return p.pos + rotationMatrix(p.rot) * Vec3{local.x * h.x, local.y * h.y, local.z * h.z};
}

Vec3 primToLocal(const MathPrim& p, const Vec3& world) {
    const Vec3 h = halfSize(p);
    const Vec3 r = rotationMatrix(p.rot).transposeMul(world - p.pos);
    return {r.x / h.x, r.y / h.y, r.z / h.z};
}

std::vector<RayInterval> primIntervals(const MathPrim& p, const Vec3& o, const Vec3& d) {
    const Mat3 R = rotationMatrix(p.rot);
    const Vec3 h = halfSize(p);
    std::vector<LIv> loc;
    bool rotatedFrame = false; // cube works in the rotated, unscaled frame
    Vec3 ol, dl;
    if (p.kind == PrimKind::Cube) {
        rotatedFrame = true;
        ol = R.transposeMul(o - p.pos);
        dl = R.transposeMul(d);
        loc = cubeR(ol, dl, h, std::clamp(p.round, 0.0, std::min({h.x, h.y, h.z})));
    } else {
        const Vec3 ro = R.transposeMul(o - p.pos), rd = R.transposeMul(d);
        ol = {ro.x / h.x, ro.y / h.y, ro.z / h.z};
        dl = {rd.x / h.x, rd.y / h.y, rd.z / h.z};
        switch (p.kind) {
            case PrimKind::Sphere: loc = sphereL(ol, dl); break;
            case PrimKind::Cylinder: loc = cylinderL(ol, dl); break;
            case PrimKind::Cone: loc = coneL(ol, dl); break;
            case PrimKind::Torus: loc = torusL(ol, dl, torusThickness(p)); break;
            case PrimKind::Capsule: loc = capsuleL(ol, dl, capsuleCap(p)); break;
            case PrimKind::Disc: loc = discL(ol, dl); break;
            default: break;
        }
    }
    auto toWorldN = [&](const Vec3& n) {
        if (length(n) < 1e-300) return n;
        if (rotatedFrame) return normalize(R * n);
        return normalize(R * Vec3{n.x / h.x, n.y / h.y, n.z / h.z});
    };
    std::vector<RayInterval> out;
    for (const LIv& l : loc)
        out.push_back({l.t0, l.t1, toWorldN(l.n0), toWorldN(l.n1), p.id, p.id});
    return out;
}

std::vector<RayInterval> comboIntervals(const std::vector<const MathPrim*>& members, const Vec3& o, const Vec3& d) {
    std::vector<RayInterval> add;
    for (const MathPrim* m : members)
        if (m->op == PrimOp::Add || members.size() == 1)
            for (const RayInterval& r : primIntervals(*m, o, d)) add.push_back(r);
    std::vector<RayInterval> cur = unionIntervals(add);
    if (members.size() == 1) return cur;
    for (const MathPrim* m : members)
        if (m->op == PrimOp::Subtract) cur = subtractIntervals(cur, unionIntervals(primIntervals(*m, o, d)));
    for (const MathPrim* m : members)
        if (m->op == PrimOp::Intersect) cur = intersectIntervals(cur, unionIntervals(primIntervals(*m, o, d)));
    return cur;
}

std::vector<const MathPrim*> comboMembers(const std::vector<MathPrim>& all, std::uint32_t combo) {
    std::vector<const MathPrim*> out;
    for (const MathPrim& p : all)
        if (p.combo == combo) out.push_back(&p);
    return out;
}

std::uint32_t newComboId(const std::vector<MathPrim>& all) {
    std::uint32_t m = 0;
    for (const MathPrim& p : all) m = std::max(m, p.combo);
    return m + 1;
}

std::optional<PrimHit> rayHitPrims(const std::vector<MathPrim>& prims, const Vec3& o, const Vec3& d) {
    std::optional<PrimHit> best;
    auto consider = [&](const std::vector<const MathPrim*>& members) {
        MathPrimId first = members[0]->id;
        for (const MathPrim* m : members) first = std::min(first, m->id);
        for (const RayInterval& r : comboIntervals(members, o, d)) {
            if (r.t0 < 0) continue; // inside: skip this stretch
            if (!best || r.t0 < best->t) best = PrimHit{r.t0, r.n0, r.prim0, first};
            break;
        }
    };
    std::map<std::uint32_t, std::vector<const MathPrim*>> combos;
    for (const MathPrim& p : prims) {
        if (p.combo == 0) consider({&p});
        else combos[p.combo].push_back(&p);
    }
    for (const auto& [id, members] : combos) consider(members);
    return best;
}

AABB primBounds(const MathPrim& p) {
    AABB b;
    for (int i = 0; i < 8; ++i) {
        const Vec3 c{i & 1 ? 1.0 : -1.0, i & 2 ? 1.0 : -1.0, p.kind == PrimKind::Disc ? 0.0 : (i & 4 ? 1.0 : -1.0)};
        b.add(primToWorld(p, c));
    }
    return b;
}

std::vector<Polyline> primOutline(const MathPrim& p, int n) {
    std::vector<Polyline> loc; // in the unit shape's space
    auto circle = [&](double cz, double r, int plane = 2, Vec3 center = {}) {
        Polyline l;
        l.closed = true;
        for (int i = 0; i < n; ++i) {
            const double a = 2 * kPi * i / n;
            Vec3 q = center;
            const int u = (plane + 1) % 3, v = (plane + 2) % 3;
            q[u] += std::cos(a) * r;
            q[v] += std::sin(a) * r;
            if (plane == 2) q.z = cz;
            l.pts.push_back(q);
        }
        loc.push_back(l);
    };
    auto line = [&](const Vec3& a, const Vec3& b) { loc.push_back({{a, b}, false}); };
    const double c4[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    switch (p.kind) {
        case PrimKind::Sphere:
            circle(0, 1, 2);
            circle(0, 1, 0);
            circle(0, 1, 1);
            break;
        case PrimKind::Cube: {
            for (int a = 0; a < 3; ++a)
                for (int s1 : {-1, 1})
                    for (int s2 : {-1, 1}) {
                        Vec3 u, v;
                        u[a] = -1;
                        v[a] = 1;
                        u[(a + 1) % 3] = v[(a + 1) % 3] = s1;
                        u[(a + 2) % 3] = v[(a + 2) % 3] = s2;
                        line(u, v);
                    }
            break;
        }
        case PrimKind::Cylinder:
            circle(-1, 1);
            circle(1, 1);
            for (auto& c : c4) line({c[0], c[1], -1}, {c[0], c[1], 1});
            break;
        case PrimKind::Cone:
            circle(-1, 1);
            for (auto& c : c4) line({c[0], c[1], -1}, {0, 0, 1});
            break;
        case PrimKind::Torus: {
            const double th = torusThickness(p), R = 1 - th;
            circle(0, 1);
            circle(0, 1 - 2 * th);
            for (auto& c : c4) {
                Polyline l;
                l.closed = true;
                for (int i = 0; i < n; ++i) {
                    const double a = 2 * kPi * i / n;
                    const double rr = R + th * std::cos(a);
                    l.pts.push_back({c[0] * rr, c[1] * rr, std::sin(a)});
                }
                loc.push_back(l);
            }
            break;
        }
        case PrimKind::Capsule: {
            const double c = capsuleCap(p), zc = 1 - c;
            circle(-zc, 1);
            circle(zc, 1);
            for (auto& q : c4) line({q[0], q[1], -zc}, {q[0], q[1], zc});
            for (int plane : {0, 1})
                for (int s : {-1, 1}) {
                    Polyline l;
                    for (int i = 0; i <= n / 2; ++i) {
                        const double a = kPi * i / (n / 2);
                        Vec3 q;
                        q[plane] = std::cos(a);
                        q.z = s * (zc + c * std::sin(a));
                        l.pts.push_back(q);
                    }
                    loc.push_back(l);
                }
            break;
        }
        case PrimKind::Disc:
            circle(0, 1);
            line({-1, 0, 0}, {1, 0, 0});
            line({0, -1, 0}, {0, 1, 0});
            break;
    }
    for (Polyline& l : loc)
        for (Vec3& q : l.pts) q = primToWorld(p, q);
    return loc;
}

} // namespace scal
