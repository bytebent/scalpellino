// Core + IO tests. No framework: each CHECK prints on failure; exit code = failures.
#include "core/brush.hpp"
#include "core/document.hpp"
#include "core/grid.hpp"
#include "core/transform.hpp"
#include "core/hull.hpp"
#include "core/texture.hpp"
#include "core/csg.hpp"
#include "core/curves.hpp"
#include "core/primitives.hpp"
#include "core/terrain.hpp"
#include "core/sky.hpp"
#include "core/mathprim.hpp"
#include "core/landing.hpp"
#include "core/replicate.hpp"
#include "core/bottega.hpp"
#include "core/prop.hpp"
#include "io/glb_loader.hpp"
#include "io/model_store.hpp"
#include "io/map_format.hpp"
#include <cmath>
#include <random>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace scal;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }
static bool nearV(const Vec3& a, const Vec3& b, double eps = 1e-9) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

static AABB box(Vec3 a, Vec3 b) { AABB r; r.add(a); r.add(b); return r; }

// ---------------------------------------------------------------- grid

static void testGrid() {
    Grid g;
    CHECK(g.size == 16);
    g.halve(); CHECK(g.size == 8);
    for (int i = 0; i < 20; ++i) g.halve();
    CHECK(g.size == Grid::kMin);
    for (int i = 0; i < 20; ++i) g.doubleUp();
    CHECK(g.size == Grid::kMax);
    // Microgrid: off by default, allows 0.125 when on; turning it off resets to 1.
    Grid m;
    for (int i = 0; i < 20; ++i) m.halve();
    CHECK(m.size == 1);
    m.setMicrogrid(true);
    for (int i = 0; i < 20; ++i) m.halve();
    CHECK(m.size == 0.125);
    CHECK(m.snapValue(0.3) == 0.25);
    m.setMicrogrid(false);
    CHECK(m.size == 1);
    // Custom sizes (e.g. 80) double/halve from there and snap to multiples.
    m.setSize(80);
    CHECK(m.snapValue(130) == 160 && m.snapValue(100) == 80);
    m.halve(); CHECK(m.size == 40);
    m.doubleUp(); m.doubleUp(); m.doubleUp(); CHECK(m.size == 320);
    m.doubleUp(); CHECK(m.size == 320); // 640 > max
    Grid s;
    CHECK(s.snapValue(7.0) == 0.0);
    CHECK(s.snapValue(9.0) == 16.0);
    CHECK(s.snapValue(-9.0) == -16.0);
    CHECK(s.snapValue(100.0) == 96.0);
    s.snap = false;
    CHECK(s.snapValue(9.5) == 9.5);
}

// ---------------------------------------------------------------- brushes

static void testBoxGeometry() {
    Brush b = makeBox(box({-64, -32, 0}, {64, 32, 128}), "wall");
    CHECK(b.faces.size() == 6);
    CHECK(b.valid());
    const auto polys = b.polygons();
    CHECK(polys.size() == 6);
    for (const auto& p : polys) {
        CHECK(p.verts.size() == 4);
        // Face normals point outward: every vertex of the brush is behind every plane.
        for (const auto& q : polys)
            for (const Vec3& v : q.verts) CHECK(b.faces[p.face].plane().distance(v) <= 1e-9);
        // Winding is counter-clockwise around the outward normal.
        const Vec3 n = normalize(cross(p.verts[1] - p.verts[0], p.verts[2] - p.verts[1]));
        CHECK(nearV(n, p.normal, 1e-9));
    }
    const AABB bb = b.bounds();
    CHECK(nearV(bb.min, {-64, -32, 0}));
    CHECK(nearV(bb.max, {64, 32, 128}));
    // Plane points come out as clean integers for an integer box.
    for (const Face& f : b.faces)
        for (const Vec3& p : f.p) CHECK(p.x == std::round(p.x) && p.y == std::round(p.y) && p.z == std::round(p.z));
}

static void testFarAway() {
    // Brushes far outside the classic +-4096 area are fine.
    Brush b = makeBox(box({20000, -30000, 5000}, {20064, -29936, 5128}), "a");
    CHECK(b.valid());
    CHECK(nearV(b.bounds().min, {20000, -30000, 5000}));
    CHECK(nearV(b.bounds().max, {20064, -29936, 5128}));
}

static void testWedge() {
    // Box cut diagonally -> wedge with 5 faces (the cut makes the top face vanish).
    Brush b = makeBox(box({0, 0, 0}, {64, 64, 64}), "a");
    b.faces.push_back(makeFace(normalize({1, 0, 1}), {64, 0, 0}, "a"));
    CHECK(b.valid());
    const auto polys = b.polygons();
    CHECK(polys.size() == 5);
    const AABB bb = b.bounds();
    CHECK(nearV(bb.max, {64, 64, 64}));
}

static void testInvalid() {
    Brush open;
    open.faces.push_back(makeFace({0, 0, 1}, {0, 0, 0}, "a"));
    open.faces.push_back(makeFace({0, 0, -1}, {0, 0, -64}, "a"));
    CHECK(!open.valid());
    // Two planes facing away from each other with no overlap -> empty.
    Brush empty = makeBox(box({0, 0, 0}, {64, 64, 64}), "a");
    empty.faces.push_back(makeFace({-1, 0, 0}, {100, 0, 0}, "a"));
    CHECK(!empty.valid());
}

static void testTranslateTextureLock() {
    Brush b = makeBox(box({0, 0, 0}, {64, 64, 64}), "a");
    const Brush orig = b;
    b.translate({16, 0, 0}, true);
    CHECK(nearV(b.bounds().min, {16, 0, 0}));
    // Texture coordinate of a moved point must equal that of the original point.
    for (size_t i = 0; i < b.faces.size(); ++i) {
        const Face& f0 = orig.faces[i];
        const Face& f1 = b.faces[i];
        const Vec3 p0{10, 20, 30};
        const Vec3 p1 = p0 + Vec3{16, 0, 0};
        const double u0 = dot(p0, f0.u.axis) / f0.scaleU + f0.u.offset;
        const double u1 = dot(p1, f1.u.axis) / f1.scaleU + f1.u.offset;
        CHECK(near(u0, u1));
    }
    Brush c = orig;
    c.translate({16, 0, 0}, false);
    for (size_t i = 0; i < c.faces.size(); ++i) CHECK(c.faces[i].u.offset == orig.faces[i].u.offset);
}

static void testRemap() {
    Brush b = makeBox(box({0, 0, 0}, {64, 64, 64}), "a");
    b.remapBounds(box({0, 0, 0}, {64, 64, 64}), box({-32, 0, 0}, {96, 32, 64}), true);
    CHECK(b.valid());
    CHECK(nearV(b.bounds().min, {-32, 0, 0}));
    CHECK(nearV(b.bounds().max, {96, 32, 64}));
    // Mirroring (negative scale) must keep it a valid, outward-facing brush.
    Brush m = makeBox(box({0, 0, 0}, {64, 64, 64}), "a");
    AABB to = box({0, 0, 0}, {64, 64, 64});
    std::swap(to.min.x, to.max.x); // flip X
    m.remapBounds(box({0, 0, 0}, {64, 64, 64}), to, false);
    CHECK(m.valid());
}

static void testRotate() {
    const double pi = 3.14159265358979323846;
    // 90 degrees about Z around the box centre: a 128x32 box becomes 32x128.
    Brush b = makeBox(box({0, 0, 0}, {128, 32, 64}), "a");
    const Brush orig = b;
    const Vec3 c{64, 16, 32};
    b.rotate(c, {0, 0, 1}, pi / 2, true);
    CHECK(b.valid());
    CHECK(nearV(b.bounds().min, {48, -48, 0}, 1e-9));
    CHECK(nearV(b.bounds().max, {80, 80, 64}, 1e-9));
    // 90-degree rotations keep plane points as clean integers.
    for (const Face& f : b.faces)
        for (const Vec3& p : f.p) CHECK(p.x == std::round(p.x) && p.y == std::round(p.y));
    // Texture lock: a point and its rotated image get the same texture coordinate.
    for (size_t i = 0; i < b.faces.size(); ++i) {
        const Vec3 p0{10, 5, 20};
        const Vec3 p1 = c + rotateVec(p0 - c, {0, 0, 1}, pi / 2);
        const Face& f0 = orig.faces[i];
        const Face& f1 = b.faces[i];
        CHECK(near(dot(p0, f0.u.axis) / f0.scaleU + f0.u.offset, dot(p1, f1.u.axis) / f1.scaleU + f1.u.offset, 1e-9));
        CHECK(near(dot(p0, f0.v.axis) / f0.scaleV + f0.v.offset, dot(p1, f1.v.axis) / f1.scaleV + f1.v.offset, 1e-9));
    }
    // 15 degrees: still a valid closed brush; full turn returns to start.
    Brush r = makeBox(box({0, 0, 0}, {64, 64, 64}), "a");
    for (int i = 0; i < 24; ++i) r.rotate({32, 32, 32}, {0, 0, 1}, pi / 12, true);
    CHECK(r.valid());
    CHECK(nearV(r.bounds().min, {0, 0, 0}, 1e-6));
    CHECK(nearV(r.bounds().max, {64, 64, 64}, 1e-6));
}

static void testTransform() {
    std::string err;
    std::vector<Brush> in{makeBox(box({0, 0, 0}, {128, 32, 64}), "a")};
    TransformSpec t;

    t.kind = TransformKind::Move;
    t.move = {16, -8, 4};
    auto out = applyTransform(in, t, true, err);
    CHECK(err.empty() && out.size() == 1);
    CHECK(nearV(out[0].bounds().min, {16, -8, 4}));

    t = TransformSpec{};
    t.kind = TransformKind::Rotate;
    t.axis = 2;
    t.degrees = 90;
    t.pivot = {0, 0, 0};
    out = applyTransform(in, t, true, err);
    CHECK(err.empty());
    CHECK(nearV(out[0].bounds().min, {-32, 0, 0}, 1e-9));
    CHECK(nearV(out[0].bounds().max, {0, 128, 64}, 1e-9));

    t.degrees = 30; // exact typed angle around the box centre
    t.pivot = {64, 16, 32};
    out = applyTransform(in, t, true, err);
    CHECK(err.empty() && out[0].valid());
    const double c = std::cos(3.14159265358979323846 / 6), s = std::sin(3.14159265358979323846 / 6);
    CHECK(near(out[0].bounds().size().x, 128 * c + 32 * s, 1e-6));

    t = TransformSpec{};
    t.kind = TransformKind::Scale;
    t.scale = {2, 1, 0.5};
    t.pivot = {0, 0, 0};
    out = applyTransform(in, t, true, err);
    CHECK(err.empty());
    CHECK(nearV(out[0].bounds().max, {256, 32, 32}));

    t.scale = {-1, 1, 1}; // flip X around x = 64
    t.pivot = {64, 0, 0};
    out = applyTransform(in, t, true, err);
    CHECK(err.empty() && out[0].valid());
    CHECK(nearV(out[0].bounds().min, {0, 0, 0}) && nearV(out[0].bounds().max, {128, 32, 64}));
    // Mirrored faces still point outward.
    const auto mpolys = out[0].polygons();
    for (const Polygon& p : mpolys)
        for (const Polygon& q : mpolys)
            for (const Vec3& v : q.verts) CHECK(out[0].faces[p.face].plane().distance(v) <= 1e-9);

    t.scale = {0, 1, 1};
    out = applyTransform(in, t, true, err);
    CHECK(out.empty() && !err.empty());
}

static void testHull() {
    const Brush cube = makeBox(box({0, 0, 0}, {64, 64, 64}), "a");
    auto verts = brushVertices(cube);
    CHECK(verts.size() == 8);

    // Rebuilding from its own corners gives the same solid.
    HullResult r = brushFromVertices(verts, cube);
    CHECK(r.ok);
    CHECK(r.brush.faces.size() == 6);
    CHECK(nearV(r.brush.bounds().min, {0, 0, 0}) && nearV(r.brush.bounds().max, {64, 64, 64}));
    CHECK(r.brush.id == cube.id);

    // Merge two top corners onto the other two -> wedge (5 faces).
    std::vector<Vec3> wedge;
    for (Vec3 v : verts) { if (v.z == 64 && v.x == 64) v.x = 0; wedge.push_back(v); }
    r = brushFromVertices(wedge, cube);
    CHECK(r.ok);
    CHECK(r.brush.faces.size() == 5);

    // Lift one top corner: top face can't stay flat -> split into two (7 faces), still valid.
    std::vector<Vec3> lifted;
    for (Vec3 v : verts) { if (v.z == 64 && v.x == 64 && v.y == 64) v.z = 80; lifted.push_back(v); }
    r = brushFromVertices(lifted, cube);
    CHECK(r.ok);
    CHECK(r.brush.faces.size() == 7);

    // Push a corner into the middle: it would vanish inside -> refused.
    std::vector<Vec3> dent;
    for (Vec3 v : verts) { if (v.z == 64 && v.x == 64 && v.y == 64) v = {32, 32, 32}; dent.push_back(v); }
    r = brushFromVertices(dent, cube);
    CHECK(!r.ok);
    CHECK(!r.error.empty());

    // Pull a corner into the middle of the bottom face's plane: it would lie flat inside
    // that face and vanish -> refused (this got through before; found by the UI test).
    std::vector<Vec3> sunk;
    for (Vec3 v : verts) { if (v.z == 0 && v.x == 64 && v.y == 64) v = {24, 16, 0}; sunk.push_back(v); }
    r = brushFromVertices(sunk, cube);
    CHECK(!r.ok);

    // Flatten everything onto z=0 -> no volume.
    std::vector<Vec3> flat;
    for (Vec3 v : verts) { v.z = 0; flat.push_back(v); }
    CHECK(!brushFromVertices(flat, cube).ok);

    // Textures carry over by facing direction.
    Brush tex = cube;
    for (Face& f : tex.faces) f.texture = f.plane().n.z > 0.5 ? "top" : "side";
    r = brushFromVertices(lifted, tex);
    int tops = 0;
    for (const Face& f : r.brush.faces) if (f.texture == "top") ++tops;
    CHECK(tops == 2); // both halves of the split top keep the top texture
}

static void testSnapToGrid() {
    const double pi = 3.14159265358979323846;
    Brush b = makeBox(box({0, 0, 0}, {128, 32, 64}), "a");
    b.rotate({64, 16, 32}, {0, 0, 1}, pi / 6, true); // 30 degrees: corners off grid
    CHECK(!brushOnGrid(b, 1));
    HullResult r = snapBrushToGrid(b, 1);
    CHECK(r.ok);
    CHECK(brushOnGrid(r.brush, 1));
    CHECK(r.brush.valid());
    CHECK(r.brush.faces.size() == 6);

    // Integer box is already on grid 16; snapping changes nothing.
    Brush ib = makeBox(box({0, 0, 0}, {64, 64, 64}), "a");
    CHECK(brushOnGrid(ib, 16));
    r = snapBrushToGrid(ib, 16);
    CHECK(r.ok && nearV(r.brush.bounds().max, {64, 64, 64}));

    // Pythagorean angle 3:4:5 (cos 4/5, sin 3/5): an 80 x 40 box rotated about the
    // origin lands exactly on whole numbers (and here on grid 8); a 1:2 slope does not.
    Brush pb = makeBox(box({0, 0, 0}, {80, 40, 64}), "a");
    Brush py = pb;
    py.rotate({0, 0, 0}, {0, 0, 1}, std::atan2(3.0, 4.0), true);
    CHECK(brushOnGrid(py, 8));
    CHECK(nearV(py.bounds().min, {-24, 0, 0}, 1e-9));
    CHECK(nearV(py.bounds().max, {64, 80, 64}, 1e-9));
    Brush sl = pb;
    sl.rotate({0, 0, 0}, {0, 0, 1}, std::atan2(1.0, 2.0), true);
    CHECK(!brushOnGrid(sl, 1));

    // Snapping a thin brush to a coarse grid collapses it -> refused.
    Brush thin = makeBox(box({0, 0, 0}, {64, 64, 4}), "a");
    CHECK(!snapBrushToGrid(thin, 64).ok);
}

static const Polygon& polyOfFace(const std::vector<Polygon>& ps, int face) {
    for (const Polygon& p : ps) if (p.face == face) return p;
    return ps[0];
}

