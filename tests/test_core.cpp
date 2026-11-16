// Core + IO tests. No framework: each CHECK prints on failure; exit code = failures.
#include "core/brush.hpp"
#include "core/document.hpp"
#include "core/grid.hpp"
#include "core/transform.hpp"
#include "core/hull.hpp"
#include "core/texture.hpp"
#include "core/csg.hpp"
#include "io/map_format.hpp"
#include <cmath>
#include <cstdio>
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
    testSnapToGrid();
    testTextures();
    testCsg();
    testRay();
    testUndo();
    testMakeChange();
    testDirty();
    testMapRoundTrip();
    testMapQuakeFormat();
    testMapErrors();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
