#include "core/terrain.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace scal {

namespace {
constexpr double kPi = 3.14159265358979323846;

double sampleGrid(const std::vector<double>& v, int n, double s, double t) {
    const double x = std::clamp(s, 0.0, 1.0) * (n - 1), y = std::clamp(t, 0.0, 1.0) * (n - 1);
    const int i0 = std::min((int)std::floor(x), n - 2), j0 = std::min((int)std::floor(y), n - 2);
    const double fx = x - i0, fy = y - j0;
    auto at = [&](int i, int j) { return v[(size_t)j * n + i]; };
    return (at(i0, j0) * (1 - fx) + at(i0 + 1, j0) * fx) * (1 - fy) +
           (at(i0, j0 + 1) * (1 - fx) + at(i0 + 1, j0 + 1) * fx) * fy;
}

// Grid points of all terrains, with points at the same spot (same flat position and
// normal) merged into one shared point.
struct Shared {
    struct Ref { size_t brush; int idx; };
    std::vector<std::optional<TerrainGrid>> grids;  // per brush
    std::vector<std::vector<int>> groupOf;          // per brush: grid index -> shared point
    std::vector<std::vector<Ref>> members;          // per shared point
    std::vector<Vec3> base, normal;                 // per shared point

    explicit Shared(const std::vector<Brush>& brushes) {
        using Key = std::tuple<long long, long long, long long, long long, long long, long long>;
        std::map<Key, int> index;
        auto q = [](double x, double k) { return (long long)std::llround(x * k); };
        grids.resize(brushes.size());
        groupOf.resize(brushes.size());
        for (size_t b = 0; b < brushes.size(); ++b) {
            grids[b] = terrainGrid(brushes[b]);
            if (!grids[b]) continue;
            const TerrainGrid& g = *grids[b];
            groupOf[b].resize(g.base.size());
            for (size_t k = 0; k < g.base.size(); ++k) {
                const Vec3& p = g.base[k];
                const Key key{q(p.x, 64), q(p.y, 64), q(p.z, 64), q(g.normal.x, 1000), q(g.normal.y, 1000), q(g.normal.z, 1000)};
                auto it = index.find(key);
                int id;
                if (it == index.end()) {
                    id = (int)members.size();
                    index.emplace(key, id);
                    members.emplace_back();
                    base.push_back(p);
                    normal.push_back(g.normal);
                } else {
                    id = it->second;
                }
                members[id].push_back({b, (int)k});
                groupOf[b][k] = id;
            }
        }
    }
    double height(const std::vector<Brush>& bs, int g) const {
        double s = 0;
        for (const Ref& r : members[g]) s += bs[r.brush].terrain->heights[r.idx];
        return s / members[g].size();
    }
    double alpha(const std::vector<Brush>& bs, int g) const {
        double s = 0;
        for (const Ref& r : members[g]) s += bs[r.brush].terrain->alpha[r.idx];
        return s / members[g].size();
    }
    // Shared points next to g (grid neighbours in every patch that has g).
    std::vector<int> neighbours(int g) const {
        std::set<int> out;
        for (const Ref& r : members[g]) {
            const int n = grids[r.brush]->n, i = r.idx % n, j = r.idx / n;
            const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k) {
                const int a = i + di[k], c = j + dj[k];
                if (a < 0 || c < 0 || a >= n || c >= n) continue;
                out.insert(groupOf[r.brush][(size_t)c * n + a]);
            }
        }
        return {out.begin(), out.end()};
    }
};

// Repeatable pseudo-random value in [-1, 1] for a point.
double noiseAt(const Vec3& p, unsigned seed) {
    unsigned long long h = 1469598103934665603ull ^ seed;
    for (double c : {p.x, p.y, p.z}) {
        h ^= (unsigned long long)std::llround(c * 64.0);
        h *= 1099511628211ull;
        h ^= h >> 29;
    }
    h *= 0x9E3779B97F4A7C15ull;
    h ^= h >> 32;
    return (double)(h & 0xFFFFFF) / (double)0xFFFFFF * 2.0 - 1.0;
}

