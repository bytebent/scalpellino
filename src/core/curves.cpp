#include "core/curves.hpp"
#include "core/hull.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace scal {

namespace {
constexpr double kPi = 3.14159265358979323846;

struct P2 { double u, v; };

P2 roundP(P2 p, bool on) { return on ? P2{std::round(p.u), std::round(p.v)} : p; }

class Builder {
public:
    Builder(const CurveParams& p) : p_(p) {}

    Vec3 world(P2 q, double d) const {
        Vec3 w;
        w[p_.axU] = q.u;
        w[p_.axV] = q.v;
        w[p_.axD] = d;
        return w;
    }

    // Extrudes a convex 2D polygon between depths d0 and d1 into a brush.
    bool prism(const std::vector<P2>& poly, double d0, double d1, CurveResult& r) const {
        std::vector<Vec3> pts;
        for (const P2& q : poly) { pts.push_back(world(q, d0)); pts.push_back(world(q, d1)); }
        Brush tmpl;
        HullResult h = brushFromVertices(pts, tmpl);
        if (!h.ok) return false;
        for (Face& f : h.brush.faces) {
            f.texture = p_.texture;
            f.scaleU = f.scaleV = p_.texScale;
            defaultTexAxes(f.plane().n, f.u.axis, f.v.axis);
            f.u.offset = f.v.offset = 0;
            f.rotation = 0;
        }
        h.brush.id = 0;
        r.brushes.push_back(std::move(h.brush));
        return true;
    }

    P2 round(P2 q) const { return roundP(q, p_.roundToWhole); }

