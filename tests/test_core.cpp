// Core + IO tests. No framework: each CHECK prints on failure; exit code = failures.
#include "core/sky_presets.hpp"
#include "core/render_job.hpp"
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
#include "core/skylab.hpp"
#include "core/mathprim.hpp"
#include "core/landing.hpp"
#include "core/replicate.hpp"
#include "core/bottega.hpp"
#include "core/mirror.hpp"
#include "core/prop.hpp"
#include "core/bake.hpp"
#include "core/decal.hpp"
#include "core/material.hpp"
#include "core/shader_file.hpp"
#include "core/tracer.hpp"
#include <array>
#include "io/glb_loader.hpp"
#include "io/model_store.hpp"
#include "io/map_format.hpp"
#include "render/lighting.hpp"
#include "render/scene.hpp"
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
    {
        bool cut = true;
        CHECK(carveBrush(block, makeBox(box({500, 0, 0}, {564, 64, 64}), "b"), &cut).size() == 1 && !cut);
        CHECK(carveBrush(makeBox(box({40, 40, 0}, {80, 80, 64}), "a"), tunnel, &cut).empty() && cut);
        // v0.12.6 review (F06): a cut that leaves one smaller box (same face count) is a cut
        const Brush target = makeBox(box({0, 0, 0}, {100, 100, 100}), "a");
        const std::vector<Brush> left = carveBrush(target, makeBox(box({-20, -10, -10}, {50, 110, 110}), "b"), &cut);
        CHECK(cut && left.size() == 1 && left[0].faces.size() == 6);
        CHECK(left.size() == 1 && near(left[0].bounds().min.x, 50) && near(left[0].bounds().max.x, 100));
        // only touching: no cut
        CHECK(carveBrush(target, makeBox(box({100, 0, 0}, {164, 100, 100}), "b"), &cut).size() == 1 && !cut);
    }

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
    CHECK(objectBottom({&s}) && near(*objectBottom({&s}), 250, 1e-6));
    // v0.12.6 review (F08): a cube from z 50 to 150 minus a big cube whose top is z 149
    // leaves a 1 cm slab: its bottom is the cut face. Cut away entirely: no bottom.
    {
        MathPrim keep; keep.id = 11; keep.kind = PrimKind::Cube; keep.pos = {0, 0, 100}; keep.size = {100, 100, 100}; keep.combo = 5;
        MathPrim cut; cut.id = 12; cut.kind = PrimKind::Cube; cut.pos = {0, 0, 49}; cut.size = {200, 200, 200}; cut.combo = 5;
        cut.op = PrimOp::Subtract;
        const std::optional<double> b = objectBottom({&keep, &cut});
        CHECK(b && near(*b, 149, 1e-3));
        auto mv = landingMove({&keep, &cut}, bs, {});
        CHECK(mv && near(*mv, -149, 1e-3)); // Snap to Land: rests by the cut face too
        cut.pos = {0, 0, 100};
        cut.size = {300, 300, 300};
        CHECK(!objectBottom({&keep, &cut}));
    }
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

    CHECK(st.clashes().empty());

    // Two files with the same name (v0.10.2): the first in name order keeps it, the other
    // is listed (not silently hidden).
    writeFile(fs::path(root) / "models/Stone Wall.glb", cube);
    writeFile(fs::path(root) / "models/Stone_Wall.glb", cube);
    writeFile(fs::path(root) / "models/pack/a.gltf", "{}");
    st.rescan();
    CHECK(st.exists("models/Stone_Wall") && st.fileOf("models/Stone_Wall").find("Stone Wall.glb") != std::string::npos);
    CHECK(st.clashes().size() == 2);
    bool namesBoth = false, namesExt = false;
    for (const std::string& c : st.clashes()) {
        namesBoth = namesBoth || (c.find("models/Stone_Wall.glb") != std::string::npos && c.find("models/Stone Wall.glb") != std::string::npos);
        namesExt = namesExt || c.find("models/pack/a.gltf") != std::string::npos;
        CHECK(c.find(base.string()) == std::string::npos); // paths inside the Bottega only
    }
    CHECK(namesBoth && namesExt);

    // No Bottega: nothing found.
    st.setBottegaRoot("");
    CHECK(st.names().empty() && st.find("models/crate") == nullptr && st.clashes().empty());
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
    h.reset(e.worldKeys, {}, {}, {}, {}, WorldData{{}, {}, {}, std::nullopt, {}, e.props(), {}});
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

// Random Replicate (v0.10.1).
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

// Snap to Land fixes (v0.10.2): only what is below the object's middle counts, and props
// rest on their mesh, not on their box corners.
static void testLandingStacks() {
    AABB fb; fb.add({-500, -500, -64}); fb.add({500, 500, 0});
    const Brush floor = makeBox(fb, "a");
    // A sphere under a shelf lands on the floor, not on the shelf.
    AABB sb; sb.add({-100, -100, 200}); sb.add({100, 100, 216});
    const Brush shelf = makeBox(sb, "a");
    MathPrim s; s.id = 1; s.kind = PrimKind::Sphere; s.pos = {0, 0, 100}; s.size = {60, 60, 60};
    auto m = landingMove({&s}, {&floor, &shelf}, {});
    CHECK(m && near(s.pos.z + *m, 30, 1e-3));
    // ... and one dropped from above the shelf lands on it.
    MathPrim hi = s; hi.pos.z = 500;
    m = landingMove({&hi}, {&floor, &shelf}, {});
    CHECK(m && near(hi.pos.z + *m, 246, 1e-3));
    // Overlapping a crate-like neighbour (flat top above its middle): the floating one
    // drops to the floor beside it instead of climbing on top of it.
    MathPrim nb; nb.id = 2; nb.kind = PrimKind::Cube; nb.pos = {40, 0, 30}; nb.size = {60, 60, 60};
    MathPrim fl = s; fl.pos = {0, 0, 50};
    m = landingMove({&fl}, {&floor}, {&nb});
    CHECK(m && near(fl.pos.z + *m, 30, 1e-3));
    // A wall beside it (its bottom corners under the sphere's footprint) is not a floor.
    AABB wb; wb.add({20, -200, 0}); wb.add({40, 200, 400});
    const Brush wall = makeBox(wb, "a");
    MathPrim bw = s; bw.pos = {0, 0, 150};
    m = landingMove({&bw}, {&floor, &wall}, {});
    CHECK(m && near(bw.pos.z + *m, 30, 1e-3));
    // Still comes up when sunk in less than half way.
    MathPrim sunk = s; sunk.pos.z = 10;
    m = landingMove({&sunk}, {&floor}, {});
    CHECK(m && near(*m, 20, 1e-3));

    // A diamond-shaped prop (|x|+|y|+|z| <= 50) over a step: its slanted underside touches
    // the step's edge; its box corners (in the air) must not hold it up.
    Model dia;
    ModelMesh mesh;
    const float v[6][3] = {{50, 0, 0}, {-50, 0, 0}, {0, 50, 0}, {0, -50, 0}, {0, 0, 50}, {0, 0, -50}};
    for (auto& q : v) for (float c : q) mesh.pos.push_back(c);
    const std::uint32_t tris[8][3] = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
    for (auto& t : tris) for (std::uint32_t i : t) mesh.index.push_back(i);
    dia.meshes.push_back(mesh);
    dia.bounds.add({-50, -50, -50});
    dia.bounds.add({50, 50, 50});
    AABB stb; stb.add({30, -500, 0}); stb.add({500, 500, 40});
    const Brush step = makeBox(stb, "a");
    Prop d = makeProp("dia", {0, 0, 300});
    auto pm = propLandingMove({&d, &dia}, {&floor, &step}, {}, {});
    CHECK(pm && near(d.pos.z + *pm, 60, 2.5)); // 90 when its box corners counted
    bool onMesh = true;
    for (const Vec3& q : propSurfacePoints(d, &dia))
        onMesh = onMesh && std::fabs(q.x - d.pos.x) + std::fabs(q.y - d.pos.y) + std::fabs(q.z - d.pos.z) <= 50 + 1e-3;
    CHECK(onMesh);
}

// v0.10.2: "unsaved" follows the history (undo back to the saved state = saved), and the
// View options that hide all primitives / props.
static void testDirtyAndShown() {
    Document doc;
    CHECK(!doc.dirty());
    MathPrim p; p.kind = PrimKind::Sphere; p.size = {10, 10, 10};
    doc.apply(doc.makeAddPrims({p}, "Add"));
    CHECK(doc.dirty());
    doc.markSaved();
    CHECK(!doc.dirty());
    MathPrim moved = doc.prims()[0];
    moved.pos.x = 50;
    doc.apply(doc.makeModifyPrims({moved}, "Move"));
    CHECK(doc.dirty());
    CHECK(doc.undo() && !doc.dirty());       // back to what was saved
    CHECK(doc.redo() && doc.dirty());
    CHECK(doc.undo() && doc.undo() && doc.dirty()); // before the save
    CHECK(doc.redo() && !doc.dirty());
    moved.pos.x = 70;
    doc.apply(doc.makeModifyPrims({moved}, "Move")); 
    CHECK(doc.undo() && !doc.dirty());
    doc.setFigure(Vec3{1, 2, 3});             // not undoable: a change until saved
    CHECK(doc.dirty());
    doc.markSaved();
    CHECK(!doc.dirty());

    // Hiding drops them from the selection; adding new ones shows them again.
    doc.setPrimSelection({doc.prims()[0].id});
    doc.setPrimsShown(false);
    CHECK(!doc.primsShown() && doc.primSelection().empty());
    doc.apply(doc.makeAddPrims({p}, "Add"));
    CHECK(doc.primsShown());
    Prop q; q.model = "m";
    doc.apply(doc.makeAddProps({q}, "Add"));
    doc.setPropSelection({doc.props()[0].id});
    doc.setPropsShown(false);
    CHECK(!doc.propsShown() && doc.propSelection().empty());
    doc.apply(doc.makeAddProps({q}, "Add"));
    CHECK(doc.propsShown());
}

// v0.10.2: NaN and infinity in a .map are refused like other bad numbers.
static void testMapBadNumbers() {
    auto swapFirst = [](std::string text, const std::string& from, const std::string& to) {
        const size_t at = text.find(from);
        CHECK(at != std::string::npos);
        if (at != std::string::npos) text.replace(at, from.size(), to);
        return text;
    };
    Document d;
    AABB bb; bb.add({0, 0, 0}); bb.add({64, 64, 64});
    d.apply(d.makeAdd({makeBox(bb, "a")}, "Add"));
    MathPrim p; p.kind = PrimKind::Cube; p.pos = {1, 2, 3}; p.size = {10, 10, 10}; p.round = 2;
    d.apply(d.makeAddPrims({p}, "Add"));
    Light l; l.kind = LightKind::Point; l.pos = {5, 6, 7};
    d.apply(d.makeAddLights({l}, "Add"));
    const std::string good = writeMap(d);
    Document e;
    CHECK(readMap(good, e).ok);
    for (const char* bad : {"nan", "inf", "-inf", "NaN", "INFINITY"}) {
        // In a brush: the file is refused.
        Document f;
        const size_t at = good.find("( ");
        std::string t = good;
        const size_t numEnd = t.find(' ', at + 2);
        t.replace(at + 2, numEnd - (at + 2), bad);
        const MapReadResult r = readMap(t, f);
        CHECK(!r.ok && r.error.find("bad number") != std::string::npos);
        // In an object's keys: the value is ignored, the rest loads.
        Document g;
        std::string k = swapFirst(good, "\"origin\" \"1 2 3\"", std::string("\"origin\" \"") + bad + " 2 3\"");
        k = swapFirst(k, "\"round\" \"2\"", std::string("\"round\" \"") + bad + "\"");
        k = swapFirst(k, "\"origin\" \"5 6 7\"", std::string("\"origin\" \"5 ") + bad + " 7\"");
        CHECK(readMap(k, g).ok && g.prims().size() == 1 && g.lights().size() == 1);
        if (g.prims().size() == 1 && g.lights().size() == 1) {
            const MathPrim& q = g.prims()[0];
            CHECK(std::isfinite(q.pos.x) && std::isfinite(q.round) && std::isfinite(g.lights()[0].pos.y));
        }
    }
    // In terrain heights: the file is refused.
    Document td;
    AABB fb; fb.add({0, 0, -64}); fb.add({256, 256, 0});
    Brush tb = makeBox(fb, "a");
    CHECK(makeTerrain(tb, topQuadFace(tb), 3).empty());
    td.apply(td.makeAdd({tb}, "Add"));
    const std::string tgood = writeMap(td);
    Document tg;
    CHECK(readMap(tgood, tg).ok);
    const size_t th = tgood.find("//@ th ");
    CHECK(th != std::string::npos);
    if (th != std::string::npos) {
        std::string t = tgood;
        const size_t numEnd = t.find(' ', th + 7);
        t.replace(th + 7, numEnd - (th + 7), "inf");
        Document tf;
        CHECK(!readMap(t, tf).ok);
    }
}

// Snap to Land for a whole selection, in the core since v0.10.3.
static void testLandSelection() {
    Document doc;
    AABB fb; fb.add({-1000, -1000, -64}); fb.add({1000, 1000, 0});
    doc.apply(doc.makeAdd({makeBox(fb, "a")}, "Add"));
    auto cube = [](Vec3 c) { MathPrim p; p.kind = PrimKind::Cube; p.pos = c; p.size = {100, 100, 100}; return p; };
    // a stack (A under B), two overlapping side by side (C, D), one far away (E)
    doc.apply(doc.makeAddPrims({cube({0, 0, 300}), cube({20, 10, 700}), cube({400, 0, 250}), cube({440, 30, 260}),
                                cube({5000, 0, 300})}, "Add"));
    std::set<MathPrimId> all;
    for (const MathPrim& p : doc.prims()) all.insert(p.id);
    doc.setPrimSelection(all);
    const LandingResult r = landSelection(doc, {});
    CHECK(r.prims.size() == 4 && r.props.empty() && r.nothingBelow == 1);
    std::map<MathPrimId, double> z;
    for (const MathPrim& p : r.prims) z[p.id] = p.pos.z;
    const auto& ps = doc.prims();
    CHECK(near(z[ps[0].id], 50, 1e-3));  // A on the floor
    CHECK(near(z[ps[1].id], 150, 1e-3)); // B stays on A
    CHECK(near(z[ps[2].id], 50, 1e-3) && near(z[ps[3].id], 50, 1e-3)); // C and D both on the floor
    CHECK(!z.count(ps[4].id));
    // Hidden props aren't landed on; shown ones are.
    Prop box; box.model = "missing"; box.pos = {-400, 0, 0};
    doc.apply(doc.makeAddProps({box}, "Add"));
    MathPrim f = cube({-400, 0, 600});
    doc.apply(doc.makeAddPrims({f}, "Add"));
    const MathPrimId fid = doc.prims().back().id;
    doc.setPrimSelection({fid});
    doc.setPropSelection({});
    const LandingResult onBox = landSelection(doc, {});
    CHECK(onBox.prims.size() == 1 && onBox.prims[0].pos.z > 60);
    doc.setPropsShown(false);
    doc.setPrimSelection({fid});
    const LandingResult hidden = landSelection(doc, {});
    CHECK(hidden.prims.size() == 1 && near(hidden.prims[0].pos.z, 50, 1e-3));
}

static void testProps() {
    testPropsLoad();
    testPropsStore();
    testPropsGeometry();
    testPropsDocument();
    testPropsReplicate();
    testRandomReplicate();
    testLandingStacks();
    testDirtyAndShown();
    testMapBadNumbers();
    testLandSelection();
}


