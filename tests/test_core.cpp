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
#include "io/map_format.hpp"
#include <cmath>
#include <random>
#include <cstdio>
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
    d.otherEntities.push_back({{{"classname", "light"}, {"origin", "0 0 64"}, {"light", "300"}}, {}});

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

    // Terrain round trip: exact heights, blend, second texture, start corner; brushes
    // without painting have no blend rows; broken terrain data is reported.
    {
        AABB tb; tb.add({0, 0, 0}); tb.add({256, 256, 64});
        Brush t1 = makeBox(tb, "a");
        CHECK(makeTerrain(t1, topQuadFace(t1), 3).empty());
        std::vector<Brush> ts{t1};
        SculptDab d; d.center = {100, 90, 64}; d.radius = 120; d.amount = 13.37;
        sculpt(ts, d);
        d.mode = SculptMode::Paint; d.strength = 0.3; d.texture = "Tile_19";
        sculpt(ts, d);
        tb.min.x += 300; tb.max.x += 300;
        Brush t2 = makeBox(tb, "a");
        CHECK(makeTerrain(t2, topQuadFace(t2), 2).empty());
        t2.terrain->heights[3] = -7.25;
        Document td;
        td.apply(td.makeAdd({ts[0], t2}, "add"));
        const std::string text = writeMap(td);
        CHECK(text.find("//@ terrain face=") != std::string::npos);
        CHECK(text.find("tex2=Tile_19") != std::string::npos);
        Document te;
        CHECK(readMap(text, te).ok);
        CHECK(te.brushes().size() == 2);
        if (te.brushes().size() == 2) {
            const Brush &r1 = te.brushes()[0], &r2 = te.brushes()[1];
            CHECK(r1.terrain && r2.terrain);
            if (r1.terrain && r2.terrain) {
                CHECK(r1.terrain->heights == ts[0].terrain->heights);
                CHECK(r1.terrain->alpha == ts[0].terrain->alpha);
                CHECK(r1.terrain->texture2 == "Tile_19");
                CHECK(nearV(r1.terrain->start, ts[0].terrain->start));
                CHECK(r2.terrain->power == 2 && near(r2.terrain->heights[3], -7.25));
                CHECK(r2.terrain->alpha.size() == 25);
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
    // A 3-corner face can't be terrain.
    {
        const SplitResult sr = splitBrush(makeBox(box, "a"), {0, 128, 0}, {0, 128, 1}, {128, 0, 0}, "a");
        CHECK(sr.front.has_value());
        if (sr.front) {
            Brush tri = *sr.front;
            int triTop = -1;
            for (const Polygon& p : tri.polygons()) if (p.normal.z > 0.9) triTop = p.face;
            CHECK(triTop >= 0);
            CHECK(!makeTerrain(tri, triTop, 3).empty());
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
        // Paint sets the second texture and blend; erase takes it away.
        d.mode = SculptMode::Paint; d.strength = 0.5; d.texture = "Tile_19"; d.radius = 40;
        CHECK(sculpt(bs, d));
        CHECK(bs[0].terrain->texture2 == "Tile_19");
        CHECK(near(bs[0].terrain->alpha[4 * 9 + 4], 0.5));
        CHECK(near(bs[0].terrain->alpha[0], 0));
        d.mode = SculptMode::Erase; d.strength = 1;
        CHECK(sculpt(bs, d));
        CHECK(near(bs[0].terrain->alpha[4 * 9 + 4], 0));
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
                        if (std::fabs(bs[0].terrain->alpha[p] - bs[1].terrain->alpha[q]) > 1e-9) ok = false;
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