    // Brush from arbitrary 3D corner points (convex hull), textured like prisms.
    bool hull(const std::vector<Vec3>& pts, CurveResult& r) const {
        Brush tmpl;
        HullResult h = brushFromVertices(pts, tmpl, false);
        if (!h.ok) return false;
        for (Face& f : h.brush.faces) {
            f.texture = p_.texture;
            f.scaleU = f.scaleV = p_.texScale;
            defaultTexAxes(f.plane().n, f.u.axis, f.v.axis);
            f.u.offset = f.v.offset = 0;
            f.rotation = 0;
        }
        h.brush.id = 0;
        r.brushes.push_back(std::move(h.brush));
        return true;
    }

private:
    const CurveParams& p_;
};

// Point where the ray from c at angle a leaves the rectangle [u0,u1] x [vb,vt].
P2 rayToRect(P2 c, double a, double u0, double u1, double vb, double vt) {
    const double dx = std::cos(a), dy = std::sin(a);
    double t = 1e300;
    if (dx > 1e-12) t = std::min(t, (u1 - c.u) / dx);
    if (dx < -1e-12) t = std::min(t, (u0 - c.u) / dx);
    if (dy > 1e-12) t = std::min(t, (vt - c.v) / dy);
    if (dy < -1e-12) t = std::min(t, (vb - c.v) / dy);
    return {c.u + dx * t, c.v + dy * t};
}

CurveResult arch(const CurveParams& p, bool cutter) {
    CurveResult r;
    Builder b(p);
    const double W = p.u1 - p.u0, H = p.v1 - p.v0;
    const double Ro = W / 2;
    if (W <= 0 || H <= 0) { r.error = "draw a box first"; return r; }
    if (H < Ro - 1e-9) { r.error = "too low for a round arch: make the box at least half as tall as it is wide"; return r; }
    const int N = std::max(2, p.segments);
    const P2 c{(p.u0 + p.u1) / 2, p.v1 - Ro}; // spring line: the arch top touches the box top
    if (cutter) {
        // Cutter: the whole box, a little deeper than the wall so it cuts through.
        if (!b.prism({b.round({p.u0, p.v0}), b.round({p.u1, p.v0}), b.round({p.u1, p.v1}), b.round({p.u0, p.v1})},
                     p.d0 - 1, p.d1 + 1, r)) { r.error = "could not build the opening"; return r; }
        // Fill: the box's top corners outside the half circle, as radial slices.
        CurveResult fill;
        for (int i = 0; i < N; ++i) {
            const double a0 = kPi * i / N, a1 = kPi * (i + 1) / N;
            std::vector<P2> poly{b.round({c.u + Ro * std::cos(a0), c.v + Ro * std::sin(a0)}),
                                 b.round({c.u + Ro * std::cos(a1), c.v + Ro * std::sin(a1)}),
                                 b.round(rayToRect(c, a1, p.u0, p.u1, c.v, p.v1)),
                                 b.round(rayToRect(c, a0, p.u0, p.u1, c.v, p.v1))};
            for (const P2 corner : {P2{p.u0, p.v1}, P2{p.u1, p.v1}}) {
                const double ca = std::atan2(corner.v - c.v, corner.u - c.u);
                if (ca > a0 + 1e-9 && ca < a1 - 1e-9) poly.push_back(b.round(corner));
            }
            b.prism(poly, p.d0, p.d1, fill); // slices with no area (at the very top) are skipped
        }
        r.fill = std::move(fill.brushes);
        return r;
    }
    const double T = p.thickness;
    if (T <= 0 || T >= Ro) { r.error = "thickness must be between 0 and half the width"; return r; }
    const double Ri = Ro - T;
    for (int i = 0; i < N; ++i) {
        const double a0 = kPi * i / N, a1 = kPi * (i + 1) / N;
        std::vector<P2> poly{b.round({c.u + Ri * std::cos(a0), c.v + Ri * std::sin(a0)}),
                             b.round({c.u + Ri * std::cos(a1), c.v + Ri * std::sin(a1)})};
        if (p.flatTop) {
            poly.push_back(b.round(rayToRect(c, a1, p.u0, p.u1, c.v, p.v1)));
            poly.push_back(b.round(rayToRect(c, a0, p.u0, p.u1, c.v, p.v1)));
            // Box corners that fall inside this slice's wedge.
            for (const P2 corner : {P2{p.u0, p.v1}, P2{p.u1, p.v1}}) {
                const double ca = std::atan2(corner.v - c.v, corner.u - c.u);
                if (ca > a0 + 1e-9 && ca < a1 - 1e-9) poly.push_back(b.round(corner));
            }
        } else {
            poly.push_back(b.round({c.u + Ro * std::cos(a1), c.v + Ro * std::sin(a1)}));
            poly.push_back(b.round({c.u + Ro * std::cos(a0), c.v + Ro * std::sin(a0)}));
        }
        if (!b.prism(poly, p.d0, p.d1, r)) { r.error = "a slice came out invalid (try fewer segments or turn off rounding)"; return r; }
    }
    if (p.legs && c.v - p.v0 > 1e-9) {
        b.prism({b.round({p.u0, p.v0}), b.round({p.u0 + T, p.v0}), b.round({p.u0 + T, c.v}), b.round({p.u0, c.v})}, p.d0, p.d1, r);
        b.prism({b.round({p.u1 - T, p.v0}), b.round({p.u1, p.v0}), b.round({p.u1, c.v}), b.round({p.u1 - T, c.v})}, p.d0, p.d1, r);
    }
    return r;
}

CurveResult ring(const CurveParams& p) {
    CurveResult r;
    Builder b(p);
    const double W = p.u1 - p.u0, H = p.v1 - p.v0;
    if (W <= 0 || H <= 0) { r.error = "draw a box first"; return r; }
    const double Ro = std::min(W, H) / 2;
    const double Ri = Ro - p.thickness;
    if (p.thickness <= 0 || Ri < -1e-9) { r.error = "thickness must be between 0 and the radius"; return r; }
    const P2 c{(p.u0 + p.u1) / 2, (p.v0 + p.v1) / 2};
    double sweep = p.endDeg - p.startDeg;
    if (sweep <= 0) sweep += 360;
    const bool full = sweep >= 360 - 1e-9;
    if (full) sweep = 360;
    const int N = std::max(full ? 3 : 1, p.segments);
    if (sweep / N >= 180) { r.error = "use more segments (each slice must span less than 180 degrees)"; return r; }
    auto ang = [&](int i) { return (p.startDeg + sweep * (full ? i % N : i) / N) * kPi / 180; };
    for (int i = 0; i < N; ++i) {
        const double a0 = ang(i), a1 = ang(i + 1);
        std::vector<P2> poly;
        if (Ri > 1e-9) {
            poly.push_back(b.round({c.u + Ri * std::cos(a0), c.v + Ri * std::sin(a0)}));
            poly.push_back(b.round({c.u + Ri * std::cos(a1), c.v + Ri * std::sin(a1)}));
        } else {
            poly.push_back(b.round(c)); // solid disc / cylinder
        }
        poly.push_back(b.round({c.u + Ro * std::cos(a1), c.v + Ro * std::sin(a1)}));
        poly.push_back(b.round({c.u + Ro * std::cos(a0), c.v + Ro * std::sin(a0)}));
        if (!b.prism(poly, p.d0, p.d1, r)) { r.error = "a slice came out invalid (try fewer segments or turn off rounding)"; return r; }
    }
    return r;
}

CurveResult curvedWall(const CurveParams& p) {
    CurveResult r;
    Builder b(p);
    const P2 s{p.u0, p.v0}, e{p.u1, p.v1}, k{p.cu, p.cv};
    if (std::hypot(e.u - s.u, e.v - s.v) < 1e-9) { r.error = "draw the wall's start and end first"; return r; }
    if (p.thickness <= 0) { r.error = "thickness must be positive"; return r; }
    const int N = std::max(1, p.segments);
    auto at = [&](double t) {
        const double a = (1 - t) * (1 - t), bb = 2 * (1 - t) * t, c = t * t;
        return P2{a * s.u + bb * k.u + c * e.u, a * s.v + bb * k.v + c * e.v};
    };
    auto normalAt = [&](double t) {
        const double du = 2 * (1 - t) * (k.u - s.u) + 2 * t * (e.u - k.u);
        const double dv = 2 * (1 - t) * (k.v - s.v) + 2 * t * (e.v - k.v);
        const double l = std::hypot(du, dv);
        return l > 1e-12 ? P2{-dv / l, du / l} : P2{0, 1};
    };
    const double h = p.thickness / 2;
    std::vector<P2> left, right;
    for (int i = 0; i <= N; ++i) {
        const double t = double(i) / N;
        const P2 q = at(t), n = normalAt(t);
        left.push_back(b.round({q.u + n.u * h, q.v + n.v * h}));
        right.push_back(b.round({q.u - n.u * h, q.v - n.v * h}));
    }
    for (int i = 0; i < N; ++i) {
        const std::vector<P2> poly{left[i], left[i + 1], right[i + 1], right[i]};
        if (!b.prism(poly, p.d0, p.d1, r)) { r.error = "the curve is too tight for this thickness: use more segments, a gentler bend or a thinner wall"; return r; }
    }
    return r;
}

CurveResult spiralStairs(const CurveParams& p) {
    CurveResult r;
    Builder b(p);
    const double W = p.u1 - p.u0, H = p.v1 - p.v0;
    if (W <= 0 || H <= 0) { r.error = "draw a box first"; return r; }
    const double Ro = std::min(W, H) / 2, Ri = p.innerRadius;
    if (Ri < 0 || Ri >= Ro) { r.error = "inner radius must be between 0 and the outer radius"; return r; }
    const int N = std::max(1, p.steps);
    const double per = p.turnDeg / N;
    if (std::fabs(per) >= 180) { r.error = "use more steps (each step must turn less than 180 degrees)"; return r; }
    if (p.thickness <= 0) { r.error = "step thickness must be positive"; return r; }
    const P2 c{(p.u0 + p.u1) / 2, (p.v0 + p.v1) / 2};
    const double dir = p.clockwise ? -1 : 1;
    for (int k = 0; k < N; ++k) {
        const double a0 = (p.startDeg + dir * per * k) * kPi / 180, a1 = (p.startDeg + dir * per * (k + 1)) * kPi / 180;
        std::vector<P2> poly;
        if (Ri > 1e-9) {
            poly.push_back(b.round({c.u + Ri * std::cos(a0), c.v + Ri * std::sin(a0)}));
            poly.push_back(b.round({c.u + Ri * std::cos(a1), c.v + Ri * std::sin(a1)}));
        } else {
            poly.push_back(b.round(c));
        }
        poly.push_back(b.round({c.u + Ro * std::cos(a1), c.v + Ro * std::sin(a1)}));
        poly.push_back(b.round({c.u + Ro * std::cos(a0), c.v + Ro * std::sin(a0)}));
        const double z0 = p.d0 + k * p.rise;
        if (!b.prism(poly, z0, z0 + p.thickness, r)) { r.error = "a step came out invalid (try other sizes or turn off rounding)"; return r; }
    }
    if (p.column && Ri > 1e-9) {
        // Centre column filling the inner radius, floor to the top of the last step.
        std::vector<P2> col;
        const int n = std::max(8, p.segments);
        for (int i = 0; i < n; ++i) col.push_back(b.round({c.u + Ri * std::cos(2 * kPi * i / n), c.v + Ri * std::sin(2 * kPi * i / n)}));
        if (!b.prism(col, p.d0, p.d0 + (N - 1) * p.rise + p.thickness, r)) { r.error = "the centre column came out invalid"; return r; }
    }
    return r;
}

CurveResult helixRamp(const CurveParams& p) {
    CurveResult r;
    Builder b(p);
    const double W = p.u1 - p.u0, H = p.v1 - p.v0;
    if (W <= 0 || H <= 0) { r.error = "draw a box first"; return r; }
    const double Ro = std::min(W, H) / 2, Ri = p.innerRadius;
    if (Ri < 0 || Ri >= Ro) { r.error = "inner radius must be between 0 and the outer radius"; return r; }
    const int N = std::max(1, p.steps);
    const double per = p.turnDeg / N;
    if (std::fabs(per) >= 180) { r.error = "use more segments (each must turn less than 180 degrees)"; return r; }
    if (p.thickness <= 0) { r.error = "thickness must be positive"; return r; }
    const P2 c{(p.u0 + p.u1) / 2, (p.v0 + p.v1) / 2};
    const double dir = p.clockwise ? -1 : 1;
    // Corner points per segment edge k (shared by segments k-1 and k): exact reuse.
    auto edge = [&](int k, double& z) {
        const double a = (p.startDeg + dir * per * k) * kPi / 180;
        z = p.d0 + k * p.rise;
        return std::pair<P2, P2>{b.round(Ri > 1e-9 ? P2{c.u + Ri * std::cos(a), c.v + Ri * std::sin(a)} : c),
                                 b.round({c.u + Ro * std::cos(a), c.v + Ro * std::sin(a)})};
    };
    for (int k = 0; k < N; ++k) {
        double z0, z1;
        const auto e0 = edge(k, z0), e1 = edge(k + 1, z1);
        std::vector<Vec3> pts;
        for (const P2& q : {e0.first, e0.second}) { pts.push_back(b.world(q, z0)); pts.push_back(b.world(q, z0 + p.thickness)); }
        for (const P2& q : {e1.first, e1.second}) { pts.push_back(b.world(q, z1)); pts.push_back(b.world(q, z1 + p.thickness)); }
        if (!b.hull(pts, r)) { r.error = "a ramp piece came out invalid (try more segments or turn off rounding)"; return r; }
    }
    return r;
}

CurveResult dome(const CurveParams& p) {
    CurveResult r;
    Builder b(p);
    const double W = p.u1 - p.u0, H = p.v1 - p.v0;
    if (W <= 0 || H <= 0) { r.error = "draw a box first"; return r; }
    const double Ro = std::min(W, H) / 2, Ri = Ro - p.thickness;
    if (p.thickness <= 0 || Ri <= 0) { r.error = "thickness must be between 0 and the radius"; return r; }
    const int N = std::max(3, p.segments), M = std::max(1, p.rings);
    const P2 c{(p.u0 + p.u1) / 2, (p.v0 + p.v1) / 2};
    auto pt = [&](double R, int i, int j) {
        const double lon = 2 * kPi * (i % N) / N, lat = (kPi / 2) * j / M;
        const P2 q = b.round({c.u + R * std::cos(lat) * std::cos(lon), c.v + R * std::cos(lat) * std::sin(lon)});
        double h = p.d0 + R * std::sin(lat);
        if (p.roundToWhole) h = std::round(h);
        return b.world(q, h);
    };
    for (int j = 0; j < M; ++j)
        for (int i = 0; i < N; ++i) {
            std::vector<Vec3> pts;
            for (double R : {Ro, Ri})
                for (int dj : {0, 1})
                    for (int di : {0, 1}) pts.push_back(pt(R, i + di, j + dj));
            if (!b.hull(pts, r)) { r.error = "a dome piece came out invalid (try fewer segments or a thicker shell)"; return r; }
        }
    return r;
}

CurveResult straightStairs(const CurveParams& p) {
    CurveResult r;
    Builder b(p);
    const double W = p.u1 - p.u0, H = p.v1 - p.v0;
    if (W <= 0 || H <= 0) { r.error = "draw a box first"; return r; }
    const int N = std::max(1, p.steps);
    if (!p.solid && p.thickness <= 0) { r.error = "step thickness must be positive"; return r; }
    for (int k = 0; k < N; ++k) {
        const int idx = p.flip ? N - 1 - k : k;      // which step from the left
        const double ua = p.u0 + W * idx / N, ub = p.u0 + W * (idx + 1) / N;
        const double top = p.v0 + H * (k + 1) / N;   // k-th step from the bottom
        const double bottom = p.solid ? p.v0 : std::max(p.v0, top - p.thickness);
        if (!b.prism({b.round({ua, bottom}), b.round({ub, bottom}), b.round({ub, top}), b.round({ua, top})}, p.d0, p.d1, r)) {
            r.error = "a step came out invalid (box too small for that many steps?)";
            return r;
        }
    }
    return r;
}

// Number formatting for keys (round-trips exactly).
std::string num(double v) {
    char buf[40];
    for (int prec : {6, 10, 15, 17}) {
        std::snprintf(buf, sizeof buf, "%.*g", prec, v);
        if (std::strtod(buf, nullptr) == v) break;
    }
    return buf;
}
} // namespace