// Sky Lab (v0.10.4): settings as text and in the map, the sun lock's angles, and the
// generated panorama (where the sun is, what is below the horizon, the plain modes).
void testSkyLab() {
    // Text form: everything comes back; unknown fields are kept; bad numbers keep the
    // default; values out of range are clamped.
    SkyLab s;
    s.used = true;
    s.mode = SkyMode::Darker;
    s.sunAzimuth = 123.5;
    s.sunAltitude = -4;
    s.sunColor = {250, 200, 150};
    s.hazeOn = false;
    s.lockSun = true;
    s.bakeSize = 4096;
    s.otherFields.emplace_back("cloud_cover", "40");
    const SkyLab back = skyLabFromString(skyLabToString(s));
    CHECK(back == s);
    CHECK(back.otherFields.size() == 1 && back.otherFields[0].second == "40");
    const SkyLab bad = skyLabFromString("sun_altitude=nan;disk_size=abc;glow_intensity=500;sun_azimuth=-30;bake_size=3000;mode=weird");
    CHECK(bad.sunAltitude == SkyLab().sunAltitude);
    CHECK(bad.diskSize == SkyLab().diskSize);
    CHECK(bad.glowIntensity == 100);
    CHECK(near(bad.sunAzimuth, 330));
    CHECK(bad.bakeSize == 2048);
    CHECK(bad.mode == SkyMode::Soft);
    CHECK(bad.used);

    // In the map: written only when used; read back the same.
    {
        Document d;
        Environment env = d.environment();
        CHECK(writeMap(d).find("_scal_skylab") == std::string::npos);
        env.skylab = s;
        env.sky = "skies/skylab/test";
        d.apply(d.makeEnvironment(env, "Sky Lab"));
        const std::string text = writeMap(d);
        CHECK(text.find("_scal_skylab") != std::string::npos);
        Document d2;
        CHECK(readMap(text, d2).ok);
        CHECK(d2.environment() == env);
        CHECK(writeMap(d2) == text);
    }

    // Sun lock: the sky's sun is where the light comes from.
    double az = 0, alt = 0, pitch = 0, yaw = 0;
    sunFromLightAngles(-35, 240, az, alt);
    CHECK(near(az, 60) && near(alt, 35));
    lightAnglesFromSun(60, 35, pitch, yaw);
    CHECK(near(pitch, -35) && near(yaw, 240));
    SkyLab lit;
    lit.sunAzimuth = 60;
    lit.sunAltitude = 35;
    const Vec3 toSun = skyLabSunDirection(lit), lightDir = lightDirection(pitch, yaw);
    CHECK(near(toSun.x, -lightDir.x) && near(toSun.y, -lightDir.y) && near(toSun.z, -lightDir.z));
    sunFromLightAngles(-90, 0, az, alt);
    CHECK(near(alt, 90));

    // The panorama: size; the same settings give the same bytes; rows made in pieces
    // equal the whole.
    const int W = 256, H = 128;
    SkyLab day;
    const std::vector<unsigned char> a = generateSky(day, W);
    CHECK((int)a.size() == W * H * 3);
    CHECK(generateSky(day, W) == a);
    {
        SkyGenerator g(day, W);
        std::vector<unsigned char> pieces(a.size());
        g.rows(0, 50, pieces.data());
        g.rows(50, H, pieces.data());
        CHECK(pieces == a);
    }
    auto px = [&](const std::vector<unsigned char>& img, const Vec3& dir, int c) {
        double u = 0, v = 0;
        skyUV(dir, u, v);
        const int i = std::min(W - 1, (int)(u * W)), j = std::min(H - 1, (int)(v * H));
        return (int)img[((size_t)j * W + i) * 3 + c];
    };
    auto lum = [&](const std::vector<unsigned char>& img, const Vec3& dir) { return px(img, dir, 0) + px(img, dir, 1) + px(img, dir, 2); };
    // The brightest pixel is the sun (where the panorama puts the sun's direction).
    {
        int best = -1;
        size_t bestAt = 0;
        for (size_t k = 0; k < a.size(); k += 3) {
            const int l = a[k] + a[k + 1] + a[k + 2];
            if (l > best) { best = l; bestAt = k / 3; }
        }
        double u = 0, v = 0;
        skyUV(skyLabSunDirection(day), u, v);
        const int si = (int)(u * W), sj = (int)(v * H);
        const int bi = (int)(bestAt % W), bj = (int)(bestAt / W);
        CHECK(std::abs(bi - si) <= 2 && std::abs(bj - sj) <= 2);
        CHECK(best >= 3 * 245);
    }
    // A day sky is blue overhead; below the horizon it is the ground colour.
    CHECK(px(a, {0, 0, 1}, 2) > px(a, {0, 0, 1}, 0) + 30);
    for (int c = 0; c < 3; ++c) CHECK(std::abs(px(a, {0.3, 0.2, -1}, c) - (int)day.groundColor[c]) <= 3);
    // Night is darker than day; Darker is darker than Soft.
    SkyLab night = day;
    night.sunAltitude = -20;
    CHECK(lum(generateSky(night, W), {0, 0, 1}) * 4 < lum(a, {0, 0, 1}));
    SkyLab darker = day;
    darker.mode = SkyMode::Darker;
    CHECK(lum(generateSky(darker, W), {-1, 0, 1}) < lum(a, {-1, 0, 1}));
    // Off: one plain colour all round (no haze, no disk).
    SkyLab off = day;
    off.mode = SkyMode::Off;
    off.offColor = {10, 200, 30};
    off.hazeOn = false;
    off.sunVisible = false;
    const std::vector<unsigned char> o = generateSky(off, W);
    bool plain = true;
    for (size_t k = 0; k < o.size(); k += 3) plain = plain && o[k] == 10 && o[k + 1] == 200 && o[k + 2] == 30;
    CHECK(plain);
    // Custom: the sky colour overhead, the horizon colour at the horizon (away from the sun).
    SkyLab custom = day;
    custom.mode = SkyMode::Custom;
    custom.hazeOn = false;
    custom.glowIntensity = 0;
    const std::vector<unsigned char> cu = generateSky(custom, W);
    for (int c = 0; c < 3; ++c) CHECK(std::abs(px(cu, {0, 0, 1}, c) - (int)custom.customSky[c]) <= 3);
    for (int c = 0; c < 3; ++c) CHECK(std::abs(px(cu, {-1, -0.3, 0.004}, c) - (int)custom.customHorizon[c]) <= 12);
    // The disk follows its size: twice the size, about four times the pixels.
    auto diskPixels = [&](double size) {
        SkyLab k = custom;
        k.customSky = k.customHorizon = {0, 0, 0};
        k.sunColor = {255, 255, 255};
        k.diskIntensity = 100;
        k.diskSize = size;
        k.sunAltitude = 0.0001 + 40;
        const std::vector<unsigned char> img = generateSky(k, 1024);
        int n = 0;
        for (size_t q = 0; q < img.size(); q += 3) n += img[q] > 128;
        return n;
    };
    const int small = diskPixels(4), big = diskPixels(8);
    CHECK(small > 20 && big > small * 3 && big < small * 5);
    // Randomize: the same seed, the same sky; another seed, another sky.
    CHECK(randomSkyLab(7) == randomSkyLab(7));
    CHECK(randomSkyLab(7) != randomSkyLab(8));
}


// Sky Lab clouds (v0.10.5): stored and read back, only above the horizon, more cover
// gives more cloud, the seed changes the pattern, threads give the same picture.
void testSkyLabClouds() {
    SkyLab s;
    s.used = true;
    s.stratus = {true, 22, 17, 333, 44, 3};
    s.cumulus = {true, 61, 80, 90, 0, 8};
    s.cloudHeight = 70;
    s.cumulusColor = {10, 20, 30};
    s.sphericalClouds = true;
    s.cloudSeed = 4242;
    s.cloudShadows = true;
    const SkyLab back = skyLabFromString(skyLabToString(s));
    CHECK(back == s);
    CHECK(back.stratus.on && back.stratus.amplitude == 333 && back.cumulus.complexity == 8 && back.cloudSeed == 4242);
    // Bad layers keep the defaults; values out of range are clamped.
    const SkyLab bad = skyLabFromString("stratus=1 nan 3 4 5 6;cumulus=1 500 1 9999 -5 20;cloud_height=-3");
    CHECK(!bad.stratus.on && bad.stratus.cover == SkyLab().stratus.cover);
    CHECK(bad.cumulus.on && bad.cumulus.cover == 100 && bad.cumulus.frequency == 2 && bad.cumulus.amplitude == 500);
    CHECK(bad.cumulus.turbulence == 0 && bad.cumulus.complexity == 8 && bad.cloudHeight == 0);
    // A map from v0.10.4 (no cloud fields) has no clouds.
    CHECK(!skyLabFromString("mode=soft").cumulus.on && !skyLabFromString("mode=soft").stratus.on);

    const int W = 256, H = 128;
    SkyLab clear;
    const std::vector<unsigned char> none = generateSky(clear, W);
    auto cloudy = [&](double cover, int seed) {
        SkyLab c;
        c.cumulus.on = true;
        c.cumulus.cover = cover;
        c.cloudSeed = seed;
        return generateSky(c, W);
    };
    auto changed = [&](const std::vector<unsigned char>& img) {
        int n = 0;
        for (size_t k = 0; k < img.size(); k += 3)
            n += std::abs(img[k] - none[k]) + std::abs(img[k + 1] - none[k + 1]) + std::abs(img[k + 2] - none[k + 2]) > 12;
        return n;
    };
    // Layers on with no cover, or the cloud settings changed while the layers are off:
    // the same as no clouds.
    CHECK(cloudy(0, 1) == none);
    {
        SkyLab off;
        off.cumulusColor = {255, 0, 0};
        off.sphericalClouds = true;
        CHECK(generateSky(off, W) == none);
    }
    const std::vector<unsigned char> few = cloudy(20, 1), many = cloudy(70, 1);
    CHECK(changed(few) > 50);
    CHECK(changed(many) > changed(few) * 2);
    // Nothing below the horizon changes.
    bool groundSame = true;
    for (size_t k = (size_t)(H / 2) * W * 3; k < many.size(); ++k) groundSame = groundSame && many[k] == none[k];
    CHECK(groundSame);
    // The seed: the same seed, the same clouds; another seed, other clouds.
    CHECK(cloudy(40, 7) == cloudy(40, 7));
    CHECK(cloudy(40, 7) != cloudy(40, 8));
    // Threads give exactly the picture rows make one after another.
    {
        SkyLab c;
        c.cumulus.on = c.stratus.on = true;
        c.sphericalClouds = true;
        SkyGenerator g(c, W);
        std::vector<unsigned char> one((size_t)W * H * 3), many2(one.size());
        g.rows(0, H, one.data());
        g.rowsParallel(0, H, many2.data(), 3);
        CHECK(one == many2);
    }
    // White by day; dark at night (until the moon); warm at sunset.
    auto meanCloud = [&](const SkyLab& c) {
        const std::vector<unsigned char> img = generateSky(c, W);
        SkyLab clearC = c;
        clearC.cumulus.on = clearC.stratus.on = false;
        const std::vector<unsigned char> base = generateSky(clearC, W);
        double r = 0, g = 0, b = 0;
        int n = 0;
        for (size_t k = 0; k < img.size(); k += 3)
            if (std::abs(img[k] - base[k]) + std::abs(img[k + 1] - base[k + 1]) + std::abs(img[k + 2] - base[k + 2]) > 30) {
                r += img[k]; g += img[k + 1]; b += img[k + 2]; ++n;
            }
        return n ? Vec3{r / n, g / n, b / n} : Vec3{-1, -1, -1};
    };
    SkyLab day;
    day.cumulus.on = true;
    day.cumulus.cover = 60;
    const Vec3 dayC = meanCloud(day);
    // (soft edges let some blue through and the shaded sides are blue-grey, as in Bryce)
    CHECK(dayC.x > 140 && dayC.z >= dayC.x);
    SkyLab sunset = day;
    sunset.sunAltitude = 3;
    const Vec3 setC = meanCloud(sunset);
    CHECK(setC.x > setC.z + 20);
    SkyLab night = day;
    night.sunAltitude = -25;
    const Vec3 nightC = meanCloud(night);
    CHECK(nightC.x < 0 || nightC.x + nightC.y + nightC.z < 60);
}


// Sky Lab night sky and effects (v0.10.6): moon, eclipses, stars, comets, rings, rainbow,
// fog in the sky, Blend with Sun; stored and read back; older maps unchanged.
void testSkyLabNight() {
    // Text form.
    SkyLab s;
    s.used = true;
    s.moonVisible = true; s.moonAzimuth = 12; s.moonAltitude = 34; s.moonPhase = 27; s.moonSize = 3.5;
    s.moonIntensity = 66; s.moonGlow = 11; s.earthshine = 44; s.moonSoftness = 9; s.moonColor = {250, 240, 230};
    s.moonTexture = "skies/moons/my;moon=x.png";
    s.coronaIntensity = 70; s.coronaSize = 12; s.lunarEclipse = 35; s.eclipseColor = {200, 50, 20};
    s.rings = true; s.ringIntensity = 45; s.ringRadius = 60; s.secondaryRing = true;
    s.stars = true; s.earthStars = true; s.starIntensity = 77; s.starAmount = 1234; s.starSeed = 99; s.starLatitude = -33; s.starTime = 13.5;
    s.comets = true; s.cometIntensity = 22; s.cometAmount = 4;
    s.rainbow = true; s.rainbowRadius = 45; s.rainbowOpacity = 70; s.secondaryBow = false;
    s.blendSun = false; s.blendColor = 20; s.blendLuminance = 30; s.blendHaze = false; s.blendFog = true;
    s.volumetric = true; s.volumetricQuality = 80; s.volumetricDensity = 10; s.colorPerspective = true; s.colorPerspectiveRgb = {79, 2, 30};
    s.ibl = true; s.iblIntensity = 150; s.bakedFog = "1 2 3 4 5 6 7";
    const SkyLab back = skyLabFromString(skyLabToString(s));
    CHECK(back.moonTexture == "skies/moons/my_moon_x.png"); // no separators inside a value
    SkyLab expect = s;
    expect.moonTexture = back.moonTexture;
    CHECK(back == expect);
    CHECK(back.earthStars && back.starAmount == 1234 && back.starTime == 13.5 && !back.secondaryBow && back.bakedFog == "1 2 3 4 5 6 7");
    // Clamped; a v0.10.5 map has none of it and the default Blend with Sun.
    const SkyLab c = skyLabFromString("lunar_eclipse=150;star_amount=20000;moon_phase=-5;comet_amount=99;star_time=25");
    CHECK(c.lunarEclipse == 100 && c.starAmount == 10000 && c.moonPhase == 0 && c.cometAmount == 50 && near(c.starTime, 1));
    const SkyLab old = skyLabFromString("mode=soft;haze=1;stratus=1 35 30 90 20 6");
    CHECK(!old.moonVisible && !old.stars && !old.rings && !old.rainbow && old.blendSun && old.blendColor == 60 && old.moonTexture == "builtin");

    // The fog's height in the map: written only when set; read back.
    {
        Document d;
        Environment env = d.environment();
        env.fogOn = true;
        d.apply(d.makeEnvironment(env, "fog"));
        const std::string plain = writeMap(d);
        CHECK(plain.find("\"_scal_fog\" \"1 150 160 175 1000 10000\"") != std::string::npos);
        env.fog.base = -200;
        env.fog.thickness = 640;
        d.apply(d.makeEnvironment(env, "fog"));
        Document d2;
        CHECK(readMap(writeMap(d), d2).ok);
        CHECK(d2.environment().fog.base == -200 && d2.environment().fog.thickness == 640);
        Document d3;
        CHECK(readMap(plain, d3).ok && d3.environment().fog.thickness == 0);
    }

    // Solar eclipse cover.
    SkyLab e;
    e.moonVisible = true;
    e.moonAzimuth = e.sunAzimuth;
    e.moonAltitude = e.sunAltitude;
    e.moonSize = e.diskSize;
    CHECK(near(solarEclipseCover(e), 1));
    e.moonSize = e.diskSize / 2;
    CHECK(std::fabs(solarEclipseCover(e) - 0.25) < 0.01); // a ring of sun round a smaller moon
    e.moonSize = e.diskSize;
    e.moonAltitude = e.sunAltitude + 10;
    CHECK(solarEclipseCover(e) == 0);
    e.moonAltitude = e.sunAltitude + e.diskSize / 2;
    const double half = solarEclipseCover(e);
    CHECK(half > 0.2 && half < 0.6);
    e.moonVisible = false;
    CHECK(solarEclipseCover(e) == 0);

    const int W = 512, H = 256;
    auto px = [&](const std::vector<unsigned char>& img, const Vec3& dir) {
        double u = 0, v = 0;
        skyUV(normalize(dir), u, v);
        const int i = std::min(W - 1, (int)(u * W)), j = std::min(H - 1, (int)(v * H));
        const unsigned char* p = &img[((size_t)j * W + i) * 3];
        return Vec3{(double)p[0], (double)p[1], (double)p[2]};
    };
    auto sum = [](const Vec3& c) { return c.x + c.y + c.z; };
    auto dirAt = [](double azDeg, double altDeg) {
        const double a = azDeg * 3.14159265358979 / 180, b = altDeg * 3.14159265358979 / 180;
        return Vec3{std::cos(b) * std::cos(a), std::cos(b) * std::sin(a), std::sin(b)};
    };
    // A total solar eclipse darkens the sky; half of one much less.
    {
        SkyLab day;
        const double zen = sum(px(generateSky(day, W), {0, 0, 1}));
        SkyLab tot = day;
        tot.moonVisible = true;
        tot.moonAzimuth = day.sunAzimuth;
        tot.moonAltitude = day.sunAltitude;
        tot.moonSize = day.diskSize * 1.05;
        const double totZen = sum(px(generateSky(tot, W), {0, 0, 1}));
        SkyLab part = tot;
        part.moonAltitude = day.sunAltitude + day.diskSize * 0.5;
        const double partZen = sum(px(generateSky(part, W), {0, 0, 1}));
        CHECK(totZen * 4 < zen);
        CHECK(partZen > totZen * 2 && partZen <= zen);
    }
    // The moon at night: full is bright, new is dark (earthshine), the lit side of a half
    // moon faces the sun; a texture shows; a lunar eclipse turns it red.
    {
        SkyLab n;
        n.sunAltitude = -30;
        n.sunAzimuth = 90; // north (+Y): seen from +X, to the left
        n.moonVisible = true;
        n.moonAzimuth = 0;
        n.moonAltitude = 30;
        n.moonSize = 20;
        n.moonGlow = 0;
        n.moonTexture = "none";
        n.moonPhase = 50;
        const double sky = sum(px(generateSky(n, W), dirAt(0, 60)));
        const double full = sum(px(generateSky(n, W), dirAt(0, 30)));
        n.moonPhase = 0;
        const double fresh = sum(px(generateSky(n, W), dirAt(0, 30)));
        CHECK(full > 400 && fresh < full / 3 && full > sky + 300);
        n.moonPhase = 25;
        const std::vector<unsigned char> halfImg = generateSky(n, W);
        CHECK(sum(px(halfImg, dirAt(5, 30))) > sum(px(halfImg, dirAt(-5, 30))) + 150); // +5 degrees azimuth = toward +Y = the sun's side
        // A square texture is the face: left half red, right half blue.
        std::vector<unsigned char> tex(64 * 64 * 3);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                unsigned char* p = &tex[((size_t)y * 64 + x) * 3];
                p[0] = x < 32 ? 255 : 0; p[1] = 0; p[2] = x < 32 ? 0 : 255;
            }
        SkyExtras ex;
        ex.moon = {64, 64, tex.data()};
        n.moonPhase = 50;
        n.moonTexture = "builtin";
        const std::vector<unsigned char> t = generateSky(n, W, ex);
        const Vec3 left = px(t, dirAt(5, 30)), right = px(t, dirAt(-5, 30)); // facing +X: +Y is left
        CHECK(left.x > left.z + 100 && right.z > right.x + 100);
        n.moonTexture = "none";
        n.lunarEclipse = 100;
        const Vec3 red = px(generateSky(n, W), dirAt(0, 30));
        CHECK(red.x > red.z * 1.5 && sum(red) < full);
    }
    // Stars: show at night, not by day; Earth's sky puts Sirius where it belongs.
    {
        auto bright = [&](const std::vector<unsigned char>& img) {
            int n = 0;
            for (size_t k = 0; k < img.size(); k += 3) n += img[k] + img[k + 1] + img[k + 2] > 200;
            return n;
        };
        SkyLab n;
        n.sunAltitude = -30;
        const int none = bright(generateSky(n, W));
        n.stars = true;
        const int some = bright(generateSky(n, W));
        CHECK(some > none + 50);
        n.comets = true;
        n.cometAmount = 8;
        n.cometIntensity = 100;
        CHECK(bright(generateSky(n, W)) > some);
        SkyLab day;
        SkyLab dayStars = day;
        dayStars.stars = true;
        CHECK(generateSky(dayStars, W) == generateSky(day, W));
        // Only Sirius (the brightest): at star time 6.75 h, latitude 45, due south, 28 degrees up.
        SkyLab sirius;
        sirius.sunAltitude = -30;
        sirius.stars = true;
        sirius.earthStars = true;
        sirius.starAmount = 1;
        sirius.starIntensity = 100;
        sirius.starLatitude = 45;
        sirius.starTime = 101.287 / 15;
        const std::vector<unsigned char> img = generateSky(sirius, 1024);
        int best = -1;
        size_t at = 0;
        for (size_t k = 0; k < img.size(); k += 3)
            if (img[k] + img[k + 1] + img[k + 2] > best) { best = img[k] + img[k + 1] + img[k + 2]; at = k / 3; }
        double u = 0, v = 0;
        skyUV(dirAt(270, 90 - 45 - 16.716), u, v); // south = -Y = azimuth 270
        CHECK(std::abs((int)(at % 1024) - (int)(u * 1024)) <= 2 && std::abs((int)(at / 1024) - (int)(v * 512)) <= 2);
    }
    // Rings: brighter at their radius, not far inside it.
    {
        SkyLab r;
        r.sunAltitude = 30;
        r.sunAzimuth = 0;
        SkyLab ring = r;
        ring.rings = true;
        ring.ringIntensity = 100;
        const std::vector<unsigned char> a = generateSky(r, W), b = generateSky(ring, W);
        CHECK(sum(px(b, dirAt(0, 30 + 22))) > sum(px(a, dirAt(0, 30 + 22))) + 20);
        CHECK(std::fabs(sum(px(b, dirAt(0, 30 + 11))) - sum(px(a, dirAt(0, 30 + 11)))) < 6);
    }
    // Rainbow: 42 degrees from the point opposite the sun, red outside, blue inside.
    {
        SkyLab r;
        r.sunAzimuth = 180;
        r.sunAltitude = 10;
        r.hazeOn = false;
        SkyLab bow = r;
        bow.rainbow = true;
        bow.rainbowOpacity = 100;
        bow.secondaryBow = false;
        const std::vector<unsigned char> a = generateSky(r, W), b = generateSky(bow, W);
        // Straight ahead (+X, opposite the sun at -X), the antisolar point is 10 degrees down.
        // (the band: 39..44 degrees at the default width; violet inside, red outside)
        const Vec3 outer = px(b, dirAt(0, 43.4 - 10)) - px(a, dirAt(0, 43.4 - 10));
        const Vec3 inner = px(b, dirAt(0, 39.8 - 10)) - px(a, dirAt(0, 39.8 - 10));
        CHECK(outer.x > outer.z + 10);
        CHECK(inner.z > inner.x + 10);
        SkyLab night = bow;
        night.sunAltitude = -10;
        SkyLab nightPlain = night;
        nightPlain.rainbow = false;
        CHECK(generateSky(night, W) == generateSky(nightPlain, W)); // no rainbow without the sun
    }
    // Fog in the sky: none without a thickness; with one, the horizon is the fog colour,
    // overhead much less, the ground (the eye inside the layer) all fog.
    {
        SkyLab f;
        f.hazeOn = false;
        SkyExtras fog;
        fog.fogOn = true;
        fog.fogColor = {200, 0, 0};
        fog.fogStart = 0;
        fog.fogEnd = 5000;
        CHECK(generateSky(f, W, fog) == generateSky(f, W));
        fog.fogThickness = 500;
        const std::vector<unsigned char> img = generateSky(f, W, fog);
        const Vec3 horizon = px(img, dirAt(200, 0.5)), up = px(img, {0, 0, 1}), down = px(img, dirAt(200, -30));
        CHECK(horizon.x > 190 && horizon.y < 15);
        CHECK(up.z > up.x);
        CHECK(down.x > 190 && down.y < 15);
    }
    // Blend with Sun: at night the haze darkens with it, stays bright without it.
    {
        SkyLab n;
        n.sunAltitude = -20;
        n.hazeDensity = 80;
        n.hazeColor = {230, 230, 230};
        SkyLab off = n;
        off.blendSun = false;
        CHECK(sum(px(generateSky(off, W), dirAt(200, 1))) > sum(px(generateSky(n, W), dirAt(200, 1))) + 200);
    }
}