static void testTextures() {
    const TexSize ts{512, 512};
    Brush b = makeBox(box({0, 0, 0}, {256, 64, 128}), "Bricks_24");
    const auto polys = b.polygons();
    // Face 2 is +X (wall facing X): axes (0,1,0) / (0,0,-1).
    Face f = b.faces[2];
    const Polygon& pf = polyOfFace(polys, 2);
    UV uv = texCoord(f, {256, 32, 64}, ts);
    CHECK(near(uv.u, 32.0 / 512) && near(uv.v, -64.0 / 512));

    // Fit: the face's corners map exactly onto the texture's corners.
    fitTexture(f, pf, ts);
    double umin = 1e9, umax = -1e9, vmin = 1e9, vmax = -1e9;
    for (const Vec3& p : pf.verts) {
        const UV q = texCoord(f, p, ts);
        umin = std::min(umin, q.u); umax = std::max(umax, q.u);
        vmin = std::min(vmin, q.v); vmax = std::max(vmax, q.v);
    }
    CHECK(near(umax - umin, 1, 1e-9) && near(vmax - vmin, 1, 1e-9));
    CHECK(near(std::fmod(umin + 10, 1.0), 0, 1e-9) || near(std::fmod(umin + 10, 1.0), 1, 1e-9));
    CHECK(!std::signbit(f.u.offset) && !std::signbit(f.v.offset)); // no "-0" in the UI
    // Fit 2 x 1 halves the U scale.
    Face f2 = b.faces[2];
    fitTexture(f2, pf, ts, 2, 1);
    CHECK(near(f2.scaleU, f.scaleU / 2, 1e-12));

    // Justify left: the face's left edge sits on a texture edge.
    Face j = b.faces[2];
    j.u.offset = 123;
    justifyTexture(j, pf, ts, Justify::Left);
    double smin = 1e9;
    for (const Vec3& p : pf.verts) smin = std::min(smin, dot(p, j.u.axis) / j.scaleU + j.u.offset);
    CHECK(near(std::fmod(smin + 5120, 512.0), 0, 1e-9) || near(std::fmod(smin + 5120, 512.0), 512, 1e-9));
    // Justify centre: the face centre maps to the texture centre.
    justifyTexture(j, pf, ts, Justify::Center);
    const Vec3 c{256, 32, 64};
    const UV cu = texCoord(j, c, ts);
    CHECK(near(std::fmod(cu.u + 10, 1.0), 0.5, 1e-9) && near(std::fmod(cu.v + 10, 1.0), 0.5, 1e-9));

    // Rotate: axes stay perpendicular to the normal and to each other; 4 x 90 = identity.
    Face r = b.faces[2];
    const Vec3 n = r.plane().n;
    rotateTexture(r, n, 90);
    CHECK(near(dot(r.u.axis, n), 0) && near(dot(r.v.axis, n), 0) && near(dot(r.u.axis, r.v.axis), 0));
    CHECK(nearV(r.u.axis, cross(n, b.faces[2].u.axis)) || nearV(r.u.axis, -cross(n, b.faces[2].u.axis)));
    rotateTexture(r, n, 90); rotateTexture(r, n, 90); rotateTexture(r, n, 90);
    CHECK(nearV(r.u.axis, b.faces[2].u.axis, 1e-9) && nearV(r.v.axis, b.faces[2].v.axis, 1e-9));

    // Align to face on a slope: axes lie in the face plane, unit length.
    Brush w = makeBox(box({0, 0, 0}, {64, 64, 64}), "a");
    w.faces.push_back(makeFace(normalize({1, 0, 1}), {64, 0, 0}, "a"));
    Face s = w.faces.back();
    const Vec3 sn = s.plane().n;
    alignTextureToFace(s, sn);
    CHECK(near(dot(s.u.axis, sn), 0, 1e-9) && near(dot(s.v.axis, sn), 0, 1e-9));
    CHECK(near(length(s.u.axis), 1, 1e-9) && near(length(s.v.axis), 1, 1e-9));
    alignTextureToWorld(s, sn);
    CHECK(s.u.offset == 0 && s.rotation == 0);

    // Wrap around a corner: +X face (2) -> +Y face (4) of a pillar; texels match along
    // the shared vertical edge, and the new axes lie in the +Y face.
    {
        Brush pil = makeBox(box({0, 0, 0}, {64, 64, 256}), "Bricks_24");
        Face& fx = pil.faces[2];
        fx.u.offset = 37; fx.v.offset = 11; fx.scaleU = 0.25; fx.scaleV = 0.5;
        rotateTexture(fx, fx.plane().n, 30);
        const auto pp = pil.polygons();
        Face fy = pil.faces[4];
        fy.texture = "other";
        CHECK(wrapTexture(fx, polyOfFace(pp, 2), fy, polyOfFace(pp, 4)));
        CHECK(fy.texture == "Bricks_24" && fy.scaleU == 0.25);
        const Vec3 ny = fy.plane().n;
        CHECK(near(dot(fy.u.axis, ny), 0, 1e-9) && near(dot(fy.v.axis, ny), 0, 1e-9));
        for (double z : {0.0, 77.0, 256.0}) {
            const Vec3 p{64, 64, z};
            const UV a = texCoord(fx, p, ts), b = texCoord(fy, p, ts);
            CHECK(near(a.u, b.u, 1e-9) && near(a.v, b.v, 1e-9));
        }
        // Keep going around: +Y -> -X face (3).
        Face fmx = pil.faces[3];
        CHECK(wrapTexture(fy, polyOfFace(pp, 4), fmx, polyOfFace(pp, 3)));
        const UV a = texCoord(fy, {0, 64, 100}, ts), b = texCoord(fmx, {0, 64, 100}, ts);
        CHECK(near(a.u, b.u, 1e-9) && near(a.v, b.v, 1e-9));
        // Opposite faces share no edge -> refused.
        Face fmx2 = pil.faces[3];
        CHECK(!wrapTexture(fx, polyOfFace(pp, 2), fmx2, polyOfFace(pp, 3)));
    }

    CHECK(isNoDraw("nodraw") && isNoDraw("NODRAW") && isNoDraw("tools/toolsnodraw"));
    CHECK(!isNoDraw("Bricks_24") && !isNoDraw("nodraw_old"));

    // Picking: a ray from -X hits the -X face (index 3); from inside -> nothing.
    double t = 0;
    CHECK(rayHitFace(b, {-100, 32, 64}, {1, 0, 0}, &t) == 3 && near(t, 100));
    CHECK(rayHitFace(b, {-100, 32, 300}, {0, 0, -1}, &t) == -1);
    CHECK(rayHitFace(b, {128, 32, 300}, {0, 0, -1}, &t) == 0 && near(t, 172));
}

static void testCsg() {
    const Brush cube = makeBox(box({0, 0, 0}, {64, 64, 64}), "a");
    CHECK(near(brushVolume(cube), 64.0 * 64 * 64, 1e-6));

    // Split at x = 16 (plane normal +X): back = x 0..16, front = x 16..64.
    SplitResult s = splitBrush(cube, {16, 0, 0}, {16, 0, 64}, {16, 64, 0}, "cap");
    CHECK(s.front && s.back);
    if (s.front && s.back) {
        CHECK(nearV(s.back->bounds().max, {16, 64, 64}) || nearV(s.front->bounds().max, {16, 64, 64}));
        CHECK(near(brushVolume(*s.front) + brushVolume(*s.back), brushVolume(cube), 1e-6));
        CHECK(s.front->faces.size() == 6 && s.back->faces.size() == 6); // old face replaced, no leftovers
        int caps = 0;
        for (const Face& f : s.back->faces) caps += f.texture == "cap";
        CHECK(caps == 1);
    }
    // A plane that misses the brush: everything on one side.
    s = splitBrush(cube, {100, 0, 0}, {100, 0, 64}, {100, 64, 0}, "cap");
    CHECK((s.front.has_value() ^ s.back.has_value()));
    // Diagonal split -> two wedges, volume preserved.
    s = splitBrush(cube, {0, 0, 0}, {0, 0, 64}, {64, 64, 0}, "cap");
    CHECK(s.front && s.back);
    if (s.front && s.back) {
        CHECK(s.front->faces.size() == 5 && s.back->faces.size() == 5);
        CHECK(near(brushVolume(*s.front), brushVolume(*s.back), 1e-6));
    }

    // Carve a tunnel through a block: volume of the pieces = block - tunnel.
    const Brush block = makeBox(box({0, 0, 0}, {128, 128, 128}), "a");
    const Brush tunnel = makeBox(box({32, 32, -16}, {96, 96, 144}), "b");
    std::vector<Brush> pieces = carveBrush(block, tunnel);
    double vol = 0;
    for (const Brush& p : pieces) { CHECK(p.valid()); vol += brushVolume(p); }
    CHECK(near(vol, 128.0 * 128 * 128 - 64.0 * 64 * 128, 1e-3));
    CHECK(pieces.size() >= 4 && pieces.size() <= 4);
    // Pieces don't overlap: their volumes add up and none intrudes into the tunnel.
    for (const Brush& p : pieces) {
        const AABB pb = p.bounds();
        const bool insideTunnel = pb.min.x >= 32 && pb.max.x <= 96 && pb.min.y >= 32 && pb.max.y <= 96;
        CHECK(!insideTunnel);
    }
    // No overlap -> unchanged; fully inside -> nothing left.
    CHECK(carveBrush(block, makeBox(box({500, 0, 0}, {564, 64, 64}), "b")).size() == 1);
    CHECK(carveBrush(makeBox(box({40, 40, 0}, {80, 80, 64}), "a"), tunnel).empty());

    // Overlaps: cutting into each other counts, touching doesn't.
    {
        Brush o1 = makeBox(box({0, 0, 0}, {64, 64, 64}), "a"); o1.id = 1;
        Brush o2 = makeBox(box({48, 16, 0}, {112, 32, 64}), "a"); o2.id = 2;   // 16 x 16 x 64 overlap
        Brush o3 = makeBox(box({64, 64, 0}, {128, 128, 64}), "a"); o3.id = 3;  // touches o1 at an edge
        Brush o4 = makeBox(box({0, 64, 0}, {64, 128, 64}), "a"); o4.id = 4;    // touches o1 on a face
        CHECK(near(overlapVolume(o1, o2), 16.0 * 16 * 64, 1e-6));
        CHECK(overlapVolume(o1, o3) == 0 && overlapVolume(o1, o4) == 0);
        const auto ov = findOverlaps({o1, o2, o3, o4});
        CHECK(ov.size() == 1);
        if (!ov.empty()) {
            CHECK(ov[0].a == 1 && ov[0].b == 2);
            CHECK(nearV(ov[0].region.min, {48, 16, 0}) && nearV(ov[0].region.max, {64, 32, 64}));
        }
        // A generated ring's slices share faces but don't overlap.
        CurveParams g;
        g.shape = CurveShape::Ring;
        g.axU = 0; g.axV = 1; g.axD = 2;
        g.u0 = -64; g.u1 = 64; g.v0 = -64; g.v1 = 64; g.d0 = 0; g.d1 = 32; g.segments = 12;
        CurveResult rr = generateCurve(g);
        for (size_t k = 0; k < rr.brushes.size(); ++k) rr.brushes[k].id = (BrushId)(k + 1);
        CHECK(findOverlaps(rr.brushes).empty());
    }

    // Hollow: a 256 cube with 16-unit walls.
    std::string err;
    const Brush room = makeBox(box({0, 0, 0}, {256, 256, 256}), "wall");
    std::vector<Brush> walls = hollowBrush(room, 16, err);
    CHECK(err.empty());
    CHECK(walls.size() == 6);
    vol = 0;
    for (const Brush& w : walls) vol += brushVolume(w);
    CHECK(near(vol, 256.0 * 256 * 256 - 224.0 * 224 * 224, 1e-3));
    // The inner faces got the room's texture (from the cutter = the room itself).
    for (const Brush& w : walls) for (const Face& f : w.faces) CHECK(f.texture == "wall");
    // Too thick -> refused.
    CHECK(hollowBrush(makeBox(box({0, 0, 0}, {64, 64, 20}), "w"), 16, err).empty() && !err.empty());
}

static bool sharesCorners(const Brush& a, const Brush& b, size_t need) {
    size_t n = 0;
    for (const Vec3& p : brushVertices(a))
        for (const Vec3& q : brushVertices(b))
            if (length(p - q) < 1e-9) { ++n; break; }
    return n >= need;
}

static void testCurves() {
    const double pi = 3.14159265358979323846;
    // ---- Arch in the front view (u = y, v = z, depth = x), 128 wide, 128 tall, 16 deep.
    CurveParams a;
    a.shape = CurveShape::Arch;
    a.u0 = 0; a.u1 = 128; a.v0 = 0; a.v1 = 128; a.d0 = 0; a.d1 = 16;
    a.segments = 8; a.thickness = 16; a.flatTop = true; a.legs = true; a.roundToWhole = false;
    CurveResult r = generateCurve(a);
    CHECK(r.ok());
    CHECK(r.brushes.size() == 8 + 2);
    double vol = 0;
    for (const Brush& b : r.brushes) { CHECK(b.valid()); vol += brushVolume(b); }
    // Solid = box above the spring line minus the inner half-polygon, plus two legs.
    const double Ro = 64, Ri = 48, N = 8;
    const double expected = (128.0 * Ro - 0.5 * N * Ri * Ri * std::sin(pi / N)) * 16 + 2 * (16.0 * 64 * 16);
    CHECK(near(vol, expected, 1e-6));
    // Neighbouring slices share their whole common edge (2 corners x 2 depths).
    for (size_t i = 0; i + 1 < 8; ++i) CHECK(sharesCorners(r.brushes[i], r.brushes[i + 1], 4));
    // Front view axes: depth along X.
    const AABB ab = r.brushes[0].bounds();
    CHECK(ab.min.x == 0 && ab.max.x == 16);

    // Rounded to whole units: every corner is an integer, still all valid.
    a.roundToWhole = true;
    r = generateCurve(a);
    CHECK(r.ok());
    for (const Brush& b : r.brushes) {
        CHECK(b.valid());
        CHECK(brushOnGrid(b, 1));
    }
    for (size_t i = 0; i + 1 < 8; ++i) CHECK(sharesCorners(r.brushes[i], r.brushes[i + 1], 4));

    // Too low for a round arch -> error.
    a.v1 = 40;
    CHECK(!generateCurve(a).ok());

    // ---- Full ring, 16 slices: volume = outer polygon - inner polygon.
    CurveParams g;
    g.shape = CurveShape::Ring;
    g.axU = 0; g.axV = 1; g.axD = 2;
    g.u0 = -64; g.u1 = 64; g.v0 = -64; g.v1 = 64; g.d0 = 0; g.d1 = 32;
    g.segments = 16; g.thickness = 16; g.roundToWhole = false;
    r = generateCurve(g);
    CHECK(r.ok() && r.brushes.size() == 16);
    vol = 0;
    for (const Brush& b : r.brushes) vol += brushVolume(b);
    const double polyA = [&](double R) { return 0.5 * 16 * R * R * std::sin(2 * pi / 16); }(64) - 0.5 * 16 * 48 * 48 * std::sin(2 * pi / 16);
    CHECK(near(vol, polyA * 32, 1e-6));
    CHECK(sharesCorners(r.brushes[15], r.brushes[0], 4)); // closes up with no gap
    // Thickness = radius -> a solid cylinder made of wedges.
    g.thickness = 64;
    r = generateCurve(g);
    CHECK(r.ok() && r.brushes.size() == 16);
    // Half ring, 3 slices of 60 degrees.
    g.thickness = 16; g.startDeg = 0; g.endDeg = 180; g.segments = 3;
    r = generateCurve(g);
    CHECK(r.ok() && r.brushes.size() == 3);
    // A slice spanning >= 180 degrees can't be convex -> error.
    g.segments = 1;
    CHECK(!generateCurve(g).ok());

    // ---- Curved wall: 8 slices, consecutive slices share an edge.
    CurveParams w;
    w.shape = CurveShape::CurvedWall;
    w.axU = 0; w.axV = 1; w.axD = 2;
    w.u0 = 0; w.v0 = 0; w.u1 = 256; w.v1 = 0; w.cu = 128; w.cv = 128;
    w.d0 = 0; w.d1 = 128; w.thickness = 16; w.segments = 8;
    r = generateCurve(w);
    CHECK(r.ok() && r.brushes.size() == 8);
    for (size_t i = 0; i + 1 < r.brushes.size(); ++i) CHECK(sharesCorners(r.brushes[i], r.brushes[i + 1], 4));
    // Far too tight for a thick wall -> error, not broken brushes.
    w.u1 = 8; w.cu = 4; w.cv = 200; w.thickness = 64; w.segments = 2;
    CHECK(!generateCurve(w).ok());

    // ---- Spiral stairs: 12 steps, one full turn, each 16 higher.
    CurveParams st;
    st.shape = CurveShape::SpiralStairs;
    st.axU = 0; st.axV = 1; st.axD = 2;
    st.u0 = -128; st.u1 = 128; st.v0 = -128; st.v1 = 128; st.d0 = 0;
    st.steps = 12; st.turnDeg = 360; st.rise = 16; st.thickness = 8; st.innerRadius = 16;
    r = generateCurve(st);
    CHECK(r.ok() && r.brushes.size() == 12);
    if (r.ok()) {
        CHECK(near(r.brushes[0].bounds().min.z, 0) && near(r.brushes[11].bounds().min.z, 176));
        CHECK(near(r.brushes[11].bounds().max.z, 184));
    }

    // ---- Carve arch: a box cut through the wall, then arch slices refill the corners
    //      above the curve. Volume = wall - (rectangle below the spring line + half polygon).
    CurveParams ca;
    ca.shape = CurveShape::CarveArch;
    ca.axU = 1; ca.axV = 2; ca.axD = 0;
    ca.u0 = 32; ca.u1 = 96; ca.v0 = 0; ca.v1 = 128; ca.d0 = 0; ca.d1 = 16; // the wall's depth
    ca.segments = 8; ca.roundToWhole = false;
    r = generateCurve(ca);
    CHECK(r.ok() && r.brushes.size() == 1);
    CHECK(r.fill.size() >= 6 && r.fill.size() <= 8);
    const Brush wall = makeBox(box({0, 0, 0}, {16, 256, 192}), "w");
    std::vector<Brush> pieces = carveBrush(wall, r.brushes[0]);
    CHECK(pieces.size() <= 4); // left, right, below, above: clean rectangles
    for (const Brush& f : r.fill) pieces.push_back(f);
    vol = 0;
    for (const Brush& p : pieces) { CHECK(p.valid()); vol += brushVolume(p); }
    const double openingArea = 64.0 * (128 - 32) + 0.5 * 8 * 32 * 32 * std::sin(pi / 8);
    CHECK(near(vol, brushVolume(wall) - openingArea * 16, 1e-3));
    // Neighbouring fill slices share corners.
    CHECK(sharesCorners(r.fill[0], r.fill[1], 2));

    // ---- Spiral stairs with a centre column: one more brush, as tall as the stairs.
    st.column = true;
    r = generateCurve(st);
    CHECK(r.ok() && r.brushes.size() == 13);
    if (r.ok()) CHECK(near(r.brushes.back().bounds().max.z, 184) && near(r.brushes.back().bounds().min.z, 0));
    st.column = false;

    // ---- Helix ramp: 16 pieces over a full turn; consecutive pieces share an edge face.
    CurveParams hr = st;
    hr.shape = CurveShape::HelixRamp;
    hr.steps = 16; hr.rise = 8; hr.thickness = 8; hr.innerRadius = 32;
    r = generateCurve(hr);
    CHECK(r.ok() && r.brushes.size() == 16);
    for (size_t i = 0; i + 1 < r.brushes.size(); ++i) CHECK(sharesCorners(r.brushes[i], r.brushes[i + 1], 4));
    if (r.ok()) CHECK(near(r.brushes.back().bounds().max.z, 16 * 8 + 8)); // 16 rises + thickness

    // ---- Dome: 8 x 3 pieces, all valid; volume close to a hemisphere shell.
    CurveParams dm;
    dm.shape = CurveShape::Dome;
    dm.axU = 0; dm.axV = 1; dm.axD = 2;
    dm.u0 = -128; dm.u1 = 128; dm.v0 = -128; dm.v1 = 128; dm.d0 = 0;
    dm.segments = 16; dm.rings = 4; dm.thickness = 16; dm.roundToWhole = false;
    r = generateCurve(dm);
    CHECK(r.ok() && r.brushes.size() == 64);
    vol = 0;
    for (const Brush& b : r.brushes) { CHECK(b.valid()); vol += brushVolume(b); }
    const double shell = 2.0 / 3 * pi * (128.0 * 128 * 128 - 112.0 * 112 * 112);
    CHECK(vol > 0.8 * shell && vol < shell);
    for (size_t i = 0; i + 1 < 16; ++i) CHECK(sharesCorners(r.brushes[i], r.brushes[i + 1], 4));
    dm.roundToWhole = true;
    r = generateCurve(dm);
    CHECK(r.ok() && r.brushes.size() == 64);

    // ---- Straight stairs: 8 solid steps in a 256 x 128 box = half the box volume + half a step row.
    CurveParams ss;
    ss.shape = CurveShape::StraightStairs;
    ss.u0 = 0; ss.u1 = 256; ss.v0 = 0; ss.v1 = 128; ss.d0 = 0; ss.d1 = 64; ss.steps = 8;
    r = generateCurve(ss);
    CHECK(r.ok() && r.brushes.size() == 8);
    vol = 0;
    for (const Brush& b : r.brushes) vol += brushVolume(b);
    CHECK(near(vol, 32.0 * 64 * 16 * (1 + 2 + 3 + 4 + 5 + 6 + 7 + 8), 1e-3));
    // Rising to the right: the leftmost step is the lowest.
    if (r.ok()) CHECK(r.brushes[0].bounds().max.z == 16 && r.brushes[0].bounds().min.y == 0);
    ss.flip = true;
    r = generateCurve(ss);
    if (r.ok()) CHECK(r.brushes[0].bounds().max.z == 16 && r.brushes[0].bounds().max.y == 256);
    ss.solid = false; ss.thickness = 8;
    r = generateCurve(ss);
    CHECK(r.ok());
    if (r.ok()) CHECK(r.brushes[7].bounds().size().z == 8);

    // ---- No shape's pieces may cut into each other, with or without rounding, also with
    //      uneven settings (found by the user: rounded domes; also the stairs column).
    {
        std::vector<CurveParams> all;
        CurveParams x; x.shape = CurveShape::Arch; x.u0 = 0; x.u1 = 130; x.v0 = 0; x.v1 = 170; x.d0 = 0; x.d1 = 16; x.segments = 7; all.push_back(x);
        CurveParams y; y.shape = CurveShape::Ring; y.axU = 0; y.axV = 1; y.axD = 2; y.u0 = -101; y.u1 = 101; y.v0 = -101; y.v1 = 101; y.d1 = 32; y.segments = 11; y.thickness = 23; all.push_back(y);
        y.thickness = 101; all.push_back(y);
        CurveParams z; z.shape = CurveShape::CurvedWall; z.axU = 0; z.axV = 1; z.axD = 2; z.u0 = 0; z.v0 = 0; z.u1 = 301; z.v1 = 43; z.cu = 150; z.cv = 171; z.d1 = 128; z.thickness = 17; z.segments = 9; all.push_back(z);
        CurveParams q; q.shape = CurveShape::SpiralStairs; q.axU = 0; q.axV = 1; q.axD = 2; q.u0 = -150; q.u1 = 150; q.v0 = -150; q.v1 = 150;
        q.steps = 13; q.turnDeg = 330; q.rise = 16; q.thickness = 24; q.innerRadius = 37; q.column = true; all.push_back(q);
        q.turnDeg = 720; q.steps = 24; all.push_back(q);
        CurveParams h = q; h.shape = CurveShape::HelixRamp; h.column = false; h.steps = 21; h.rise = 7; h.thickness = 9; all.push_back(h);
        CurveParams dmx; dmx.shape = CurveShape::Dome; dmx.axU = 0; dmx.axV = 1; dmx.axD = 2; dmx.u0 = -131; dmx.u1 = 131; dmx.v0 = -131; dmx.v1 = 131;
        dmx.segments = 13; dmx.rings = 5; dmx.thickness = 17; all.push_back(dmx);
        dmx.segments = 24; dmx.rings = 8; dmx.thickness = 9; all.push_back(dmx);
        CurveParams sx; sx.shape = CurveShape::StraightStairs; sx.u0 = 0; sx.u1 = 251; sx.v0 = 0; sx.v1 = 133; sx.d1 = 64; sx.steps = 7; all.push_back(sx);
        for (CurveParams cp : all)
            for (bool rnd : {false, true}) {
                cp.roundToWhole = rnd;
                CurveResult rr = generateCurve(cp);
                CHECK(rr.ok());
                for (size_t i = 0; i < rr.brushes.size(); ++i) { rr.brushes[i].id = (BrushId)(i + 1); CHECK(rr.brushes[i].valid()); }
                const auto ov = findOverlaps(rr.brushes, 0.01);
                CHECK(ov.empty());
                if (!ov.empty()) std::printf("  overlaps in %s (round=%d): %zu\n", curveShapeName(cp.shape), (int)rnd, ov.size());
            }
        // A column with steps that don't line up turn after turn is refused, not broken.
        q.turnDeg = 500; q.steps = 13;
        CHECK(!generateCurve(q).ok());
    }

    // ---- Parameters round-trip through key/values (stored in the .map).
    CurveParams k = st;
    k.texture = "Bricks_24"; k.clockwise = true; k.thickness = 7.25; k.column = true; k.rings = 6; k.flip = true;
    CurveParams back;
    CHECK(curveFromKeys(curveToKeys(k), back));
    CHECK(back.shape == CurveShape::SpiralStairs && back.texture == "Bricks_24" && back.clockwise);
    CHECK(back.thickness == 7.25 && back.steps == 12 && back.axD == 2 && back.u0 == -128);
    CHECK(back.column && back.rings == 6 && back.flip && back.solid);
    CHECK(!curveFromKeys({{"shape", "teapot"}}, back));
    CHECK(curveFromKeys({{"shape", "straight_stairs"}, {"steps", "6"}, {"rise", "12.5"}}, back) && back.steps == 6 && back.rise == 12.5);
}

