#pragma once
// Editor grid: size, visibility, snapping. Pure logic, no graphics.
#include <cmath>

namespace scal {

// Half-extent of the editable world, in Hammer units (GoldSrc: -4096..4096).
constexpr double kWorldHalf = 4096.0;

struct Grid {
    static constexpr int kMin = 1;
    static constexpr int kMax = 512;

    int  size    = 16;
    bool visible = true;
    bool snap    = true;

    void halve()   { if (size > kMin) size /= 2; }
    void doubleUp(){ if (size < kMax) size *= 2; }

    double snapValue(double v) const {
        return snap ? std::round(v / size) * size : v;
    }
};

} // namespace scal
