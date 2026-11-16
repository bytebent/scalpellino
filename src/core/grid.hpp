#pragma once
// Editor grid: size, visibility, snapping. Pure logic, no graphics.
// Sizes are doubles: powers of two by default (1..512), a microgrid (0.125, 0.25, 0.5)
// when enabled, and any custom size (e.g. 80) via setSize().
#include <algorithm>
#include <cmath>

namespace scal {

// Half-extent of the editable world, in Hammer units (GoldSrc: -4096..4096).
constexpr double kWorldHalf = 4096.0;

struct Grid {
    static constexpr double kMin = 1;       // smallest size without microgrid
    static constexpr double kMicroMin = 0.125;
    static constexpr double kMax = 512;
    static constexpr double kCustomMax = 4096;

    double size = 16;
    bool visible = true;
    bool snap = true;
    bool microgrid = false;  // allow sizes below 1

    double minSize() const { return microgrid ? kMicroMin : kMin; }
    void halve()    { if (size / 2 >= minSize() - 1e-12) size /= 2; }
    void doubleUp() { if (size * 2 <= std::max(kMax, size) + 1e-12 && size < kMax) size *= 2; }
    // Any size; clamped to the allowed range.
    void setSize(double s) { size = std::clamp(s, kMicroMin, kCustomMax); if (size < 1) microgrid = true; }
    // Turning microgrid off brings a sub-unit grid back to 1.
    void setMicrogrid(bool on) { microgrid = on; if (!on && size < 1) size = 1; }

    double snapValue(double v) const {
        return snap ? std::round(v / size) * size : v;
    }
};

} // namespace scal