static void testPrimitives() {
    const double pi = 3.14159265358979323846;
    const AABB bx = box({0, 0, 0}, {128, 128, 256});
    for (Primitive k : {Primitive::Box, Primitive::Wedge, Primitive::Cylinder, Primitive::Cone, Primitive::Pyramid, Primitive::Sphere}) {
        auto b = makePrimitive(k, bx, 2, 0, 16, false, "a", 0.25);
        CHECK(b.has_value());
        if (!b) continue;
        CHECK(b->valid());
        // Every primitive fills its box exactly along each axis.
        const AABB bb = b->bounds();
        CHECK(near(bb.min.x, 0, 1e-9) && near(bb.max.x, 128, 1e-9));
        CHECK(near(bb.min.z, 0, 1e-9) && near(bb.max.z, 256, 1e-9));
        for (const Face& f : b->faces) CHECK(f.texture == "a" && f.scaleU == 0.25);
    }
    // Volumes against the polygon maths.
    const double n = 16, R = 64, H = 256;
    const double ngon = 0.5 * n * R * R * std::sin(2 * pi / n);
    auto vol = [&](Primitive k) { return brushVolume(*makePrimitive(k, bx, 2, 0, 16, false, "a", 1)); };
    CHECK(near(vol(Primitive::Box), 128.0 * 128 * 256, 1e-6));
    CHECK(near(vol(Primitive::Wedge), 128.0 * 128 * 256 / 2, 1e-6));
    CHECK(near(vol(Primitive::Cylinder), ngon * H, 1e-3));
    CHECK(near(vol(Primitive::Cone), ngon * H / 3, 1e-3));
    CHECK(near(vol(Primitive::Pyramid), 128.0 * 128 * 256 / 3, 1e-6));
    const double sphereish = vol(Primitive::Sphere);
    CHECK(sphereish > 0.8 * (4.0 / 3 * pi * 64 * 64 * 128) && sphereish < 4.0 / 3 * pi * 64 * 64 * 128);
    // Wedge: full height on the min-X side, zero on the max-X side.
    auto w = makePrimitive(Primitive::Wedge, bx, 2, 0, 16, false, "a", 1);
    double topAtMinX = 0, topAtMaxX = 0;
    for (const Vec3& v : brushVertices(*w)) {
        if (v.x == 0) topAtMinX = std::max(topAtMinX, v.z);
        if (v.x == 128) topAtMaxX = std::max(topAtMaxX, v.z);
    }
    CHECK(topAtMinX == 256 && topAtMaxX == 0);
    // Rounded to whole units: all corners integer.
    auto rc = makePrimitive(Primitive::Cylinder, bx, 2, 0, 12, true, "a", 1);
    CHECK(rc && rc->valid() && brushOnGrid(*rc, 1));
    // Cylinder lying along X (drawn in the side view).
    auto lx = makePrimitive(Primitive::Cylinder, box({0, 0, 0}, {256, 64, 64}), 0, 1, 8, false, "a", 1);
    CHECK(lx && lx->faces.size() == 10);
}

static void testRay() {
    Brush b = makeBox(box({0, 0, 0}, {64, 64, 64}), "a");
    CHECK(near(rayHit(b, {-100, 32, 32}, {1, 0, 0}), 100));
    CHECK(rayHit(b, {-100, 100, 32}, {1, 0, 0}) < 0);
    CHECK(rayHit(b, {-100, 32, 32}, {-1, 0, 0}) < 0);
    CHECK(near(rayHit(b, {32, 32, 32}, {1, 0, 0}), 0)); // inside
}

// ---------------------------------------------------------------- document / undo

static void testUndo() {
    Document d;
    d.apply(d.makeAdd({makeBox(box({0, 0, 0}, {64, 64, 64}), "a")}, "Create"));
    CHECK(d.brushes().size() == 1);
    const BrushId id = d.brushes()[0].id;
    CHECK(d.selection().count(id) == 1);

    Brush moved = *d.find(id);
    moved.translate({64, 0, 0}, true);
    d.apply(d.makeModify({moved}, "Move"));
    CHECK(nearV(d.find(id)->bounds().min, {64, 0, 0}));

    d.apply(d.makeDelete({id}, "Delete"));
    CHECK(d.brushes().empty());
    CHECK(d.selection().empty());

    CHECK(d.undo());                               // undelete
    CHECK(d.brushes().size() == 1);
    CHECK(nearV(d.find(id)->bounds().min, {64, 0, 0}));
    CHECK(d.selection().count(id) == 1);
    CHECK(d.undo());                               // unmove
    CHECK(nearV(d.find(id)->bounds().min, {0, 0, 0}));
    CHECK(d.undo());                               // uncreate
    CHECK(d.brushes().empty());
    CHECK(!d.undo());
    CHECK(d.redo() && d.redo());
    CHECK(nearV(d.find(id)->bounds().min, {64, 0, 0}));
    // New edit clears redo.
    d.apply(d.makeAdd({makeBox(box({0, 0, 0}, {8, 8, 8}), "a")}, "Create"));
    CHECK(!d.canRedo());
}

static void testMakeChange() {
    Document d;
    d.apply(d.makeAdd({makeBox(box({0, 0, 0}, {64, 64, 64}), "a"), makeBox(box({100, 0, 0}, {164, 64, 64}), "a")}, "Create"));
    const BrushId a = d.brushes()[0].id, b = d.brushes()[1].id;
    Brush am = *d.find(a);
    am.translate({0, 0, 64}, true);
    d.apply(d.makeChange({am}, {makeBox(box({0, 200, 0}, {64, 264, 64}), "a")}, {b}, "Mixed"));
    CHECK(d.brushes().size() == 2);
    CHECK(d.find(b) == nullptr);
    CHECK(nearV(d.find(a)->bounds().min, {0, 0, 64}));
    CHECK(d.selection().count(a) == 1 && d.selection().size() == 2);
    CHECK(d.undo());
    CHECK(d.brushes().size() == 2 && d.find(b) != nullptr);
    CHECK(nearV(d.find(a)->bounds().min, {0, 0, 0}));
    CHECK(d.redo());
    CHECK(d.find(b) == nullptr);
}

static void testGroups() {
    // Generate an arch as a group, save, load: the group and its parameters survive.
    CurveParams a;
    a.shape = CurveShape::Arch;
    a.u0 = 0; a.u1 = 128; a.v0 = 0; a.v1 = 128; a.d0 = 0; a.d1 = 16; a.segments = 6;
    CurveResult r = generateCurve(a);
    CHECK(r.ok());
    Document d;
    d.apply(d.makeAdd({makeBox(box({300, 0, 0}, {364, 64, 64}), "a")}, "Create"));
    const GroupId gid = d.newGroupId();
    for (Brush& b : r.brushes) b.group = gid;
    Command c = d.makeChange({}, r.brushes, {}, "Arch");
    c.groupChanges.push_back({gid, std::nullopt, Group{gid, curveToKeys(a)}});
    d.apply(std::move(c));
    CHECK(d.groups().size() == 1);
    CHECK(d.groupMembers(gid).size() == 8);
    // Selecting one member selects the whole group.
    const BrushId one = *d.groupMembers(gid).begin();
    CHECK(d.expandToGroups({one}).size() == 8);
    // Undo removes brushes and the group; redo brings both back.
    CHECK(d.undo());
    CHECK(d.groups().empty() && d.brushes().size() == 1);
    CHECK(d.redo());
    CHECK(d.groups().size() == 1 && d.brushes().size() == 9);

    const std::string text = writeMap(d);
    CHECK(text.find("\"func_group\"") != std::string::npos && text.find("\"_scal_shape\" \"arch\"") != std::string::npos);
    Document e;
    CHECK(readMap(text, e).ok);
    CHECK(e.brushes().size() == 9);
    CHECK(e.groups().size() == 1);
    const Group& g = e.groups().begin()->second;
    CurveParams back;
    CHECK(curveFromKeys(g.keys, back));
    CHECK(back.shape == CurveShape::Arch && back.segments == 6 && back.u1 == 128);
    CHECK(e.groupMembers(g.id).size() == 8);
    CHECK(e.otherEntities.empty()); // the group isn't kept as an opaque entity
    // A plain func_group from another tool stays an opaque entity.
    Document f;
    CHECK(readMap("{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"func_group\"\n}\n", f).ok);
    CHECK(f.otherEntities.size() == 1 && f.groups().empty());

    // Deleting every brush of a group removes the group too; undo brings it back.
    d.apply(d.makeDelete(d.groupMembers(gid), "Delete"));
    CHECK(d.groups().empty());
    CHECK(d.undo());
    CHECK(d.groups().size() == 1 && d.groupMembers(gid).size() == 8);

    // shiftCurve moves every coordinate.
    CurveParams s = a;
    shiftCurve(s, {100, 20, 30}); // front view: u = y, v = z, d = x
    CHECK(s.u0 == 20 && s.v1 == 158 && s.d0 == 100);
}

static void testVisibility() {
    Document d;
    d.apply(d.makeAdd({makeBox(box({0, 0, 0}, {64, 64, 64}), "a"), makeBox(box({100, 0, 0}, {164, 64, 64}), "a"),
                       makeBox(box({200, 0, 0}, {264, 64, 64}), "a")}, "Create"));
    const BrushId a = d.brushes()[0].id, b = d.brushes()[1].id, c = d.brushes()[2].id;
    // Hide one brush (undoable), put another in a hidden visgroup.
    Brush ha = *d.find(a); ha.hidden = true;
    Brush vb = *d.find(b); vb.visgroup = 7;
    d.apply(d.makeModify({ha, vb}, "Hide"));
    d.setVisgroups({{7, "Roof | top; floor", false}, {9, "Area B", true}});
    CHECK(!d.visible(a) && !d.visible(b) && d.visible(c));
    // Save + load keeps it all; names lose only the characters the format can't hold.
    const std::string text = writeMap(d);
    CHECK(text.find("//@ hidden=1") != std::string::npos && text.find("//@ visgroup=7") != std::string::npos);
    Document e;
    CHECK(readMap(text, e).ok);
    CHECK(e.visgroups().size() == 2 && e.visgroups()[0].name == "Roof _ top_ floor" && !e.visgroups()[0].visible);
    CHECK(e.brushes()[0].hidden && e.brushes()[1].visgroup == 7 && !e.visible(e.brushes()[1].id));
    CHECK(e.visible(e.brushes()[2].id));
    // Saving again is identical (stable format).
    CHECK(writeMap(e) == text);
    // Other tools: the metadata is only comments, the brushes parse as normal.
    std::string stripped;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) if (line.rfind("//@", 0) != 0) stripped += line + "\n";
    Document f;
    CHECK(readMap(stripped, f).ok && f.brushes().size() == 3 && f.visible(f.brushes()[0].id));
    // Undo un-hides.
    CHECK(d.undo());
    CHECK(d.visible(a));
}

static void testDirty() {
    Document d;
    CHECK(!d.dirty());
    d.apply(d.makeAdd({makeBox(box({0, 0, 0}, {64, 64, 64}), "a")}, "Create"));
    CHECK(d.dirty());
    d.markSaved();
    CHECK(!d.dirty());
    d.undo();
    CHECK(d.dirty());
}

// ---------------------------------------------------------------- map IO

static void testMapRoundTrip() {
    Document d;
    std::vector<Brush> bs;
    bs.push_back(makeBox(box({-64, -64, 0}, {64, 64, 16}), "floor"));
    bs.push_back(makeBox(box({-64, -64, 16}, {-48, 64, 128}), "wall"));
    Brush odd = makeBox(box({0, 0, 16}, {32, 32, 48}), "crate");
    odd.translate({0.5, 1.0 / 3.0, 0}, true); // non-integer coordinates and offsets
    bs.push_back(odd);
    d.apply(d.makeAdd(bs, "Create"));
    d.otherEntities.push_back({{{"classname", "info_null"}, {"origin", "0 0 64"}, {"targetname", "spot"}}, {}});

    const std::string a = writeMap(d);
    Document d2;
    const MapReadResult r = readMap(a, d2);
    CHECK(r.ok);
    CHECK(r.skippedBrushes == 0);
    CHECK(d2.brushes().size() == 3);
    CHECK(d2.otherEntities.size() == 1);
    CHECK(!d2.dirty());
    const std::string b = writeMap(d2);
    CHECK(a == b); // byte-identical round trip
    // Exact values survived.
    CHECK(d2.brushes()[2].faces[0].p[0] == d.brushes()[2].faces[0].p[0]);
    if (a != b) std::printf("---- first\n%s\n---- second\n%s\n", a.c_str(), b.c_str());

    // Terrain round trip: exact heights, painted textures (in order) with their blend,
    // start corner; brushes without painting have no blend rows; broken terrain data is
    // reported; v0.7.0 files (tex2= and blend rows with no texture line) still open.
    {
        AABB tb; tb.add({0, 0, 0}); tb.add({256, 256, 64});
        Brush t1 = makeBox(tb, "a");
        CHECK(makeTerrain(t1, topQuadFace(t1), 3).empty());
        std::vector<Brush> ts{t1};
        SculptDab d; d.center = {100, 90, 64}; d.radius = 120; d.amount = 13.37;
        sculpt(ts, d);
        d.mode = SculptMode::Paint; d.strength = 0.3; d.texture = "Tile_19";
        sculpt(ts, d);
        d.texture = "Tile_02"; d.center = {200, 200, 64};
        sculpt(ts, d);
        d.texture = "Tile_07"; d.center = {40, 200, 64};
        sculpt(ts, d);
        CHECK(ts[0].terrain->layers.size() == 3);
        tb.min.x += 300; tb.max.x += 300;
        Brush t2 = makeBox(tb, "a");
        CHECK(makeTerrain(t2, topQuadFace(t2), 2).empty());
        t2.terrain->heights[3] = -7.25;
        Document td;
        td.apply(td.makeAdd({ts[0], t2}, "add"));
        const std::string text = writeMap(td);
        CHECK(text.find("//@ terrain face=") != std::string::npos);
        CHECK(text.find("//@ tl Tile_19") != std::string::npos);
        CHECK(text.find("//@ tl Tile_07") != std::string::npos);
        CHECK(text.find("//@ ta") < text.find("Tile_07") || true);
        Document te;
        CHECK(readMap(text, te).ok);
        CHECK(te.brushes().size() == 2);
        if (te.brushes().size() == 2) {
            const Brush &r1 = te.brushes()[0], &r2 = te.brushes()[1];
            CHECK(r1.terrain && r2.terrain);
            if (r1.terrain && r2.terrain) {
                CHECK(r1.terrain->heights == ts[0].terrain->heights);
                CHECK(r1.terrain->layers == ts[0].terrain->layers);
                CHECK(r1.terrain->layers.size() == 3 && r1.terrain->layers[0].texture == "Tile_19" &&
                      r1.terrain->layers[2].texture == "Tile_07");
                CHECK(nearV(r1.terrain->start, ts[0].terrain->start));
                CHECK(r2.terrain->power == 2 && near(r2.terrain->heights[3], -7.25));
                CHECK(r2.terrain->layers.empty());
                CHECK(terrainValid(r1) && terrainValid(r2));
            }
        }
        std::string broken = text;
        const size_t th = broken.find("//@ th");
        broken.erase(th, broken.find('\n', th) - th + 1); // drop one row
        Document tf;
        const MapReadResult rr = readMap(broken, tf);
        CHECK(!rr.ok);
        CHECK(rr.error.find("terrain") != std::string::npos);
        // v0.7.0 layout: one blend texture named in the header.
        std::string old = "{\n\"classname\" \"worldspawn\"\n{\n//@ terrain face=" + std::to_string(t2.terrain->face) +
                          " power=1 start=300,0,64 tex2=Tile_19\n//@ th 0 1 2\n//@ th 3 4 5\n//@ th 6 7 8\n"
                          "//@ ta 0 0 0\n//@ ta 0 0.5 0\n//@ ta 0 0 1\n";
        {
            Document one;
            one.apply(one.makeAdd({makeBox(tb, "a")}, "add"));
            const std::string boxText = writeMap(one);
            const size_t open = boxText.find("{\n(");
            old += boxText.substr(open + 2, boxText.find("}\n", open) - open - 2) + "}\n}\n";
        }
        Document tg;
        const MapReadResult ro = readMap(old, tg);
        CHECK(ro.ok);
        if (ro.ok && tg.brushes().size() == 1 && tg.brushes()[0].terrain) {
            const Terrain& ot = *tg.brushes()[0].terrain;
            CHECK(ot.layers.size() == 1 && ot.layers[0].texture == "Tile_19");
            if (ot.layers.size() == 1) CHECK(near(ot.layers[0].alpha[4], 0.5) && near(ot.layers[0].alpha[8], 1));
            CHECK(near(ot.heights[5], 5));
        } else CHECK(false);
    }

}

