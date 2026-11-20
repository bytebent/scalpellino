#include "core/render_job.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace scal {

const char* renderQualityName(RenderQuality q) {
    switch (q) {
    case RenderQuality::Draft: return "Draft";
    case RenderQuality::Normal: return "Normal";
    case RenderQuality::High: return "High";
    }
    return "Normal";
}

void applyQuality(RenderSettings& s, RenderQuality q) {
    s.quality = q;
    s.aa = q == RenderQuality::Draft ? 1 : q == RenderQuality::Normal ? 2 : 3;
}

std::string renderSettingsToString(const RenderSettings& s) {
    std::ostringstream o;
    o << "quality " << (s.quality == RenderQuality::Draft ? "draft" : s.quality == RenderQuality::High ? "high" : "normal") << "\n";
    o << "aa " << s.aa << "\n";
    o << "filter " << (s.smooth ? "smooth" : "crisp") << "\n";
    return o.str();
}

RenderSettings renderSettingsFromString(const std::string& text) {
    RenderSettings s;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string k, v;
        if (!(l >> k >> v)) continue;
        if (k == "quality") {
            if (v == "draft") s.quality = RenderQuality::Draft;
            else if (v == "normal") s.quality = RenderQuality::Normal;
            else if (v == "high") s.quality = RenderQuality::High;
        } else if (k == "aa") {
            try {
                const int n = std::stoi(v);
                if (n >= 1 && n <= kMaxRenderAA) s.aa = n;
            } catch (...) {}
        } else if (k == "filter") {
            if (v == "smooth") s.smooth = true;
            else if (v == "crisp") s.smooth = false;
        }
    }
    return s;
}

int fitAA(int w, int h, int aa, int maxSide, long long maxPixels) {
    if (w < 1 || h < 1) return 0;
    for (int f = std::clamp(aa, 1, kMaxRenderAA); f >= 1; --f)
        if ((long long)w * f <= maxSide && (long long)h * f <= maxSide && (long long)w * f * h * f <= maxPixels) return f;
    return 0;
}

namespace {
// sRGB <-> linear light, exact formulas; a table for the 256 byte values.
const std::array<float, 256>& toLinearTable() {
    static const std::array<float, 256> t = [] {
        std::array<float, 256> a{};
        for (int i = 0; i < 256; ++i) {
            const double c = i / 255.0;
            a[i] = (float)(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
        }
        return a;
    }();
    return t;
}
unsigned char toByte(double lin) {
    lin = std::clamp(lin, 0.0, 1.0);
    const double c = lin <= 0.0031308 ? lin * 12.92 : 1.055 * std::pow(lin, 1 / 2.4) - 0.055;
    return (unsigned char)std::clamp((int)std::lround(c * 255), 0, 255);
}
} // namespace

std::vector<unsigned char> downscaleRGB(const std::vector<unsigned char>& rgba, int w, int h, int f) {
    if (f < 1 || w < f || h < f || (size_t)w * h * 4 > rgba.size()) return {};
    const int ow = w / f, oh = h / f;
    std::vector<unsigned char> out((size_t)ow * oh * 3);
    if (f == 1) {
        for (size_t i = 0, n = (size_t)ow * oh; i < n; ++i)
            for (int c = 0; c < 3; ++c) out[i * 3 + c] = rgba[i * 4 + c];
        return out;
    }
    const auto& lin = toLinearTable();
    const double inv = 1.0 / (f * f);
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x) {
            double sum[3] = {0, 0, 0};
            for (int j = 0; j < f; ++j) {
                const unsigned char* row = &rgba[((size_t)(y * f + j) * w + (size_t)x * f) * 4];
                for (int i = 0; i < f; ++i)
                    for (int c = 0; c < 3; ++c) sum[c] += lin[row[i * 4 + c]];
            }
            for (int c = 0; c < 3; ++c) out[((size_t)y * ow + x) * 3 + c] = toByte(sum[c] * inv);
        }
    return out;
}

std::string cleanFileName(const std::string& raw) {
    std::string s;
    for (char c : raw) s += (std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    return s;
}

std::string renderFileStem(const std::string& mapPath, const std::string& cameraName, const std::tm& when) {
    std::string map = mapPath;
    const size_t slash = map.find_last_of("/\\");
    if (slash != std::string::npos) map = map.substr(slash + 1);
    const size_t dot = map.find_last_of('.');
    if (dot != std::string::npos && dot > 0) map = map.substr(0, dot);
    if (map.empty()) map = "untitled";
    std::string cam = cameraName.empty() ? "camera" : cameraName;
    char t[64];
    std::snprintf(t, sizeof t, "%04d-%02d-%02d_%02d-%02d-%02d", when.tm_year + 1900, when.tm_mon + 1, when.tm_mday, when.tm_hour,
                  when.tm_min, when.tm_sec);
    return cleanFileName(map) + "_" + cleanFileName(cam) + "_" + t;
}

} // namespace scal