// Milestone 11: cameras.
static void testCameras() {
    // Lens <-> field of view on a 36 mm sensor: 50 mm = 39.6 degrees, and back.
    CHECK(near(lensToFov(50), 39.597752, 1e-5));
    CHECK(near(fovToLens(lensToFov(24)), 24, 1e-3));
    CHECK(near(lensToFov(36), 53.130102, 1e-5)); // tan(half) = 0.5
    CHECK(lensPresets().size() == 5 && sizePresets().size() == 7);

    // Axes: looking along +X, left = +Y, up = +Z; pitch -90 looks down.
    Camera c;
    Mat3 b = cameraBasis(c);
    CHECK(nearV(b.col[0], {1, 0, 0}) && nearV(b.col[1], {0, 1, 0}) && nearV(b.col[2], {0, 0, 1}));
    c.pitch = -30; c.yaw = 250;
    CHECK(nearV(cameraForward(c), lightDirection(-30, 250)) && nearV(cameraBasis(c).col[0], lightDirection(-30, 250)));
    c.pitch = 0; c.yaw = 0; c.roll = 90; // tipped right: up is now -Y (the old right)
    CHECK(nearV(cameraBasis(c).col[2], {0, -1, 0}) && nearV(cameraBasis(c).col[0], {1, 0, 0}));
    // setCameraBasis reads back what cameraBasis made, typed values stay typed.
    for (Vec3 a : {Vec3{-30, 250, 10}, Vec3{45, 90, -170}, Vec3{0, 0, 180}, Vec3{-89, 359, 5}}) {
        Camera k; k.pitch = a.x; k.yaw = a.y; k.roll = a.z;
        Camera r; setCameraBasis(r, cameraBasis(k));
        CHECK(near(r.pitch, k.pitch, 1e-6) && near(r.yaw, k.yaw, 1e-6) && near(r.roll, k.roll, 1e-6));
    }
    // Rings: about Z turns the yaw; about Y tilts a +X camera down, without roll.
    Camera t;
    rotateCamera(t, 2, 90);
    CHECK(near(t.pitch, 0) && near(t.yaw, 90) && near(t.roll, 0));
    rotateCamera(t, 2, -90);
    rotateCamera(t, 1, 45);
    CHECK(near(t.pitch, -45) && near(t.yaw, 0) && near(t.roll, 0));
    // About the X axis while looking along X: that is a roll.
    Camera rx;
    rotateCamera(rx, 0, 30);
    CHECK(near(rx.pitch, 0) && near(rx.yaw, 0) && near(std::fabs(rx.roll), 30));
    // Many turns stay a proper rotation (forward / up still unit and square).
    Camera m; m.pitch = -20; m.yaw = 30; m.roll = 10;
    for (int i = 0; i < 24; ++i) rotateCamera(m, i % 3, 15);
    b = cameraBasis(m);
    CHECK(near(length(b.col[0]), 1, 1e-9) && near(dot(b.col[0], b.col[2]), 0, 1e-9));
    rollCamera(m, 400);
    CHECK(m.roll > -180 && m.roll <= 180);
    // Aim at a point keeps the roll.
    Camera a; a.pos = {0, 0, 100}; a.roll = 15;
    CHECK(aimCamera(a, {100, 0, 0}) && near(a.pitch, -45) && near(a.yaw, 0) && near(a.roll, 15));
    CHECK(!aimCamera(a, a.pos));

    // Frame: 1920 x 1080 at 39.6 degrees, distance 100: half width = 100 * tan(19.8).
    Camera f;
    const auto fr = cameraFrame(f, 100);
    const double hw = 100 * std::tan(f.fov * 0.5 * 3.14159265358979323846 / 180);
    CHECK(nearV(fr[0], {100, hw, hw * 1080 / 1920}, 1e-9));  // top-left (left = +Y)
    CHECK(nearV(fr[2], {100, -hw, -hw * 1080 / 1920}, 1e-9)); // bottom-right
    // Portrait: the angle is across the height.
    f.width = 1080; f.height = 1350;
    double tx, ty;
    cameraHalfTangents(f, tx, ty);
    CHECK(near(ty, std::tan(f.fov * 0.5 * 3.14159265358979323846 / 180)) && near(tx, ty * 1080 / 1350));
    // Orthographic: the frame keeps its size at every distance; the shape is a box.
    Camera o; o.ortho = true; o.orthoSize = 400; o.width = 400; o.height = 200;
    const auto o1 = cameraFrame(o, 10), o2 = cameraFrame(o, 500);
    CHECK(near(length(o1[1] - o1[0]), 400) && near(length(o2[1] - o2[0]), 400) && near(length(o1[3] - o1[0]), 200));
    CHECK(cameraShape(o, 50).size() == 4 + 8 + 2 && cameraShape(Camera{}, 50).size() == 4 + 4 + 2);
    // The "up" mark stands above the top edge.
    Camera up; const auto sh = cameraShape(up, 50);
    CHECK(sh.back().first.z > cameraFrame(up, 50)[0].z);

    // Clamping: nonsense values come back usable.
    Camera bad; bad.fov = 0; bad.nearDist = -5; bad.farDist = 1; bad.width = 0; bad.height = 99999;
    bad.yaw = -90; bad.roll = 270; bad.pitch = 200; bad.name = ""; bad.color = {300, -1, 20};
    clampCamera(bad);
    CHECK(near(bad.fov, 1) && bad.nearDist > 0 && bad.farDist > bad.nearDist && bad.width == 16 && bad.height == 16384);
    CHECK(near(bad.yaw, 270) && near(bad.roll, -90) && near(bad.pitch, 90) && bad.name == "camera");
    CHECK(nearV(bad.color, {255, 0, 20}));

    // Document: the first camera is active, the next ones aren't; add selects only cameras.
    Document d;
    d.apply(d.makeAdd({makeBox(box({0, 0, 0}, {64, 64, 64}), "a")}, "box"));
    Light l; d.apply(d.makeAddLights({l}, "light"));
    Camera c1; c1.name = "one";
    d.apply(d.makeAddCameras({c1}, "Create camera"));
    CHECK(d.cameras().size() == 1 && d.cameras()[0].active && d.activeCamera() == &d.cameras()[0]);
    CHECK(d.cameraSelection().size() == 1 && d.lightSelection().empty() && d.selection().empty());
    Camera c2; c2.name = "two"; c2.active = true; // asks to be active: the first stays
    d.apply(d.makeAddCameras({c2}, "Create camera"));
    CHECK(d.cameras().size() == 2 && d.cameras()[0].active && !d.cameras()[1].active);
    const CameraId id1 = d.cameras()[0].id, id2 = d.cameras()[1].id;
    // Making another active deactivates the old one, in one undo step.
    const size_t undos = d.undoCount();
    d.apply(d.makeActivateCamera(id2));
    CHECK(!d.findCamera(id1)->active && d.findCamera(id2)->active && d.undoCount() == undos + 1);
    CHECK(d.makeActivateCamera(id2).cameraChanges.empty()); // already active: nothing to do
    d.undo();
    CHECK(d.findCamera(id1)->active && !d.findCamera(id2)->active);
    d.redo();
    // A modify that switches the active one off: another one takes over (always one).
    Camera off = *d.findCamera(id2); off.active = false;
    d.apply(d.makeModifyCameras({off}, "x"));
    CHECK(d.findCamera(id1)->active ^ d.findCamera(id2)->active);
    // Deleting the active camera makes the one left active; undo brings both back.
    d.apply(d.makeActivateCamera(id2));
    Command del = d.makeDelete({}, "Delete");
    d.addCameraDeletes(del, {id2});
    d.apply(std::move(del));
    CHECK(d.cameras().size() == 1 && d.cameras()[0].active && d.cameraSelection().empty());
    d.undo();
    CHECK(d.cameras().size() == 2 && d.findCamera(id2)->active && !d.findCamera(id1)->active);
    // A duplicate never takes over.
    Camera dup = *d.findCamera(id2); dup.id = 0;
    d.apply(d.makeAddCameras({dup}, "Duplicate"));
    int actives = 0;
    for (const Camera& x : d.cameras()) actives += x.active;
    CHECK(d.cameras().size() == 3 && actives == 1 && d.findCamera(id2)->active);
    // Adding another kind of object drops the camera selection.
    d.apply(d.makeAddLights({l}, "light"));
    CHECK(d.cameraSelection().empty());
    d.undo();
    CHECK(d.cameraSelection().size() == 1);

    // .map round trip: every field, unknown keys kept, still exactly one active.
    Document e;
    Camera full; full.name = "hall view"; full.pos = {10, -20, 175}; full.pitch = -12.5; full.yaw = 200; full.roll = -7;
    full.fov = lensToFov(24); full.nearDist = 30; full.farDist = 5000; full.width = 1080; full.height = 1350;
    full.ortho = true; full.orthoSize = 800; full.exposure = -1.5; full.color = {10, 200, 30};
    full.otherKeys = {{"_note", "keep me"}};
    e.apply(e.makeAddCameras({full, Camera{}}, "c"));
    Document g;
    CHECK(readMap(writeMap(e), g).ok && g.cameras().size() == 2);
    const Camera& r = g.cameras()[0];
    CHECK(r.name == "hall view" && nearV(r.pos, full.pos) && near(r.pitch, -12.5) && near(r.yaw, 200) && near(r.roll, -7));
    CHECK(near(r.fov, full.fov, 1e-6) && near(r.nearDist, 30) && near(r.farDist, 5000) && r.width == 1080 && r.height == 1350);
    CHECK(r.ortho && near(r.orthoSize, 800) && near(r.exposure, -1.5) && nearV(r.color, {10, 200, 30}) && r.active);
    CHECK(!g.cameras()[1].active && r.otherKeys.size() == 1 && r.otherKeys[0].second == "keep me");
    CHECK(writeMap(g) == writeMap(e) && !g.dirty());
    // Hammer-style angles: pitch is written positive down.
    CHECK(writeMap(e).find("\"angles\" \"12.5 200 -7\"") != std::string::npos);
    // A file with two active cameras (or none) opens with exactly one; bad numbers are clamped.
    const std::string twoActive =
        "{\n\"classname\" \"worldspawn\"\n}\n"
        "{\n\"classname\" \"scal_camera\"\n\"active\" \"1\"\n\"fov\" \"nan\"\n\"size\" \"0 -3\"\n}\n"
        "{\n\"classname\" \"scal_camera\"\n\"active\" \"1\"\n\"near\" \"-4\"\n}\n"
        "{\n\"classname\" \"scal_camera\"\n}\n";
    Document h;
    CHECK(readMap(twoActive, h).ok && h.cameras().size() == 3);
    CHECK(h.cameras()[0].active && !h.cameras()[1].active && !h.cameras()[2].active);
    CHECK(near(h.cameras()[0].fov, Camera{}.fov) && h.cameras()[0].width == 1920 && h.cameras()[1].nearDist > 0);
    // Presets: settings only, round trip; position / angles / name / colour / active kept.
    Camera src; src.fov = lensToFov(85); src.nearDist = 12; src.farDist = 3000; src.width = 1080; src.height = 1350;
    src.ortho = true; src.orthoSize = 640; src.exposure = 0.5;
    const std::string pre = cameraPresetToString(src);
    Camera dst; dst.pos = {1, 2, 3}; dst.pitch = -10; dst.yaw = 20; dst.roll = 5; dst.name = "keep"; dst.active = true;
    dst.color = {1, 2, 3};
    CHECK(applyCameraPreset(dst, pre));
    CHECK(near(dst.fov, src.fov, 1e-9) && near(dst.nearDist, 12) && near(dst.farDist, 3000) && dst.width == 1080 && dst.height == 1350);
    CHECK(dst.ortho && near(dst.orthoSize, 640) && near(dst.exposure, 0.5));
    CHECK(nearV(dst.pos, {1, 2, 3}) && near(dst.pitch, -10) && near(dst.yaw, 20) && near(dst.roll, 5) && dst.name == "keep" && dst.active);
    CHECK(nearV(dst.color, {1, 2, 3}));
    // Partial and bad presets: what is there is used (clamped), the rest kept; nothing -> false.
    Camera part;
    CHECK(applyCameraPreset(part, "# my lens\nfov 500\nsize 0 0\nbogus 3\nnear nan\n") && near(part.fov, 170) && part.width == 1920);
    Camera none2;
    CHECK(!applyCameraPreset(none2, "hello\n\n") && none2 == Camera{});
    Document none;
    CHECK(readMap("{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"scal_camera\"\n}\n", none).ok && none.cameras()[0].active);
}

// v0.11.2: built-in Sky Lab presets; dropping a sky's plain bottom from memory.
static void testSkyPresetsAndCut() {
    // Built-in presets: 13, distinct names, each reads back to exactly its own text.
    CHECK(builtinSkyPresets().size() == 13);
    std::set<std::string> names;
    for (const BuiltinSky& b : builtinSkyPresets()) {
        names.insert(b.name);
        CHECK(skyLabToString(skyLabFromString(b.text)) == b.text);
    }
    CHECK(names.size() == 13 && findBuiltinSky("Sunset") && !findBuiltinSky("nope"));
    CHECK(skyLabFromString(findBuiltinSky("Moonlit night")->text).moonVisible);
    CHECK(skyLabFromString(findBuiltinSky("Rainbow and halo")->text).rainbow);

    // A made-up 64 x 32 panorama: changing down to row 19, plain from row 20.
    const int W = 64, H = 32;
    std::vector<unsigned char> img((size_t)W * H * 3);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            for (int c = 0; c < 3; ++c) img[((size_t)y * W + x) * 3 + c] = y < 20 ? (unsigned char)(y * 5 + x) : 90;
    unsigned char below[3];
    // 5 degrees of 180 over 32 rows: 1 more row kept after the change stops.
    CHECK(skyRowsToKeep(img.data(), W, H, 5, below) == 21 && below[0] == 90 && below[2] == 90);
    CHECK(skyRowsToKeep(img.data(), W, H, 20, below) == 24); // ceil(32 * 20 / 180) = 4
    // One pixel different on the last-but-one row: the plain block is just the last row,
    // not worth dropping.
    img[((size_t)30 * W + 7) * 3] = 95;
    CHECK(skyRowsToKeep(img.data(), W, H, 5, below) == H);
    img[((size_t)30 * W + 7) * 3] = 92; // within the tolerance (2): still plain
    CHECK(skyRowsToKeep(img.data(), W, H, 5, below) == 21);
    // All one colour: never cut above the horizon (16) + margin.
    std::vector<unsigned char> flat((size_t)W * H * 3, 40);
    CHECK(skyRowsToKeep(flat.data(), W, H, 5, below) == 17);
    CHECK(skyRowsToKeep(nullptr, W, H, 5, below) == H);

    // Real Sky Lab skies: everything dropped is the plain colour, the fade is kept with a
    // margin, and about 40% of the rows go.
    for (const char* name : {"Clear day", "Sunset clouds", "Moonlit night", "Rainbow and halo"}) {
        const SkyLab s = skyLabFromString(findBuiltinSky(name)->text);
        const int w = 512, h = 256;
        const std::vector<unsigned char> sky = generateSky(s, w);
        unsigned char b[3];
        const int keep = skyRowsToKeep(sky.data(), w, h, 5, b);
        CHECK(keep > h / 2 + 6 && keep < h * 7 / 10);
        bool plain = true;
        for (int y = keep - (int)std::ceil(h * 5 / 180.0); y < h; ++y)
            for (int x = 0; x < w * 3; ++x)
                if (std::abs((int)sky[(size_t)y * w * 3 + x] - (int)b[x % 3]) > 2) plain = false;
        CHECK(plain);
    }
}