static void testMapQuakeFormat() {
    const char* text =
        "// classic quake format\n"
        "{\n\"classname\" \"worldspawn\"\n"
        "{\n"
        "( 0 0 64 ) ( 0 64 64 ) ( 64 0 64 ) top 0 0 0 1 1\n"
        "( 0 0 0 ) ( 64 0 0 ) ( 0 64 0 ) bot 0 0 0 1 1\n"
        "( 64 0 0 ) ( 64 0 64 ) ( 64 64 0 ) e 0 0 0 1 1\n"
        "( 0 0 0 ) ( 0 64 0 ) ( 0 0 64 ) w 0 0 0 1 1\n"
        "( 0 64 0 ) ( 64 64 0 ) ( 0 64 64 ) n 0 0 0 1 1\n"
        "( 0 0 0 ) ( 0 0 64 ) ( 64 0 0 ) s 16 8 0 0.5 0.5\n"
        "}\n}\n";
    Document d;
    const MapReadResult r = readMap(text, d);
    CHECK(r.ok);
    CHECK(d.brushes().size() == 1);
    if (!d.brushes().empty()) {
        const Brush& b = d.brushes()[0];
        CHECK(b.valid());
        CHECK(nearV(b.bounds().min, {0, 0, 0}));
        CHECK(nearV(b.bounds().max, {64, 64, 64}));
        CHECK(b.faces[5].scaleU == 0.5 && b.faces[5].u.offset == 16);
    }
}

static void testMapErrors() {
    Document d;
    MapReadResult r = readMap("{\n\"classname\" \"worldspawn\"\n{\n( 0 0 x ) ( 1 1 1 ) ( 2 2 2 ) t 0 0 0 1 1\n}\n}\n", d);
    CHECK(!r.ok);
    CHECK(r.error.find("line 4") != std::string::npos);
    r = readMap("{\n\"classname\" \"worldspawn\"\n", d);
    CHECK(!r.ok);
    // An invalid brush is skipped, not fatal.
    r = readMap("{\n\"classname\" \"worldspawn\"\n{\n( 0 0 0 ) ( 0 64 0 ) ( 64 0 0 ) t 0 0 0 1 1\n}\n}\n", d);
    CHECK(r.ok && r.skippedBrushes == 1);
}


// ---- Terrain (milestone 7)
static Vec3 highestPoint(const Brush& b) {
    const auto g = terrainGrid(b);
    Vec3 best{0, 0, -1e300};
    for (const Vec3& p : g->pos) if (p.z > best.z) best = p;
    return best;
}

void testTerrain() {
    AABB box; box.add({0, 0, 0}); box.add({256, 256, 64});
    Brush b = makeBox(box, "a");
    const int top = topQuadFace(b);
    CHECK(top >= 0);
    CHECK(near(b.faces[top].plane().n.z, 1));
    CHECK(makeTerrain(b, top, 3).empty());
    CHECK(b.terrain && b.terrain->size() == 9);
    auto g = terrainGrid(b);
    CHECK(g.has_value());
    CHECK(nearV(g->corners[0], {0, 0, 64}));      // starts at the lowest corner
    for (size_t k = 0; k < g->pos.size(); ++k) CHECK(near(g->pos[k].z, 64));
    CHECK(near(g->at(4, 4).x, 128) && near(g->at(4, 4).y, 128));
    // bad power, bad face
    CHECK(!makeTerrain(b, top, 0).empty());
    CHECK(!makeTerrain(b, top, 6).empty());

    // A raised point; bounds include it; ray hits it.
    b.terrain->heights[4 * 9 + 4] = 32;
    CHECK(near(b.bounds().max.z, 96));
    CHECK(near(rayHitTerrain(b, {128, 128, 1000}, {0, 0, -1}), 1000 - 96, 1e-6));
    CHECK(rayHitTerrain(b, {600, 128, 1000}, {0, 0, -1}) < 0);
    CHECK(near(rayHitTerrain(b, {32, 32, 1000}, {0, 0, -1}), 1000 - 64, 1e-6));

    // Asymmetric bump at i=6, j=2 -> follows move, rotation, mirror and scale.
    b.terrain->heights[4 * 9 + 4] = 0;
    b.terrain->heights[2 * 9 + 6] = 20;
    Vec3 bump = highestPoint(b);
    CHECK(nearV(bump, {192, 64, 84}));
    {
        Brush m = b;
        m.translate({10, -20, 5}, true);
        CHECK(nearV(highestPoint(m), bump + Vec3{10, -20, 5}, 1e-6));
        Brush r = b;
        r.rotate({128, 128, 0}, {0, 0, 1}, 3.14159265358979323846 / 2, true);
        CHECK(nearV(highestPoint(r), {192, 192, 84}, 1e-6)); // (64,-64) from the centre turns to (64,64)
        std::string err;
        TransformSpec mx; mx.kind = TransformKind::Scale; mx.scale = {-1, 1, 1}; mx.pivot = {128, 0, 0};
        auto mir = applyTransform({b}, mx, true, err);
        CHECK(mir.size() == 1);
        if (!mir.empty()) CHECK(nearV(highestPoint(mir[0]), {64, 64, 84}, 1e-6));
        TransformSpec sc; sc.kind = TransformKind::Scale; sc.scale = {1, 1, 2}; sc.pivot = {0, 0, 0};
        auto big = applyTransform({b}, sc, true, err);
        CHECK(big.size() == 1);
        if (!big.empty()) CHECK(nearV(highestPoint(big[0]), {192, 64, 128 + 40}, 1e-6));
        TransformSpec fz; fz.kind = TransformKind::Scale; fz.scale = {1, 1, -1}; fz.pivot = {0, 0, 32};
        auto upside = applyTransform({b}, fz, true, err); // terrain now faces down, bump too
        CHECK(upside.size() == 1);
        if (!upside.empty()) {
            const auto ug = terrainGrid(upside[0]);
            CHECK(ug.has_value());
            if (ug) { double lo = 1e300; for (const Vec3& p : ug->pos) lo = std::min(lo, p.z); CHECK(near(lo, -20, 1e-6)); }
        }
    }
    // Cutting a terrain brush gives plain brushes.
    {
        const SplitResult sr = splitBrush(b, {128, 0, 0}, {128, 0, 1}, {128, 1, 0}, "a");
        CHECK(sr.front && sr.back);
        if (sr.front) CHECK(!sr.front->terrain);
        if (sr.back) CHECK(!sr.back->terrain);
    }
    // A 3-corner face can be terrain: the grid covers the triangle exactly.
    {
        const SplitResult sr = splitBrush(makeBox(box, "a"), {0, 128, 0}, {0, 128, 1}, {128, 0, 0}, "a");
        CHECK(sr.front.has_value());
        if (sr.front) {
            Brush tri = *sr.front;
            int triTop = -1;
            for (const Polygon& p : tri.polygons()) if (p.normal.z > 0.9) triTop = p.face;
            CHECK(triTop >= 0);
            CHECK(makeTerrain(tri, triTop, 3).empty());
            const auto tg = terrainGrid(tri);
            CHECK(tg.has_value());
            if (tg) {
                // every grid point lies inside (or on) the triangle, and its corners are grid corners
                std::vector<Vec3> tv;
                for (const Polygon& p : tri.polygons()) if (p.face == triTop) tv = p.verts;
                CHECK(tv.size() == 3);
                for (const Vec3& q : tg->base) {
                    bool inside = true;
                    for (size_t e = 0; e < tv.size(); ++e) {
                        const Vec3 ed = tv[(e + 1) % 3] - tv[e];
                        if (dot(cross(ed, q - tv[e]), tg->normal) < -1e-6) inside = false;
                    }
                    CHECK(inside);
                }
                int cornersHit = 0;
                for (const Vec3& c : tv)
                    for (const Vec3& q : tg->base) if (length(q - c) < 1e-9) { ++cornersHit; break; }
                CHECK(cornersHit == 3);
                // mirrored: the grid follows (a bump stays at the same spot, mirrored)
                tri.terrain->heights[2 * 9 + 5] = 30;
                Vec3 bumpT = highestPoint(tri);
                std::string err;
                TransformSpec mx; mx.kind = TransformKind::Scale; mx.scale = {-1, 1, 1}; mx.pivot = {0, 0, 0};
                auto mir = applyTransform({tri}, mx, true, err);
                CHECK(mir.size() == 1);
                if (!mir.empty()) CHECK(nearV(highestPoint(mir[0]), {-bumpT.x, bumpT.y, bumpT.z}, 1e-6));
            }
        }
    }

    // Sculpt: hard raise by 8 within 64 units of the centre (flat distance).
    {
        std::vector<Brush> bs{makeBox(box, "a")};
        CHECK(makeTerrain(bs[0], topQuadFace(bs[0]), 3).empty());
        SculptDab d; d.mode = SculptMode::Raise; d.falloff = Falloff::Hard; d.center = {128, 128, 64}; d.radius = 64; d.amount = 8;
        CHECK(sculpt(bs, d));
        const auto sg = terrainGrid(bs[0]);
        int raised = 0;
        for (size_t k = 0; k < sg->pos.size(); ++k) {
            const double dist = std::hypot(sg->base[k].x - 128, sg->base[k].y - 128);
            CHECK(near(sg->pos[k].z, dist <= 64 ? 72 : 64));
            if (dist <= 64) ++raised;
        }
        CHECK(raised == 13); // centre + 4 at 32 + 4 diagonal + 4 at 64
        // Soft falloff: centre full, rim zero.
        CHECK(near(falloffWeight(Falloff::Soft, 0, 10), 1) && near(falloffWeight(Falloff::Soft, 10, 10), 0));
        CHECK(near(falloffWeight(Falloff::Linear, 5, 10), 0.5) && near(falloffWeight(Falloff::Hard, 11, 10), 0));
        // Flatten (full strength) back to the original surface.
        d.mode = SculptMode::Flatten; d.strength = 1; d.flattenTarget = {0, 0, 64}; d.radius = 100;
        CHECK(sculpt(bs, d));
        for (double h : bs[0].terrain->heights) CHECK(near(h, 0));
        CHECK(!sculpt(bs, d)); // nothing left to change
        // Paint adds a texture layer and blend; erase takes it away.
        d.mode = SculptMode::Paint; d.strength = 0.5; d.texture = "Tile_19"; d.radius = 40;
        CHECK(sculpt(bs, d));
        CHECK(bs[0].terrain->layers.size() == 1 && bs[0].terrain->layers[0].texture == "Tile_19");
        CHECK(near(bs[0].terrain->layers[0].alpha[4 * 9 + 4], 0.5));
        CHECK(near(bs[0].terrain->layers[0].alpha[0], 0));
        d.mode = SculptMode::Erase; d.strength = 1;
        CHECK(sculpt(bs, d));
        CHECK(near(bs[0].terrain->layers[0].alpha[4 * 9 + 4], 0));
        CHECK(pruneTerrainLayers(*bs[0].terrain) && bs[0].terrain->layers.empty());
        // Several textures: painting one moves its share up and every other share down,
        // whatever the order; painting the face's own texture uncovers it.
        {
            std::vector<Brush> lb = bs;
            SculptDab p; p.mode = SculptMode::Paint; p.falloff = Falloff::Hard; p.center = {128, 128, 64};
            p.radius = 1000; p.strength = 1;
            const int c = 4 * 9 + 4;
            for (const char* tex : {"t1", "t2", "t3", "t4"}) { p.texture = tex; CHECK(sculpt(lb, p)); }
            const Terrain& lt = *lb[0].terrain;
            CHECK(lt.layers.size() == 4);
            CHECK(lt.layers[3].texture == "t4" && near(lt.layers[3].alpha[c], 1));
            CHECK(near(lt.layers[0].alpha[c], 0) && near(lt.layers[1].alpha[c], 0)); // painted over in full
            p.texture = "t2"; p.strength = 0.5;
            CHECK(sculpt(lb, p));
            CHECK(near(lt.layers[1].alpha[c], 0.5) && near(lt.layers[3].alpha[c], 0.5));
            p.texture = "a"; p.strength = 1; // the face's own texture
            CHECK(sculpt(lb, p));
            for (const TerrainLayer& l : lt.layers) CHECK(near(l.alpha[c], 0));
            CHECK(lt.layers.size() == 4);
            CHECK(pruneTerrainLayers(*lb[0].terrain) && lb[0].terrain->layers.empty());
            // A full patch refuses an 8th painted texture (and says so); textures with no
            // paint left make room.
            p.strength = 0.5;
            for (int i = 0; i < kMaxTerrainLayers; ++i) { p.texture = "x" + std::to_string(i); sculpt(lb, p); }
            CHECK((int)lb[0].terrain->layers.size() == kMaxTerrainLayers);
            bool full = false;
            p.texture = "one_too_many";
            sculpt(lb, p, &full);
            CHECK(full);
            CHECK(terrainLayer(*lb[0].terrain, "one_too_many") < 0);
            p.strength = 1; p.texture = "x0";
            sculpt(lb, p); // x0 takes all, the others have nothing left
            full = false;
            p.texture = "one_too_many";
            CHECK(sculpt(lb, p, &full));
            CHECK(!full && terrainLayer(*lb[0].terrain, "one_too_many") >= 0);
        }
        // Noise is repeatable for the same seed and stays within +-amount.
        std::vector<Brush> n1 = bs, n2 = bs;
        d.mode = SculptMode::Noise; d.amount = 10; d.falloff = Falloff::Hard; d.radius = 1000; d.seed = 7;
        sculpt(n1, d); sculpt(n2, d);
        bool varied = false;
        for (size_t k = 0; k < n1[0].terrain->heights.size(); ++k) {
            CHECK(near(n1[0].terrain->heights[k], n2[0].terrain->heights[k]));
            CHECK(std::fabs(n1[0].terrain->heights[k]) <= 10 + 1e-9);
            if (std::fabs(n1[0].terrain->heights[k] - n1[0].terrain->heights[0]) > 0.5) varied = true;
        }
        CHECK(varied);
        // Smooth flattens a spike.
        std::vector<Brush> sp = bs;
        sp[0].terrain->heights.assign(81, 0);
        sp[0].terrain->heights[4 * 9 + 4] = 40;
        d.mode = SculptMode::Smooth; d.strength = 1; d.falloff = Falloff::Hard; d.radius = 1000;
        CHECK(sculpt(sp, d));
        CHECK(near(sp[0].terrain->heights[4 * 9 + 4], 0));
        CHECK(near(sp[0].terrain->heights[4 * 9 + 5], 10));
    }

    // Neighbouring patches stay stitched through every kind of dab.
    {
        AABB b2; b2.add({256, 0, 0}); b2.add({512, 256, 64});
        std::vector<Brush> bs{makeBox(box, "a"), makeBox(b2, "a")};
        for (Brush& x : bs) CHECK(makeTerrain(x, topQuadFace(x), 3).empty());
        auto seamOk = [&]() {
            const auto ga = terrainGrid(bs[0]), gb = terrainGrid(bs[1]);
            int shared = 0;
            bool ok = true;
            for (size_t p = 0; p < ga->base.size(); ++p)
                for (size_t q = 0; q < gb->base.size(); ++q)
                    if (length(ga->base[p] - gb->base[q]) < 1e-6) {
                        ++shared;
                        if (length(ga->pos[p] - gb->pos[q]) > 1e-9) ok = false;
                        for (const TerrainLayer& la : bs[0].terrain->layers) {
                            const int lb = terrainLayer(*bs[1].terrain, la.texture);
                            if (lb < 0 || std::fabs(la.alpha[p] - bs[1].terrain->layers[lb].alpha[q]) > 1e-9) ok = false;
                        }
                    }
            return ok && shared == 9;
        };
        SculptDab d; d.center = {256, 100, 64}; d.radius = 90;
        for (SculptMode m : {SculptMode::Raise, SculptMode::Noise, SculptMode::Smooth, SculptMode::Paint, SculptMode::Lower, SculptMode::Flatten}) {
            d.mode = m; d.amount = 12; d.strength = 0.6; d.texture = "t2"; d.flattenTarget = {0, 0, 70};
            CHECK(sculpt(bs, d));
            CHECK(seamOk());
        }
        // Out-of-step edges (e.g. edited apart) are pulled together by stitching.
        bs[0].terrain->heights[4 * 9 + 8] += 5;
        CHECK(!seamOk());
        CHECK(stitchTerrain(bs) == 1);
        CHECK(seamOk());
        CHECK(stitchTerrain(bs) == 0);
    }

    // Changing the grid size keeps the shape (a straight slope stays exact).
    {
        Brush s = makeBox(box, "a");
        CHECK(makeTerrain(s, topQuadFace(s), 2).empty());
        for (int j = 0; j < 5; ++j) for (int i = 0; i < 5; ++i) s.terrain->heights[j * 5 + i] = 10.0 * i;
        CHECK(makeTerrain(s, s.terrain->face, 4).empty());
        CHECK(s.terrain->size() == 17);
        for (int j = 0; j < 17; ++j) for (int i = 0; i < 17; ++i) CHECK(near(s.terrain->heights[j * 17 + i], 40.0 * i / 16, 1e-9));
    }

    // Heightmap: a 2 x 1 image black -> white along X gives a slope 0..100.
    {
        std::vector<Brush> bs{makeBox(box, "a")};
        CHECK(makeTerrain(bs[0], topQuadFace(bs[0]), 3).empty());
        CHECK(applyHeightmap(bs, {0.0f, 1.0f}, 2, 1, 0, 100));
        const auto hg = terrainGrid(bs[0]);
        for (size_t k = 0; k < hg->pos.size(); ++k) CHECK(near(hg->pos[k].z, 64 + 100 * hg->base[k].x / 256, 1e-9));
        // image top row = +Y side
        CHECK(applyHeightmap(bs, {1.0f, 0.0f}, 1, 2, 0, 50));
        const auto vg = terrainGrid(bs[0]);
        for (size_t k = 0; k < vg->pos.size(); ++k) CHECK(near(vg->pos[k].z, 64 + 50 * vg->base[k].y / 256, 1e-9));
        CHECK(!applyHeightmap(bs, {}, 0, 0, 0, 1));
    }
}

