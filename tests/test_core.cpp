// Core + IO tests. No framework: each CHECK prints on failure; exit code = failures.
#include "core/brush.hpp"
#include "core/document.hpp"
#include "core/grid.hpp"
#include "core/transform.hpp"
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
    testWedge();
    testInvalid();
    testTranslateTextureLock();
    testRemap();
    testRotate();
    testTransform();
    testRay();
    testUndo();
    testDirty();
    testMapRoundTrip();
    testMapQuakeFormat();
    testMapErrors();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