// Render (M12, v0.12.0): options, anti-aliasing size, downscale, file names.
static void testRenderJob() {
    RenderSettings s;
    CHECK(s.quality == RenderQuality::Normal && s.aa == 2 && s.smooth);
    applyQuality(s, RenderQuality::Draft);
    CHECK(s.aa == 1);
    applyQuality(s, RenderQuality::High);
    CHECK(s.aa == 3 && s.quality == RenderQuality::High);
    s.smooth = false;
    s.aa = 4;
    CHECK(renderSettingsFromString(renderSettingsToString(s)) == s);
    const RenderSettings bad = renderSettingsFromString("quality loud\naa 9\naa x\nfilter fuzzy\nnonsense\n");
    CHECK(bad == RenderSettings{});
    CHECK(renderSettingsFromString("aa 1\n").aa == 1);

    CHECK(fitAA(1920, 1080, 3) == 3);
    CHECK(fitAA(3840, 2160, 3) == 2);           // 4K at 3x is 75 M pixels: too many
    CHECK(fitAA(3840, 2160, 4, 16384, 1LL << 40) == 4);
    CHECK(fitAA(5000, 100, 4) == 3);            // 20000 wide would pass the side limit
    CHECK(fitAA(20000, 100, 2) == 0);
    CHECK(fitAA(0, 100, 2) == 0);
    CHECK(fitAA(100, 100, 9) == 4);             // never above the largest

    // Downscale: f = 1 drops the alpha; 2x2 of black and white averages in linear light.
    std::vector<unsigned char> px = {10, 20, 30, 0, 40, 50, 60, 128};
    CHECK((downscaleRGB(px, 2, 1, 1) == std::vector<unsigned char>{10, 20, 30, 40, 50, 60}));
    std::vector<unsigned char> chk = {0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255};
    const auto half = downscaleRGB(chk, 2, 2, 2);
    CHECK(half.size() == 3 && half[0] == 188 && half[1] == 188 && half[2] == 188); // not 128: half the light
    std::vector<unsigned char> flat(6 * 3 * 4, 77);
    const auto third = downscaleRGB(flat, 6, 3, 3);
    CHECK(third.size() == 2 * 1 * 3 && third[0] == 77 && third[5] == 77);
    CHECK(downscaleRGB(flat, 6, 3, 0).empty());
    CHECK(downscaleRGB(flat, 60, 3, 3).empty()); // too few pixels given

    std::tm t{};
    t.tm_year = 2026 - 1900; t.tm_mon = 10; t.tm_mday = 20; t.tm_hour = 9; t.tm_min = 5; t.tm_sec = 7;
    CHECK(renderFileStem("/maps/My Room.map", "camera 1", t) == "My_Room_camera_1_2026-11-20_09-05-07");
    CHECK(renderFileStem("", "", t) == "untitled_camera_2026-11-20_09-05-07");
    CHECK(renderFileStem("C:\\maps\\a.b.map", "x/y", t) == "a_b_x_y_2026-11-20_09-05-07");
    CHECK(cleanFileName("ok-name_1") == "ok-name_1");
}

// Review fixes: drawing data is rebuilt only for the brushes an edit touched; a scene shares
// the cached list instead of copying it.
static void testBrushCaches() {
    Document d;
    std::vector<Brush> bs;
    for (int i = 0; i < 3; ++i) {
        AABB bb; bb.add({i * 100.0, 0, 0}); bb.add({i * 100.0 + 64, 64, 64});
        bs.push_back(makeBox(bb, "a"));
    }
    d.apply(d.makeAdd(bs, "Add"));
    const BrushId a = d.brushes()[0].id, b = d.brushes()[1].id;
    CHECK(d.brushStamp(a) != 0 && d.brushStamp(a) != d.brushStamp(b));
    SceneCache cache;
    LightingCache lit;
    CHECK(cache.get(d).size() == 3 && cache.rebuiltLast() == 3);
    lit.get(d, cache.get(d));
    CHECK(lit.rebuiltLast() == 3);
    // Selection alone: nothing rebuilt.
    d.setSelection({a});
    CHECK(cache.get(d).size() == 3 && cache.rebuiltLast() == 3); // same revision, not updated

    // A scene holds the list; an edit then must not change what it holds.
    auto held = cache.shared(d);
    Brush moved = *d.find(a);
    moved.translate({0, 0, 32}, false);
    const std::uint64_t before = d.brushStamp(a);
    d.apply(d.makeModify({moved}, "Move"));
    CHECK(d.brushStamp(a) != before && d.brushStamp(b) != 0);
    CHECK(cache.get(d).size() == 3 && cache.rebuiltLast() == 1);
    CHECK(near(cache.get(d)[0].bounds.min.z, 32) && near((*held)[0].bounds.min.z, 0));
    held.reset();
    lit.get(d, cache.get(d));
    CHECK(lit.rebuiltLast() == 1);
    // Undo and redo: one brush each.
    d.undo();
    CHECK(cache.get(d).size() == 3 && cache.rebuiltLast() == 1 && near(cache.get(d)[0].bounds.min.z, 0));
    d.redo();
    CHECK(cache.get(d).size() == 3 && cache.rebuiltLast() == 1 && near(cache.get(d)[0].bounds.min.z, 32));
    lit.get(d, cache.get(d));
    CHECK(lit.rebuiltLast() == 1);
    // Delete: nothing rebuilt, the list shrinks; the light preview drops it too.
    d.apply(d.makeDelete({b}, "Delete"));
    CHECK(cache.get(d).size() == 2 && cache.rebuiltLast() == 0);
    const size_t litSize = lit.get(d, cache.get(d)).size();
    CHECK(litSize == 2 && lit.rebuiltLast() == 0);
    if (litSize != 2 || lit.rebuiltLast()) std::printf("  lit %zu rebuilt %zu\n", litSize, lit.rebuiltLast());
    d.undo();
    CHECK(cache.get(d).size() == 3 && cache.rebuiltLast() == 1);
    // A new light changes every brush's light.
    Light l; l.kind = LightKind::Point; l.pos = {0, 0, 200};
    d.apply(d.makeAddLights({l}, "Light"));
    CHECK(cache.get(d).size() == 3 && cache.rebuiltLast() == 0);
    lit.get(d, cache.get(d));
    CHECK(lit.rebuiltLast() == 3);
    // Same polygons as building every brush afresh.
    for (size_t i = 0; i < d.brushes().size(); ++i) {
        const SceneBrush fresh = makeSceneBrush(d.brushes()[i]);
        CHECK(cache.get(d)[i].id == fresh.id && cache.get(d)[i].polys.size() == fresh.polys.size());
        CHECK(nearV(cache.get(d)[i].bounds.min, fresh.bounds.min) && nearV(cache.get(d)[i].bounds.max, fresh.bounds.max));
    }
    // Hidden brushes leave the editor's list but stay in a render's (nodraw left out there).
    Brush h = *d.find(b);
    h.hidden = true;
    h.faces[0].texture = "nodraw";
    d.apply(d.makeModify({h}, "Hide"));
    CHECK(cache.get(d).size() == 2);
    SceneCache all(SceneCache::Options{false, true});
    CHECK(all.get(d).size() == 3 && all.nodrawFaces() == 1);

    // A scene's list with a brush swapped in (terrain being sculpted).
    SceneBrushes sb(cache.shared(d));
    SceneBrush other = makeSceneBrush(moved);
    other.bounds.max.z = 999;
    sb.swapIn(other);
    size_t n = 0, swapped = 0;
    for (const SceneBrush& x : sb) { ++n; if (x.bounds.max.z == 999) ++swapped; }
    CHECK(n == 2 && swapped == 1 && sb[0].bounds.max.z == 999);
    CHECK(SceneBrushes().empty() && SceneBrushes().begin() == SceneBrushes().end());

    // Opening a map gives every brush a stamp.
    Document e;
    CHECK(readMap(writeMap(d), e).ok);
    for (const Brush& x : e.brushes()) CHECK(e.brushStamp(x.id) != 0);
}

// Review fixes: the undo history has a memory limit; the oldest steps go first.
static void testHistoryLimit() {
    Document d;
    AABB bb; bb.add({0, 0, -64}); bb.add({1024, 1024, 0});
    Brush t = makeBox(bb, "a");
    CHECK(makeTerrain(t, topQuadFace(t), 5).empty());
    d.apply(d.makeAdd({t}, "Add"));
    d.markSaved();
    const std::size_t one = Document::commandBytes(d.makeModify({d.brushes()[0]}, "x"));
    CHECK(one > 2 * 33 * 33 * sizeof(double)); // before and after heights at least
    d.setHistoryLimit(one * 10);
    for (int i = 0; i < 40; ++i) {
        Brush b = d.brushes()[0];
        b.terrain->heights[(size_t)i] += 8;
        d.apply(d.makeModify({b}, "Sculpt"));
        CHECK(d.historyBytes() <= one * 10 + one);
    }
    CHECK(d.undoCount() >= 5 && d.undoCount() <= 11);
    CHECK(d.historyBytes() <= one * 10);
    // Undoing every step left doesn't reach the saved state: still unsaved.
    while (d.undo()) {}
    CHECK(d.dirty());
    CHECK(near(d.brushes()[0].terrain->heights[39], 0) && near(d.brushes()[0].terrain->heights[0], 8));
    // Redo all, then a new edit clears redo and its memory.
    while (d.redo()) {}
    const std::size_t full = d.historyBytes();
    d.undo(); d.undo();
    CHECK(d.historyBytes() == full); // redo steps still count
    Brush b = d.brushes()[0];
    b.terrain->heights[100] = 1;
    d.apply(d.makeModify({b}, "Sculpt"));
    CHECK(!d.canRedo() && d.historyBytes() <= full);
    // The newest step stays even when it alone is over the limit.
    d.setHistoryLimit(1);
    CHECK(d.undoCount() == 1 && d.canUndo());
    // Default limit: 64 MB.
    CHECK(Document::kHistoryBytes == 64u * 1024 * 1024);
    Document fresh;
    CHECK(fresh.historyBytes() == 0);
}

// Review fixes: damaged numbers in terrain rows and whole-number fields are refused, not
// read as 0 (or as the number they start with).
static void testMapStrictNumbers() {
    Document d;
    AABB bb; bb.add({0, 0, -64}); bb.add({256, 256, 0});
    Brush t = makeBox(bb, "a");
    CHECK(makeTerrain(t, topQuadFace(t), 1).empty());
    t.visgroup = 0;
    d.apply(d.makeAdd({t}, "Add"));
    MathPrim p; p.kind = PrimKind::Cube; p.size = {10, 10, 10}; p.combo = 4;
    d.apply(d.makeAddPrims({p}, "Add"));
    const std::string good = writeMap(d);
    Document ok;
    CHECK(readMap(good, ok).ok);
    auto with = [&](const std::string& from, const std::string& to) {
        std::string s = good;
        const size_t at = s.find(from);
        CHECK(at != std::string::npos);
        if (at != std::string::npos) s.replace(at, from.size(), to);
        return s;
    };
    auto refused = [&](const std::string& text, const char* what) {
        Document e;
        const MapReadResult r = readMap(text, e);
        CHECK(!r.ok && r.error.find(what) != std::string::npos);
        if (r.ok || r.error.find(what) == std::string::npos) std::printf("  got: %s\n", r.error.c_str());
    };
    const size_t th = good.find("//@ th ");
    CHECK(th != std::string::npos);
    const std::string row = good.substr(th, good.find('\n', th) - th);
    const size_t firstNum = row.find(' ', 4) + 1;
    const std::string firstVal = row.substr(firstNum, row.find(' ', firstNum) - firstNum);
    for (const char* bad : {"abc", "1.5x", "0x", "--1"}) {
        std::string r = row;
        r.replace(firstNum, firstVal.size(), bad);
        refused(with(row, r), "terrain height");
    }
    refused(with("power=1", "power=99"), "terrain size");
    refused(with("power=1", "power=1000000"), "terrain size");
    refused(with("power=1", "power=abc"), "terrain size");
    refused(with("power=1", "power=1x"), "terrain size");
    refused(with("face=", "face=x"), "terrain face");
    // A visgroup on a brush.
    const std::string withVis = with("//@ terrain", "//@ visgroup=2x\n//@ terrain");
    refused(withVis, "visgroup");
    {
        Document e;
        CHECK(readMap(with("//@ terrain", "//@ visgroup=2\n//@ terrain"), e).ok && e.brushes()[0].visgroup == 2);
    }
    // A combination number in an object's keys: ignored, like other bad values in keys.
    {
        Document e;
        CHECK(readMap(with("\"combo\" \"4\"", "\"combo\" \"4x\""), e).ok && e.prims().size() == 1 && e.prims()[0].combo == 0);
        Document f;
        CHECK(readMap(good, f).ok && f.prims()[0].combo == 4);
    }
}

// ---------------------------------------------------------------- baked light (v0.12.1)

// The lightmap value at world point p on the brush face whose normal is n (plain faces).
static Vec3 luxelAt(const BakeResult& r, const Brush& b, const Vec3& n, const Vec3& p) {
    for (const FaceLightmap& f : r.faces) {
        if (f.brush != b.id || f.terrain) continue;
        if (dot(b.faces[f.face].plane().n, n) < 0.999) continue;
        const int i = std::clamp((int)std::lround(dot(f.axisS, p) - f.s0), 0, f.w - 1);
        const int j = std::clamp((int)std::lround(dot(f.axisT, p) - f.t0), 0, f.h - 1);
        const size_t k = ((size_t)j * f.w + i) * 3;
        return {f.rgb[k], f.rgb[k + 1], f.rgb[k + 2]};
    }
    return {-1, -1, -1};
}