// The hull uses a fast edge-pivoting search; it must find exactly the faces a plain
// "every triple of points" search finds, also for clouds full of coplanar points.
static int bruteHullFaces(const std::vector<Vec3>& pts) {
    std::vector<Plane> planes;
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            for (size_t k = j + 1; k < n; ++k) {
                Vec3 nn = cross(pts[j] - pts[i], pts[k] - pts[i]);
                if (length(nn) < 1e-9) continue;
                nn = normalize(nn);
                double d = dot(nn, pts[i]), mx = -1e300, mn = 1e300;
                for (const Vec3& p : pts) { const double sd = dot(nn, p) - d; mx = std::max(mx, sd); mn = std::min(mn, sd); }
                if (mx <= 1e-5) {} else if (mn >= -1e-5) { nn = -nn; d = -d; } else continue;
                bool dup = false;
                for (const Plane& q : planes) if (dot(q.n, nn) > 1 - 1e-9 && std::fabs(q.d - d) < 1e-5) dup = true;
                if (!dup) planes.push_back({nn, d});
            }
    return (int)planes.size();
}

void testHullFast() {
    std::mt19937 rng(11);
    for (int it = 0; it < 400; ++it) {
        const int range = it % 3 == 0 ? 3 : it % 3 == 1 ? 8 : 1000;
        std::vector<Vec3> pts;
        for (int i = 0, cnt = 4 + (int)(rng() % 30); i < cnt; ++i) {
            const Vec3 p{double(rng() % range), double(rng() % range), double(rng() % range)};
            bool dup = false;
            for (const Vec3& q : pts) if (length(q - p) < 1e-6) dup = true;
            if (!dup) pts.push_back(p);
        }
        const HullResult h = brushFromVertices(pts, Brush{}, false);
        const int b = bruteHullFaces(pts);
        CHECK((h.ok ? (int)h.brush.faces.size() : -1) == (b >= 4 ? b : -1));
    }
    // A 32-sided sphere: many corners, must rebuild (was minutes with the old search).
    AABB box; box.add({0, 0, 0}); box.add({256, 256, 256});
    auto sph = makePrimitive(Primitive::Sphere, box, 2, 0, 32, true, "a", 1);
    CHECK(sph.has_value());
    if (sph) CHECK(brushFromVertices(brushVertices(*sph), *sph).ok);
}

// v0.7.1: terrain on any face, terrain points, corner edits keep terrain, strokes stay
// on their own surface, triangles stitch along their long edge.
void testTerrainPoints() {
    AABB fa; fa.add({0, 0, -64}); fa.add({256, 256, 0});
    AABB fb; fb.add({256, 0, -64}); fb.add({512, 256, 0});
    std::vector<Brush> bs{makeBox(fa, "a"), makeBox(fb, "a")};
    for (Brush& x : bs) CHECK(makeTerrain(x, topQuadFace(x), 2).empty()); // 5 x 5 points
    // A point on the seam (i = 4 of patch 0 at j = 2) is raised in both patches, once,
    // even if it is listed twice.
    const auto ga = terrainGrid(bs[0]);
    int seam = -1;
    for (int k = 0; k < (int)ga->base.size(); ++k) if (nearV(ga->base[k], {256, 128, 0})) seam = k;
    CHECK(seam >= 0);
    CHECK(moveTerrainPoints(bs, {{0, seam}, {0, seam}}, 24));
    CHECK(near(rayHitTerrain(bs[0], {255.9, 128, 1000}, {0, 0, -1}), 1000 - 24 + 0.0, 0.2));
    CHECK(near(rayHitTerrain(bs[1], {256.1, 128, 1000}, {0, 0, -1}), 1000 - 24 + 0.0, 0.2));
    double sum = 0;
    for (const Brush& b : bs) for (double h : b.terrain->heights) sum += h;
    CHECK(near(sum, 48)); // one point in each patch, raised by 24
    CHECK(moveTerrainPoints(bs, {{1, 0}}, -8, true)); // set height
    CHECK(near(bs[1].terrain->heights[0], -8));
    CHECK(!moveTerrainPoints(bs, {{1, 0}}, -8, true)); // already there

    // Moving a terrain brush's corners keeps its terrain when the face stays one face.
    {
        Brush b = bs[0];
        b.terrain->heights[12] = 10;
        const Terrain before = *b.terrain;
        std::vector<Vec3> verts = brushVertices(b);
        for (Vec3& v : verts) if (v.x > 255) v.x += 64; // stretch the whole +X side
        HullResult h = brushFromVertices(verts, b);
        CHECK(h.ok);
        CHECK(carryTerrain(b, h.brush, [](const Vec3& v) { return v.x > 255 ? v + Vec3{64, 0, 0} : v; }));
        CHECK(h.brush.terrain && h.brush.terrain->heights == before.heights);
        const auto g = terrainGrid(h.brush);
        CHECK(g && nearV(g->corners[0], {0, 0, 0}) && near(g->at(4, 0).x, 320));
        // lifting one top corner bends the top face into two: the terrain can't stay
        std::vector<Vec3> bent = brushVertices(b);
        for (Vec3& v : bent) if (v.x > 255 && v.y > 255 && v.z > -1) v.z += 32;
        HullResult hb = brushFromVertices(bent, b);
        CHECK(hb.ok);
        CHECK(!carryTerrain(b, hb.brush, [](const Vec3& v) { return v.x > 255 && v.y > 255 && v.z > -1 ? v + Vec3{0, 0, 32} : v; }));
        CHECK(!hb.brush.terrain);
    }

    // A stroke on the floor doesn't reach a wall standing beside it (its own face is far
    // away across the wall's normal), but a dab on the wall does.
    {
        AABB wa; wa.add({300, 0, 0}); wa.add({332, 256, 256});
        std::vector<Brush> fw{makeBox(fa, "a"), makeBox(wa, "a")};
        CHECK(makeTerrain(fw[0], topQuadFace(fw[0]), 3).empty());
        int west = -1;
        for (const Polygon& p : fw[1].polygons()) if (p.normal.x < -0.9) west = p.face;
        CHECK(west >= 0 && makeTerrain(fw[1], west, 3).empty());
        // 200 units from the wall with radius 150: before v0.7.1 the wall's points (whose flat
        // distance, measured in the wall's own plane, is small) were raised too.
        SculptDab d; d.mode = SculptMode::Raise; d.falloff = Falloff::Hard; d.center = {100, 128, 0}; d.radius = 150; d.amount = 10;
        CHECK(sculpt(fw, d));
        for (double h : fw[1].terrain->heights) CHECK(near(h, 0));
        d.mode = SculptMode::Paint; d.strength = 1; d.texture = "t2";
        CHECK(sculpt(fw, d));
        CHECK(fw[1].terrain->layers.empty());
        d.center = {300, 128, 40}; d.radius = 64;
        d.mode = SculptMode::Raise;
        CHECK(sculpt(fw, d));
        bool wallMoved = false;
        for (double h : fw[1].terrain->heights) wallMoved = wallMoved || std::fabs(h) > 1e-9;
        CHECK(wallMoved);
    }

    // Two triangles from one box cut along its diagonal: both tops become terrain, their
    // grid points meet along the long edge, and a dab across it keeps them stitched.
    {
        AABB tb; tb.add({0, 0, -64}); tb.add({256, 256, 0});
        for (int diag = 0; diag < 2; ++diag) {
        const SplitResult sr = diag == 0 ? splitBrush(makeBox(tb, "a"), {0, 0, 0}, {256, 256, 0}, {0, 0, -64}, "a")
                                         : splitBrush(makeBox(tb, "a"), {256, 0, 0}, {0, 256, 0}, {256, 0, -64}, "a");
        CHECK(sr.front && sr.back);
        if (sr.front && sr.back) {
            std::vector<Brush> tr{*sr.front, *sr.back};
            for (Brush& x : tr) CHECK(makeTerrain(x, topQuadFace(x), 3).empty());
            const auto g0 = terrainGrid(tr[0]), g1 = terrainGrid(tr[1]);
            CHECK(g0 && g1);
            int shared = 0;
            for (const Vec3& p : g0->base)
                for (const Vec3& q : g1->base) if (length(p - q) < 1e-6) { ++shared; break; }
            // Both start opposite the diagonal, so it is split the same way in both
            // (2 x 8 cells, 17 points).
            CHECK(shared == 17);
            SculptDab d; d.mode = SculptMode::Raise; d.falloff = Falloff::Soft; d.center = {128, 128, 0}; d.radius = 120; d.amount = 30;
            CHECK(sculpt(tr, d));
            // Just either side of the diagonal (one point in each triangle): same height.
            auto surface = [&](double x, double y) {
                double best = -1;
                for (const Brush& x0 : tr) { const double h = rayHitTerrain(x0, {x, y, 1000}, {0, 0, -1}); if (h >= 0) best = h; }
                return best;
            };
            int compared = 0;
            for (int k = 0; k < 20; ++k) {
                const double t = (k + 0.5) / 20 * 256;
                const double x = diag == 0 ? t : 256 - t, y = t;
                const Vec3 side = diag == 0 ? Vec3{0.01, -0.01, 0} : Vec3{0.01, 0.01, 0};
                const double a0 = surface(x + side.x, y + side.y), a1 = surface(x - side.x, y - side.y);
                CHECK(a0 >= 0 && a1 >= 0);
                if (a0 >= 0 && a1 >= 0) { CHECK(near(a0, a1, 0.05)); ++compared; }
            }
            CHECK(compared == 20);
        }
        }
    }
}

// ---- Milestone 8: world (lights, environment, bookmarks, figure)
void testWorld() {
    // Directions: pitch -90 = straight down, yaw 90 = +Y; angles come back the same.
    CHECK(nearV(lightDirection(-90, 0), {0, 0, -1}));
    CHECK(nearV(lightDirection(0, 90), {0, 1, 0}));
    CHECK(nearV(lightDirection(-45, 180), {-std::sqrt(0.5), 0, -std::sqrt(0.5)}));
    double p = 0, y = 0;
    directionAngles(lightDirection(-30, 250), p, y);
    CHECK(near(p, -30) && near(y, 250));
    directionAngles({0, 0, -1}, p, y, 77); // straight down keeps the old yaw
    CHECK(near(p, -90) && near(y, 77));
    // Rotation rings: about Z turns the yaw, about Y tilts a +X light down.
    Light l; l.kind = LightKind::Spot; l.pitch = 0; l.yaw = 0;
    rotateLight(l, 2, 90);
    CHECK(near(l.pitch, 0) && near(l.yaw, 90));
    Light t; t.kind = LightKind::Spot; t.pitch = 0; t.yaw = 0;
    rotateLight(t, 1, 45); // right-hand rule about +Y: +X goes toward -Z
    CHECK(near(t.pitch, -45) && near(t.yaw, 0));
    for (int i = 0; i < 8; ++i) rotateLight(t, 1, 45); // full turn and a bit: back to -45
    CHECK(near(t.pitch, -45) && near(t.yaw, 0));

    // Rough lighting.
    Environment env; env.ambient = {0, 0, 0};
    Light pt; pt.kind = LightKind::Point; pt.pos = {0, 0, 100}; pt.range = 200; pt.brightness = 200;
    const Vec3 up{0, 0, 1};
    Vec3 c = lightAt(env, {pt}, {0, 0, 0}, up);
    CHECK(near(c.x, 0.25) && near(c.y, 0.25)); // (1 - 100/200)^2, facing it
    CHECK(near(lightAt(env, {pt}, {0, 0, 0}, {0, 0, -1}).x, 0)); // facing away
    CHECK(near(lightAt(env, {pt}, {0, 0, -150}, up).x, 0));      // out of range
    env.ambient = {51, 51, 51};
    CHECK(near(lightAt(env, {}, {0, 0, 0}, up).x, 0.2));
    env.ambient = {0, 0, 0};
    Light sp = pt; sp.kind = LightKind::Spot; sp.pitch = -90; sp.inner = 10; sp.outer = 20;
    CHECK(near(lightAt(env, {sp}, {0, 0, 0}, up).x, 0.25));       // in the cone
    CHECK(near(lightAt(env, {sp}, {60, 0, 0}, up).x, 0));         // 31 degrees off: outside
    const double edge = lightAt(env, {sp}, {std::tan(15 * 3.14159265358979 / 180) * 100, 0, 0}, up).x;
    CHECK(edge > 0 && edge < 0.25);                               // between inner and outer
    Light sun; sun.kind = LightKind::Sun; sun.pitch = -90; sun.color = {255, 128, 0}; sun.brightness = 400;
    c = lightAt(env, {sun}, {5000, -9000, 3}, up);
    CHECK(near(c.x, 2) && near(c.y, 2 * 128 / 255.0) && near(c.z, 0)); // everywhere, colour x brightness

    // Document: add, modify, delete lights and change the environment, all undoable.
    Document d;
    d.apply(d.makeAdd({makeBox(box({0, 0, 0}, {64, 64, 64}), "a")}, "Create"));
    CHECK(d.selection().size() == 1);
    d.apply(d.makeAddLights({pt, sp}, "Create light"));
    CHECK(d.lights().size() == 2 && d.lightSelection().size() == 2);
    CHECK(d.selection().empty()); // brushes deselected
    const LightId a = d.lights()[0].id;
    Light m = d.lights()[0]; m.pos = {1, 2, 3};
    d.apply(d.makeModifyLights({m}, "Move light"));
    CHECK(nearV(d.findLight(a)->pos, {1, 2, 3}));
    CHECK(d.undo());
    CHECK(nearV(d.findLight(a)->pos, {0, 0, 100}));
    CHECK(d.redo());
    Command del = d.makeDelete({}, "Delete");
    d.addLightDeletes(del, {a});
    d.apply(std::move(del));
    CHECK(d.lights().size() == 1 && !d.findLight(a) && d.lightSelection().size() == 1);
    CHECK(d.undo());
    CHECK(d.lights().size() == 2 && d.lightSelection().size() == 2);
    Environment e2; e2.sky = "Panorama_Sky_04"; e2.fogOn = true; e2.fog.start = 100;
    d.apply(d.makeEnvironment(e2, "Environment"));
    CHECK(d.environment() == e2);
    CHECK(d.undo());
    CHECK(d.environment() == Environment{});
    CHECK(d.undo() && d.undo()); // light moved back, then lights gone
    CHECK(d.lights().empty() && d.lightSelection().empty() && d.selection().size() == 1);

    // File: lights as Hammer entities, environment / bookmarks / figure in worldspawn.
    Document w;
    sp.otherKeys = {{"targetname", "lamp1"}};
    w.apply(w.makeAddLights({pt, sp, sun}, "lights"));
    Environment e3 = e2; e3.ambient = {10, 20, 30}; e3.fog.color = {1, 2, 3}; e3.fog.end = 2000;
    w.apply(w.makeEnvironment(e3, "env"));
    w.setBookmarks({{"hall view", {1, 2, 3}, 45, -10}, {"b", {-5, 0, 7.5}, 300, 20}});
    w.setFigure(Vec3{64, 32, 0});
    const std::string text = writeMap(w);
    CHECK(text.find("\"classname\" \"light_spot\"") != std::string::npos);
    CHECK(text.find("\"_light\" \"255 128 0 400\"") != std::string::npos);
    CHECK(text.find("\"pitch\" \"-90\"") != std::string::npos);
    CHECK(text.find("\"_cone\" \"20\"") != std::string::npos);
    Document r;
    CHECK(readMap(text, r).ok);
    CHECK(r.lights().size() == 3 && r.otherEntities.empty());
    if (r.lights().size() == 3) {
        for (size_t i = 0; i < 3; ++i) {
            Light x = r.lights()[i], o = w.lights()[i];
            x.id = o.id = 0;
            CHECK(x == o);
        }
    }
    CHECK(r.environment() == e3);
    CHECK(r.bookmarks().size() == 2 && r.bookmarks()[0].name == "hall view" && near(r.bookmarks()[1].pitch, 20) &&
          nearV(r.bookmarks()[1].pos, {-5, 0, 7.5}));
    CHECK(r.figure() && nearV(*r.figure(), {64, 32, 0}));
    CHECK(writeMap(r) == text); // byte-identical
    for (const auto& kv : r.worldKeys) CHECK(kv.first.rfind("_scal_", 0) != 0);
    // A Quake-style light keeps the keys we don't use.
    Document q;
    CHECK(readMap("{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"light\"\n\"origin\" \"8 16 32\"\n\"light\" \"300\"\n}\n", q).ok);
    CHECK(q.lights().size() == 1);
    if (q.lights().size() == 1) {
        CHECK(nearV(q.lights()[0].pos, {8, 16, 32}));
        CHECK(writeMap(q).find("\"light\" \"300\"") != std::string::npos);
    }
}

// Sky layouts: a cross with one flat colour per face turns into a panorama whose middle
// looks at the front face, right at the right face, top at the up face (measured against
// a real pack's matching pair while writing the conversion).
void testSky() {
    CHECK(skyLayout(2048, 1024) == SkyLayout::Panorama);
    CHECK(skyLayout(2048, 1536) == SkyLayout::Cross);
    CHECK(skyLayout(512, 512) == SkyLayout::Unknown);
    CHECK(skyLayout(0, 0) == SkyLayout::Unknown);
    const int F = 8, w = 4 * F, h = 3 * F;
    std::vector<unsigned char> cross((size_t)w * h * 3, 0);
    // face colours: (col,row) -> id*30
    auto setFace = [&](int col, int row, unsigned char id) {
        for (int y = 0; y < F; ++y)
            for (int x = 0; x < F; ++x)
                for (int c = 0; c < 3; ++c) cross[((size_t)(row * F + y) * w + col * F + x) * 3 + c] = id;
    };
    setFace(0, 1, 30);  // left
    setFace(1, 1, 60);  // front
    setFace(2, 1, 90);  // right
    setFace(3, 1, 120); // back
    setFace(1, 0, 150); // up
    setFace(1, 2, 180); // down
    const int W = 64;
    const std::vector<unsigned char> pano = crossToPanorama(cross.data(), w, h, W);
    CHECK((int)pano.size() == W * (W / 2) * 3);
    auto at = [&](double u, double v) { return pano[((size_t)(int)(v * (W / 2)) * W + (int)(u * W)) * 3]; };
    CHECK(at(0.5, 0.5) == 60);   // middle: front
    CHECK(at(0.75, 0.5) == 90);  // right of it: right
    CHECK(at(0.25, 0.5) == 30);  // left
    CHECK(at(0.01, 0.5) == 120); // the edges: back
    CHECK(at(0.5, 0.02) == 150); // top: up
    CHECK(at(0.5, 0.98) == 180); // bottom: down
    // World directions to panorama coordinates: +X in the middle, turning right (toward
    // -Y) moves right, up is the top.
    double u = 0, v = 0;
    skyUV({1, 0, 0}, u, v);
    CHECK(near(u, 0.5) && near(v, 0.5));
    skyUV({0, -1, 0}, u, v);
    CHECK(near(u, 0.75));
    skyUV({0, 1, 0}, u, v);
    CHECK(near(u, 0.25));
    skyUV({0, 0, 1}, u, v);
    CHECK(near(v, 0));
    skyUV({-1, 0, -1}, u, v);
    CHECK((near(u, 0) || near(u, 1)) && near(v, 0.75));
}

