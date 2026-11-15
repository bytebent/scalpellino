// Core tests. No framework: each CHECK prints on failure; exit code = failures.
#include "core/grid.hpp"
#include <cstdio>

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static void testGridSizes() {
    scal::Grid g;
    CHECK(g.size == 16);
    g.halve(); CHECK(g.size == 8);
    for (int i = 0; i < 20; ++i) g.halve();
    CHECK(g.size == scal::Grid::kMin);
    for (int i = 0; i < 20; ++i) g.doubleUp();
    CHECK(g.size == scal::Grid::kMax);
}

static void testGridSnap() {
    scal::Grid g;              // size 16, snap on
    CHECK(g.snapValue(7.0)   == 0.0);
    CHECK(g.snapValue(9.0)   == 16.0);
    CHECK(g.snapValue(-9.0)  == -16.0);
    CHECK(g.snapValue(100.0) == 96.0);
    g.snap = false;
    CHECK(g.snapValue(9.5) == 9.5);
}

int main() {
    testGridSizes();
    testGridSnap();
    if (g_failures == 0) std::printf("core tests: all passed\n");
    return g_failures;
}