// Ray vs triangle (both sides). Returns t, or <0.
double rayTri(const Vec3& o, const Vec3& d, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 e1 = b - a, e2 = c - a, pv = cross(d, e2);
    const double det = dot(e1, pv);
    if (std::fabs(det) < 1e-12) return -1;
    const double inv = 1.0 / det;
    const Vec3 tv = o - a;
    const double u = dot(tv, pv) * inv;
    if (u < -1e-9 || u > 1 + 1e-9) return -1;
    const Vec3 qv = cross(tv, e1);
    const double v = dot(d, qv) * inv;
    if (v < -1e-9 || u + v > 1 + 1e-9) return -1;
    const double t = dot(e2, qv) * inv;
    return t >= 0 ? t : -1;
}
} // namespace

const char* sculptModeName(SculptMode m) {
    switch (m) {
        case SculptMode::Raise: return "raise";
        case SculptMode::Lower: return "lower";
        case SculptMode::Smooth: return "smooth";
        case SculptMode::Flatten: return "flatten";
        case SculptMode::Noise: return "noise";
        case SculptMode::Paint: return "paint";
        case SculptMode::Erase: return "erase";
    }
    return "raise";
}

const char* falloffName(Falloff f) {
    switch (f) {
        case Falloff::Soft: return "soft";
        case Falloff::Linear: return "linear";
        case Falloff::Hard: return "hard";
    }
    return "soft";
}

std::optional<std::array<Vec3, 4>> faceQuad(const Brush& b, int face, const Vec3& start) {
    if (face < 0 || face >= (int)b.faces.size()) return std::nullopt;
    for (const Polygon& p : b.polygons()) {
        if (p.face != face) continue;
        if (p.verts.size() != 4) return std::nullopt;
        int k = 0;
        for (int m = 1; m < 4; ++m)
            if (length(p.verts[m] - start) < length(p.verts[k] - start)) k = m;
        return std::array<Vec3, 4>{p.verts[k], p.verts[(k + 1) % 4], p.verts[(k + 2) % 4], p.verts[(k + 3) % 4]};
    }
    return std::nullopt;
}

std::optional<TerrainGrid> terrainGrid(const Brush& b) {
    if (!b.terrain) return std::nullopt;
    const Terrain& t = *b.terrain;
    if (t.power < kMinTerrainPower || t.power > kMaxTerrainPower) return std::nullopt;
    const int n = t.size();
    if ((int)t.heights.size() != n * n || (int)t.alpha.size() != n * n) return std::nullopt;
    const auto q = faceQuad(b, t.face, t.start);
    if (!q) return std::nullopt;
    TerrainGrid g;
    g.n = n;
    g.face = t.face;
    g.normal = b.faces[t.face].plane().n;
    g.corners = *q;
    g.base.resize((size_t)n * n);
    g.pos.resize((size_t)n * n);
    g.alpha = t.alpha;
    const Vec3 &c0 = (*q)[0], &c1 = (*q)[1], &c2 = (*q)[2], &c3 = (*q)[3];
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const double s = (double)i / (n - 1), u = (double)j / (n - 1);
            const Vec3 p = c0 * ((1 - s) * (1 - u)) + c1 * (s * (1 - u)) + c2 * (s * u) + c3 * ((1 - s) * u);
            const size_t k = (size_t)j * n + i;
            g.base[k] = p;
            g.pos[k] = p + g.normal * t.heights[k];
        }
    return g;
}

bool terrainValid(const Brush& b) { return terrainGrid(b).has_value(); }

int topQuadFace(const Brush& b) {
    int best = -1;
    double bestZ = -2;
    for (const Polygon& p : b.polygons()) {
        if (p.verts.size() != 4) continue;
        if (p.normal.z > bestZ + 1e-9) { bestZ = p.normal.z; best = p.face; }
    }
    return best;
}

