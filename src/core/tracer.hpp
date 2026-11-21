#pragma once
// The light bake's ray tracer (core/bake.cpp). Speed-up before v0.12.4b (2026-11-21): the same
// rays and the same answers as before, found faster:
//  - each mesh gets a tree of boxes split by surface area (binned SAH) instead of halves by
//    count;
//  - the nearer child box is searched first, so "the nearest hit" stops sooner;
//  - triangles and boxes are stored in single precision (the ray's start is subtracted in
//    double, so big maps keep their accuracy);
//  - a prop model is stored once and placed many times (instances: the ray is moved into
//    the model's own space), so 1000 rocks are one rock in memory.
// Brush faces, terrain and anything else go into the world mesh (addTri); primitives are
// tested exactly, as before.
#include "core/bake.hpp"
#include "core/mathprim.hpp"
#include <array>
#include <vector>

namespace scal {

class Tracer {
public:
    // A cutout ($alphatest, v0.12.4): texel (u, v) = (dot(P, uAxis) + uOff, dot(P, vAxis) + vOff),
    // wrapped; a hit where the picture's alpha is under the reference doesn't count.
    struct Mask {
        const BakeMaterial* mat = nullptr;
        Vec3 uAxis, vAxis;
        double uOff = 0, vOff = 0;
    };
    int addMask(const Mask& m);
    // A world triangle. `tag`: what it belongs to (closestHit reports it; -1 = nothing that
    // bounces); `mask`: its cutout (-1 none).
    void addTri(const Vec3& a, const Vec3& b, const Vec3& c, int tag = -1, int mask = -1);
    // A model's triangles in its own space, stored once; returns its index. Degenerate
    // triangles are kept (they are never hit) so local index k stays triangle k.
    int addMesh(const std::vector<std::array<Vec3, 3>>& tris);
    // The mesh placed in the world: world = lin * local + pos. Local triangle k reports the
    // tag tagBase + k. False if `lin` can't be inverted (a scale of 0): nothing added.
    bool addInstance(int mesh, const Vec3 linCols[3], const Vec3& pos, int tagBase);
    void addPrims(std::vector<MathPrim> members);
    void build();
    size_t triangles() const;          // in the world (an instance counts its mesh again)
    size_t primObjects() const { return prims_.size(); }

    // The nearest thing along the ray (d unit), up to maxT: its distance, the triangle's tag
    // (or -1) and the primitive object's index (or -1). False if nothing.
    struct Hit { double t = 0; int tag = -1; int prim = -1; };
    bool closestHit(const Vec3& o, const Vec3& d, double maxT, Hit& hit) const;
    // Anything between o and o + d * maxT (d unit)?
    bool occluded(const Vec3& o, const Vec3& d, double maxT) const;

private:
    struct FTri { float a[3], e1[3], e2[3]; };
    // A box of the tree. Leaf: n > 0 triangles from `a`. Inside: children a and a + 1.
    struct FNode { float mn[3]; int a; float mx[3]; int n; };
    struct Mesh {
        std::vector<std::array<Vec3, 3>> src; // until build()
        std::vector<int> srcTag, srcMask;
        std::vector<FTri> tris;               // in leaf order
        std::vector<int> tag, mask;
        std::vector<FNode> nodes;
        AABB box;
    };
    struct Inst {
        int mesh = 0;
        bool identity = true;
        double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // world -> local (rows)
        Vec3 pos;
        int tagBase = 0;
        AABB box;                              // in the world
    };
    struct PrimObj { std::vector<MathPrim> members; std::vector<const MathPrim*> ptrs; AABB box; };
    struct Ray;

    std::vector<Mesh> meshes_{1};              // 0: the world mesh
    std::vector<Inst> insts_;
    std::vector<FNode> top_;                   // the tree over the instances
    std::vector<int> topIdx_;
    std::vector<Mask> masks_;
    std::vector<PrimObj> prims_;

    static void buildMesh(Mesh& m);
    template <bool AnyHit>
    bool traceMesh(const Inst& in, const Ray& r, double& best, int& tag) const;
    bool seeThrough(int mask, const Vec3& P) const;
};

} // namespace scal