const char* curveShapeName(CurveShape s) {
    switch (s) {
        case CurveShape::Arch: return "arch";
        case CurveShape::Ring: return "ring";
        case CurveShape::CurvedWall: return "curved_wall";
        case CurveShape::SpiralStairs: return "spiral_stairs";
        case CurveShape::CarveArch: return "carve_arch";
        case CurveShape::Dome: return "dome";
        case CurveShape::HelixRamp: return "helix_ramp";
        case CurveShape::StraightStairs: return "straight_stairs";
    }
    return "arch";
}

bool curveShapeFromName(const std::string& n, CurveShape& out) {
    for (CurveShape s : {CurveShape::Arch, CurveShape::Ring, CurveShape::CurvedWall, CurveShape::SpiralStairs, CurveShape::CarveArch,
                         CurveShape::Dome, CurveShape::HelixRamp, CurveShape::StraightStairs})
        if (n == curveShapeName(s)) { out = s; return true; }
    return false;
}

KeyValues curveToKeys(const CurveParams& p) {
    return {{"shape", curveShapeName(p.shape)},
            {"axes", std::to_string(p.axU) + " " + std::to_string(p.axV) + " " + std::to_string(p.axD)},
            {"box", num(p.u0) + " " + num(p.v0) + " " + num(p.u1) + " " + num(p.v1)},
            {"depth", num(p.d0) + " " + num(p.d1)},
            {"segments", std::to_string(p.segments)},
            {"thickness", num(p.thickness)},
            {"sweep", num(p.startDeg) + " " + num(p.endDeg)},
            {"flat_top", p.flatTop ? "1" : "0"},
            {"legs", p.legs ? "1" : "0"},
            {"control", num(p.cu) + " " + num(p.cv)},
            {"stairs", num(p.innerRadius) + " " + num(p.turnDeg) + " " + num(p.rise) + " " + std::to_string(p.steps) + " " + (p.clockwise ? "1" : "0")},
            {"column", p.column ? "1" : "0"},
            {"rings", std::to_string(p.rings)},
            {"solid", p.solid ? "1" : "0"},
            {"flip", p.flip ? "1" : "0"},
            {"round", p.roundToWhole ? "1" : "0"},
            {"texture", p.texture},
            {"tex_scale", num(p.texScale)}};
}