std::string makeTerrain(Brush& b, int face, int power) {
    if (power < kMinTerrainPower || power > kMaxTerrainPower) return "grid size must be 2, 4, 8, 16 or 32 cells";
    if (face < 0 || face >= (int)b.faces.size()) return "no such face";
    // Resample an existing terrain on the same face.
    if (b.terrain && b.terrain->face == face && terrainValid(b)) {
        Terrain& t = *b.terrain;
        if (t.power == power) return "";
        const int on = t.size();
        Terrain nt = t;
        nt.power = power;
        const int n = nt.size();
        nt.heights.assign((size_t)n * n, 0);
        nt.alpha.assign((size_t)n * n, 0);
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                const double s = (double)i / (n - 1), u = (double)j / (n - 1);
                nt.heights[(size_t)j * n + i] = sampleGrid(t.heights, on, s, u);
                nt.alpha[(size_t)j * n + i] = sampleGrid(t.alpha, on, s, u);
            }
        t = std::move(nt);
        return "";
    }
    // New terrain: start at the lowest corner (x, then y, then z) so it's predictable.
    const auto q = faceQuad(b, face, Vec3{});
    if (!q) return "the face needs exactly 4 corners to become terrain";
    Vec3 start = (*q)[0];
    for (const Vec3& c : *q)
        if (std::tie(c.x, c.y, c.z) < std::tie(start.x, start.y, start.z)) start = c;
    Terrain t;
    t.face = face;
    t.power = power;
    t.start = start;
    const int n = t.size();
    t.heights.assign((size_t)n * n, 0);
    t.alpha.assign((size_t)n * n, 0);
    b.terrain = std::move(t);
    return "";
}

void transposeTerrain(Terrain& t) {
    const int n = t.size();
    if ((int)t.heights.size() != n * n || (int)t.alpha.size() != n * n) return;
    for (int j = 0; j < n; ++j)
        for (int i = j + 1; i < n; ++i) {
            std::swap(t.heights[(size_t)j * n + i], t.heights[(size_t)i * n + j]);
            std::swap(t.alpha[(size_t)j * n + i], t.alpha[(size_t)i * n + j]);
        }
}

double falloffWeight(Falloff f, double d, double r) {
    if (r <= 0 || d > r) return 0;
    const double t = d / r;
    switch (f) {
        case Falloff::Soft: return 0.5 * (1 + std::cos(kPi * t));
        case Falloff::Linear: return 1 - t;
        case Falloff::Hard: return 1;
    }
    return 0;
}

bool sculpt(std::vector<Brush>& brushes, const SculptDab& dab) {
    Shared sh(brushes);
    const int G = (int)sh.members.size();
    std::vector<double> newH(G), newA(G);
    std::vector<char> touched(G, 0);
    const double k = std::clamp(dab.strength, 0.0, 1.0);
    for (int g = 0; g < G; ++g) {
        const Vec3 v = sh.base[g] - dab.center;
        const double along = dot(v, sh.normal[g]);
        const double d = std::sqrt(std::max(0.0, dot(v, v) - along * along));
        const double w = falloffWeight(dab.falloff, d, dab.radius);
        if (w <= 0) continue;
        touched[g] = 1;
        const double h = sh.height(brushes, g), a = sh.alpha(brushes, g);
        double nh = h, na = a;
        switch (dab.mode) {
            case SculptMode::Raise: nh = h + w * dab.amount; break;
            case SculptMode::Lower: nh = h - w * dab.amount; break;
            case SculptMode::Noise: nh = h + w * dab.amount * noiseAt(sh.base[g], dab.seed); break;
            case SculptMode::Smooth: {
                const std::vector<int> nb = sh.neighbours(g);
                if (nb.empty()) break;
                double avg = 0;
                for (int o : nb) avg += sh.height(brushes, o);
                avg /= nb.size();
                nh = h + w * k * (avg - h);
                break;
            }
            case SculptMode::Flatten: {
                const double target = dot(dab.flattenTarget - sh.base[g], sh.normal[g]);
                nh = h + w * k * (target - h);
                break;
            }
            case SculptMode::Paint: na = a + w * k * (1 - a); break;
            case SculptMode::Erase: na = a - w * k * a; break;
        }
        newH[g] = nh;
        newA[g] = std::clamp(na, 0.0, 1.0);
    }
    bool changed = false;
    std::set<size_t> painted;
    for (int g = 0; g < G; ++g) {
        if (!touched[g]) continue;
        for (const Shared::Ref& r : sh.members[g]) {
            Terrain& t = *brushes[r.brush].terrain;
            if (std::fabs(t.heights[r.idx] - newH[g]) > 1e-12 || std::fabs(t.alpha[r.idx] - newA[g]) > 1e-12) changed = true;
            t.heights[r.idx] = newH[g];
            t.alpha[r.idx] = newA[g];
            if (dab.mode == SculptMode::Paint) painted.insert(r.brush);
        }
    }
    if (!dab.texture.empty())
        for (size_t b : painted)
            if (brushes[b].terrain->texture2 != dab.texture) { brushes[b].terrain->texture2 = dab.texture; changed = true; }
    return changed;
}