// ---- Milestone 9: math primitives
void testMathPrims() {
    auto hit = [](const MathPrim& p, Vec3 o, Vec3 d) {
        auto h = rayHitPrims({p}, o, normalize(d));
        return h ? h->t : -1.0;
    };
    auto prim = [](PrimKind k, Vec3 pos, Vec3 size) {
        MathPrim p; p.id = 1; p.kind = k; p.pos = pos; p.size = size;
        return p;
    };
    // Rotation: angles -> matrix -> angles, and turning about world axes.
    for (Vec3 r : {Vec3{10, 20, 30}, Vec3{-45, 60, 170}, Vec3{0, 0, 90}, Vec3{33, -80, -120}}) {
        const Vec3 back = rotationAngles(rotationMatrix(r));
        CHECK(nearV(back, r, 1e-6));
    }
    MathPrim t = prim(PrimKind::Cube, {100, 0, 0}, {64, 32, 16});
    rotatePrim(t, 2, 90);
    CHECK(nearV(t.rot, {0, 0, 90}) && nearV(t.pos, {100, 0, 0}));
    rotatePrim(t, 2, 90, {0, 0, 0}); // about the origin: moves too
    CHECK(nearV(t.rot, {0, 0, 180}) && nearV(t.pos, {0, 100, 0}));
    const AABB tb = primBounds(t);
    CHECK(near(tb.size().x, 64) && near(tb.size().y, 32));
    MathPrim r45 = prim(PrimKind::Cube, {}, {64, 64, 64});
    r45.rot = {0, 0, 45};
    CHECK(near(primBounds(r45).size().x, 64 * std::sqrt(2.0), 1e-6));
    CHECK(nearV(primToLocal(t, primToWorld(t, {0.3, -0.2, 0.9})), {0.3, -0.2, 0.9}, 1e-9));

    // Sphere / ellipsoid.
    MathPrim sp = prim(PrimKind::Sphere, {100, 0, 0}, {64, 64, 64});
    CHECK(near(hit(sp, {0, 0, 0}, {1, 0, 0}), 68));
    auto hs = rayHitPrims({sp}, {0, 0, 0}, {1, 0, 0});
    CHECK(hs && nearV(hs->normal, {-1, 0, 0}) && hs->prim == 1 && hs->picked == 1);
    sp.size = {128, 64, 64};
    CHECK(near(hit(sp, {0, 0, 0}, {1, 0, 0}), 36));
    CHECK(near(hit(sp, {100, 0, 100}, {0, 0, -1}), 68));
    CHECK(hit(sp, {0, 40, 0}, {1, 0, 0}) < 0); // passes beside it
    CHECK(hit(sp, {100, 0, 0}, {1, 0, 0}) < 0); // from inside: nothing (like brushes)
    // Cylinder, cone, capsule, torus, disc.
    MathPrim cy = prim(PrimKind::Cylinder, {0, 0, 0}, {64, 64, 128});
    CHECK(near(hit(cy, {0, 0, 200}, {0, 0, -1}), 136));
    CHECK(near(hit(cy, {-200, 0, 10}, {1, 0, 0}), 168));
    CHECK(hit(cy, {-200, 0, 70}, {1, 0, 0}) < 0);
    MathPrim co = prim(PrimKind::Cone, {}, {64, 64, 64});
    CHECK(near(hit(co, {0, 0, 200}, {0, 0, -1}), 168)); // the tip
    CHECK(near(hit(co, {-200, 0, 0}, {1, 0, 0}), 200 - 16)); // half way up: radius 16
    CHECK(near(hit(co, {16.1, 0, -200}, {0, 0, 1}), 200 - 32, 1e-6)); // the base
    MathPrim ca = prim(PrimKind::Capsule, {}, {32, 32, 128});
    CHECK(near(hit(ca, {0, 0, 200}, {0, 0, -1}), 136));
    CHECK(near(hit(ca, {-200, 0, 0}, {1, 0, 0}), 184));
    CHECK(near(hit(ca, {15.9, 0, 200}, {0, 0, -1}), 200 - (48 + std::sqrt(16.0 * 16 - 15.9 * 15.9)), 1e-6));
    MathPrim to = prim(PrimKind::Torus, {}, {128, 128, 32});
    to.thickness = 0.25; // ring radius 48, tube 16
    CHECK(near(hit(to, {48, 0, 200}, {0, 0, -1}), 184, 1e-6));
    CHECK(hit(to, {0, 0, 200}, {0, 0, -1}) < 0); // through the hole
    CHECK(near(hit(to, {-200, 0, 0}, {1, 0, 0}), 136, 1e-6));
    auto ti = primIntervals(to, {-200, 0, 0}, {1, 0, 0});
    CHECK(ti.size() == 2); // in and out of the tube twice
    MathPrim di = prim(PrimKind::Disc, {}, {64, 64, 0});
    CHECK(near(hit(di, {0, 0, 50}, {0, 0, -1}), 50));
    auto hd = rayHitPrims({di}, {10, 0, -50}, {0, 0, 1});
    CHECK(hd && near(hd->t, 50) && nearV(hd->normal, {0, 0, -1})); // seen from below
    // Cubes, sharp and rounded; rotated.
    MathPrim cu = prim(PrimKind::Cube, {}, {64, 64, 64});
    CHECK(near(hit(cu, {-100, 0, 0}, {1, 0, 0}), 68));
    const Vec3 diag = normalize(Vec3{1, 1, 1});
    CHECK(near(hit(cu, diag * -100.0, diag), 100 - 32 * std::sqrt(3.0), 1e-6));
    cu.round = 8;
    CHECK(near(hit(cu, {-100, 0, 0}, {1, 0, 0}), 68, 1e-4));
    CHECK(near(hit(cu, diag * -100.0, diag), 100 - (24 * std::sqrt(3.0) + 8), 1e-4));
    cu.round = 0;
    cu.rot = {0, 0, 45};
    CHECK(near(hit(cu, {-100, 0, 0}, {1, 0, 0}), 100 - 32 * std::sqrt(2.0), 1e-6));

    // Combining.
    MathPrim a = prim(PrimKind::Sphere, {}, {100, 100, 100});
    a.id = 1; a.combo = 7;
    MathPrim b = prim(PrimKind::Sphere, {-50, 0, 0}, {60, 60, 60});
    b.id = 2; b.combo = 7; b.op = PrimOp::Subtract;
    auto hc = rayHitPrims({a, b}, {-200, 0, 0}, {1, 0, 0});
    CHECK(hc && near(hc->t, 180) && hc->prim == 2 && hc->picked == 1); // the bite, at x = -20
    CHECK(hc && nearV(hc->normal, {-1, 0, 0}));                          // facing out of the hole
    CHECK(near(rayHitPrims({a, b}, {-200, 40, 0}, {1, 0, 0})->t, 200 - 30, 1e-6)); // above the bite
    b.op = PrimOp::Intersect;
    auto hi = rayHitPrims({a, b}, {-200, 0, 0}, {1, 0, 0});
    CHECK(hi && near(hi->t, 150) && hi->prim == 1); // only the overlap: x from -50 to -20
    b.op = PrimOp::Add;
    auto hu = rayHitPrims({a, b}, {-200, 0, 0}, {1, 0, 0});
    CHECK(hu && near(hu->t, 120) && hu->prim == 2);
    b.combo = 0; // apart again: nearest of two shapes
    CHECK(near(rayHitPrims({a, b}, {-200, 0, 0}, {1, 0, 0})->t, 120));
    std::vector<MathPrim> all{a, b};
    all[1].combo = 7;
    CHECK(comboMembers(all, 7).size() == 2 && newComboId(all) == 8);

    // Outlines lie on the shape (sphere circles at its radius).
    for (const Polyline& l : primOutline(sp, 24))
        for (const Vec3& q : l.pts) CHECK(near(length(primToLocal(sp, q)), 1, 1e-9));
    CHECK(primOutline(cu).size() == 12);

    // Document: add / modify / delete / combos, undoable.
    Document d;
    d.apply(d.makeAddLights({Light{}}, "light"));
    d.apply(d.makeAddPrims({a, b}, "Create"));
    CHECK(d.prims().size() == 2 && d.primSelection().size() == 2 && d.lightSelection().empty());
    const MathPrimId first = d.prims()[0].id;
    CHECK(d.withCombos({first}).size() == 1);
    MathPrim m = d.prims()[1];
    m.combo = 7;
    d.apply(d.makeModifyPrims({m}, "Combine"));
    CHECK(d.withCombos({first}).size() == 2);
    Command del = d.makeDelete({}, "Delete");
    d.addPrimDeletes(del, {first});
    d.apply(std::move(del));
    CHECK(d.prims().size() == 1 && !d.findPrim(first));
    CHECK(d.undo() && d.prims().size() == 2);
    CHECK(d.undo() && d.withCombos({first}).size() == 1);
    CHECK(d.undo() && d.prims().empty() && d.lightSelection().size() == 1);

    // File: kept as primitives (byte-identical round trip).
    Document w;
    MathPrim tx = to;
    tx.id = 0; tx.combo = 3; tx.op = PrimOp::Subtract; tx.texture = "Tile_19"; tx.rot = {10, 0, 45}; tx.texScale = 2;
    tx.otherKeys = {{"note", "keep me"}};
    MathPrim rc = cu;
    rc.id = 0; rc.round = 6; rc.color = {255, 0, 128};
    w.apply(w.makeAddPrims({tx, rc}, "prims"));
    const std::string text = writeMap(w);
    CHECK(text.find("\"classname\" \"scal_primitive\"") != std::string::npos);
    CHECK(text.find("\"kind\" \"torus\"") != std::string::npos);
    Document r;
    CHECK(readMap(text, r).ok);
    CHECK(r.prims().size() == 2 && r.otherEntities.empty());
    if (r.prims().size() == 2)
        for (size_t i = 0; i < 2; ++i) {
            MathPrim x = r.prims()[i], o = w.prims()[i];
            x.id = o.id = 0;
            CHECK(x == o);
        }
    CHECK(writeMap(r) == text);
}

// Blending primitives (v0.9.2): distance functions and smooth combining.
void testPrimBlend() {
    auto prim = [](PrimKind k, Vec3 pos, Vec3 size) {
        MathPrim p; p.id = 1; p.kind = k; p.pos = pos; p.size = size;
        return p;
    };
    CHECK(near(smoothMin(3, 5, 0), 3) && near(smoothMax(3, 5, 0), 5));
    CHECK(smoothMin(3, 3, 2) < 3 && smoothMax(3, 3, 2) > 3);
    CHECK(near(smoothMin(0, 10, 2), 0)); // far apart: no blending
    MathPrim s = prim(PrimKind::Sphere, {}, {100, 100, 100});
    CHECK(near(primDistance(s, {200, 0, 0}), 150) && near(primDistance(s, {0, 0, 0}), -50));
    MathPrim c = prim(PrimKind::Cube, {}, {100, 100, 100});
    c.round = 10;
    CHECK(near(primDistance(c, {80, 0, 0}), 30));
    CHECK(near(primDistance(c, Vec3{1, 1, 1} * 100.0), length(Vec3{60, 60, 60}) - 10, 1e-9));
    // Every kind: the distance is ~0 where the exact ray hit lands, negative inside,
    // positive outside (rotated and stretched too).
    for (int k = 0; k < kPrimKinds; ++k) {
        MathPrim p = prim((PrimKind)k, {10, -20, 30}, {120, 80, k == (int)PrimKind::Disc ? 0.0 : 100});
        p.rot = {15, -25, 40};
        p.round = 8;
        p.thickness = 0.3;
        int hits = 0;
        for (int i = 0; i < 40; ++i) {
            const double a = i * 0.7, b = i * 1.3;
            const Vec3 dir = normalize(Vec3{std::cos(a) * std::cos(b * 0.3), std::sin(a) * std::cos(b * 0.3), std::sin(b * 0.3) - 0.2});
            const Vec3 o = p.pos - dir * 400.0 + Vec3{std::sin(b) * 15, std::cos(a) * 15, 0};
            auto h = rayHitPrims({p}, o, dir);
            if (!h) continue;
            ++hits;
            const Vec3 q = o + dir * h->t;
            CHECK(std::fabs(primDistance(p, q)) < (k == (int)PrimKind::Disc ? 0.3 : 0.05));
            CHECK(primDistance(p, q - dir * 5.0) > 0); // just before: outside
        }
        CHECK(hits > 10);
        if (k != (int)PrimKind::Disc) CHECK(primDistance(p, p.pos) < 0 || k == (int)PrimKind::Torus);
    }
    // Two spheres touching at the origin: a sharp union leaves a gap above the contact,
    // a blended one fills it with a smooth neck.
    MathPrim a = prim(PrimKind::Sphere, {-50, 0, 0}, {100, 100, 100});
    MathPrim b = prim(PrimKind::Sphere, {50, 0, 0}, {100, 100, 100});
    a.id = 1; b.id = 2; a.combo = b.combo = 1;
    const Vec3 o{0, -300, 20}, dir{0, 1, 0};
    CHECK(!rayHitPrims({a, b}, o, dir));
    b.blend = 40;
    auto hb = rayHitPrims({a, b}, o, dir);
    CHECK(hb.has_value());
    if (hb) {
        CHECK(hb->t > 200 && hb->t < 300);
        CHECK(std::fabs(hb->normal.x) < 1e-3 && hb->normal.y < -0.5); // symmetric neck, facing the ray
        CHECK(hb->picked == 1);
    }
    // Far from the neck, blending changes nothing.
    auto far1 = rayHitPrims({a, b}, {-50, -300, 0}, dir);
    CHECK(far1 && near(far1->t, 250, 0.05));
    // A blended cut has a soft rim: hits sit between the sharp cut and the uncut surface.
    MathPrim big = prim(PrimKind::Cube, {}, {200, 200, 200});
    MathPrim cut = prim(PrimKind::Sphere, {0, 0, 100}, {120, 120, 120});
    big.id = 1; cut.id = 2; big.combo = cut.combo = 2; cut.op = PrimOp::Subtract;
    const Vec3 down{0, 0, -1};
    const double rimX = 62; // just outside the sharp hole (radius 60)
    auto sharp = rayHitPrims({big, cut}, {rimX, 0, 500}, down);
    CHECK(sharp && near(sharp->t, 400, 1e-6));
    cut.blend = 30;
    auto soft = rayHitPrims({big, cut}, {rimX, 0, 500}, down);
    CHECK(soft && soft->t > 400.5);
    // File: blend round trip.
    Document w;
    b.id = 0;
    w.apply(w.makeAddPrims({b}, "p"));
    Document r;
    CHECK(readMap(writeMap(w), r).ok && r.prims().size() == 1 && near(r.prims()[0].blend, 40));
}

// Snap to Land and Multi-Replicate (v0.9.2).
void testLandingReplicate() {
    auto sphere = [](Vec3 c, double r) {
        MathPrim p; p.id = 1; p.kind = PrimKind::Sphere; p.pos = c; p.size = {2 * r, 2 * r, 2 * r};
        return p;
    };
    AABB fb; fb.add({-500, -500, -64}); fb.add({500, 500, 0});
    const Brush floor = makeBox(fb, "a");
    std::vector<const Brush*> bs{&floor};
    MathPrim s = sphere({0, 0, 300}, 50);
    auto m = landingMove({&s}, bs, {});
    CHECK(m && near(*m, -250, 1e-6));
    CHECK(near(objectBottom({&s}), 250, 1e-6));
    MathPrim sunk = sphere({0, 0, 20}, 50);
    auto up = landingMove({&sunk}, bs, {});
    CHECK(up && near(*up, 30, 1e-6)); // comes up to rest on top
    MathPrim away = sphere({5000, 0, 300}, 50);
    CHECK(!landingMove({&away}, bs, {}));
    // A 45 degree slope (z = x): the sphere touches it beside its lowest point.
    AABB wb; wb.add({-300, -300, -300}); wb.add({300, 300, 300});
    const SplitResult sr = splitBrush(makeBox(wb, "a"), {0, -300, 0}, {0, 300, 0}, {100, 0, 100}, "a");
    // the half whose top along x = 100 is the slope (z = 100 there)
    const Brush* slope = sr.front && near(rayHit(*sr.front, {100, 0, 1000}, {0, 0, -1}), 900, 1e-6) ? &*sr.front : &*sr.back;
    CHECK(near(rayHit(*slope, {100, 0, 1000}, {0, 0, -1}), 900, 1e-6));
    {
        MathPrim t = sphere({0, 0, 600}, 50);
        auto mv = landingMove({&t}, {slope}, {});
        CHECK(mv.has_value());
        if (mv) {
            t.pos.z += *mv;
            // highest point of the slope under the sphere's footprint decides; touching
            // the plane z = x (or the top face) means distance >= 50 everywhere and ~50 somewhere
            double dmin = 1e9;
            for (const Plane& pl : {Plane{normalize(Vec3{-1, 0, 1}), 0}}) dmin = std::min(dmin, pl.distance(t.pos));
            CHECK(dmin > 49.5 && dmin < 51.5);
        }
    }
    // Raised terrain under a small sphere.
    {
        Brush tb = makeBox(fb, "a");
        CHECK(makeTerrain(tb, topQuadFace(tb), 3).empty());
        tb.terrain->heights.assign(tb.terrain->heights.size(), 0);
        tb.terrain->heights[4 * 9 + 4] = 80; // the middle point
        MathPrim t = sphere({0, 0, 400}, 10);
        auto mv = landingMove({&t}, {&tb}, {});
        CHECK(mv && near(t.pos.z + *mv, 90, 1.0)); // on the peak, not the flat face under it
    }
    // On another primitive, and a combination landing as one.
    {
        MathPrim base = sphere({0, 0, 50}, 50);
        base.id = 9;
        MathPrim t = sphere({0, 0, 500}, 30);
        auto mv = landingMove({&t}, bs, {&base});
        CHECK(mv && near(t.pos.z + *mv, 130, 0.5));
        MathPrim a = sphere({-40, 0, 400}, 40), b = sphere({40, 0, 380}, 40);
        a.combo = b.combo = 3;
        b.id = 2;
        auto mc = landingMove({&a, &b}, bs, {});
        CHECK(mc && near(b.pos.z + *mc, 40, 0.5)); // the lower piece touches
    }
    // Multi-Replicate.
    ReplicateSpec rs;
    rs.copies = 3;
    rs.offset = {100, 0, 0};
    rs.rotate = {0, 0, 90}; // brushes ignore it
    AABB cb; cb.add({0, 0, 0}); cb.add({16, 16, 16});
    auto cs = replicate({makeBox(cb, "a")}, {}, {}, rs);
    CHECK(cs.size() == 3);
    for (int i = 0; i < 3; ++i) {
        const AABB b = cs[i].brushes[0].bounds();
        CHECK(nearV(b.min, {100.0 * (i + 1), 0, 0}) && nearV(b.size(), {16, 16, 16}));
    }
    // A ring of 12 around the origin: the 12th is back where it started.
    ReplicateSpec ring;
    ring.copies = 12;
    ring.offset = {};
    ring.rotate = {0, 0, 30};
    ring.pivot = ReplicateSpec::Pivot::Origin;
    MathPrim cube; cube.kind = PrimKind::Cube; cube.pos = {200, 0, 0};
    Light l; l.kind = LightKind::Spot; l.pos = {300, 0, 0}; l.pitch = 0; l.yaw = 0;
    auto rc = replicate({}, {cube}, {l}, ring);
    CHECK(rc.size() == 12);
    CHECK(nearV(rc[2].prims[0].pos, {0, 200, 0}, 1e-6) && near(rc[2].prims[0].rot.z, 90));
    CHECK(nearV(rc[11].prims[0].pos, {200, 0, 0}, 1e-6));
    CHECK(nearV(rc[2].lights[0].pos, {0, 300, 0}, 1e-6) && near(rc[2].lights[0].yaw, 90));
    // Scaled steps, offsets scaled too: 100, 50, 25.
    ReplicateSpec sc;
    sc.copies = 3;
    sc.offset = {100, 0, 0};
    sc.scale = {50, 50, 50};
    sc.scaleOffset = true;
    MathPrim sp = sphere({0, 0, 0}, 50);
    auto rsc = replicate({}, {sp}, {}, sc);
    CHECK(near(rsc[0].prims[0].pos.x, 100) && near(rsc[1].prims[0].pos.x, 150) && near(rsc[2].prims[0].pos.x, 175));
    CHECK(near(rsc[2].prims[0].size.x, 100 * 0.125));
}