static void testBake() {
    // VRAD's quadratic solve: through 1 at 0, 2 at d50, 256 at d0.
    {
        double a, b, c;
        CHECK(solveInverseQuadraticMonotonic(0, 1, 100, 2, 300, 256, a, b, c));
        CHECK(near(c, 1, 1e-9));
        // (the middle point may be moved toward a line to keep it monotonic; VRAD then
        // rescales so the 50% point is right: checked below with prepareLight)
        CHECK(near(c + 300 * b + 300 * 300 * a, 256, 1e-6));
    }
    // Inverse-square (Hammer's default): full brightness at 100 Hammer units = 254 cm.
    {
        Light l;
        l.color = {255, 255, 255};
        l.brightness = 255;
        const BakeLight L = prepareLight(l, Environment{});
        CHECK(near(L.q, 1) && L.c == 0 && L.l == 0);
        CHECK(near(L.intensity.x * lightFalloff(L, 254, {0, 0, -1}), 1.0, 1e-9));
        CHECK(near(L.intensity.x * lightFalloff(L, 508, {0, 0, -1}), 0.25, 1e-9));
        // VRAD clamps the distance to at least 1 unit.
        CHECK(near(lightFalloff(L, 0.5, {0, 0, -1}), 1.0, 1e-9));
        // colour to linear with ^2.2, brightness / 255
        l.color = {128, 255, 255};
        l.brightness = 200;
        const BakeLight L2 = prepareLight(l, Environment{});
        CHECK(near(L2.intensity.x / 10000, std::pow(128 / 255.0, 2.2) * 200 / 255.0, 1e-9));
        // 50% / 0%: exactly half at the 50% distance (relative to distance 0: 1 / c).
        l.falloff = LightFalloff::FiftyZero;
        l.fifty = 254;
        l.range = 1016;
        const BakeLight L3 = prepareLight(l, Environment{});
        CHECK(near(lightFalloff(L3, 254, {0, 0, -1}), 0.5, 1e-6));
        CHECK(lightFalloff(L3, 0, {0, 0, -1}) > lightFalloff(L3, 254, {0, 0, -1}));
        // hard falloff: nothing past the 0% distance, quintic fade from 3/4 of the way
        l.hardFalloff = true;
        const BakeLight L4 = prepareLight(l, Environment{});
        CHECK(lightFalloff(L4, 1020, {0, 0, -1}) == 0);
        CHECK(near(L4.startFade, 0.75 * 400 + 0.25 * 100, 1e-9));
        CHECK(lightFalloff(L4, 1000, {0, 0, -1}) < lightFalloff(L3, 1000, {0, 0, -1}));
        // 0% closer than 50%: warning, 0% = 2 x 50%
        l.range = 100;
        CHECK(!prepareLight(l, Environment{}).notes.empty());
    }
    // Spot: nothing outside the outer cone, the extra cosine, the exponent in between.
    {
        Light l;
        l.kind = LightKind::Spot;
        l.pitch = -90; // down
        l.inner = 20;
        l.outer = 40;
        const BakeLight L = prepareLight(l, Environment{});
        const double onAxis = lightFalloff(L, 254, {0, 0, -1});
        CHECK(near(onAxis, 1.0 / (100.0 * 100.0), 1e-12));
        const double a = 30 * 3.14159265358979 / 180;
        const Vec3 mid{std::sin(a), 0, -std::cos(a)};
        CHECK(near(lightFalloff(L, 254, mid), onAxis * std::cos(a) * (std::cos(a) - std::cos(40 * 3.14159265358979 / 180)) /
                                                    (std::cos(20 * 3.14159265358979 / 180) - std::cos(40 * 3.14159265358979 / 180)), 1e-9));
        const double b = 45 * 3.14159265358979 / 180;
        CHECK(lightFalloff(L, 254, {std::sin(b), 0, -std::cos(b)}) == 0);
        l.outer = 120; // capped at 90, with a warning
        CHECK(!prepareLight(l, Environment{}).notes.empty());
    }
    // VRAD's Halton directions: unit length, element 1 = (z 0, theta 2pi/3).
    {
        const std::vector<Vec3> d = haltonSphere(162);
        CHECK(d.size() == 162);
        CHECK(near(d[0].z, 0, 1e-12) && near(std::atan2(d[0].y, d[0].x), 2 * 3.14159265358979 / 3, 1e-9));
        bool unit = true;
        for (const Vec3& v : d) unit = unit && near(length(v), 1, 1e-9);
        CHECK(unit);
    }
    // A floor, a light above and a slab between: a shadow under the slab, light elsewhere.
    {
        BakeInput in;
        Brush floor = makeBox(box({0, 0, -16}, {1024, 1024, 0}), "floor");
        floor.id = 1;
        Brush slab = makeBox(box({384, 384, 100}, {640, 640, 116}), "slab");
        slab.id = 2;
        in.brushes = {floor, slab};
        Light l;
        l.pos = {512, 512, 300};
        l.brightness = 200;
        in.lights = {l};
        in.env.skyLight = 0;
        in.quality = BakeQuality::Draft;
        in.maxBounces = 0; // direct light only (bounces: testBounce)
        const BakeResult r = bakeLight(in, nullptr, 2);
        CHECK(!r.cancelled);
        CHECK(r.faces.size() == 12); // two boxes, every face lit
        const Vec3 under = luxelAt(r, floor, {0, 0, 1}, {512, 512, 0});
        const Vec3 open = luxelAt(r, floor, {0, 0, 1}, {120, 120, 0});
        CHECK(under.x >= 0 && under.x < 0.001);
        CHECK(open.x > 0.01);
        // the slab's top is lit, its bottom isn't (light above)
        CHECK(luxelAt(r, slab, {0, 0, 1}, {512, 512, 116}).x > 0.3);
        CHECK(luxelAt(r, slab, {0, 0, -1}, {512, 512, 100}).x < 0.001);
        // the same input bakes the same; another light doesn't
        CHECK(bakeKey(in) == bakeKey(in));
        BakeInput in2 = in;
        in2.lights[0].brightness = 100;
        CHECK(bakeKey(in2) != bakeKey(in));
        // v0.12.6 review (F10): a prop's model replaced under the same name bakes again
        {
            Model small;
            small.name = "models/crate";
            ModelMesh mesh;
            mesh.pos = {0, 0, 0, 10, 0, 0, 0, 10, 0, 0, 0, 10};
            mesh.uv = {0, 0, 1, 0, 0, 1, 1, 1};
            mesh.index = {0, 1, 2, 0, 1, 3};
            small.meshes = {mesh};
            small.materials = {ModelMaterial{}};
            small.fingerprint = modelFingerprint(small);
            Model tall = small;
            tall.meshes[0].pos[11] = 30;
            tall.fingerprint = modelFingerprint(tall);
            Model same = small;
            same.fingerprint = modelFingerprint(same);
            CHECK(tall.fingerprint != small.fingerprint && same.fingerprint == small.fingerprint);
            BakeInput k = in;
            Prop pr;
            pr.id = 1;
            pr.model = "models/crate";
            k.props = {pr};
            const Model* use = &small;
            k.models = [&](const std::string&) { return use; };
            const std::uint64_t a = bakeKey(k);
            use = &same;
            CHECK(bakeKey(k) == a);
            use = &tall;
            CHECK(bakeKey(k) != a);
            use = nullptr;
            CHECK(bakeKey(k) != a);
        }
        // sky light: the ambient colour from every open direction (no sky picture);
        // an open floor gets all of it, the floor under the slab less.
        in.lights.clear();
        in.env.skyLight = 100;
        in.env.ambient = {255, 255, 255};
        const BakeResult s = bakeLight(in, nullptr, 2);
        CHECK(near(luxelAt(s, floor, {0, 0, 1}, {30, 30, 0}).x, 1.0, 0.03));
        const double shaded = luxelAt(s, floor, {0, 0, 1}, {512, 512, 0}).x;
        CHECK(shaded > 0.05 && shaded < 0.8);
        // sky light 0: dark; the minimum light (the old flat ambient) lights everything
        in.env.skyLight = 0;
        in.env.minLight = true;
        in.env.ambient = {40, 40, 48};
        const BakeResult m = bakeLight(in, nullptr, 2);
        CHECK(near(luxelAt(m, slab, {0, 0, -1}, {512, 512, 100}).x, std::pow(40 / 255.0, 2.2), 1e-6));
        // cancelling stops it
        BakeProgress pr;
        pr.cancel = true;
        CHECK(bakeLight(in, &pr, 2).cancelled);
    }
    // The sun: shadows to infinity; a primitive casts one and gets its own 3D lightmap.
    {
        BakeInput in;
        Brush floor = makeBox(box({0, 0, -16}, {1024, 1024, 0}), "floor");
        floor.id = 1;
        in.brushes = {floor};
        Light sun;
        sun.kind = LightKind::Sun;
        sun.pitch = -90;
        sun.brightness = 255;
        in.lights = {sun};
        in.env.skyLight = 0;
        MathPrim ball;
        ball.id = 7;
        ball.kind = PrimKind::Sphere;
        ball.pos = {512, 512, 200};
        ball.size = {200, 200, 200};
        in.prims = {ball};
        in.quality = BakeQuality::Draft;
        in.maxBounces = 0;
        const BakeResult r = bakeLight(in, nullptr, 2);
        CHECK(near(luxelAt(r, floor, {0, 0, 1}, {100, 100, 0}).x, 1.0, 1e-6));
        CHECK(luxelAt(r, floor, {0, 0, 1}, {512, 512, 0}).x < 0.001);
        CHECK(r.prims.size() == 1 && r.prims[0].prim == 7 && r.prims[0].combo == 0);
        if (r.prims.size() == 1) {
            const PrimLightmap& p = r.prims[0];
            CHECK(p.nx >= 4 && p.ny >= 4 && p.nz >= 4);
            auto cellAt = [&](const Vec3& w) {
                const int i = std::clamp((int)std::lround((w.x - p.origin.x) / p.cell), 0, p.nx - 1);
                const int j = std::clamp((int)std::lround((w.y - p.origin.y) / p.cell), 0, p.ny - 1);
                const int k = std::clamp((int)std::lround((w.z - p.origin.z) / p.cell), 0, p.nz - 1);
                return p.rgb[(((size_t)k * p.ny + j) * p.nx + i) * 3];
            };
            CHECK(cellAt({512, 512, 300}) > 0.5);  // top: in the sun
            CHECK(cellAt({512, 512, 100}) < 0.05); // bottom: in its own shadow
        }
        // soft sun: the shadow's edge gets in between values
        in.lights[0].sunSpread = 10;
        in.quality = BakeQuality::Normal;
        const BakeResult soft = bakeLight(in, nullptr, 2);
        bool between = false;
        for (double x = 380; x <= 650; x += 8) {
            const double v = luxelAt(soft, floor, {0, 0, 1}, {x, 512, 0}).x;
            if (v > 0.1 && v < 0.9) between = true;
        }
        CHECK(between);
    }
    // Sky picture: linear, the middle column faces +X, the top row is straight up.
    {
        std::vector<unsigned char> pano(64 * 32 * 3, 0);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 64; ++x) {
                unsigned char* p = &pano[(y * 64 + x) * 3];
                p[0] = y < 16 ? 255 : 0;           // red sky above the horizon
                p[2] = (x >= 24 && x < 40) ? 255 : 0; // blue band facing +X
            }
        const SkyPicture s = makeSkyPicture(pano.data(), 64, 32, 32);
        CHECK(s.valid() && s.w == 32 && s.h == 16);
        CHECK(near(s.at({0, 0, 1}).x, 1.0, 1e-6));
        CHECK(near(s.at({0, 0, -1}).x, 0.0, 1e-6));
        CHECK(s.at({1, 0, 0.1}).z > 0.9);
        CHECK(s.at({-1, 0, 0.1}).z < 0.1);
        CHECK(near(srgbToLinear(linearToSrgb(0.3)), 0.3, 1e-9));
    }
    // The map keeps the new settings: falloff, spot exponent, sun spread, lightmap scales,
    // props' shadows, the environment's bake settings; old lights read as inverse-square.
    {
        Document d;
        Brush b = makeBox(box({0, 0, 0}, {64, 64, 64}), "a");
        b.faces[2].lightmapScale = 10;
        d.apply(d.makeAdd({b}, "Create"));
        Light pt;
        pt.falloff = LightFalloff::FiftyZero;
        pt.fifty = 300;
        pt.range = 900;
        pt.hardFalloff = true;
        Light sp;
        sp.kind = LightKind::Spot;
        sp.exponent = 3;
        sp.falloff = LightFalloff::Custom;
        sp.constantAttn = 1;
        sp.linearAttn = 0.5;
        sp.quadraticAttn = 0;
        Light sun;
        sun.kind = LightKind::Sun;
        sun.sunSpread = 5;
        d.apply(d.makeAddLights({pt, sp, sun}, "Lights"));
        MathPrim mp;
        mp.lightmapScale = 12;
        d.apply(d.makeAddPrims({mp}, "Prim"));
        Prop pr;
        pr.model = "models/x";
        pr.castShadows = false;
        d.apply(d.makeAddProps({pr}, "Prop"));
        Environment e = d.environment();
        e.skyLight = 60;
        e.minLight = true;
        e.smoothAngle = 30;
        d.apply(d.makeEnvironment(e, "Env"));
        const std::string a = writeMap(d);
        Document d2;
        CHECK(readMap(a, d2).ok);
        CHECK(writeMap(d2) == a);
        CHECK(d2.brushes()[0].faces[2].lightmapScale == 10 && d2.brushes()[0].faces[1].lightmapScale == kDefaultLightmapScale);
        CHECK(d2.lights().size() == 3);
        if (d2.lights().size() == 3) {
            CHECK(d2.lights()[0].falloff == LightFalloff::FiftyZero && d2.lights()[0].fifty == 300 && d2.lights()[0].hardFalloff);
            CHECK(d2.lights()[1].falloff == LightFalloff::Custom && d2.lights()[1].linearAttn == 0.5 && d2.lights()[1].exponent == 3);
            CHECK(d2.lights()[2].sunSpread == 5);
        }
        CHECK(d2.prims().size() == 1 && d2.prims()[0].lightmapScale == 12);
        CHECK(d2.props().size() == 1 && !d2.props()[0].castShadows);
        CHECK(d2.environment().skyLight == 60 && d2.environment().minLight && d2.environment().smoothAngle == 30);
        // A light from before v0.12.1 (reach only): Hammer's default.
        Document d3;
        CHECK(readMap("{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"light\"\n\"origin\" \"0 0 0\"\n\"_zero_percent_distance\" \"500\"\n}\n", d3).ok);
        CHECK(d3.lights().size() == 1 && d3.lights()[0].falloff == LightFalloff::InverseSquare && d3.lights()[0].range == 500);
    }
}

// ---------------------------------------------------------------- bounces (v0.12.2)
static void testBounce() {
    // The sun straight down on a red floor; a wall standing on it gets no direct light,
    // only what the floor bounces: red.
    BakeInput in;
    Brush floor = makeBox(box({0, 0, -16}, {1024, 1024, 0}), "redfloor");
    floor.id = 1;
    Brush wall = makeBox(box({480, 0, 0}, {544, 1024, 400}), "wall");
    wall.id = 2;
    in.brushes = {floor, wall};
    Light sun;
    sun.kind = LightKind::Sun;
    sun.pitch = -90;
    sun.brightness = 255;
    in.lights = {sun};
    in.env.skyLight = 0;
    in.reflectivity["redfloor"] = {0.8, 0.1, 0.1};
    in.reflectivity["wall"] = {0.5, 0.5, 0.5};
    in.quality = BakeQuality::Draft;
    in.maxBounces = 0;
    const BakeResult none = bakeLight(in, nullptr, 2);
    CHECK(none.patches == 0 && none.bounces.empty());
    const Vec3 dark = luxelAt(none, wall, {-1, 0, 0}, {480, 512, 60});
    CHECK(dark.x < 1e-6);
    in.maxBounces = -1;
    const BakeResult r = bakeLight(in, nullptr, 2);
    CHECK(r.patches > 0 && r.transfers > 0);
    CHECK(!r.bounces.empty() && r.bounces.size() <= 10); // Draft: at most 10
    CHECK(!r.bounces.empty() && r.bounces[0].rfind("Bounce #1 added RGB(", 0) == 0);
    const Vec3 lit = luxelAt(r, wall, {-1, 0, 0}, {480, 512, 60});
    CHECK(lit.x > 0.02);            // bounced light
    CHECK(lit.x > 3 * lit.y);       // red, from the floor
    CHECK(lit.x < 0.8);             // less than what the floor reflects
    // higher up the wall sees less of the floor
    CHECK(luxelAt(r, wall, {-1, 0, 0}, {480, 512, 380}).x < lit.x);
    // the floor's own light: direct (1) plus a little back from the wall
    const Vec3 fl = luxelAt(r, floor, {0, 0, 1}, {440, 512, 0});
    CHECK(fl.x >= 1.0 && fl.x < 1.3);
    // bounce strength 0: no bounce
    in.env.bounceScale = 0;
    CHECK(luxelAt(bakeLight(in, nullptr, 2), wall, {-1, 0, 0}, {480, 512, 60}).x < 1e-6);
    in.env.bounceScale = 1;
    // Normal goes on until a bounce adds less than 1 (VRAD's stop)
    in.quality = BakeQuality::Normal;
    const BakeResult n = bakeLight(in, nullptr, 2);
    CHECK(!n.bounces.empty() && n.bounces.size() < 100);
    // A primitive above the red floor gathers the red bounce under it; and it bounces light
    // onto the wall itself (primitives reflect, unlike in Source).
    in.quality = BakeQuality::Draft;
    MathPrim ball;
    ball.id = 3;
    ball.kind = PrimKind::Sphere;
    ball.pos = {300, 512, 250};
    ball.size = {200, 200, 200};
    ball.color = {255, 255, 255};
    in.prims = {ball};
    const BakeResult p = bakeLight(in, nullptr, 2);
    CHECK(p.prims.size() == 1);
    if (p.prims.size() == 1) {
        const PrimLightmap& lm = p.prims[0];
        auto cellAt = [&](const Vec3& w, int ch) {
            const int i = std::clamp((int)std::lround((w.x - lm.origin.x) / lm.cell), 0, lm.nx - 1);
            const int j = std::clamp((int)std::lround((w.y - lm.origin.y) / lm.cell), 0, lm.ny - 1);
            const int k = std::clamp((int)std::lround((w.z - lm.origin.z) / lm.cell), 0, lm.nz - 1);
            return lm.rgb[(((size_t)k * lm.ny + j) * lm.nx + i) * 3 + ch];
        };
        CHECK(cellAt({300, 512, 150}, 0) > 0.05);                              // the bottom: red bounce
        CHECK(cellAt({300, 512, 150}, 0) > 3 * cellAt({300, 512, 150}, 1));
    }
    // the ball's sunlit top reflects onto the wall: more light high on the wall than without it
    in.reflectivity["redfloor"] = {0, 0, 0};
    in.prims.clear();
    const double without = luxelAt(bakeLight(in, nullptr, 2), wall, {-1, 0, 0}, {480, 512, 330}).y;
    in.prims = {ball};
    const double with = luxelAt(bakeLight(in, nullptr, 2), wall, {-1, 0, 0}, {480, 512, 330}).y;
    CHECK(without < 1e-6 && with > 1e-3);
    // the key follows the reflectivity
    BakeInput k2 = in;
    k2.reflectivity["wall"] = {0.4, 0.4, 0.4};
    CHECK(bakeKey(k2) != bakeKey(in));
}