bool curveFromKeys(const KeyValues& kv, CurveParams& p) {
    bool haveShape = false;
    for (const auto& [k, v] : kv) {
        const char* s = v.c_str();
        if (k == "shape") haveShape = curveShapeFromName(v, p.shape);
        else if (k == "axes") std::sscanf(s, "%d %d %d", &p.axU, &p.axV, &p.axD);
        else if (k == "box") std::sscanf(s, "%lf %lf %lf %lf", &p.u0, &p.v0, &p.u1, &p.v1);
        else if (k == "depth") std::sscanf(s, "%lf %lf", &p.d0, &p.d1);
        else if (k == "segments") p.segments = std::atoi(s);
        else if (k == "thickness") p.thickness = std::strtod(s, nullptr);
        else if (k == "sweep") std::sscanf(s, "%lf %lf", &p.startDeg, &p.endDeg);
        else if (k == "flat_top") p.flatTop = v == "1";
        else if (k == "legs") p.legs = v == "1";
        else if (k == "control") std::sscanf(s, "%lf %lf", &p.cu, &p.cv);
        else if (k == "stairs") { int cw = 0; std::sscanf(s, "%lf %lf %lf %d %d", &p.innerRadius, &p.turnDeg, &p.rise, &p.steps, &cw); p.clockwise = cw != 0; }
        else if (k == "round") p.roundToWhole = v == "1";
        // Single-value aliases (handy for scripts; the .map uses the combined "stairs").
        else if (k == "steps") p.steps = std::atoi(s);
        else if (k == "rise") p.rise = std::strtod(s, nullptr);
        else if (k == "turn") p.turnDeg = std::strtod(s, nullptr);
        else if (k == "inner_radius") p.innerRadius = std::strtod(s, nullptr);
        else if (k == "column") p.column = v == "1";
        else if (k == "rings") p.rings = std::atoi(s);
        else if (k == "solid") p.solid = v == "1";
        else if (k == "flip") p.flip = v == "1";
        else if (k == "texture") p.texture = v;
        else if (k == "tex_scale") p.texScale = std::strtod(s, nullptr);
    }
    const bool axesOk = p.axU >= 0 && p.axU < 3 && p.axV >= 0 && p.axV < 3 && p.axD >= 0 && p.axD < 3 &&
                        p.axU != p.axV && p.axU != p.axD && p.axV != p.axD;
    return haveShape && axesOk;
}

void shiftCurve(CurveParams& p, const Vec3& d) {
    p.u0 += d[p.axU]; p.u1 += d[p.axU]; p.cu += d[p.axU];
    p.v0 += d[p.axV]; p.v1 += d[p.axV]; p.cv += d[p.axV];
    p.d0 += d[p.axD]; p.d1 += d[p.axD];
}

CurveResult generateCurve(const CurveParams& p) {
    switch (p.shape) {
        case CurveShape::Arch: return arch(p, false);
        case CurveShape::CarveArch: return arch(p, true);
        case CurveShape::Ring: return ring(p);
        case CurveShape::CurvedWall: return curvedWall(p);
        case CurveShape::SpiralStairs: return spiralStairs(p);
        case CurveShape::Dome: return dome(p);
        case CurveShape::HelixRamp: return helixRamp(p);
        case CurveShape::StraightStairs: return straightStairs(p);
    }
    return {};
}

} // namespace scal
