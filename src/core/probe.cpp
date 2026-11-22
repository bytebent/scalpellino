#include "core/probe.hpp"
#include <algorithm>
#include <cmath>

namespace scal {

int probeSide(int size) {
    if (size <= 0) return kProbeDefaultSide;
    return 1 << (std::min(size, kProbeMaxSize) - 1);
}

std::string probeSizeLabel(int size) {
    const std::string s = std::to_string(probeSide(size));
    if (size <= 0) return "Default (" + s + " x " + s + ")";
    return s + " x " + s;
}

int probeSideForQuality(int size, int quality) {
    int s = probeSide(size);
    if (quality <= 0) s /= 2;
    else if (quality >= 2) s *= 2;
    return std::clamp(s, 4, 512);
}

Vec3 probePlacement(const Vec3& hit, const Vec3& normal) {
    const Vec3 n = normalize(normal);
    if (n.z > 0.7) return hit + Vec3{0, 0, kProbeFloorHeight};
    return hit + n * kProbeWallDistance;
}

std::vector<ProbeWeight> probeWeights(const std::vector<Vec3>& probes, const Vec3& p, const Vec3& n) {
    std::vector<ProbeWeight> all;
    all.reserve(probes.size());
    for (size_t i = 0; i < probes.size(); ++i) {
        const Vec3 v = probes[i] - p;
        double w = 1.0 / (dot(v, v) + kProbeSoft * kProbeSoft);
        if (dot(v, n) < 0) w *= kProbeBehind;
        all.push_back({i, w});
    }
    // largest first; equal weights: the lower index first (as the shader's insertion)
    std::stable_sort(all.begin(), all.end(), [](const ProbeWeight& a, const ProbeWeight& b) { return a.weight > b.weight; });
    const double fifth = all.size() > 4 ? all[4].weight : 0;
    if (all.size() > 4) all.resize(4);
    double sum = 0;
    for (ProbeWeight& w : all) sum += (w.weight -= fifth);
    if (sum <= 1e-30) { // as many probes as far away as the 5th: an even share
        for (ProbeWeight& w : all) w.weight = 1.0 / all.size();
        return all;
    }
    for (ProbeWeight& w : all) w.weight /= sum;
    return all;
}

int cubeFace(const Vec3& d, double& x, double& y) {
    const double ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
    int f;
    double sc, tc, ma;
    if (ax >= ay && ax >= az) {
        ma = ax;
        if (d.x >= 0) { f = 0; sc = -d.z; tc = -d.y; }
        else { f = 1; sc = d.z; tc = -d.y; }
    } else if (ay >= az) {
        ma = ay;
        if (d.y >= 0) { f = 2; sc = d.x; tc = d.z; }
        else { f = 3; sc = d.x; tc = -d.z; }
    } else {
        ma = az;
        if (d.z >= 0) { f = 4; sc = d.x; tc = -d.y; }
        else { f = 5; sc = -d.x; tc = -d.y; }
    }
    if (ma <= 0) { x = y = 0; return 4; }
    x = sc / ma;
    y = tc / ma;
    return f;
}

Vec3 cubeDir(int face, double x, double y) {
    switch (face) {
        case 0: return normalize(Vec3{1, -y, -x});
        case 1: return normalize(Vec3{-1, -y, x});
        case 2: return normalize(Vec3{x, 1, y});
        case 3: return normalize(Vec3{x, -1, -y});
        case 4: return normalize(Vec3{x, -y, 1});
        default: return normalize(Vec3{-x, -y, -1});
    }
}

} // namespace scal