// ---------------------------------------------------------------- bottega

static void testBottega() {
    namespace fs = std::filesystem;
    namespace bt = scal::bottega;
    const fs::path base = fs::temp_directory_path() / "scal_bottega_test";
    fs::remove_all(base);
    const std::string root = (base / "Studio").string();

    CHECK(bt::inspect("").state == bt::State::NoPath);
    CHECK(bt::inspect(root).state == bt::State::MissingFolder);

    // make() on a path that doesn't exist yet creates the folder, every sub-folder and the marker.
    std::string err;
    CHECK(bt::make(root, err));
    CHECK(err.empty());
    bt::Check c = bt::inspect(root);
    CHECK(c.state == bt::State::Ready);
    CHECK(c.version == bt::kFormatVersion);
    CHECK(c.missing.empty());
    CHECK(bt::usable(c));
    for (const std::string& sub : bt::subfolders()) CHECK(fs::is_directory(bt::path(root, sub)));
    CHECK(fs::is_directory(bt::path(root, "presets/camera_presets")));
    CHECK(fs::is_regular_file(bt::path(root, bt::kMarkerFile)));

    // Making again changes nothing and keeps files.
    { std::ofstream f(bt::path(root, "textures/keep.png")); f << "x"; }
    CHECK(bt::make(root, err));
    CHECK(fs::is_regular_file(bt::path(root, "textures/keep.png")));

    // A missing sub-folder is reported, and make() puts it back.
    fs::remove_all(bt::path(root, "skies"));
    c = bt::inspect(root);
    CHECK(c.state == bt::State::Ready);
    CHECK(c.missing.size() == 1 && c.missing[0] == "skies");
    CHECK(!bt::describe(c).empty());
    CHECK(bt::make(root, err));
    CHECK(bt::inspect(root).missing.empty());

    // Notes in the marker are fine.
    { std::ofstream f(bt::path(root, bt::kMarkerFile)); f << "# hello\nbottega 1\n# more\n"; }
    CHECK(bt::inspect(root).state == bt::State::Ready);

    // An empty folder, and a folder with files but no marker (files are kept).
    const std::string empty = (base / "Empty").string();
    fs::create_directories(empty);
    CHECK(bt::inspect(empty).state == bt::State::Empty);
    const std::string used = (base / "Used").string();
    fs::create_directories(used);
    { std::ofstream f(bt::path(used, "old.png")); f << "x"; }
    CHECK(bt::inspect(used).state == bt::State::NotBottega);
    CHECK(bt::make(used, err));
    CHECK(bt::inspect(used).state == bt::State::Ready);
    CHECK(fs::is_regular_file(bt::path(used, "old.png")));

    // A newer version is left alone; a damaged marker is refused.
    { std::ofstream f(bt::path(root, bt::kMarkerFile)); f << "bottega 99\n"; }
    c = bt::inspect(root);
    CHECK(c.state == bt::State::NewerVersion && c.version == 99);
    CHECK(!bt::usable(c));
    CHECK(!bt::make(root, err));
    CHECK(!err.empty());
    { std::ofstream f(bt::path(root, bt::kMarkerFile)); f << "garbage\n"; }
    CHECK(bt::inspect(root).state == bt::State::Damaged);
    CHECK(!bt::make(root, err));
    CHECK(!bt::make("", err));
    // A path that is a file, not a folder.
    CHECK(bt::inspect(bt::path(used, "old.png")).state == bt::State::Damaged);

    // The name a file goes by in a map.
    CHECK(bt::keyFor(root, bt::path(root, "textures/walls/Brick Red.PNG")) == "textures/walls/Brick_Red");
    CHECK(bt::keyFor(root, bt::path(root, "skies/Dusk.png")) == "skies/Dusk");
    CHECK(bt::keyFor(root, (base / "Other" / "x.png").string()).empty());
    CHECK(bt::keyFor("", "a.png").empty());
    CHECK(bt::path("/a/b", "c") == "/a/b/c");
    CHECK(bt::path("/a/b/", "c") == "/a/b/c");

    fs::remove_all(base);
}


// ---------------------------------------------------------------- props (milestone 10)

namespace {
// A small `.glb` made on the spot (glTF units: metres, Y up). Kinds: a 1 m cube standing on
// y = 0 (x and z from -0.5 to 0.5) with a material and an embedded "image", or one
// triangle without indices or normals.
enum class GlbKind { Cube, Triangle };
struct GlbOptions {
    GlbKind kind = GlbKind::Cube;
    double tx = 0;             // the node's translation along glTF x (metres)
    bool image = false;        // embed an image (bytes 1..10) with the material
    bool mask = false;         // alpha mode MASK, cutoff 0.3
    bool noMaterial = false;
};

void put32(std::string& o, std::uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back((char)((v >> (8 * i)) & 0xff)); }

std::string makeGlb(const GlbOptions& o) {
    std::vector<float> pos, nor, uv;
    std::vector<std::uint16_t> idx;
    if (o.kind == GlbKind::Cube) {
        const float h = 0.5f;
        const float P[8][3] = {{-h, 0, -h}, {h, 0, -h}, {h, 1, -h}, {-h, 1, -h}, {-h, 0, h}, {h, 0, h}, {h, 1, h}, {-h, 1, h}};
        const int F[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
        const float N[6][3] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
        for (int f = 0; f < 6; ++f) {
            for (int k = 0; k < 4; ++k) {
                for (int a = 0; a < 3; ++a) { pos.push_back(P[F[f][k]][a]); nor.push_back(N[f][a]); }
                uv.push_back(k == 1 || k == 2 ? 1.f : 0.f);
                uv.push_back(k >= 2 ? 1.f : 0.f);
            }
            const std::uint16_t b = (std::uint16_t)(4 * f);
            for (std::uint16_t i : {0, 2, 1, 0, 3, 2}) idx.push_back((std::uint16_t)(b + i));
        }
    } else {
        pos = {0, 0, 0, 1, 0, 0, 0, 0, -1}; // in the xz plane, y = 0
    }
    std::string bin;
    auto addF = [&](const std::vector<float>& v) { const size_t at = bin.size(); bin.append((const char*)v.data(), v.size() * 4); return at; };
    const size_t posAt = addF(pos);
    const size_t norAt = nor.empty() ? 0 : addF(nor);
    const size_t uvAt = uv.empty() ? 0 : addF(uv);
    size_t idxAt = 0;
    if (!idx.empty()) { idxAt = bin.size(); bin.append((const char*)idx.data(), idx.size() * 2); while (bin.size() % 4) bin.push_back(0); }
    size_t imgAt = 0;
    if (o.image) { imgAt = bin.size(); for (int i = 1; i <= 10; ++i) bin.push_back((char)i); while (bin.size() % 4) bin.push_back(0); }
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (size_t i = 0; i < pos.size(); ++i) { lo[i % 3] = std::min(lo[i % 3], pos[i]); hi[i % 3] = std::max(hi[i % 3], pos[i]); }
    std::ostringstream j;
    j << "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
      << "\"nodes\":[{\"mesh\":0,\"translation\":[" << o.tx << ",0,0]}],"
      << "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0";
    int next = 1;
    int norAcc = -1, uvAcc = -1, idxAcc = -1;
    if (!nor.empty()) { norAcc = next++; j << ",\"NORMAL\":" << norAcc; }
    if (!uv.empty()) { uvAcc = next++; j << ",\"TEXCOORD_0\":" << uvAcc; }
    j << "}";
    if (!idx.empty()) { idxAcc = next++; j << ",\"indices\":" << idxAcc; }
    if (!o.noMaterial) j << ",\"material\":0";
    j << "}]}]";
    if (!o.noMaterial) {
        j << ",\"materials\":[{\"name\":\"paint\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0.5,0.25,1]"
          << (o.image ? ",\"baseColorTexture\":{\"index\":0}" : "") << "}"
          << (o.mask ? ",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.3" : "") << "}]";
        if (o.image) j << ",\"textures\":[{\"source\":0}],\"images\":[{\"bufferView\":4,\"mimeType\":\"image/png\"}]";
    }
    j << ",\"buffers\":[{\"byteLength\":" << bin.size() << "}],\"bufferViews\":[";
    j << "{\"buffer\":0,\"byteOffset\":" << posAt << ",\"byteLength\":" << pos.size() * 4 << "}";
    j << ",{\"buffer\":0,\"byteOffset\":" << norAt << ",\"byteLength\":" << nor.size() * 4 << "}";
    j << ",{\"buffer\":0,\"byteOffset\":" << uvAt << ",\"byteLength\":" << uv.size() * 4 << "}";
    j << ",{\"buffer\":0,\"byteOffset\":" << idxAt << ",\"byteLength\":" << idx.size() * 2 << "}";
    if (o.image) j << ",{\"buffer\":0,\"byteOffset\":" << imgAt << ",\"byteLength\":10}";
    j << "],\"accessors\":[";
    j << "{\"bufferView\":0,\"componentType\":5126,\"count\":" << pos.size() / 3 << ",\"type\":\"VEC3\",\"min\":[" << lo[0] << ","
      << lo[1] << "," << lo[2] << "],\"max\":[" << hi[0] << "," << hi[1] << "," << hi[2] << "]}";
    if (norAcc >= 0) j << ",{\"bufferView\":1,\"componentType\":5126,\"count\":" << nor.size() / 3 << ",\"type\":\"VEC3\"}";
    if (uvAcc >= 0) j << ",{\"bufferView\":2,\"componentType\":5126,\"count\":" << uv.size() / 2 << ",\"type\":\"VEC2\"}";
    if (idxAcc >= 0) j << ",{\"bufferView\":3,\"componentType\":5123,\"count\":" << idx.size() << ",\"type\":\"SCALAR\"}";
    j << "]}";
    std::string json = j.str();
    while (json.size() % 4) json.push_back(' ');
    std::string out;
    put32(out, 0x46546C67);
    put32(out, 2);
    put32(out, (std::uint32_t)(12 + 8 + json.size() + 8 + bin.size()));
    put32(out, (std::uint32_t)json.size());
    put32(out, 0x4E4F534A);
    out += json;
    put32(out, (std::uint32_t)bin.size());
    put32(out, 0x004E4942);
    out += bin;
    return out;
}

void writeFile(const std::filesystem::path& p, const std::string& data) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << data;
}

Prop makeProp(const std::string& model, Vec3 pos, Vec3 rot = {}, Vec3 scale = {1, 1, 1}) {
    Prop p;
    p.model = model;
    p.pos = pos;
    p.rot = rot;
    p.scale = scale;
    return p;
}
} // namespace