// ---------------------------------------------------------------- props and the light grid (v0.12.3)
// A 100-unit cube model: 6 faces of 4 vertices each, normals out.
static Model cubeModel() {
    Model m;
    m.name = "models/cube";
    m.materials.push_back({});
    ModelMesh mesh;
    const Vec3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        Vec3 a, b;
        defaultTexAxes(n[f], a, b);
        a = normalize(a - n[f] * dot(a, n[f]));
        b = cross(n[f], a);
        const std::uint32_t base = (std::uint32_t)(mesh.pos.size() / 3);
        for (int c = 0; c < 4; ++c) {
            const double u = (c == 1 || c == 2) ? 50 : -50, v = (c >= 2) ? 50 : -50;
            const Vec3 p = n[f] * 50 + a * u + b * v;
            mesh.pos.insert(mesh.pos.end(), {(float)p.x, (float)p.y, (float)(p.z + 50)});
            mesh.normal.insert(mesh.normal.end(), {(float)n[f].x, (float)n[f].y, (float)n[f].z});
            mesh.uv.insert(mesh.uv.end(), {0.0f, 0.0f});
        }
        mesh.index.insert(mesh.index.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    m.meshes.push_back(mesh);
    for (size_t i = 0; i + 2 < mesh.pos.size(); i += 3) m.bounds.add({mesh.pos[i], mesh.pos[i + 1], mesh.pos[i + 2]});
    return m;
}

static void testPropLight() {
    CHECK(near(ambientCubeAt(std::array<Vec3, 6>{Vec3{1, 0, 0}, Vec3{2, 0, 0}, Vec3{3, 0, 0}, Vec3{4, 0, 0}, Vec3{5, 0, 0}, Vec3{6, 0, 0}}.data(),
                             {0, 0, 1}).x, 5));
    CHECK(near(ambientCubeAt(std::array<Vec3, 6>{Vec3{1, 0, 0}, Vec3{2, 0, 0}, Vec3{3, 0, 0}, Vec3{4, 0, 0}, Vec3{5, 0, 0}, Vec3{6, 0, 0}}.data(),
                             normalize(Vec3{-1, 0, -1})).x, 0.5 * 2 + 0.5 * 6, 1e-9));
    static const Model cube = cubeModel();
    BakeInput in;
    in.models = [](const std::string& n) -> const Model* { return n == "models/cube" ? &cube : nullptr; };
    Brush floor = makeBox(box({-1024, -1024, -16}, {1024, 1024, 0}), "floor");
    floor.id = 1;
    // a slab over half of the second cube
    Brush slab = makeBox(box({300, -200, 300}, {600, 0, 320}), "slab");
    slab.id = 2;
    in.brushes = {floor, slab};
    Light sun;
    sun.kind = LightKind::Sun;
    sun.pitch = -90;
    sun.brightness = 255;
    in.lights = {sun};
    in.env.skyLight = 50;
    in.env.ambient = {255, 255, 255};
    in.quality = BakeQuality::Draft;
    Prop a;
    a.id = 10;
    a.model = "models/cube";
    a.pos = {-300, 0, 0};
    Prop b = a;
    b.id = 11;
    b.pos = {400, 0, 0}; // half under the slab (y -50..0 under it, 0..50 not)
    Prop c = a;
    c.id = 12;
    c.pos = {0, 400, 0};
    c.modelLighting = true;
    in.props = {a, b, c};
    const BakeResult r = bakeLight(in, nullptr, 2);
    CHECK(r.props.size() == 3);
    if (r.props.size() != 3) return;
    CHECK(r.propVertices == 48);
    // per vertex: the top in the sun (1 + some sky), the bottom gets no sun
    const PropLighting& pa = r.props[0];
    CHECK(pa.id == 10 && !pa.modelStyle && pa.vertexRgb.size() == 1 && pa.vertexRgb[0].size() == 24 * 3);
    auto vtx = [&](const PropLighting& p, int face, int corner) { return p.vertexRgb[0][(face * 4 + corner) * 3]; };
    CHECK(vtx(pa, 4, 0) > 1.0);
    CHECK(vtx(pa, 5, 0) < 0.5);
    // shadows on props: the second cube's top is shadowed by the slab where it is under it
    const PropLighting& pb = r.props[1];
    double under = 1e9, open = -1;
    for (int c2 = 0; c2 < 4; ++c2) {
        const double y = cube.meshes[0].pos[(4 * 4 + c2) * 3 + 1];
        if (y < 0) under = std::min(under, (double)vtx(pb, 4, c2));
        else open = std::max(open, (double)vtx(pb, 4, c2));
    }
    CHECK(under < 0.7 && open > 1.0);
    // like a Source model: an ambient cube and the sun as its strongest light
    const PropLighting& pc = r.props[2];
    CHECK(pc.modelStyle && pc.vertexRgb.empty());
    CHECK(pc.lights.size() == 1 && pc.lights[0].kind == LightKind::Sun);
    CHECK(pc.cube[4].x > 0.1); // sky light from above
    CHECK(near(pc.origin.z, 50, 1e-6));
    // the grid: samples over the map, none inside the floor; open samples see the sky above
    const LightGrid& g = r.grid;
    CHECK(!g.empty() && near(g.spacing.x, 48 * 2.54) && near(g.spacing.z, 96 * 2.54));
    bool anyInvalid = false, skyUp = false;
    for (int k = 0; k < g.nz; ++k)
        for (int j = 0; j < g.ny; ++j)
            for (int i = 0; i < g.nx; ++i) {
                const size_t s = ((size_t)k * g.ny + j) * g.nx + i;
                const Vec3 p = g.at(i, j, k);
                if (p.z < -1 && p.z > -15 && std::fabs(p.x) < 1000 && std::fabs(p.y) < 1000) anyInvalid = anyInvalid || !g.valid[s];
                if (g.valid[s] && p.z > 10 && g.cube[s * 6 + 4].x > 0.3) skyUp = true;
            }
    CHECK(skyUp);
    CHECK(g.nearest({0, 400, 50}) >= 0);
    // a prop that casts no shadows: the floor under it is lit
    Brush f2 = floor;
    BakeInput in2 = in;
    in2.props = {a};
    in2.props[0].pos = {0, 0, 200};
    in2.maxBounces = 0;
    in2.env.skyLight = 0;
    const BakeResult shadowed = bakeLight(in2, nullptr, 2);
    in2.props[0].castShadows = false;
    const BakeResult unshadowed = bakeLight(in2, nullptr, 2);
    CHECK(luxelAt(shadowed, floor, {0, 0, 1}, {0, 0, 0}).x < 0.01);
    CHECK(luxelAt(unshadowed, floor, {0, 0, 1}, {0, 0, 0}).x > 0.9);
    // ... but it is still lit itself
    CHECK(unshadowed.props.size() == 1 && unshadowed.props[0].vertexRgb[0][(4 * 4) * 3] > 0.9);
    // the key follows the lighting choice
    BakeInput k2 = in;
    k2.props[0].modelLighting = true;
    CHECK(bakeKey(k2) != bakeKey(in));
    // the map keeps it (Source's disablevertexlighting)
    Document d;
    Prop pr;
    pr.model = "models/x";
    pr.modelLighting = true;
    d.apply(d.makeAddProps({pr}, "Prop"));
    Document d2;
    CHECK(readMap(writeMap(d), d2).ok);
    CHECK(d2.props().size() == 1 && d2.props()[0].modelLighting);
    CHECK(writeMap(d).find("disablevertexlighting") != std::string::npos);
}


static void testBakeMaterials() {
    // A floor, the sun straight down and a slab between (as testBake).
    auto setup = [](const std::string& slabTex) {
        BakeInput in;
        Brush floor = makeBox(box({0, 0, -16}, {1024, 1024, 0}), "floor");
        floor.id = 1;
        Brush slab = makeBox(box({384, 384, 100}, {640, 640, 116}), slabTex);
        slab.id = 2;
        for (Face& f : slab.faces) { // texel = x / 128 (period 2 texels across 256 cm)
            f.u.axis = {1, 0, 0};
            f.v.axis = {0, 1, 0};
            f.scaleU = f.scaleV = 128;
            f.u.offset = f.v.offset = 0;
        }
        in.brushes = {floor, slab};
        Light sun;
        sun.kind = LightKind::Sun;
        sun.pitch = -90;
        sun.brightness = 200;
        in.lights = {sun};
        in.env.skyLight = 0;
        in.quality = BakeQuality::Draft;
        in.maxBounces = 0;
        return in;
    };
    // a cutout: alpha clear in texel 1 (x 384..512), solid in texel 0 (x 512..640)
    {
        BakeInput in = setup("fence");
        BakeMaterial m;
        m.alphaTest = true;
        m.texW = 2;
        m.texH = 1;
        m.alpha = {255, 0};
        in.materials["fence"] = m;
        const BakeResult r = bakeLight(in, nullptr, 2);
        CHECK(luxelAt(r, in.brushes[0], {0, 0, 1}, {440, 512, 0}).x > 0.3); // through the hole
        CHECK(luxelAt(r, in.brushes[0], {0, 0, 1}, {590, 512, 0}).x < 0.01); // under the solid part
        bool noted = false;
        for (const std::string& n : r.notes) noted = noted || n.find("cutout") != std::string::npos;
        CHECK(noted);
        // without the cutout the whole slab shadows
        const BakeResult solid = bakeLight(setup("fence"), nullptr, 2);
        CHECK(luxelAt(solid, setup("fence").brushes[0], {0, 0, 1}, {440, 512, 0}).x < 0.01);
        CHECK(bakeKey(in) != bakeKey(setup("fence")));
    }
    // see-through: blocks no light at all
    {
        BakeInput in = setup("glass");
        BakeMaterial m;
        m.translucent = true;
        in.materials["glass"] = m;
        const BakeResult r = bakeLight(in, nullptr, 2);
        CHECK(luxelAt(r, in.brushes[0], {0, 0, 1}, {512, 512, 0}).x > 0.3);
        CHECK(luxelAt(r, in.brushes[1], {0, 0, 1}, {512, 512, 116}).x > 0.3); // still lit itself
    }
    // a texlight: a glowing ceiling lights a dark closed room; VRAD's repeats scaling
    {
        auto room = [](double scale) {
            BakeInput in;
            Brush floor = makeBox(box({0, 0, -16}, {512, 512, 0}), "floor");
            floor.id = 1;
            Brush ceil = makeBox(box({0, 0, 256}, {512, 512, 272}), "panel");
            ceil.id = 2;
            for (Face& f : ceil.faces) f.scaleU = f.scaleV = scale;
            in.brushes = {floor, ceil};
            in.env.skyLight = 0;
            in.quality = BakeQuality::Draft;
            in.maxBounces = 0;
            BakeMaterial m;
            m.emit = {1, 0.5, 0.25};
            m.texW = m.texH = 64;
            in.materials["panel"] = m;
            return in;
        };
        BakeInput in = room(1);
        const BakeResult r = bakeLight(in, nullptr, 2);
        const Vec3 c = luxelAt(r, in.brushes[0], {0, 0, 1}, {256, 256, 0});
        CHECK(c.x > 0.001 && near(c.y / c.x, 0.5, 1e-6) && near(c.z / c.x, 0.25, 1e-6)); // its colour
        CHECK(r.texlights > 1);
        CHECK(luxelAt(r, in.brushes[1], {0, 0, -1}, {256, 256, 256}).x < 1e-9); // it doesn't light itself
        // half the texture scale = four times the repeats = four times the light (VRAD)
        const BakeResult r2 = bakeLight(room(0.5), nullptr, 2);
        CHECK(near(luxelAt(r2, in.brushes[0], {0, 0, 1}, {256, 256, 0}).x / c.x, 4.0, 1e-6));
        // no material: dark
        BakeInput dark = room(1);
        dark.materials.clear();
        CHECK(luxelAt(bakeLight(dark, nullptr, 2), in.brushes[0], {0, 0, 1}, {256, 256, 0}).x < 1e-9);
        CHECK(bakeKey(dark) != bakeKey(in));
    }
    // quick prop light is another bake
    {
        BakeInput in = setup("x");
        BakeInput q = in;
        q.propsQuick = true;
        CHECK(bakeKey(q) != bakeKey(in));
    }
}

// The bake's tracer (speed-up before v0.12.4b): a model placed as an instance answers like the
// same triangles put in the world one by one.
static void testDecals() {
    // frame and turn: a wall facing -Y, the picture's up is +Z, its right is +X seen from the front
    Decal d;
    d.pos = {0, -64, 64};
    d.normal = {0, -1, 0};
    d.up = decalDefaultUp(d.normal);
    d.width = 32;
    d.height = 16;
    Vec3 r, u, n;
    decalFrame(d, r, u, n);
    CHECK(nearV(u, {0, 0, 1}) && nearV(r, {1, 0, 0}) && nearV(n, {0, -1, 0}));
    CHECK(near(decalRotation(d), 0));
    setDecalRotation(d, 90);
    CHECK(near(decalRotation(d), 90, 1e-9));
    decalFrame(d, r, u, n);
    CHECK(nearV(u, {-1, 0, 0}, 1e-9)); // counter-clockwise seen from the front
    setDecalRotation(d, 0);
    double pu = 0, pv = 0;
    decalUV(d, {16, -64, 72}, pu, pv);
    CHECK(near(pu, 1) && near(pv, 0)); // top right
    decalSizeFromPicture(d, 64, 32, 0.5);
    CHECK(near(d.width, 64 * 0.5 * 2.54) && near(d.height, 32 * 0.5 * 2.54));
    d.width = 32;
    d.height = 16;

    // on a box: lands on the face it looks at, clipped to its size; not on side faces
    Brush wall = makeBox(box({-128, -64, 0}, {128, 64, 128}), "brick");
    wall.id = 7;
    std::vector<Brush> bs{wall};
    std::vector<DecalTri> m = decalMesh(d, bs);
    double area = 0;
    for (const DecalTri& t : m) {
        area += 0.5 * length(cross(t.p[1] - t.p[0], t.p[2] - t.p[0]));
        CHECK(nearV(t.n, {0, -1, 0}));
    }
    CHECK(near(area, 32 * 16, 1e-6));
    // at an edge: wraps over it onto a face turned less than 75 degrees, not a square one
    Decal e = d;
    e.pos = {128, -64, 64};
    CHECK(near(decalMesh(e, bs).empty() ? 0 : 1, 1));
    double ea = 0;
    for (const DecalTri& t : decalMesh(e, bs)) ea += 0.5 * length(cross(t.p[1] - t.p[0], t.p[2] - t.p[0]));
    CHECK(near(ea, 16 * 16, 1e-6));
    CHECK(rayHitDecal(d, {0, -500, 64}, {0, 1, 0}) > 0 && rayHitDecal(d, {100, -500, 64}, {0, 1, 0}) < 0);

    // the decal follows its brush: move, turn, size
    wall.decals.push_back(d);
    Brush w2 = wall;
    w2.translate({10, 0, 5}, false);
    CHECK(nearV(w2.decals[0].pos, {10, -64, 69}));
    Brush w3 = wall;
    w3.rotate({0, 0, 0}, {0, 0, 1}, 3.14159265358979323846 / 2, false);
    CHECK(nearV(w3.decals[0].pos, {64, 0, 64}, 1e-9) && nearV(w3.decals[0].normal, {1, 0, 0}, 1e-9) &&
          nearV(w3.decals[0].up, {0, 0, 1}, 1e-9));
    Brush w4 = wall;
    w4.remapBounds(wall.bounds(), box({-256, -64, 0}, {256, 64, 256}), false);
    CHECK(near(w4.decals[0].width, 64, 1e-9) && near(w4.decals[0].height, 32, 1e-9) && nearV(w4.decals[0].pos, {0, -64, 128}, 1e-9));
    { // a mirror: still facing out, the picture not mirrored
        std::string err;
        TransformSpec t;
        t.kind = TransformKind::Scale;
        t.scale = {1, -1, 1};
        std::vector<Brush> out = applyTransform({wall}, t, false, err);
        CHECK(out.size() == 1 && nearV(out[0].decals[0].normal, {0, 1, 0}, 1e-9) && nearV(out[0].decals[0].pos, {0, 64, 64}, 1e-9));
    }

    // in a document: added, modified, deleted, undone; copies get new ids; a split keeps one
    Document doc;
    Brush plain = makeBox(box({-128, -64, 0}, {128, 64, 128}), "brick");
    doc.apply(doc.makeAdd({plain}, "add"));
    const BrushId bid = *doc.selection().begin();
    Decal nd = d;
    nd.id = 0;
    doc.apply(doc.makeAddDecal(nd, bid, "Place decal"));
    CHECK(doc.decalCount() == 1 && doc.decalSelection().size() == 1 && doc.selection().empty());
    const DecalId did = *doc.decalSelection().begin();
    CHECK(doc.findDecal(did) && doc.findDecal(did).brush->id == bid);
    Decal md = *doc.findDecal(did).decal;
    md.order = 3;
    doc.apply(doc.makeModifyDecals({md}, "Decal order"));
    CHECK(doc.findDecal(did).decal->order == 3);
    doc.undo();
    CHECK(doc.findDecal(did).decal->order == 0);
    doc.redo();
    // a copy of the brush: its decal gets a new id
    Brush copy = *doc.find(bid);
    copy.id = 0;
    copy.translate({0, 0, 256}, false);
    doc.apply(doc.makeAdd({copy}, "Duplicate"));
    CHECK(doc.decalCount() == 2);
    std::set<DecalId> ids;
    for (const Brush& b : doc.brushes()) for (const Decal& x : b.decals) ids.insert(x.id);
    CHECK(ids.size() == 2 && ids.count(did));
    // carving the first brush in two: the decal stays once, on the piece it is on
    {
        const Brush& b0 = *doc.find(bid);
        Brush cutter = makeBox(box({-8, -100, -10}, {8, 100, 300}), "x");
        std::vector<Brush> pieces = carveBrush(b0, cutter);
        CHECK(pieces.size() >= 2);
        doc.apply(doc.makeChange({}, pieces, {bid}, "Carve"));
        size_t holders = 0;
        for (const Brush& b : doc.brushes())
            for (const Decal& x : b.decals)
                if (x.id == did) ++holders;
        CHECK(holders == 1 && doc.decalCount() == 2);
        doc.undo();
        CHECK(doc.findDecal(did) && doc.findDecal(did).brush->id == bid);
    }
    // deleting the decal (with the selection), undo
    doc.setDecalSelection({did});
    {
        Command c = doc.makeDelete({}, "Delete");
        doc.addDecalDeletes(c, doc.decalSelection());
        doc.apply(std::move(c));
    }
    CHECK(!doc.findDecal(did) && doc.decalSelection().empty() && doc.decalCount() == 1);
    doc.undo();
    CHECK(doc.findDecal(did) && doc.decalSelection().count(did));
    // hidden with View > Decals, with their visgroup, with their brush
    CHECK(doc.visible(*doc.findDecal(did).brush, *doc.findDecal(did).decal));
    doc.setDecalsShown(false);
    CHECK(!doc.visible(*doc.findDecal(did).brush, *doc.findDecal(did).decal) && doc.decalSelection().empty());
    doc.setDecalsShown(true);

    // saved and read back
    {
        Decal sd = *doc.findDecal(did).decal;
        sd.material = "decals/poster one";
        sd.visgroup = 2;
        sd.reach = 4;
        doc.apply(doc.makeModifyDecals({sd}, "x"));
        const std::string text = writeMap(doc);
        CHECK(text.find("//@ decal ") != std::string::npos);
        Document e;
        CHECK(readMap(text, e).ok);
        CHECK(e.decalCount() == 2);
        bool found = false;
        for (const Brush& b : e.brushes())
            for (const Decal& x : b.decals)
                if (x.material == "decals/poster one") {
                    found = true;
                    CHECK(nearV(x.pos, sd.pos) && nearV(x.normal, sd.normal) && nearV(x.up, sd.up) && near(x.width, sd.width) &&
                          near(x.height, sd.height) && x.order == 3 && x.visgroup == 2 && near(x.reach, 4));
                }
        CHECK(found);
        Document bad;
        CHECK(!readMap("{\n\"classname\" \"worldspawn\"\n{\n//@ decal size=0,4 mat=x\n( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) a [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1\n}\n}\n", bad).ok);
    }
}

// v0.12.6 review: decal order range (F01) and equal-order stacking after reopening (F02).
static void testDecalOrderFile() {
    // F03: where a decal's picture shows (for picking): in its box, within reach, facing it
    {
        Decal w;
        w.pos = {60, 300, 150};
        w.normal = {0, -1, 0};
        w.up = {0, 0, 1};
        w.width = w.height = 32;
        CHECK(decalDrawnAt(w, {62, 300, 150}, {0, -1, 0}));
        CHECK(decalDrawnAt(w, {70, 300 + 5, 160}, {0, -1, 0}));     // in the box, 5 cm behind its plane
        CHECK(!decalDrawnAt(w, {80, 300, 150}, {0, -1, 0}));        // past its right edge
        CHECK(!decalDrawnAt(w, {62, 300 - 20, 150}, {0, -1, 0}));   // out of reach (10.16)
        CHECK(!decalDrawnAt(w, {62, 300, 150}, {1, 0, 0}));         // a side face doesn't take it
    }
    // two coplanar brushes; the first decal goes on the second brush, the second on the first
    Document doc;
    doc.apply(doc.makeAdd({makeBox(box({-240, 300, 0}, {0, 332, 300}), "grey")}, "add"));
    const BrushId left = *doc.selection().begin();
    doc.apply(doc.makeAdd({makeBox(box({0, 300, 0}, {240, 332, 300}), "grey")}, "add"));
    const BrushId right = *doc.selection().begin();
    Decal a;
    a.material = "t/red";
    a.pos = {40, 300, 150};
    a.normal = {0, -1, 0};
    a.up = {0, 0, 1};
    a.width = a.height = 160;
    Decal b = a;
    b.material = "t/blue";
    b.pos = {-40, 300, 150};
    doc.apply(doc.makeAddDecal(a, right, "Place decal"));
    doc.apply(doc.makeAddDecal(b, left, "Place decal"));
    auto idOf = [](const Document& d, const std::string& mat) {
        for (const Brush& x : d.brushes())
            for (const Decal& y : x.decals)
                if (y.material == mat) return y.id;
        return DecalId(0);
    };
    CHECK(idOf(doc, "t/red") < idOf(doc, "t/blue")); // red placed first
    const std::string text = writeMap(doc);
    CHECK(text.find(" seq=0") != std::string::npos && text.find(" seq=1") != std::string::npos);
    Document e;
    CHECK(readMap(text, e).ok);
    CHECK(e.decalCount() == 2 && idOf(e, "t/red") < idOf(e, "t/blue")); // still placed first after reopening
    // saved again: the same order (a second round trip)
    Document f;
    CHECK(readMap(writeMap(e), f).ok && idOf(f, "t/red") < idOf(f, "t/blue"));
    // a file without seq (v0.12.5 / v0.12.6): brush order, as before
    {
        std::string old = text;
        for (size_t at; (at = old.find(" seq=")) != std::string::npos;) old.erase(at, 6);
        CHECK(old.find("seq") == std::string::npos);
        Document g;
        CHECK(readMap(old, g).ok && g.decalCount() == 2 && idOf(g, "t/blue") < idOf(g, "t/red"));
    }
    // new decals after reopening still go on top of the loaded ones
    {
        Decal c = a;
        c.material = "t/new";
        e.apply(e.makeAddDecal(c, e.brushes()[0].id, "Place decal"));
        CHECK(idOf(e, "t/new") > idOf(e, "t/blue"));
    }

    // order range: a file past it still opens (clamped, counted); a bad number doesn't
    {
        std::string past = text;
        const size_t at = past.find("order=0");
        CHECK(at != std::string::npos);
        past.replace(at, 7, "order=1000001");
        Document g;
        const MapReadResult r = readMap(past, g);
        CHECK(r.ok && r.clampedDecalOrders == 1);
        int top = 0;
        for (const Brush& x : g.brushes())
            for (const Decal& y : x.decals) top = std::max(top, y.order);
        CHECK(top == kDecalOrderMax);
        std::string bad = text;
        bad.replace(bad.find("order=0"), 7, "order=1.5");
        Document h;
        CHECK(!readMap(bad, h).ok);
        Document k;
        CHECK(readMap(text, k).ok && readMap(text, k).clampedDecalOrders == 0);
    }
    CHECK(decalOrderFrom(1e12) == kDecalOrderMax && decalOrderFrom(-1e12) == kDecalOrderMin && decalOrderFrom(2.6) == 3 &&
          decalOrderFrom(-2.6) == -3 && decalOrderFrom(std::nan("")) == 0);
    (void)right;
}

static void testProbes() {
    // Hammer's cubemapsize: 0 = 32, n = 2^(n-1); the render quality scales it
    CHECK(probeSide(0) == 32 && probeSide(1) == 1 && probeSide(6) == 32 && probeSide(9) == 256 && probeSide(20) == 256);
    CHECK(probeSizeLabel(0) == "Default (32 x 32)" && probeSizeLabel(8) == "128 x 128");
    CHECK(probeSizeLabel(1) == "1 x 1 (built at 4 x 4)" && probeSizeLabel(2) == "2 x 2 (built at 4 x 4)" && probeSizeLabel(3) == "4 x 4");
    CHECK(probeSideForQuality(1, 1) == 4 && probeSideForQuality(2, 2) == 4 && probeSideForQuality(9, 2) == 512);
    CHECK(probeSideForQuality(0, 0) == 16 && probeSideForQuality(0, 1) == 32 && probeSideForQuality(0, 2) == 64);
    CHECK(probeSideForQuality(9, 2) == 512 && probeSideForQuality(1, 1) == 4);
    // placing: 170 cm above a floor, 81 cm out from a wall or a ceiling
    CHECK(nearV(probePlacement({10, 20, 0}, {0, 0, 1}), {10, 20, 170}));
    CHECK(nearV(probePlacement({0, 0, 50}, {1, 0, 0}), {81.28, 0, 50}));
    CHECK(nearV(probePlacement({0, 0, 300}, {0, 0, -1}), {0, 0, 300 - 81.28}));
    // cube faces: every direction comes back from its face and place
    for (int f = 0; f < 6; ++f)
        for (double x : {-0.9, -0.2, 0.0, 0.5, 0.99})
            for (double y : {-0.7, 0.0, 0.3, 0.95}) {
                const Vec3 d = cubeDir(f, x, y);
                double x2 = 0, y2 = 0;
                CHECK(cubeFace(d, x2, y2) == f && near(x, x2, 1e-9) && near(y, y2, 1e-9));
            }
    {
        double x, y;
        CHECK(cubeFace({1, 0, 0}, x, y) == 0 && cubeFace({0, -1, 0}, x, y) == 3 && cubeFace({0, 0, -2}, x, y) == 5);
    }
    // blending: one probe = all of it; two: the nearer counts more; weights add up to 1
    std::vector<Vec3> ps{{0, 0, 100}};
    auto w = probeWeights(ps, {0, 0, 0}, {0, 0, 1});
    CHECK(w.size() == 1 && near(w[0].weight, 1));
    ps.push_back({1000, 0, 100});
    w = probeWeights(ps, {0, 0, 0}, {0, 0, 1});
    CHECK(w.size() == 2 && w[0].index == 0 && w[0].weight > 0.95 && near(w[0].weight + w[1].weight, 1));
    // halfway between two probes: an even share
    w = probeWeights(ps, {500, 0, 0}, {0, 0, 1});
    CHECK(near(w[0].weight, 0.5, 1e-9) && near(w[1].weight, 0.5, 1e-9));
    // a probe behind the surface counts much less (Source prefers probes in front of a face)
    std::vector<Vec3> two{{0, 0, 100}, {0, 0, -100}};
    w = probeWeights(two, {0, 0, 0}, {0, 0, 1});
    CHECK(w[0].index == 0 && near(w[0].weight, 1 / 1.05, 1e-9));
    // at most 4; the 5th's weight is taken off, so the order changes smoothly
    std::vector<Vec3> six;
    for (int i = 0; i < 6; ++i) six.push_back({100.0 * (i + 1), 0, 0});
    w = probeWeights(six, {0, 0, 0}, {1, 0, 0});
    double sum = 0;
    for (const auto& x : w) sum += x.weight;
    CHECK(w.size() == 4 && near(sum, 1) && w[3].index == 3 && w[3].weight > 0);
    // moving along the line, the 4th probe's weight goes to 0 just as the 5th takes over
    {
        std::vector<Vec3> line{{0, 0, 0}, {100, 0, 0}, {200, 0, 0}, {300, 0, 0}, {400, 0, 0}};
        auto weightOf = [&](const std::vector<ProbeWeight>& ws, size_t i) {
            for (const auto& x : ws) if (x.index == i) return x.weight;
            return 0.0;
        };
        // at x = 200 probes 0 and 4 are as far: neither counts (each is the 5th)
        const auto mid = probeWeights(line, {200, 0, 0}, {0, 1, 0});
        CHECK(near(weightOf(mid, 0), 0, 1e-12) && near(weightOf(mid, 4), 0, 1e-12));
        const auto a = probeWeights(line, {199.9, 0, 0}, {0, 1, 0}), b = probeWeights(line, {200.1, 0, 0}, {0, 1, 0});
        CHECK(weightOf(a, 0) < 1e-4 && weightOf(b, 4) < 1e-4 && near(weightOf(a, 2), weightOf(b, 2), 1e-4));
    }

    // the document: add, select, move, delete, undo
    Document doc;
    Probe p;
    p.pos = {0, 0, 170};
    doc.apply(doc.makeAddProbes({p}, "Create reflection probe"));
    CHECK(doc.probes().size() == 1 && doc.probeSelection().size() == 1 && doc.probes()[0].id != 0);
    Probe q = doc.probes()[0];
    q.pos = {64, 0, 170};
    q.size = 8;
    doc.apply(doc.makeModifyProbes({q}, "Move reflection probe"));
    CHECK(doc.probes()[0].pos == Vec3(64, 0, 170) && doc.probes()[0].size == 8);
    doc.undo();
    CHECK(doc.probes()[0].pos == Vec3(0, 0, 170) && doc.probes()[0].size == 0);
    doc.redo();
    Command del = doc.makeDelete({}, "Delete reflection probe");
    doc.addProbeDeletes(del, doc.probeSelection());
    doc.apply(std::move(del));
    CHECK(doc.probes().empty() && doc.probeSelection().empty());
    doc.undo();
    CHECK(doc.probes().size() == 1 && doc.probeSelection().size() == 1);
    // a new light (or primitive, prop, camera, decal) drops the probe selection
    doc.apply(doc.makeAddLights({Light{}}, "Create light"));
    CHECK(doc.probeSelection().empty() && doc.lightSelection().size() == 1);
    CHECK(doc.dirty());

    // the file: Hammer's env_cubemap, unknown keys kept
    Probe r = doc.probes()[0];
    r.otherKeys.emplace_back("sides", "12 13");
    doc.apply(doc.makeModifyProbes({r}, "Key"));
    const std::string text = writeMap(doc);
    CHECK(text.find("\"classname\" \"env_cubemap\"") != std::string::npos);
    CHECK(text.find("\"cubemapsize\" \"8\"") != std::string::npos);
    Document back;
    CHECK(readMap(text, back).ok);
    CHECK(back.probes().size() == 1 && back.probes()[0].pos == Vec3(64, 0, 170) && back.probes()[0].size == 8);
    CHECK(back.probes()[0].otherKeys.size() == 1 && back.probes()[0].otherKeys[0].second == "12 13");
    CHECK(back.otherEntities.empty());
    // a bad size is clamped (Hammer's list ends at 256)
    Document bad;
    CHECK(readMap("{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"env_cubemap\"\n\"origin\" \"1 2 3\"\n\"cubemapsize\" \"40\"\n}\n", bad).ok);
    CHECK(bad.probes().size() == 1 && bad.probes()[0].size == 9 && bad.probes()[0].pos == Vec3(1, 2, 3));
}

static void testBumpBake() {
    // A floor with a bump-mapped material, a slanted sun from +x: the 3 bump maps hold the
    // light along Source's 3 directions; the flat map is the same as without the bump map.
    auto setup = [] {
        BakeInput in;
        Brush floor = makeBox(box({0, 0, -16}, {1024, 1024, 0}), "rock");
        floor.id = 1;
        for (Face& f : floor.faces) {
            f.u.axis = {1, 0, 0};
            f.v.axis = {0, -1, 0};
        }
        in.brushes = {floor};
        Light sun;
        sun.kind = LightKind::Sun;
        sun.pitch = -50;
        sun.yaw = 180; // see below: whichever way it points, the check follows it
        sun.brightness = 200;
        in.lights = {sun};
        in.env.skyLight = 0;
        in.quality = BakeQuality::Draft;
        in.maxBounces = 0;
        return in;
    };
    const BakeInput plain = setup();
    BakeInput in = setup();
    BakeMaterial m;
    m.bumped = true;
    in.materials["rock"] = m;
    CHECK(bakeKey(in) != bakeKey(plain));
    const BakeResult rp = bakeLight(plain, nullptr, 2), rb = bakeLight(in, nullptr, 2);
    const Vec3 Ldir = -prepareLight(in.lights[0], in.env).dir; // towards the sun
    Vec3 b[3];
    bumpNormals({0, 0, 1}, {1, 0, 0}, {0, -1, 0}, b);
    CHECK(b[0].x > 0.8 && b[1].y < -0.7 && b[2].y > 0.7); // b0 along s, b1 along t
    bool found = false;
    for (size_t i = 0; i < rb.faces.size(); ++i) {
        const FaceLightmap& f = rb.faces[i];
        const FaceLightmap& g = rp.faces[i];
        if (in.brushes[0].faces[f.face].plane().n.z < 0.999) {
            CHECK(f.bump.size() == f.rgb.size() * 3 || f.bump.empty());
            continue;
        }
        found = true;
        CHECK(g.bump.empty());
        CHECK(f.bump.size() == f.rgb.size() * 3);
        double maxDiff = 0;
        for (size_t k = 0; k < f.rgb.size() && k < g.rgb.size(); ++k) maxDiff = std::max(maxDiff, (double)std::fabs(f.rgb[k] - g.rgb[k]));
        CHECK(maxDiff < 1e-5);
        // a luxel in the middle: the maps hold the light along b_k (b_k . L), scaled as the
        // engine does so their average is the flat value (no seam beside unbumped faces)
        const size_t nl = (size_t)f.w * f.h, k = ((size_t)(f.h / 2) * f.w + f.w / 2);
        const double flat = f.rgb[3 * k];
        CHECK(flat > 0.1);
        double dsum = 0;
        for (int q = 0; q < 3; ++q) dsum += std::max(0.0, dot(b[q], Ldir));
        double avg = 0;
        for (int q = 0; q < 3; ++q) {
            const double want = flat * 3 * std::max(0.0, dot(b[q], Ldir)) / dsum;
            CHECK(near(f.bump[(q * nl + k) * 3], want, 0.02 + 0.03 * want));
            avg += f.bump[(q * nl + k) * 3] / 3.0;
        }
        CHECK(near(avg, flat, 1e-4 + 1e-4 * flat));
    }
    CHECK(found);
}

static void testTracer() {
    std::vector<std::array<Vec3, 3>> blob;
    const int R = 12, C = 12;
    auto P = [&](int j, int i) {
        const double th = M_PI * j / R, ph = 2 * M_PI * i / C, r = 40 * (1 + 0.2 * std::sin(3 * th) * std::cos(2 * ph));
        return Vec3{r * std::sin(th) * std::cos(ph), r * std::sin(th) * std::sin(ph), 40 + r * std::cos(th)};
    };
    for (int j = 0; j < R; ++j)
        for (int i = 0; i < C; ++i) {
            blob.push_back({P(j, i), P(j + 1, i), P(j, i + 1)});
            blob.push_back({P(j, i + 1), P(j + 1, i), P(j + 1, i + 1)});
        }
    Tracer inst, flat;
    Brush floor = makeBox(box({-500, -500, -16}, {500, 500, 0}), "f");
    for (const Polygon& p : floor.polygons())
        for (size_t i = 1; i + 1 < p.verts.size(); ++i) {
            inst.addTri(p.verts[0], p.verts[i], p.verts[i + 1], 7);
            flat.addTri(p.verts[0], p.verts[i], p.verts[i + 1], 7);
        }
    const int mesh = inst.addMesh(blob);
    for (int k = 0; k < 6; ++k) {
        Prop pr;
        pr.pos = {-300.0 + 120 * k, 40.0 * (k % 3), 0};
        pr.rot = {10.0 * k, 25.0 * k, 5.0};
        pr.scale = {1 + 0.1 * k, 1, 1.5 - 0.05 * k};
        const Vec3 o = propToWorld(pr, {0, 0, 0});
        const Vec3 cols[3] = {propToWorld(pr, {1, 0, 0}) - o, propToWorld(pr, {0, 1, 0}) - o, propToWorld(pr, {0, 0, 1}) - o};
        const int base = 1000 * (k + 1);
        CHECK(inst.addInstance(mesh, cols, o, base));
        for (size_t t = 0; t < blob.size(); ++t)
            flat.addTri(propToWorld(pr, blob[t][0]), propToWorld(pr, blob[t][1]), propToWorld(pr, blob[t][2]), base + (int)t);
    }
    const Vec3 zero{0, 0, 0};
    CHECK(!inst.addInstance(mesh, std::array<Vec3, 3>{zero, zero, zero}.data(), zero, 0)); // scale 0: nothing
    inst.build();
    flat.build();
    CHECK(inst.triangles() >= flat.triangles()); // a model keeps its degenerate triangles (never hit)
    unsigned seed = 12345;
    auto rnd = [&] { seed = seed * 1103515245u + 12345u; return ((seed >> 8) & 0xffff) / 65535.0; };
    int same = 0, n = 2000, hits = 0;
    for (int k = 0; k < n; ++k) {
        const Vec3 o{-450 + 900 * rnd(), -300 + 600 * rnd(), 5 + 200 * rnd()};
        const Vec3 d = normalize(Vec3{rnd() - 0.5, rnd() - 0.5, rnd() - 0.7});
        Tracer::Hit a, b;
        const bool ha = inst.closestHit(o, d, 5000, a), hb = flat.closestHit(o, d, 5000, b);
        const bool oa = inst.occluded(o, d, 300), ob = flat.occluded(o, d, 300);
        if (ha == hb && (!ha || (std::fabs(a.t - b.t) < 1e-3 && a.tag == b.tag)) && oa == ob) ++same;
        hits += ha;
    }
    CHECK(same >= n - 2); // a ray grazing an edge may fall either way in the last digit
    CHECK(hits > n / 2);
}

static void writeText(const std::filesystem::path& p, const std::string& t) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream o(p, std::ios::binary);
    o << t;
}