int stitchTerrain(std::vector<Brush>& brushes) {
    Shared sh(brushes);
    int count = 0;
    for (int g = 0; g < (int)sh.members.size(); ++g) {
        if (sh.members[g].size() < 2) continue;
        const double h = sh.height(brushes, g), a = sh.alpha(brushes, g);
        bool diff = false;
        for (const Shared::Ref& r : sh.members[g]) {
            Terrain& t = *brushes[r.brush].terrain;
            if (std::fabs(t.heights[r.idx] - h) > 1e-9 || std::fabs(t.alpha[r.idx] - a) > 1e-9) diff = true;
            t.heights[r.idx] = h;
            t.alpha[r.idx] = a;
        }
        if (diff) ++count;
    }
    return count;
}

bool applyHeightmap(std::vector<Brush>& brushes, const std::vector<float>& grey, int w, int h,
                    double low, double high) {
    if (w < 1 || h < 1 || (int)grey.size() < w * h) return false;
    Shared sh(brushes);
    if (sh.members.empty()) return false;
    // Lay the image over the terrains as seen along the first terrain's normal.
    const Vec3 n0 = sh.normal[0];
    int D = 0;
    for (int a = 1; a < 3; ++a)
        if (std::fabs(n0[a]) > std::fabs(n0[D])) D = a;
    const int U = D == 0 ? 1 : 0, V = D == 2 ? 1 : 2;
    double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
    for (const Vec3& p : sh.base) { u0 = std::min(u0, p[U]); u1 = std::max(u1, p[U]); v0 = std::min(v0, p[V]); v1 = std::max(v1, p[V]); }
    if (u1 - u0 < 1e-9 || v1 - v0 < 1e-9) return false;
    auto px = [&](int x, int y) { return (double)grey[(size_t)std::clamp(y, 0, h - 1) * w + std::clamp(x, 0, w - 1)]; };
    for (int g = 0; g < (int)sh.members.size(); ++g) {
        const double fx = (sh.base[g][U] - u0) / (u1 - u0) * (w - 1);
        const double fy = (v1 - sh.base[g][V]) / (v1 - v0) * (h - 1); // image top = high V
        const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        const double ax = fx - x0, ay = fy - y0;
        const double gv = (px(x0, y0) * (1 - ax) + px(x0 + 1, y0) * ax) * (1 - ay) +
                          (px(x0, y0 + 1) * (1 - ax) + px(x0 + 1, y0 + 1) * ax) * ay;
        const double height = low + std::clamp(gv, 0.0, 1.0) * (high - low);
        for (const Shared::Ref& r : sh.members[g]) brushes[r.brush].terrain->heights[r.idx] = height;
    }
    return true;
}

double rayHitGrid(const TerrainGrid& g, const Vec3& o, const Vec3& d) {
    double best = -1;
    for (int j = 0; j + 1 < g.n; ++j)
        for (int i = 0; i + 1 < g.n; ++i) {
            const Vec3 a = g.at(i, j), bb = g.at(i + 1, j), c = g.at(i + 1, j + 1), e = g.at(i, j + 1);
            for (double t : {rayTri(o, d, a, bb, c), rayTri(o, d, a, c, e)})
                if (t >= 0 && (best < 0 || t < best)) best = t;
        }
    return best;
}

double rayHitTerrain(const Brush& b, const Vec3& o, const Vec3& d) {
    const auto g = terrainGrid(b);
    return g ? rayHitGrid(*g, o, d) : -1;
}

} // namespace scal
