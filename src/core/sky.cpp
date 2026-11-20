#include <cstdlib>
#include "core/sky.hpp"
#include <algorithm>
#include <cmath>

namespace scal {

namespace {
constexpr double kPi = 3.14159265358979323846;
} // namespace

SkyLayout skyLayout(int w, int h) {
    if (w <= 0 || h <= 0) return SkyLayout::Unknown;
    if (w == 2 * h) return SkyLayout::Panorama;
    if (w * 3 == h * 4 && w % 4 == 0) return SkyLayout::Cross;
    return SkyLayout::Unknown;
}

int skyRowsToKeep(const unsigned char* rgb, int w, int h, double marginDeg, unsigned char below[3], int tolerance) {
    if (!rgb || w <= 0 || h <= 1) return h;
    const unsigned char* last = rgb + (size_t)(h - 1) * w * 3;
    for (int c = 0; c < 3; ++c) below[c] = last[c];
    auto plainRow = [&](int r) {
        const unsigned char* p = rgb + (size_t)r * w * 3;
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c)
                if (std::abs((int)p[3 * x + c] - (int)below[c]) > tolerance) return false;
        return true;
    };
    int plainFrom = h; // the first row of the plain block at the bottom
    while (plainFrom > 0 && plainRow(plainFrom - 1)) --plainFrom;
    const int margin = (int)std::ceil(h * marginDeg / 180.0);
    const int keep = std::min(h, std::max(plainFrom + margin, (h + 1) / 2 + margin));
    if (h - keep < h / 20) return h; // not worth it
    return keep;
}

std::vector<unsigned char> crossToPanorama(const unsigned char* rgb, int w, int h, int outW) {
    const int F = w / 4, outH = outW / 2;
    std::vector<unsigned char> out((size_t)outW * outH * 3, 0);
    if (!rgb || F <= 0 || h < 3 * F || outW < 2) return out;
    // Face origins in the cross (column, row); cube directions in GL style (y up):
    // +X right, -X left, +Y up, -Y down, +Z front, -Z back.
    auto sample = [&](double gx, double gy, double gz, unsigned char* px) {
        const double ax = std::fabs(gx), ay = std::fabs(gy), az = std::fabs(gz);
        int col, row;
        double sc, tc, ma;
        if (ax >= ay && ax >= az) {
            ma = ax; tc = -gy;
            if (gx > 0) { col = 2; row = 1; sc = -gz; } else { col = 0; row = 1; sc = gz; }
        } else if (ay >= az) {
            ma = ay; sc = gx;
            if (gy > 0) { col = 1; row = 0; tc = gz; } else { col = 1; row = 2; tc = -gz; }
        } else {
            ma = az; tc = -gy;
            if (gz > 0) { col = 1; row = 1; sc = gx; } else { col = 3; row = 1; sc = -gx; }
        }
        // Bilinear inside the face (clamped to it, so faces don't bleed into the black).
        const double fu = std::clamp((sc / ma + 1) * 0.5 * F - 0.5, 0.0, F - 1.0);
        const double fv = std::clamp((tc / ma + 1) * 0.5 * F - 0.5, 0.0, F - 1.0);
        const int x0 = (int)fu, y0 = (int)fv, x1 = std::min(x0 + 1, F - 1), y1 = std::min(y0 + 1, F - 1);
        const double ax0 = fu - x0, ay0 = fv - y0;
        auto at = [&](int x, int y, int c) {
            return (double)rgb[((size_t)(row * F + y) * w + (col * F + x)) * 3 + c];
        };
        for (int c = 0; c < 3; ++c) {
            const double v = (at(x0, y0, c) * (1 - ax0) + at(x1, y0, c) * ax0) * (1 - ay0) +
                             (at(x0, y1, c) * (1 - ax0) + at(x1, y1, c) * ax0) * ay0;
            px[c] = (unsigned char)std::clamp((int)std::lround(v), 0, 255);
        }
    };
    for (int j = 0; j < outH; ++j) {
        const double lat = (0.5 - (j + 0.5) / outH) * kPi;
        for (int i = 0; i < outW; ++i) {
            // Measured against a pack's matching panorama/cross pair: the middle column
            // looks at the cross's front face, and right in one is right in the other.
            const double lon = kPi - ((i + 0.5) / outW - 0.5) * 2 * kPi;
            sample(std::cos(lat) * std::sin(lon), std::sin(lat), -std::cos(lat) * std::cos(lon),
                   &out[((size_t)j * outW + i) * 3]);
        }
    }
    return out;
}

void skyUV(const Vec3& dir, double& u, double& v) {
    const Vec3 d = normalize(dir);
    const double yaw = std::atan2(d.y, d.x);
    u = 0.5 - yaw / (2 * kPi);
    u -= std::floor(u);
    v = 0.5 - std::asin(std::clamp(d.z, -1.0, 1.0)) / kPi;
}

} // namespace scal