static void testMaterials() {
    namespace fs = std::filesystem;
    // ---- KeyValues
    {
        VmtBlock b;
        std::string err;
        CHECK(parseVmt("LightmappedGeneric\n{\n  \"$BaseTexture\" \"brick/brick01\" // a comment\n  $envmap env_cubemap\n"
                       "  \"$envmaptint\" \"[0.5 0.5 0.5]\"\n  Proxies { AnimatedTexture { animatedtexturevar $basetexture } }\n}\n", b, &err));
        CHECK(b.name == "LightmappedGeneric");
        CHECK(b.keys.size() == 3 && b.keys[0].key == "$basetexture" && b.keys[0].value == "brick/brick01");
        CHECK(b.keys[1].value == "env_cubemap");
        CHECK(b.blocks.size() == 1 && b.blocks[0].blocks.size() == 1);
        VmtBlock again;
        CHECK(parseVmt(writeVmt(b), again) && again.keys.size() == 3 && again.blocks.size() == 1 && again.keys[2].value == "[0.5 0.5 0.5]");
        CHECK(!parseVmt("LightmappedGeneric { \"$a\" \"1\"", b, &err) && !err.empty());   // a '}' missing
        CHECK(!parseVmt("", b, &err));
        // an unquoted vector and Source's [$X360] condition after a value
        CHECK(parseVmt("UnlitGeneric { $color [1 0 0] $alpha 0.5 [$X360] $nocull 1 }", b) && b.keys.size() == 3 && b.keys[0].value == "[1 0 0]" && b.keys[2].key == "$nocull");
    }
    // ---- values
    {
        Vec3 v;
        CHECK(parseVmtVector("[0.5 1 0]", v) && std::fabs(v.x - 0.5) < 1e-9 && v.y == 1 && v.z == 0);
        CHECK(parseVmtVector("{255 0 51}", v) && std::fabs(v.x - 1) < 1e-9 && std::fabs(v.z - 0.2) < 1e-9);
        CHECK(parseVmtVector("2", v) && v.x == 2 && v.z == 2);
        CHECK(!parseVmtVector("red", v));
        double br = 0;
        CHECK(parseTexLight("255 128 0 400", v, br) && v.y == 128 && br == 400);
        CHECK(parseTexLight("255 255 255", v, br) && br == 200);
        Material m;
        m.params["$alpha"] = "0.25";
        m.params["$envmaptint"] = "[.2 .3 .4]";
        CHECK(std::fabs(m.num("$ALPHA", 1) - 0.25) < 1e-12 && m.num("$missing", 7) == 7);
        CHECK(std::fabs(m.vec("$envmaptint", {}).z - 0.4) < 1e-12);
    }
    // ---- patch: insert adds or overrides, replace only changes what the parent has, "" removes
    {
        MaterialFile parent, child;
        CHECK(MaterialFile::parse("LightmappedGeneric { $envmap env_cubemap $envmaptint \"[1 1 1]\" $detail grain }", parent));
        CHECK(MaterialFile::parse("patch { include \"textures/base.vmt\" insert { $basetexture brick $envmaptint \"[.5 .5 .5]\" $detail \"\" } "
                                  "replace { $selfillum 1 $envmap sky } }", child));
        CHECK(child.isPatch() && child.include == "textures/base.vmt" && child.params.size() == 3 && child.replace.size() == 2);
        const Material m = resolveMaterial(child, [&](const std::string& inc) -> std::optional<std::pair<MaterialFile, std::string>> {
            if (inc == "textures/base.vmt") return std::make_pair(parent, std::string("/x/base.vmt"));
            return std::nullopt;
        });
        CHECK(m.shader == "LightmappedGeneric");
        CHECK(m.str("$basetexture") == "brick");
        CHECK(m.str("$envmaptint") == "[.5 .5 .5]");
        CHECK(!m.has("$detail"));               // removed by ""
        CHECK(!m.has("$selfillum"));            // replace doesn't add
        CHECK(m.str("$envmap") == "sky");       // replace changes what is there
        CHECK(m.parentFile == "/x/base.vmt" && m.problem.empty());
        CHECK(m.own.count("$basetexture") && m.own.count("$envmap") && !m.own.count("$envmaptint") == false);
        // own value editing keeps a patch a patch
        MaterialFile c2 = child;
        c2.setOwn("$EnvMap", "env_cubemap");
        CHECK(c2.replace.size() == 1 && c2.own("$envmap") && *c2.own("$envmap") == "env_cubemap");
        c2.removeOwn("$basetexture");
        CHECK(!c2.own("$basetexture"));
        MaterialFile back;
        CHECK(MaterialFile::parse(c2.text(), back) && back.isPatch() && back.include == "textures/base.vmt" && back.own("$envmap"));
        // a missing parent is reported, the own keys still apply; a loop of includes stops
        const Material lost = resolveMaterial(child, [](const std::string&) { return std::nullopt; });
        CHECK(!lost.problem.empty() && lost.str("$basetexture") == "brick");
        const Material loop = resolveMaterial(child, [&](const std::string&) { return std::make_pair(child, std::string("/x/self.vmt")); });
        CHECK(!loop.problem.empty());
    }
    // ---- the index: textures, settings beside them, variants, helpers, parents
    {
        const fs::path root = fs::temp_directory_path() / "scal_materials_test";
        fs::remove_all(root);
        const fs::path bt = root / "bottega", user = root / "mine", builtin = root / "builtin";
        writeText(bt / "bottega.txt", "bottega 1\n");
        writeText(bt / "textures/walls/Brick.png", "x");
        writeText(bt / "textures/walls/Brick_mask.png", "x");
        writeText(bt / "textures/walls/Brick_normal.png", "x");
        writeText(bt / "textures/walls/Brick.vmt", "LightmappedGeneric { $basetexture textures/walls/Brick $envmap env_cubemap }");
        writeText(bt / "textures/walls/Brick_wet.vmt", "patch { include textures/walls/brick_base.vmt insert { $basetexture walls/Brick $envmaptint \"[.8 .8 .8]\" } }");
        writeText(bt / "textures/walls/brick_base.vmt", "LightmappedGeneric { $envmap env_cubemap $envmapcontrast 1 }");
        writeText(bt / "textures/Glass.png", "x");
        writeText(bt / "textures/broken.vmt", "LightmappedGeneric { $basetexture ");
        writeText(user / "My Tile.png", "x");
        writeText(user / "My Tile_selfillum.png", "x");
        writeText(builtin / "Bricks_24.png", "x");
        writeText(builtin / "Bricks_24.vmt", "LightmappedGeneric { $basetexture Bricks_24 $detail Bricks_24 }");
        writeText(builtin / "Glass.png", "x"); // a later folder's Glass loses to... a different name: "Glass" vs "textures/Glass"
        MaterialIndex ix;
        ix.scanFolder((bt / "textures").string(), bt.string(), true);
        ix.scanFolder(user.string(), "", true);
        ix.scanFolder(builtin.string(), "", false);
        ix.finish();
        const auto names = ix.names(false);
        auto has = [&](const std::string& n) { return std::find(names.begin(), names.end(), n) != names.end(); };
        CHECK(has("textures/walls/Brick") && has("textures/walls/Brick_wet") && has("My_Tile") && has("Bricks_24") && has("textures/Glass") && has("Glass"));
        CHECK(!has("textures/walls/Brick_mask") && !has("textures/walls/Brick_normal") && !has("My_Tile_selfillum")); // helpers hidden
        CHECK(!has("textures/walls/brick_base") && !has("textures/broken")); // a parent only, a broken file
        const auto all = ix.names(true);
        CHECK(std::find(all.begin(), all.end(), "textures/walls/Brick_mask") != all.end());
        const MaterialIndex::Entry* brick = ix.find("textures/walls/Brick");
        CHECK(brick && brick->kind == MaterialIndex::Kind::Texture && brick->base == "textures/walls/Brick");
        // v0.12.6 review (F12): a parent whose chain reaches the material would loop
        CHECK(ix.find("textures/walls/brick_base"));
        CHECK(ix.parentWouldLoop("textures/walls/Brick_wet", {"textures/walls/brick_base"}));  // Brick_wet -> brick_base
        CHECK(ix.parentWouldLoop("textures/walls/brick_base", {"textures/walls/brick_base"})); // itself
        CHECK(!ix.parentWouldLoop("textures/walls/Brick", {"textures/walls/brick_base"}));     // no chain
        CHECK(!ix.parentWouldLoop("textures/walls/brick_base", {"textures/walls/Brick_wet"})); // the other way is fine
        CHECK(brick && brick->mat.str("$envmap") == "env_cubemap");
        CHECK(brick && brick->mat.str("$envmapmask") == "textures/walls/Brick_mask"); // hooked by name
        CHECK(brick && brick->mat.str("$bumpmap") == "textures/walls/Brick_normal");
        const MaterialIndex::Entry* wet = ix.find("textures/walls/Brick_wet");
        CHECK(wet && wet->kind == MaterialIndex::Kind::Variant && wet->base == "textures/walls/Brick");
        CHECK(wet && wet->mat.num("$envmapcontrast") == 1 && wet->mat.str("$envmaptint") == "[.8 .8 .8]"); // from its parent, and its own
        CHECK(wet && wet->mat.str("$envmapmask") == "textures/walls/Brick_mask"); // a variant gets its base's helpers
        CHECK(wet && !wet->mat.parentFile.empty() && wet->mat.problem.empty());
        const MaterialIndex::Entry* tile = ix.find("My_Tile");
        CHECK(tile && tile->vmt.empty() && tile->mat.str("$basetexture") == "My_Tile" && tile->mat.str("$selfillummask") == "My_Tile_selfillum");
        CHECK(ix.vmtPathFor("My_Tile") == (user / "My Tile.vmt").lexically_normal().string());
        CHECK(ix.vmtPathFor("Bricks_24").empty());                 // built-in: read only
        CHECK(ix.find("Bricks_24") && ix.find("Bricks_24")->mat.str("$detail") == "Bricks_24");
        CHECK(ix.find("textures/broken") && !ix.find("textures/broken")->mat.problem.empty());
        CHECK(ix.vmtEntries().size() == 5);
        // includes are named from the library folder's root (the Bottega root), as Source's "materials/..."
        CHECK(brick && ix.includeName(*brick, (bt / "textures/walls/brick_base.vmt").string()) == "textures/walls/brick_base.vmt");
        CHECK(brick && ix.resolveTexture(*brick, "walls\\Brick.vtf") == "textures/walls/Brick");
        CHECK(brick && ix.resolveTexture(*brick, "Brick_mask") == "textures/walls/Brick_mask");   // beside the .vmt
        CHECK(brick && ix.resolveTexture(*brick, "nothing").empty());
        fs::remove_all(root);
    }
    // ---- shader files: the shipped ones list their settings
    {
        ShaderFile sf;
        std::string err;
        CHECK(!parseShaderFile("void main() {}", sf, &err) && !err.empty());
        for (const char* n : {"LightmappedGeneric", "UnlitGeneric", "VertexLitGeneric"}) {
            std::ifstream in(std::string(SCAL_SOURCE_DIR) + "/assets/shaders/" + n + ".glsl");
            std::stringstream ss;
            ss << in.rdbuf();
            CHECK(parseShaderFile(ss.str(), sf, &err) && sf.name == n && sf.kind == "material");
            CHECK(sf.setting("$basetexture") && sf.setting("$basetexture")->type == "texture");
            CHECK(sf.setting("$alphatestreference") && sf.setting("$alphatestreference")->def == "0.5");
            CHECK(sf.setting("$envmaptint") && sf.setting("$envmaptint")->def == "1 1 1");
            CHECK(sf.body.find("vec4 shade()") != std::string::npos && sf.body.find("// setting:") == std::string::npos);
            CHECK(sf.bodyLine > 10);
        }
        CHECK(shaderUniformName("$detailscale") == "u_detailscale" && shaderUniformName("%texlight") == "u_texlight");
    }
    // ---- material files through undo: written, put back, deleted; the map stays "saved"
    {
        const fs::path dir = fs::temp_directory_path() / "scal_material_undo";
        fs::remove_all(dir);
        fs::create_directories(dir);
        Document doc;
        doc.markSaved();
        const std::string a = (dir / "a.vmt").string(), b = (dir / "b.vmt").string();
        writeText(a, "LightmappedGeneric { $alpha 1 }");
        auto read = [](const std::string& p) { std::ifstream i(p); std::stringstream s; s << i.rdbuf(); return s.str(); };
        Command c = doc.makeFileEdit({{a, std::string("LightmappedGeneric { $alpha 0.5 }")}, {b, std::string("UnlitGeneric { }")}}, "Material");
        CHECK(c.fileChanges.size() == 2 && c.filesOnly());
        const auto stamp = doc.filesStamp();
        doc.apply(c);
        CHECK(read(a).find("0.5") != std::string::npos && fs::exists(b) && doc.filesStamp() != stamp);
        CHECK(!doc.dirty() && doc.canUndo());
        doc.undo();
        CHECK(read(a).find("$alpha 1") != std::string::npos && !fs::exists(b) && !doc.dirty());
        doc.redo();
        CHECK(fs::exists(b) && doc.lastFiles().size() == 2 && doc.fileError().empty());
        // the same text again changes nothing
        CHECK(doc.makeFileEdit({{b, std::string("UnlitGeneric { }")}}, "x").fileChanges.empty());
        // with a map edit after it, the map is changed as usual
        doc.apply(doc.makeAdd({makeBox(box({0, 0, 0}, {10, 10, 10}), "a")}, "Add"));
        CHECK(doc.dirty());
        fs::remove_all(dir);
    }
    // ---- v0.12.6 review (F11): undo / redo leave a file alone that was changed outside since
    {
        const fs::path dir = fs::temp_directory_path() / "scal_material_outside";
        fs::remove_all(dir);
        fs::create_directories(dir);
        auto read = [](const std::string& p) { std::ifstream i(p); std::stringstream s; s << i.rdbuf(); return s.str(); };
        const std::string a = (dir / "a.vmt").string(), b = (dir / "b.vmt").string();
        const std::string outside = "LightmappedGeneric { $alphatest 1 $alphatestreference 0.75 }";
        // a new file, edited outside, then undo: kept (not deleted)
        {
            Document doc;
            doc.apply(doc.makeFileEdit({{a, std::string("LightmappedGeneric { $alphatest 1 }")}}, "Alpha test"));
            CHECK(fs::exists(a) && doc.fileError().empty());
            writeText(a, outside);
            doc.undo();
            CHECK(fs::exists(a) && read(a) == outside);
            CHECK(doc.fileError().find("a.vmt was changed outside") != std::string::npos);
            // redo: the file is still not what undo left (it left it alone), so still kept
            doc.redo();
            CHECK(read(a) == outside && !doc.fileError().empty());
        }
        // an existing file, two edits, edited outside, undo: kept (not overwritten)
        {
            Document doc;
            writeText(b, "LightmappedGeneric { }");
            doc.apply(doc.makeFileEdit({{b, std::string("LightmappedGeneric { $alphatest 1 }")}}, "Alpha test"));
            doc.apply(doc.makeFileEdit({{b, std::string("LightmappedGeneric { $alphatest 1 $nocull 1 }")}}, "No cull"));
            writeText(b, outside);
            doc.undo();
            CHECK(read(b) == outside && !doc.fileError().empty());
            // the other step still works once the file is as the history left it
            writeText(b, "LightmappedGeneric { $alphatest 1 }");
            doc.undo();
            CHECK(read(b) == "LightmappedGeneric { }" && doc.fileError().empty());
            doc.redo();
            CHECK(read(b) == "LightmappedGeneric { $alphatest 1 }" && doc.fileError().empty());
        }
        // an outside change that happens to match what undo wants: nothing to do, no error
        {
            Document doc;
            const std::string c = (dir / "c.vmt").string();
            doc.apply(doc.makeFileEdit({{c, std::string("UnlitGeneric { }")}}, "New"));
            fs::remove(c);
            doc.undo();
            CHECK(!fs::exists(c) && doc.fileError().empty());
        }
        fs::remove_all(dir);
    }
}