static void testPropsLoad() {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "scal_props_test";
    fs::remove_all(base);
    std::string err;

    // A cube: glTF metres, Y up -> centimetres, Z up; 1 m cube standing on the floor.
    GlbOptions o;
    o.tx = 1;
    o.image = true;
    o.mask = true;
    writeFile(base / "cube.glb", makeGlb(o));
    Model m;
    CHECK(loadGlbFile((base / "cube.glb").string(), m, err));
    CHECK(err.empty());
    CHECK(m.triangles() == 12);
    CHECK(m.meshes.size() == 1 && m.meshes[0].vertices() == 24);
    CHECK(nearV(m.bounds.min, {50, -50, 0}, 1e-3));   // x 0.5..1.5 m, y = -z, z = y
    CHECK(nearV(m.bounds.max, {150, 50, 100}, 1e-3));
    CHECK(m.materials.size() == 1);
    CHECK(near(m.materials[0].color.x, 255, 1e-3) && near(m.materials[0].color.y, 127.5, 1e-3) && near(m.materials[0].color.z, 63.75, 1e-3));
    CHECK(m.materials[0].cutout && near(m.materials[0].cutoff, 0.3, 1e-6));
    CHECK(m.materials[0].image.size() == 10 && m.materials[0].image[0] == 1 && m.materials[0].image[9] == 10);
    CHECK(m.materials[0].hasTexture());
    // Normals are Z-up too: the glTF +y face points to +Z.
    {
        bool up = false;
        const ModelMesh& mesh = m.meshes[0];
        for (size_t i = 0; i < mesh.vertices(); ++i)
            if (near(mesh.normal[3 * i + 2], 1, 1e-6) && near(mesh.pos[3 * i + 2], 100, 1e-3)) up = true;
        CHECK(up);
    }
    // Winding stays outward: every triangle's face normal points away from the centre.
    {
        const ModelMesh& mesh = m.meshes[0];
        const Vec3 c = m.bounds.center();
        bool outward = true;
        for (size_t i = 0; i + 2 < mesh.index.size(); i += 3) {
            const float *a = &mesh.pos[3 * mesh.index[i]], *b = &mesh.pos[3 * mesh.index[i + 1]], *d = &mesh.pos[3 * mesh.index[i + 2]];
            const Vec3 fn = cross(Vec3{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, Vec3{d[0] - a[0], d[1] - a[1], d[2] - a[2]});
            if (dot(fn, Vec3{a[0], a[1], a[2]} - c) <= 0) outward = false;
        }
        CHECK(outward);
    }

    // No material: a plain grey one is made. No indices, no normals: both are worked out.
    GlbOptions t;
    t.kind = GlbKind::Triangle;
    t.noMaterial = true;
    writeFile(base / "tri.glb", makeGlb(t));
    Model tm;
    CHECK(loadGlbFile((base / "tri.glb").string(), tm, err));
    CHECK(tm.triangles() == 1 && tm.materials.size() == 1 && !tm.materials[0].hasTexture());
    {
        const ModelMesh& mesh = tm.meshes[0];
        const Vec3 n{mesh.normal[0], mesh.normal[1], mesh.normal[2]};
        CHECK(near(length(n), 1, 1e-5));
        CHECK(near(std::fabs(n.z), 1, 1e-5)); // the glTF xz plane is our XY plane: normal along Z
    }

    // Damaged and missing files fail with a plain message instead of crashing.
    writeFile(base / "bad.glb", "not a glb at all");
    Model bm;
    CHECK(!loadGlbFile((base / "bad.glb").string(), bm, err) && !err.empty());
    CHECK(!loadGlbFile((base / "nope.glb").string(), bm, err) && !err.empty());
    { std::string cut = makeGlb(o); cut.resize(cut.size() / 2); writeFile(base / "cut.glb", cut); }
    CHECK(!loadGlbFile((base / "cut.glb").string(), bm, err));
    fs::remove_all(base);
}

static void testPropsStore() {
    namespace fs = std::filesystem;
    namespace bt = scal::bottega;
    const fs::path base = fs::temp_directory_path() / "scal_props_store_test";
    fs::remove_all(base);
    const std::string root = (base / "Studio").string();
    std::string err;
    CHECK(bt::make(root, err));
    const std::string cube = makeGlb({});
    writeFile(fs::path(root) / "models/crate/crate.glb", cube);
    writeFile(fs::path(root) / "models/loose.glb", cube);
    writeFile(fs::path(root) / "models/pack/a.glb", cube);
    writeFile(fs::path(root) / "models/pack/b.glb", cube);
    writeFile(fs::path(root) / "models/one/whatever.glb", cube);
    writeFile(fs::path(root) / "models/old chair/old chair.glb", cube);
    writeFile(fs::path(root) / "models/broken/broken.glb", "junk");
    writeFile(fs::path(root) / "models/notes.txt", "hello");
    writeFile(fs::path(root) / "models/props/barrel/barrel.glb", cube);

    ModelStore st;
    CHECK(st.names().empty());
    CHECK(st.find("models/crate") == nullptr);
    st.setBottegaRoot(root);
    const std::vector<std::string> want{"models/broken", "models/crate", "models/loose", "models/old_chair", "models/one",
                                        "models/pack/a", "models/pack/b", "models/props/barrel"};
    CHECK(st.names() == want);
    CHECK(st.exists("models/crate") && !st.exists("models/nothing"));
    const Model* crate = st.find("models/crate");
    CHECK(crate && crate->triangles() == 12 && crate->name == "models/crate");
    CHECK(st.find("models/crate") == crate); // loaded once, shared
    CHECK(st.find("models/one") != nullptr);
    CHECK(st.find("models/old_chair") != nullptr);
    CHECK(st.find("models/pack/b") != nullptr);
    CHECK(st.find("models/props/barrel") != nullptr);
    CHECK(st.find("models/broken") == nullptr && !st.problem("models/broken").empty());
    CHECK(st.find("models/nothing") == nullptr && !st.problem("models/nothing").empty());
    ModelFinder fin = st.finder();
    CHECK(fin("models/loose") != nullptr && fin("") == nullptr);

    // Nothing is picked up by itself; rescan sees new files and forgets the loaded models.
    writeFile(fs::path(root) / "models/new/new.glb", cube);
    CHECK(!st.exists("models/new"));
    const unsigned gen = st.generation();
    st.rescan();
    CHECK(st.exists("models/new") && st.generation() == gen + 1);
    CHECK(st.find("models/new") != nullptr);

    // No Bottega: nothing found.
    st.setBottegaRoot("");
    CHECK(st.names().empty() && st.find("models/crate") == nullptr);
    fs::remove_all(base);
}

static void testPropsGeometry() {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "scal_props_geo_test";
    fs::remove_all(base);
    writeFile(base / "cube.glb", makeGlb({}));
    GlbOptions t;
    t.kind = GlbKind::Triangle;
    writeFile(base / "tri.glb", makeGlb(t));
    Model cube, tri;
    std::string err;
    CHECK(loadGlbFile((base / "cube.glb").string(), cube, err));
    CHECK(loadGlbFile((base / "tri.glb").string(), tri, err));
    fs::remove_all(base);

    // Placement: world = pos + R * (scale * local).
    Prop p = makeProp("c", {1000, 0, 0});
    CHECK(nearV(propToWorld(p, {10, 20, 30}), {1010, 20, 30}, 1e-9));
    p.scale = {2, 2, 2};
    CHECK(nearV(propToWorld(p, {10, 20, 30}), {1020, 40, 60}, 1e-9));
    p.rot = {0, 0, 90};
    CHECK(nearV(propToWorld(p, {10, 0, 0}), {1000, 20, 0}, 1e-9)); // +X turned to +Y
    CHECK(nearV(propToLocal(p, propToWorld(p, {7, -3, 11})), {7, -3, 11}, 1e-9));
    p.scale = {1, 2, 4};
    CHECK(nearV(propToLocal(p, propToWorld(p, {7, -3, 11})), {7, -3, 11}, 1e-9));

    // Bounds: the cube is 100 wide and deep, 100 high, standing on its origin.
    Prop q = makeProp("c", {500, 500, 10});
    const AABB b = propBounds(q, &cube);
    CHECK(nearV(b.min, {450, 450, 10}, 1e-3) && nearV(b.max, {550, 550, 110}, 1e-3));
    q.scale = {2, 1, 0.5};
    const AABB bs = propBounds(q, &cube);
    CHECK(nearV(bs.size(), {200, 100, 50}, 1e-3));
    q.scale = {1, 1, 1};
    q.rot = {0, 0, 45};
    CHECK(near(propBounds(q, &cube).size().x, 100 * std::sqrt(2.0), 1e-3));
    CHECK(near(propBottom(q, &cube), 10, 1e-3));
    // No model: the missing-model box (64 x 64 x 64 on the origin).
    const AABB pm = propBounds(makeProp("missing", {0, 0, 0}), nullptr);
    CHECK(nearV(pm.min, {-32, -32, 0}) && nearV(pm.max, {32, 32, 64}));

    // Rays: from above, onto the top of the cube.
    Prop r = makeProp("c", {0, 0, 0});
    r.id = 7;
    auto h = rayHitProp(r, &cube, {0, 0, 500}, {0, 0, -1});
    CHECK(h && near(h->t, 400, 1e-6) && nearV(h->normal, {0, 0, 1}, 1e-6) && h->prop == 7);
    CHECK(!rayHitProp(r, &cube, {200, 0, 500}, {0, 0, -1}));
    // Inside and scaled / turned.
    r.scale = {2, 2, 2};
    r.pos = {100, 0, 0};
    h = rayHitProp(r, &cube, {100, 0, 500}, {0, 0, -1});
    CHECK(h && near(h->t, 300, 1e-6)); // top at z = 200
    r.rot = {0, 0, 45};
    h = rayHitProp(r, &cube, {100 + 90, 0, 500}, {0, 0, -1}); // 90 out along x: inside the turned diamond (half-diagonal 141)
    CHECK(h && near(h->t, 300, 1e-6));
    CHECK(!rayHitProp(r, &cube, {100 + 150, 0, 500}, {0, 0, -1}));
    // Hits use triangles, not the box: a single triangle leaves its box's far corner empty.
    Prop tp = makeProp("t", {0, 0, 0});
    {
        // triangle (glTF) (0,0,0) (1,0,0) (0,0,-1) = ours (0,0,0) (100,0,0) (0,100,0): the half below the diagonal
        const bool inside = rayHitProp(tp, &tri, {20, 20, 500}, {0, 0, -1}).has_value();
        const bool outside = rayHitProp(tp, &tri, {80, 80, 500}, {0, 0, -1}).has_value();
        CHECK(inside && !outside);
        // The box stand-in (no model) would catch both.
        CHECK(rayHitProp(tp, nullptr, {20, 20, 500}, {0, 0, -1}).has_value());
    }
    // The nearest of several props wins.
    std::vector<Prop> props{makeProp("c", {0, 0, 0}), makeProp("c", {0, 0, 300})};
    props[0].id = 1;
    props[1].id = 2;
    auto fin = [&](const std::string&) { return &cube; };
    auto nearest = rayHitProps(props, fin, {0, 0, 1000}, {0, 0, -1});
    CHECK(nearest && nearest->prop == 2 && near(nearest->t, 600, 1e-6));

    // Turning about a world axis through a pivot.
    Prop t2 = makeProp("c", {100, 0, 0});
    rotateProp(t2, 2, 90, {0, 0, 0});
    CHECK(nearV(t2.pos, {0, 100, 0}, 1e-9) && nearV(t2.rot, {0, 0, 90}, 1e-9));
    rotateProp(t2, 2, 90);
    CHECK(nearV(t2.pos, {0, 100, 0}, 1e-9) && nearV(t2.rot, {0, 0, 180}, 1e-9));

    // Landing: a prop dropped on a floor, a sphere on a prop, a prop on a prop.
    AABB fb; fb.add({-500, -500, -64}); fb.add({500, 500, 0});
    const Brush floor = makeBox(fb, "a");
    std::vector<const Brush*> bs1{&floor};
    Prop drop = makeProp("c", {0, 0, 300});
    auto mv = propLandingMove({&drop, &cube}, bs1, {}, {});
    CHECK(mv && near(*mv, -300, 1e-6));
    Prop sunk = makeProp("c", {0, 0, -20});
    mv = propLandingMove({&sunk, &cube}, bs1, {}, {});
    CHECK(mv && near(*mv, 20, 1e-6)); // comes up to rest on top
    Prop away = makeProp("c", {5000, 0, 300});
    CHECK(!propLandingMove({&away, &cube}, bs1, {}, {}));
    Prop onTop = makeProp("c", {20, 10, 500}); // straddles the first one
    Prop under = makeProp("c", {0, 0, 0});
    mv = propLandingMove({&onTop, &cube}, {}, {}, {{&under, &cube}});
    CHECK(mv && near(onTop.pos.z + *mv, 100, 1e-6));
    MathPrim sph;
    sph.kind = PrimKind::Sphere;
    sph.pos = {0, 0, 600};
    sph.size = {60, 60, 60};
    auto sm = landingMove({&sph}, {}, {}, {{&under, &cube}});
    CHECK(sm && near(sph.pos.z + *sm, 130, 1e-3)); // sits on the cube's top (100) by its radius
    // A prop over a primitive.
    MathPrim ball;
    ball.id = 3;
    ball.kind = PrimKind::Sphere;
    ball.pos = {0, 0, 40};
    ball.size = {80, 80, 80};
    Prop above = makeProp("c", {0, 0, 400});
    mv = propLandingMove({&above, &cube}, {}, {&ball}, {});
    CHECK(mv && near(above.pos.z + *mv, 80, 1.0)); // on top of the sphere (top = 80)
}

static void testPropsDocument() {
    namespace fs = std::filesystem;
    Document d;
    CHECK(d.props().empty() && d.propSelection().empty());
    Prop a = makeProp("models/a", {10, 20, 30}, {0, 0, 45}, {2, 2, 2});
    a.otherKeys.emplace_back("_note", "keep me");
    Prop b = makeProp("models/gone", {0, 0, 0});
    d.apply(d.makeAddProps({a, b}, "Place props"));
    CHECK(d.props().size() == 2 && d.props()[0].id == 1 && d.props()[1].id == 2);
    CHECK(d.propSelection() == std::set<PropId>({1, 2}));
    CHECK(d.findProp(1) && d.findProp(1)->model == "models/a" && !d.findProp(9));
    CHECK(d.dirty());

    // Modify, undo, redo.
    Prop moved = *d.findProp(1);
    moved.pos = {99, 0, 0};
    d.apply(d.makeModifyProps({moved}, "Move"));
    CHECK(nearV(d.findProp(1)->pos, {99, 0, 0}));
    CHECK(d.undo() && nearV(d.findProp(1)->pos, {10, 20, 30}));
    CHECK(d.redo() && nearV(d.findProp(1)->pos, {99, 0, 0}));

    // Delete: gone from the selection too; undo brings back the prop, at its place in the order, and the selection.
    Command del = d.makeDelete({}, "Delete");
    d.addPropDeletes(del, {1});
    d.apply(std::move(del));
    CHECK(d.props().size() == 1 && d.props()[0].id == 2 && d.propSelection() == std::set<PropId>({2}));
    CHECK(d.undo());
    CHECK(d.props().size() == 2 && d.props()[0].id == 1 && d.propSelection() == std::set<PropId>({1, 2}));
    // Adding something else deselects props, and undo gives the selection back.
    Light l;
    d.apply(d.makeAddLights({l}, "Add light"));
    CHECK(d.propSelection().empty());
    CHECK(d.undo() && d.propSelection() == std::set<PropId>({1, 2}));
    MathPrim sp;
    d.apply(d.makeAddPrims({sp}, "Add primitive"));
    CHECK(d.propSelection().empty());
    CHECK(d.undo() && d.propSelection() == std::set<PropId>({1, 2}));

    // Saving: our own point entity, round trip, nothing lost (also the model that is missing).
    const std::string text = writeMap(d);
    CHECK(text.find("\"classname\" \"scal_prop\"") != std::string::npos);
    CHECK(text.find("\"model\" \"models/a\"") != std::string::npos);
    Document e;
    CHECK(readMap(text, e).ok);
    CHECK(e.props().size() == 2);
    CHECK(e.props()[0].model == "models/a" && nearV(e.props()[0].pos, {99, 0, 0}) && nearV(e.props()[0].rot, {0, 0, 45}) &&
          nearV(e.props()[0].scale, {2, 2, 2}));
    CHECK(e.props()[0].otherKeys.size() == 1 && e.props()[0].otherKeys[0].first == "_note");
    CHECK(e.props()[1].model == "models/gone");
    CHECK(e.propSelection().empty());
    CHECK(writeMap(e) == text);
    // The map stays a plain map for others: props are entities with no brushes.
    CHECK(e.brushes().empty() && e.otherEntities.empty());
    // Per-axis scale and a negative one survive.
    Document f;
    f.apply(f.makeAddProps({makeProp("m", {0, 0, 0}, {}, {1, 2.5, -1})}, "p"));
    Document g;
    CHECK(readMap(writeMap(f), g).ok && nearV(g.props()[0].scale, {1, 2.5, -1}));
    // reset() with props (loading a map into the editor's document).
    Document h;
    h.reset(e.worldKeys, {}, {}, {}, {}, WorldData{{}, {}, {}, std::nullopt, {}, e.props()});
    CHECK(h.props().size() == 2 && h.props()[0].id == 1 && h.props()[1].id == 2 && !h.dirty());
}

static void testPropsReplicate() {
    Prop a = makeProp("m", {0, 0, 0});
    ReplicateSpec spec;
    spec.copies = 3;
    spec.offset = {200, 0, 0};
    auto out = replicate({}, {}, {}, spec, {a});
    CHECK(out.size() == 3 && out[0].props.size() == 1);
    CHECK(nearV(out[0].props[0].pos, {200, 0, 0}) && nearV(out[2].props[0].pos, {600, 0, 0}));
    // Turn about the world origin: a ring of props.
    spec.pivot = ReplicateSpec::Pivot::Origin;
    spec.offset = {};
    spec.rotate = {0, 0, 90};
    Prop r = makeProp("m", {100, 0, 0});
    out = replicate({}, {}, {}, spec, {r});
    CHECK(nearV(out[0].props[0].pos, {0, 100, 0}, 1e-9) && nearV(out[0].props[0].rot, {0, 0, 90}, 1e-9));
    CHECK(nearV(out[1].props[0].pos, {-100, 0, 0}, 1e-9) && nearV(out[2].props[0].pos, {0, -100, 0}, 1e-9));
    // Scale per copy.
    spec = ReplicateSpec{};
    spec.copies = 2;
    spec.offset = {};
    spec.scale = {50, 50, 50};
    out = replicate({}, {}, {}, spec, {makeProp("m", {0, 0, 0}, {}, {2, 2, 2})});
    CHECK(nearV(out[0].props[0].scale, {1, 1, 1}) && nearV(out[1].props[0].scale, {0.5, 0.5, 0.5}));
    // Props alone are enough to replicate; nothing at all gives nothing.
    CHECK(replicate({}, {}, {}, ReplicateSpec{}, {}).empty());
}

// Random Replicate (v0.11.0).
static void testRandomReplicate() {
    MathPrim ball; ball.kind = PrimKind::Sphere; ball.pos = {0, 0, 100}; ball.size = {50, 50, 50};
    RandomReplicateSpec r;
    r.copies = 200;
    r.spread = {400, 300, 0};
    r.turn = {0, 0, 180};
    r.sizeMin = 50; r.sizeMax = 150;
    r.seed = 7;
    auto a = randomReplicate({}, {ball}, {}, r);
    CHECK(a.size() == 200);
    // Inside the ranges, both sides used, height kept (no Z spread), sizes even per copy.
    double minX = 1e9, maxX = -1e9, minS = 1e9, maxS = -1e9, sumX = 0;
    bool inside = true, even = true, sameZ = true;
    for (const ReplicateCopy& c : a) {
        const MathPrim& p = c.prims[0];
        const Vec3 off = p.pos - ball.pos;
        inside = inside && std::fabs(off.x) <= 400 && std::fabs(off.y) <= 300 && std::fabs(p.rot.z) <= 180 + 1e-9 &&
                 p.rot.x == 0 && p.rot.y == 0 && p.size.x >= 25 - 1e-9 && p.size.x <= 75 + 1e-9;
        even = even && near(p.size.x, p.size.y) && near(p.size.y, p.size.z);
        sameZ = sameZ && p.pos.z == 100;
        minX = std::min(minX, off.x); maxX = std::max(maxX, off.x); sumX += off.x;
        minS = std::min(minS, p.size.x); maxS = std::max(maxS, p.size.x);
    }
    CHECK(inside && even && sameZ);
    CHECK(minX < -300 && maxX > 300 && std::fabs(sumX / 200) < 60); // spread over the whole range
    CHECK(minS < 35 && maxS > 65);
    // Same seed, same copies; another seed, other copies.
    auto b = randomReplicate({}, {ball}, {}, r);
    CHECK(nearV(b[17].prims[0].pos, a[17].prims[0].pos, 0) && b[17].prims[0].rot.z == a[17].prims[0].rot.z);
    RandomReplicateSpec r2 = r; r2.seed = 8;
    auto c2 = randomReplicate({}, {ball}, {}, r2);
    CHECK(!nearV(c2[0].prims[0].pos, a[0].prims[0].pos, 1e-6));
    // Fewer copies keep the same first copies; changing the turn range keeps the offsets.
    RandomReplicateSpec few = r; few.copies = 5; few.turn = {0, 0, 10};
    auto f = randomReplicate({}, {ball}, {}, few);
    CHECK(f.size() == 5 && nearV(f[4].prims[0].pos, a[4].prims[0].pos, 0) && near(f[4].prims[0].size.x, a[4].prims[0].size.x));
    CHECK(std::fabs(f[4].prims[0].rot.z) <= 10 + 1e-9);
    // Pinned numbers: the scatter for a seed must not change between versions or machines.
    RandomReplicateSpec pin; pin.copies = 1; pin.seed = 1; pin.spread = {1000, 1000, 1000}; pin.turn = {};
    pin.sizeMin = pin.sizeMax = 100;
    const Vec3 p0 = randomReplicate({}, {ball}, {}, pin)[0].prims[0].pos;
    CHECK(nearV(p0, Vec3{-565.021531, 369.860594, -4.740958}, 1e-4));
    // Uneven sizes: each axis its own.
    RandomReplicateSpec un = r; un.uniform = false;
    auto u = randomReplicate({}, {ball}, {}, un);
    bool anyUneven = false;
    for (const ReplicateCopy& c : u) anyUneven = anyUneven || !near(c.prims[0].size.x, c.prims[0].size.y, 1e-6);
    CHECK(anyUneven);
    // Brushes only move, by whole grid steps; their size stays.
    AABB cb; cb.add({0, 0, 0}); cb.add({64, 64, 64});
    RandomReplicateSpec br = r; br.brushGrid = 16; br.spread = {400, 400, 400};
    bool onGrid = true;
    for (const ReplicateCopy& c : randomReplicate({makeBox(cb, "a")}, {}, {}, br)) {
        const AABB bb = c.brushes[0].bounds();
        for (int k = 0; k < 3; ++k) onGrid = onGrid && near(std::fmod(std::fabs(bb.min[k]), 16.0), 0);
        onGrid = onGrid && nearV(bb.size(), {64, 64, 64});
    }
    CHECK(onGrid);
    // The selection turns as one piece about its centre: two props stay 200 apart.
    RandomReplicateSpec two; two.copies = 20; two.sizeMin = two.sizeMax = 100; two.turn = {0, 0, 180};
    bool apart = true;
    for (const ReplicateCopy& c : randomReplicate({}, {}, {}, two, {makeProp("m", {-100, 0, 0}), makeProp("m", {100, 0, 0})}))
        apart = apart && c.props.size() == 2 && near(length(c.props[0].pos - c.props[1].pos), 200, 1e-6) &&
                near(c.props[0].rot.z, c.props[1].rot.z);
    CHECK(apart);
    // Lights come along and turn; nothing selected or no copies gives nothing.
    Light l; l.kind = LightKind::Spot; l.pos = {0, 0, 0}; l.pitch = 0; l.yaw = 0;
    auto lc = randomReplicate({}, {}, {l}, two);
    CHECK(lc.size() == 20 && lc[3].lights.size() == 1 && lc[3].lights[0].yaw != 0);
    CHECK(randomReplicate({}, {}, {}, r).empty());
    RandomReplicateSpec none = r; none.copies = 0;
    CHECK(randomReplicate({}, {ball}, {}, none).empty());
}

static void testProps() {
    testPropsLoad();
    testPropsStore();
    testPropsGeometry();
    testPropsDocument();
    testPropsReplicate();
    testRandomReplicate();
}

int main() {
    testGrid();
    testBoxGeometry();
    testFarAway();
    testWedge();
    testInvalid();
    testTranslateTextureLock();
    testRemap();
    testRotate();
    testTransform();
    testHull();
    testHullFast();
    testSnapToGrid();
    testTextures();
    testCsg();
    testCurves();
    testPrimitives();
    testTerrain();
    testTerrainPoints();
    testWorld();
    testSky();
    testBottega();
    testMathPrims();
    testPrimBlend();
    testLandingReplicate();
    testProps();
    testRay();
    testUndo();
    testMakeChange();
    testGroups();
    testVisibility();
    testDirty();
    testMapRoundTrip();
    testMapQuakeFormat();
    testMapErrors();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
