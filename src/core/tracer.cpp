#include "core/tracer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace scal {

namespace {
constexpr int kBins = 16;
constexpr int kMaxLeaf = 4;

struct FBox {
    float mn[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    float mx[3] = {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
    void add(const float p[3]) {
        for (int i = 0; i < 3; ++i) {
            mn[i] = std::min(mn[i], p[i]);
            mx[i] = std::max(mx[i], p[i]);
        }
    }
    void add(const FBox& b) {
        for (int i = 0; i < 3; ++i) {
            mn[i] = std::min(mn[i], b.mn[i]);
            mx[i] = std::max(mx[i], b.mx[i]);
        }
    }
    float area() const {
        const float x = mx[0] - mn[0], y = mx[1] - mn[1], z = mx[2] - mn[2];
        if (x < 0 || y < 0 || z < 0) return 0;
        return x * y + y * z + z * x;
    }
};

// A binned surface-area split of items [first, first + count) (their boxes and centres);
// fills `nodes` (children next to each other) and reorders `idx`.
struct Builder {
    const std::vector<FBox>& boxes;
    const std::vector<std::array<float, 3>>& centres;
    std::vector<int>& idx;
    template <typename Node>
    void build(std::vector<Node>& nodes) {
        nodes.clear();
        if (idx.empty()) return;
        nodes.reserve(idx.size() * 2);
        nodes.push_back({});
        split(nodes, 0, 0, (int)idx.size());
    }
    template <typename Node>
    void split(std::vector<Node>& nodes, int me, int first, int count) {
        FBox box, cb;
        for (int i = first; i < first + count; ++i) {
            box.add(boxes[idx[i]]);
            cb.add(centres[idx[i]].data());
        }
        for (int a = 0; a < 3; ++a) {
            nodes[me].mn[a] = box.mn[a];
            nodes[me].mx[a] = box.mx[a];
        }
        auto leaf = [&] {
            nodes[me].a = first;
            nodes[me].n = count;
        };
        if (count <= 2) return leaf();
        int axis = 0;
        for (int a = 1; a < 3; ++a)
            if (cb.mx[a] - cb.mn[a] > cb.mx[axis] - cb.mn[axis]) axis = a;
        const float lo = cb.mn[axis], ext = cb.mx[axis] - cb.mn[axis];
        auto median = [&] {
            const int m = first + count / 2;
            std::nth_element(idx.begin() + first, idx.begin() + m, idx.begin() + first + count,
                             [&](int x, int y) { return centres[x][axis] < centres[y][axis]; });
            return m;
        };
        int mid;
        if (ext <= 0) { // all centres in one spot
            if (count <= kMaxLeaf) return leaf();
            mid = median();
        } else {
            FBox bb[kBins];
            int bc[kBins] = {};
            auto binOf = [&](int it) { return std::min(kBins - 1, std::max(0, (int)((centres[it][axis] - lo) / ext * kBins))); };
            for (int i = first; i < first + count; ++i) {
                const int b = binOf(idx[i]);
                bb[b].add(boxes[idx[i]]);
                ++bc[b];
            }
            // cost of the split after bin k: area(left) * n(left) + area(right) * n(right)
            float rightArea[kBins] = {};
            int rightCount[kBins] = {};
            FBox acc;
            int n = 0;
            for (int k = kBins - 1; k > 0; --k) {
                acc.add(bb[k]);
                n += bc[k];
                rightArea[k] = acc.area();
                rightCount[k] = n;
            }
            FBox left;
            int nl = 0, best = -1;
            float bestCost = std::numeric_limits<float>::max();
            for (int k = 0; k < kBins - 1; ++k) {
                left.add(bb[k]);
                nl += bc[k];
                if (nl == 0 || rightCount[k + 1] == 0) continue;
                const float c = left.area() * nl + rightArea[k + 1] * rightCount[k + 1];
                if (c < bestCost) {
                    bestCost = c;
                    best = k;
                }
            }
            if (best < 0) mid = median();
            else {
                if (count <= kMaxLeaf && bestCost >= box.area() * count) return leaf();
                auto it = std::partition(idx.begin() + first, idx.begin() + first + count, [&](int i) { return binOf(i) <= best; });
                mid = (int)(it - idx.begin());
                if (mid == first || mid == first + count) mid = median();
            }
        }
        const int l = (int)nodes.size();
        nodes.push_back({});
        nodes.push_back({});
        nodes[me].a = l;
        nodes[me].n = 0;
        split(nodes, l, first, mid - first);
        split(nodes, l + 1, mid, first + count - mid);
    }
};
} // namespace

// The ray in a mesh's space, single precision, with what the box test needs.
struct Tracer::Ray {
    Vec3 o, d;          // double (world, or the instance's space)
    float of[3], df[3], inv[3], oi[3]; // oi = o * inv (the box test is then one multiply-add)
    void set(const Vec3& origin, const Vec3& dir) {
        o = origin;
        d = dir;
        for (int i = 0; i < 3; ++i) {
            of[i] = (float)o[i];
            df[i] = (float)d[i];
            const double di = d[i] != 0 ? d[i] : 1e-30;
            inv[i] = (float)(1.0 / di);
            oi[i] = of[i] * inv[i];
        }
    }
};

namespace {
template <typename Node>
inline bool slab(const Node& n, const float* /*o*/, const float inv[3], const float oi[3], float tmax, float& tnear) {
    float t0 = 0, t1 = tmax;
    for (int a = 0; a < 3; ++a) {
        float ta = n.mn[a] * inv[a] - oi[a], tb = n.mx[a] * inv[a] - oi[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = ta > t0 ? ta : t0;
        t1 = tb < t1 ? tb : t1;
    }
    tnear = t0;
    return t0 <= t1;
}
} // namespace

int Tracer::addMask(const Mask& m) {
    masks_.push_back(m);
    return (int)masks_.size() - 1;
}

void Tracer::addTri(const Vec3& a, const Vec3& b, const Vec3& c, int tag, int mask) {
    if (length(cross(b - a, c - a)) < 1e-9) return;
    Mesh& w = meshes_[0];
    w.src.push_back({a, b, c});
    w.srcTag.push_back(tag);
    w.srcMask.push_back(mask);
}

int Tracer::addMesh(const std::vector<std::array<Vec3, 3>>& tris) {
    Mesh m;
    m.src = tris;
    m.srcTag.resize(tris.size());
    for (size_t k = 0; k < tris.size(); ++k) m.srcTag[k] = (int)k;
    m.srcMask.assign(tris.size(), -1);
    meshes_.push_back(std::move(m));
    return (int)meshes_.size() - 1;
}

bool Tracer::addInstance(int mesh, const Vec3 c[3], const Vec3& pos, int tagBase) {
    // lin = [c0 c1 c2] (columns); its inverse by cofactors
    const double a = c[0].x, b = c[1].x, cc = c[2].x, d = c[0].y, e = c[1].y, f = c[2].y, g = c[0].z, h = c[1].z, k = c[2].z;
    const double det = a * (e * k - f * h) - b * (d * k - f * g) + cc * (d * h - e * g);
    if (std::fabs(det) < 1e-18) return false;
    Inst in;
    in.mesh = mesh;
    in.identity = false;
    const double id = 1.0 / det;
    const double m[9] = {(e * k - f * h) * id, (cc * h - b * k) * id, (b * f - cc * e) * id, (f * g - d * k) * id, (a * k - cc * g) * id,
                         (cc * d - a * f) * id, (d * h - e * g) * id, (b * g - a * h) * id, (a * e - b * d) * id};
    std::copy(m, m + 9, in.m);
    in.pos = pos;
    in.tagBase = tagBase;
    // world box: the mesh's triangles placed (computed in build())
    for (const auto& t : meshes_[mesh].src)
        for (const Vec3& v : t) in.box.add(pos + c[0] * v.x + c[1] * v.y + c[2] * v.z);
    insts_.push_back(in);
    return true;
}

void Tracer::addPrims(std::vector<MathPrim> members) {
    PrimObj o;
    o.members = std::move(members);
    double pad = 0;
    for (const MathPrim& m : o.members) {
        o.box.add(primBounds(m));
        pad = std::max(pad, m.blend);
    }
    o.box.min -= Vec3{1, 1, 1} * (1 + pad);
    o.box.max += Vec3{1, 1, 1} * (1 + pad);
    prims_.push_back(std::move(o));
}

void Tracer::buildMesh(Mesh& m) {
    const size_t n = m.src.size();
    std::vector<FBox> boxes(n);
    std::vector<std::array<float, 3>> centres(n);
    for (size_t i = 0; i < n; ++i) {
        for (const Vec3& v : m.src[i]) {
            const float p[3] = {(float)v.x, (float)v.y, (float)v.z};
            boxes[i].add(p);
            m.box.add(v);
        }
        const Vec3 c = (m.src[i][0] + m.src[i][1] + m.src[i][2]) / 3.0;
        centres[i] = {(float)c.x, (float)c.y, (float)c.z};
    }
    std::vector<int> idx(n);
    for (size_t i = 0; i < n; ++i) idx[i] = (int)i;
    Builder b{boxes, centres, idx};
    b.build(m.nodes);
    m.tris.resize(n);
    m.tag.resize(n);
    m.mask.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const auto& s = m.src[idx[i]];
        const Vec3 e1 = s[1] - s[0], e2 = s[2] - s[0];
        FTri& t = m.tris[i];
        for (int a = 0; a < 3; ++a) {
            t.a[a] = (float)s[0][a];
            t.e1[a] = (float)e1[a];
            t.e2[a] = (float)e2[a];
        }
        m.tag[i] = m.srcTag[idx[i]];
        m.mask[i] = m.srcMask[idx[i]];
    }
    m.src.clear();
    m.src.shrink_to_fit();
    m.srcTag.clear();
    m.srcMask.clear();
}

void Tracer::build() {
    for (PrimObj& o : prims_) {
        o.ptrs.clear();
        for (const MathPrim& m : o.members) o.ptrs.push_back(&m);
    }
    for (Mesh& m : meshes_) buildMesh(m);
    // the world mesh is instance 0 (no transform)
    if (!meshes_[0].tris.empty()) {
        Inst w;
        w.box = meshes_[0].box;
        insts_.insert(insts_.begin(), w);
    }
    std::vector<FBox> boxes(insts_.size());
    std::vector<std::array<float, 3>> centres(insts_.size());
    for (size_t i = 0; i < insts_.size(); ++i) {
        const AABB& b = insts_[i].box;
        const float mn[3] = {(float)b.min.x, (float)b.min.y, (float)b.min.z}, mx[3] = {(float)b.max.x, (float)b.max.y, (float)b.max.z};
        boxes[i].add(mn);
        boxes[i].add(mx);
        const Vec3 c = b.center();
        centres[i] = {(float)c.x, (float)c.y, (float)c.z};
    }
    topIdx_.resize(insts_.size());
    for (size_t i = 0; i < insts_.size(); ++i) topIdx_[i] = (int)i;
    Builder b{boxes, centres, topIdx_};
    b.build(top_);
    // boxes kept in float: grow them a hair so rounding never cuts a triangle off
    auto grow = [](std::vector<FNode>& nodes) {
        for (FNode& n : nodes)
            for (int a = 0; a < 3; ++a) {
                const float e = 1e-5f * std::max(1.0f, std::max(std::fabs(n.mn[a]), std::fabs(n.mx[a])));
                n.mn[a] -= e;
                n.mx[a] += e;
            }
    };
    grow(top_);
    for (Mesh& m : meshes_) grow(m.nodes);
}

size_t Tracer::triangles() const {
    size_t n = 0;
    for (const Inst& in : insts_) n += meshes_[in.mesh].tris.size();
    return n;
}

bool Tracer::seeThrough(int mi, const Vec3& P) const {
    if (mi < 0) return false;
    const Mask& m = masks_[mi];
    const BakeMaterial& bm = *m.mat;
    if (bm.texW <= 0 || bm.texH <= 0 || bm.alpha.size() < (size_t)bm.texW * bm.texH) return false;
    const double u = dot(P, m.uAxis) + m.uOff, v = dot(P, m.vAxis) + m.vOff;
    long long x = (long long)std::floor(u) % bm.texW, y = (long long)std::floor(v) % bm.texH;
    if (x < 0) x += bm.texW;
    if (y < 0) y += bm.texH;
    return bm.alpha[(size_t)y * bm.texW + (size_t)x] < bm.alphaRef * 255.0;
}

// Searches one mesh (in its own space, r): nearer boxes first. AnyHit: stops at the first.
template <bool AnyHit>
bool Tracer::traceMesh(const Inst& inst, const Ray& r, double& best, int& tag) const {
    const Mesh& m = meshes_[inst.mesh];
    if (m.nodes.empty()) return false;
    struct Entry { int node; float t; };
    Entry stack[128];
    int sp = 0;
    bool found = false;
    float tn = 0;
    if (!slab(m.nodes[0], r.of, r.inv, r.oi, (float)best, tn)) return false;
    int node = 0;
    for (;;) {
        const FNode& nd = m.nodes[node];
        if (nd.n > 0) {
            for (int i = nd.a; i < nd.a + nd.n; ++i) {
                const FTri& t = m.tris[i];
                // Moller-Trumbore in double, as before (the triangle is stored in float; brush
                // corners on the grid are exact there): edge cases fall the same way
                const Vec3 A{t.a[0], t.a[1], t.a[2]}, e1{t.e1[0], t.e1[1], t.e1[2]}, e2{t.e2[0], t.e2[1], t.e2[2]};
                const Vec3 p = cross(r.d, e2);
                const double det = dot(e1, p);
                if (std::fabs(det) < 1e-12) continue;
                const double inv = 1.0 / det;
                const Vec3 sv = r.o - A;
                const double u = dot(sv, p) * inv;
                if (u < 0 || u > 1) continue;
                const Vec3 q = cross(sv, e1);
                const double v = dot(r.d, q) * inv;
                if (v < 0 || u + v > 1) continue;
                const double tt = dot(e2, q) * inv;
                if (!(tt > 1e-4 && tt < best)) continue;
                if (m.mask[i] >= 0 && seeThrough(m.mask[i], r.o + r.d * tt)) continue; // world mesh only
                best = tt;
                tag = m.tag[i] < 0 ? -1 : inst.tagBase + m.tag[i];
                found = true;
                if (AnyHit) return true;
            }
        } else {
            const int c0 = nd.a, c1 = nd.a + 1;
            float t0 = 0, t1 = 0;
            const bool h0 = slab(m.nodes[c0], r.of, r.inv, r.oi, (float)best, t0);
            const bool h1 = slab(m.nodes[c1], r.of, r.inv, r.oi, (float)best, t1);
            if (h0 && h1) {
                const bool firstNear = t0 <= t1;
                if (sp < 128) stack[sp++] = {firstNear ? c1 : c0, firstNear ? t1 : t0};
                node = firstNear ? c0 : c1;
                continue;
            }
            if (h0) { node = c0; continue; }
            if (h1) { node = c1; continue; }
        }
        // next from the stack (boxes now farther than the best hit are skipped)
        bool next = false;
        while (sp) {
            const Entry e = stack[--sp];
            if (!AnyHit && e.t > (float)best) continue;
            node = e.node;
            next = true;
            break;
        }
        if (!next) break;
    }
    return found;
}

bool Tracer::closestHit(const Vec3& o, const Vec3& d, double maxT, Hit& hit) const {
    double best = maxT;
    bool any = false;
    if (!top_.empty()) {
        Ray wr;
        wr.set(o, d);
        struct Entry { int node; float t; };
        Entry stack[96];
        int sp = 0;
        float tn = 0;
        if (slab(top_[0], wr.of, wr.inv, wr.oi, (float)best, tn)) stack[sp++] = {0, tn};
        while (sp) {
            const Entry e = stack[--sp];
            if (e.t > (float)best) continue;
            const FNode& nd = top_[e.node];
            if (nd.n > 0) {
                for (int i = nd.a; i < nd.a + nd.n; ++i) {
                    const Inst& in = insts_[topIdx_[i]];
                    Ray r;
                    if (in.identity) r = wr;
                    else {
                        const Vec3 rel = o - in.pos;
                        r.set({in.m[0] * rel.x + in.m[1] * rel.y + in.m[2] * rel.z, in.m[3] * rel.x + in.m[4] * rel.y + in.m[5] * rel.z,
                               in.m[6] * rel.x + in.m[7] * rel.y + in.m[8] * rel.z},
                              {in.m[0] * d.x + in.m[1] * d.y + in.m[2] * d.z, in.m[3] * d.x + in.m[4] * d.y + in.m[5] * d.z,
                               in.m[6] * d.x + in.m[7] * d.y + in.m[8] * d.z});
                    }
                    int tag = -1;
                    if (traceMesh<false>(in, r, best, tag)) {
                        hit.t = best;
                        hit.tag = tag;
                        hit.prim = -1;
                        any = true;
                    }
                }
                continue;
            }
            float t0 = 0, t1 = 0;
            const bool h0 = slab(top_[nd.a], wr.of, wr.inv, wr.oi, (float)best, t0);
            const bool h1 = slab(top_[nd.a + 1], wr.of, wr.inv, wr.oi, (float)best, t1);
            if (sp + 2 > 96) continue;
            // the nearer one on top of the stack
            if (h0 && h1) {
                if (t0 <= t1) { stack[sp++] = {nd.a + 1, t1}; stack[sp++] = {nd.a, t0}; }
                else { stack[sp++] = {nd.a, t0}; stack[sp++] = {nd.a + 1, t1}; }
            } else if (h0) stack[sp++] = {nd.a, t0};
            else if (h1) stack[sp++] = {nd.a + 1, t1};
        }
    }
    const Vec3 inv{1.0 / (d.x != 0 ? d.x : 1e-30), 1.0 / (d.y != 0 ? d.y : 1e-30), 1.0 / (d.z != 0 ? d.z : 1e-30)};
    auto hitBox = [&](const AABB& b, double maxT2) {
        double t0 = 0, t1 = maxT2;
        for (int a = 0; a < 3; ++a) {
            double ta = (b.min[a] - o[a]) * inv[a], tb = (b.max[a] - o[a]) * inv[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
        return true;
    };
    for (size_t pi = 0; pi < prims_.size(); ++pi) {
        const PrimObj& p = prims_[pi];
        if (!hitBox(p.box, best)) continue;
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

bool Tracer::occluded(const Vec3& o, const Vec3& d, double maxT) const {
    if (!top_.empty()) {
        Ray wr;
        wr.set(o, d);
        int stack[96];
        int sp = 0;
        float tn = 0;
        if (slab(top_[0], wr.of, wr.inv, wr.oi, (float)maxT, tn)) stack[sp++] = 0;
        while (sp) {
            const FNode& nd = top_[stack[--sp]];
            if (nd.n > 0) {
                for (int i = nd.a; i < nd.a + nd.n; ++i) {
                    const Inst& in = insts_[topIdx_[i]];
                    Ray r;
                    if (in.identity) r = wr;
                    else {
                        const Vec3 rel = o - in.pos;
                        r.set({in.m[0] * rel.x + in.m[1] * rel.y + in.m[2] * rel.z, in.m[3] * rel.x + in.m[4] * rel.y + in.m[5] * rel.z,
                               in.m[6] * rel.x + in.m[7] * rel.y + in.m[8] * rel.z},
                              {in.m[0] * d.x + in.m[1] * d.y + in.m[2] * d.z, in.m[3] * d.x + in.m[4] * d.y + in.m[5] * d.z,
                               in.m[6] * d.x + in.m[7] * d.y + in.m[8] * d.z});
                    }
                    double best = maxT;
                    int tag = -1;
                    if (traceMesh<true>(in, r, best, tag)) return true;
                }
                continue;
            }
            float t0 = 0, t1 = 0;
            if (sp + 2 > 96) continue;
            if (slab(top_[nd.a], wr.of, wr.inv, wr.oi, (float)maxT, t0)) stack[sp++] = nd.a;
            if (slab(top_[nd.a + 1], wr.of, wr.inv, wr.oi, (float)maxT, t1)) stack[sp++] = nd.a + 1;
        }
    }
    const Vec3 inv{1.0 / (d.x != 0 ? d.x : 1e-30), 1.0 / (d.y != 0 ? d.y : 1e-30), 1.0 / (d.z != 0 ? d.z : 1e-30)};
    for (const PrimObj& p : prims_) {
        double t0 = 0, t1 = maxT;
        bool in = true;
        for (int a = 0; a < 3 && in; ++a) {
            double ta = (p.box.min[a] - o[a]) * inv[a], tb = (p.box.max[a] - o[a]) * inv[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) in = false;
        }
        if (!in) continue;
        const std::vector<RayInterval> iv = p.members.size() == 1 ? primIntervals(p.members[0], o, d) : comboIntervals(p.ptrs, o, d);
        for (const RayInterval& r : iv)
            if (r.t1 > 1e-3 && r.t0 < maxT) return true;
    }
    return false;
}

} // namespace scal