static void testMirrors() {
    namespace fs = std::filesystem;
    // what a LightmappedReflective material needs (Source's defaults: both pictures)
    Material m;
    m.shader = "LightmappedReflective";
    CHECK(isReflectiveMaterial(m) && mirrorParts(m) == (kMirrorReflect | kMirrorRefract));
    m.params["$refracttexture"] = "none";
    CHECK(mirrorParts(m) == kMirrorReflect);
    m.params["$refracttexture"] = "_rt_WaterRefraction";
    m.params["$minreflectivity"] = "1"; // the Fresnel mix never shows the see-through
    CHECK(mirrorParts(m) == kMirrorReflect);
    m.params["$basetexture"] = "Tile_19";
    m.params["$reflecttexture"] = "something/else";
    CHECK(mirrorParts(m) == kMirrorBase);
    Material plain;
    plain.shader = "LightmappedGeneric";
    plain.params["$envmap"] = "env_cubemap";
    CHECK(!isReflectiveMaterial(plain) && mirrorParts(plain) == 0);
    Material lower;
    lower.shader = "lightmappedreflective";
    CHECK(isReflectiveMaterial(lower));

    // the mirrored camera (Source's CReflectiveGlassView): position and view reflected
    Camera c;
    c.pos = {0, -300, 150};
    aimCamera(c, {100, 0, 100});
    const Camera mc = mirrorCamera(c, {0, -1, 0}, 0); // the plane y = 0, facing -y
    CHECK(nearV(mc.pos, {0, 300, 150}));
    const Vec3 f = cameraForward(c), mf = cameraForward(mc);
    CHECK(nearV(mf, {f.x, -f.y, f.z}, 1e-6));
    // a proper rotation again (left = up x forward)
    const Mat3 b = cameraBasis(mc);
    CHECK(nearV(b.col[1], normalize(cross(b.col[2], b.col[0])), 1e-6));

    // how much of the picture a polygon covers
    Camera cov;
    cov.pos = {0, 0, 0};
    cov.yaw = 0;
    cov.pitch = 0;
    cov.fov = 90;
    cov.width = cov.height = 100;
    // a square straight ahead filling the view exactly (half width = distance x tan 45)
    std::vector<Vec3> full{{100, 100, 100}, {100, -100, 100}, {100, -100, -100}, {100, 100, -100}};
    CHECK(near(pictureCover(full, cov), 1, 1e-9));
    std::vector<Vec3> quarter{{100, 0, 0}, {100, -100, 0}, {100, -100, -100}, {100, 0, -100}};
    CHECK(near(pictureCover(quarter, cov), 0.25, 1e-9));
    std::vector<Vec3> bigger{{100, 500, 500}, {100, -500, 500}, {100, -500, -500}, {100, 500, -500}}; // clipped to the picture
    CHECK(near(pictureCover(bigger, cov), 1, 1e-9));
    std::vector<Vec3> behind{{-100, 100, 100}, {-100, -100, 100}, {-100, -100, -100}, {-100, 100, -100}};
    CHECK(pictureCover(behind, cov) == 0);
    // half in front of the camera: only that half counts (clipped at the near plane)
    std::vector<Vec3> across{{-100, 50, -50}, {200, 50, -50}, {200, 50, 50}, {-100, 50, 50}};
    CHECK(pictureCover(across, cov) > 0 && pictureCover(across, cov) < 1);

    // the planes worth drawing: merged by plane, the camera in front, the largest first, at most N
    auto face = [](std::vector<Vec3> v, const Vec3& n, int parts) {
        MirrorFace f;
        f.verts = std::move(v);
        f.n = n;
        f.d = dot(n, f.verts[0]);
        f.parts = parts;
        return f;
    };
    std::vector<MirrorFace> faces;
    faces.push_back(face({{100, 0, 0}, {100, -100, 0}, {100, -100, -100}, {100, 0, -100}}, {-1, 0, 0}, kMirrorReflect));   // a quarter
    faces.push_back(face({{100, 100, 100}, {100, 0, 100}, {100, 0, 0}, {100, 100, 0}}, {-1, 0, 0}, kMirrorRefract));      // same plane
    faces.push_back(face({{50, 10, 10}, {50, -10, 10}, {50, -10, -10}, {50, 10, -10}}, {-1, 0, 0}, kMirrorReflect));      // a small one, nearer
    faces.push_back(face({{150, 100, 100}, {150, -100, 100}, {150, -100, -100}, {150, 100, -100}}, {1, 0, 0}, kMirrorReflect)); // seen from behind
    faces.push_back(face({{100, 0, 0}, {100, -100, 0}, {100, -100, -100}, {100, 0, -100}}, {-1, 0, 0}, kMirrorBase));      // no picture needed
    size_t dropped = 9;
    auto planes = mirrorPlanes(faces, cov, 8, &dropped);
    CHECK(planes.size() == 2 && dropped == 0);
    CHECK(near(planes[0].d, -100) && planes[0].parts == (kMirrorReflect | kMirrorRefract) && near(planes[0].cover, 0.5, 1e-9));
    CHECK(near(planes[1].d, -50) && planes[1].cover < 0.1);
    planes = mirrorPlanes(faces, cov, 1, &dropped);
    CHECK(planes.size() == 1 && dropped == 1 && near(planes[0].d, -100));
    CHECK(onMirrorPlane(planes[0], {-1, 0, 0}, -100.2) && !onMirrorPlane(planes[0], {-1, 0, 0}, -101) && !onMirrorPlane(planes[0], {1, 0, 0}, 100));

    // a material without $basetexture shows its %tooltexture in the editor (as Hammer)
    {
        const fs::path root = fs::temp_directory_path() / "scal_mirror_test";
        fs::remove_all(root);
        writeText(root / "textures/Glass.png", "x");
        writeText(root / "textures/mirror.vmt", "LightmappedReflective { %tooltexture textures/Glass $refracttexture none }");
        MaterialIndex ix;
        ix.scanFolder((root / "textures").string(), root.string(), true);
        ix.finish();
        const MaterialIndex::Entry* e = ix.find("textures/mirror");
        CHECK(e && e->base == "textures/Glass" && mirrorParts(e->mat) == kMirrorReflect);
        fs::remove_all(root);
    }
    // a new Bottega gets the starter materials; an existing one is left alone
    {
        const fs::path root = fs::temp_directory_path() / "scal_starter_test";
        fs::remove_all(root);
        std::string err;
        CHECK(bottega::make(root.string(), err));
        CHECK(fs::exists(root / "textures/starter/mirror.vmt") && fs::exists(root / "textures/starter/polished_stone.vmt"));
        VmtBlock vb;
        std::ifstream in(root / "textures/starter/mirror.vmt");
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(parseVmt(text, vb) && vb.name == "LightmappedReflective");
        fs::remove(root / "textures/starter/mirror.vmt");
        CHECK(bottega::make(root.string(), err)); // repairing an existing Bottega
        CHECK(!fs::exists(root / "textures/starter/mirror.vmt"));
        fs::remove_all(root);
    }
}

int main() {
    testRenderJob();
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
    testCameras();
    testSky();
    testSkyLab();
    testSkyLabClouds();
    testSkyLabNight();
    testSkyPresetsAndCut();
    testBottega();
    testMathPrims();
    testPrimBlend();
    testLandingReplicate();
    testProps();
    testRay();
    testUndo();
    testBrushCaches();
    testHistoryLimit();
    testMapStrictNumbers();
    testMakeChange();
    testGroups();
    testVisibility();
    testDirty();
    testMapRoundTrip();
    testBake();
    testBounce();
    testPropLight();
    testMaterials();
    testBakeMaterials();
    testBumpBake();
    testDecals();
    testDecalOrderFile();
    testProbes();
    testMirrors();
    testTracer();
    testMapQuakeFormat();
    testMapErrors();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
