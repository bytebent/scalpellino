#include "render/probes.hpp"
#include "core/camera.hpp"
#include "render/model_library.hpp"
#include "render/texture_library.hpp"
#include "raylib.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace scal {

namespace {
constexpr double kPi = 3.14159265358979323846;

float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (std::uint32_t)(h & 0x8000) << 16;
    std::uint32_t exp = (h >> 10) & 0x1f, man = h & 0x3ff, bits;
    if (exp == 0) {
        if (man == 0) bits = sign;
        else { // subnormal
            exp = 127 - 15 + 1;
            while (!(man & 0x400)) { man <<= 1; --exp; }
            man &= 0x3ff;
            bits = sign | (exp << 23) | (man << 13);
        }
    } else if (exp == 31) bits = sign | 0x7f800000 | (man << 13);
    else bits = sign | ((exp - 15 + 127) << 23) | (man << 13);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

std::uint16_t floatToHalf(float f) {
    if (!(f == f)) f = 0;                 // NaN
    f = std::clamp(f, -65000.0f, 65000.0f);
    std::uint32_t x;
    std::memcpy(&x, &f, 4);
    const std::uint16_t sign = (std::uint16_t)((x >> 16) & 0x8000);
    const int exp = (int)((x >> 23) & 0xff) - 127 + 15;
    std::uint32_t man = x & 0x7fffff;
    if (exp <= 0) {
        if (exp < -10) return sign;
        man |= 0x800000;
        const int shift = 14 - exp;
        std::uint16_t h = (std::uint16_t)(man >> shift);
        if ((man >> (shift - 1)) & 1) ++h; // round
        return sign | h;
    }
    std::uint16_t h = (std::uint16_t)(sign | (exp << 10) | (man >> 13));
    if (man & 0x1000) ++h; // round (may carry into the exponent: still right)
    return h;
}

// A float RGB picture.
struct Img {
    int w = 0, h = 0;
    std::vector<float> px; // rgb
    const float* at(int x, int y) const { return &px[((size_t)y * w + x) * 3]; }
    // Bilinear at pixel coordinates (centres at +0.5), clamped at the edges.
    void sample(double fx, double fy, float out[3]) const {
        fx -= 0.5;
        fy -= 0.5;
        const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        const double tx = fx - x0, ty = fy - y0;
        auto cl = [](int v, int n) { return std::clamp(v, 0, n - 1); };
        const float *a = at(cl(x0, w), cl(y0, h)), *b = at(cl(x0 + 1, w), cl(y0, h)), *c = at(cl(x0, w), cl(y0 + 1, h)),
                    *d = at(cl(x0 + 1, w), cl(y0 + 1, h));
        for (int k = 0; k < 3; ++k)
            out[k] = (float)((a[k] * (1 - tx) + b[k] * tx) * (1 - ty) + (c[k] * (1 - tx) + d[k] * tx) * ty);
    }
    Img half() const { // 2 x 2 average (odd sizes: the last row / column counted once more)
        Img o;
        o.w = std::max(1, w / 2);
        o.h = std::max(1, h / 2);
        o.px.assign((size_t)o.w * o.h * 3, 0);
        for (int y = 0; y < o.h; ++y)
            for (int x = 0; x < o.w; ++x)
                for (int k = 0; k < 3; ++k) {
                    const int x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1), y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
                    o.px[((size_t)y * o.w + x) * 3 + k] = 0.25f * (at(x0, y0)[k] + at(x1, y0)[k] + at(x0, y1)[k] + at(x1, y1)[k]);
                }
        return o;
    }
};

// The unnormalised direction of cubeDir (so x, y step evenly across the picture).
Vec3 faceAxis(int face, double x, double y) {
    switch (face) {
        case 0: return {1, -y, -x};
        case 1: return {-1, -y, x};
        case 2: return {x, 1, y};
        case 3: return {x, -1, -y};
        case 4: return {x, -y, 1};
        default: return {-x, -y, -1};
    }
}

} // namespace

ProbeAtlas::~ProbeAtlas() {
    if (tex) rlUnloadTexture(tex);
    if (data) rlUnloadTexture(data);
}

int probeSlotSize(int side) { return side + 2 * std::max(2, side / 4); }

const char* probeLookGlsl() {
    return R"(
vec3 probeLook(sampler2D atlas, vec2 atlasSize, vec4 tile, vec3 d, float rate) {
    vec3 a = abs(d);
    int f;
    float sc, tc, ma;
    if (a.x >= a.y && a.x >= a.z) { ma = a.x; if (d.x >= 0.0) { f = 0; sc = -d.z; tc = -d.y; } else { f = 1; sc = d.z; tc = -d.y; } }
    else if (a.y >= a.z) { ma = a.y; if (d.y >= 0.0) { f = 2; sc = d.x; tc = d.z; } else { f = 3; sc = d.x; tc = -d.z; } }
    else { ma = a.z; if (d.z >= 0.0) { f = 4; sc = d.x; tc = -d.y; } else { f = 5; sc = -d.x; tc = -d.y; } }
    ma = max(ma, 1e-6);
    float S = tile.z, T = tile.w;
    vec2 p = tile.xy + vec2(float(f % 3), float(f / 3)) * T + T * 0.5 + vec2(sc, tc) / ma * S * 0.5;
    float border = max((T - S) * 0.5, 1.0);
    float lod = clamp(log2(max(rate * S * 0.5, 1e-6)), 0.0, floor(log2(border)));
    return textureLod(atlas, p / atlasSize, lod).rgb;
}
)";
}

std::shared_ptr<ProbeAtlas> ProbeBuilder::pass(const std::vector<Probe>& probes, int quality, const Scene& sceneIn, bool smooth) {
    if (probes.empty()) return nullptr;
    if (!view_) view_ = std::make_unique<Viewport>(ViewKind::Persp);
    const int ss = quality <= 0 ? 1 : 2; // drawn this many times larger, then averaged
    Scene scene = sceneIn;
    scene.linearOut = true;
    scene.exposure = 1;
    // The picture's own texture filtering (as Renderer::draw).
    const bool texWas = scene.textures ? scene.textures->smooth() : smooth;
    const bool modelWas = scene.models ? scene.models->smooth() : smooth;
    if (scene.textures && texWas != smooth) scene.textures->setSmooth(smooth);
    if (scene.models && modelWas != smooth) scene.models->setSmooth(smooth);

    auto atlas = std::make_shared<ProbeAtlas>();
    // Lay out the tiles: rows of at most 4096 pixels (wider tiles get a row of their own).
    std::vector<size_t> order(probes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::vector<int> sides(probes.size()), slots(probes.size());
    int maxBorderLog = 0;
    for (size_t i = 0; i < probes.size(); ++i) {
        sides[i] = probeSideForQuality(probes[i].size, quality);
        slots[i] = probeSlotSize(sides[i]);
        const int border = (slots[i] - sides[i]) / 2;
        maxBorderLog = std::max(maxBorderLog, (int)std::floor(std::log2((double)border)));
    }
    atlas->levels = std::clamp(maxBorderLog + 1, 1, 6);
    const int gap = 1 << (atlas->levels - 1); // tiles stay apart at the smallest copy
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return slots[a] > slots[b]; });
    atlas->slots.resize(probes.size());
    int x = 0, y = 0, rowH = 0, width = 0;
    for (size_t i : order) {
        const int tw = 3 * slots[i], th = 2 * slots[i];
        if (x > 0 && x + tw > 4096) {
            x = 0;
            y += rowH + gap;
            rowH = 0;
        }
        ProbeAtlas::Slot& s = atlas->slots[i];
        s.pos = probes[i].pos;
        s.x = x;
        s.y = y;
        s.side = sides[i];
        s.slot = slots[i];
        x += tw + gap;
        rowH = std::max(rowH, th);
        width = std::max(width, x);
    }
    const int lv = atlas->levels;
    auto roundUp = [&](int v) { return ((v + (1 << (lv - 1)) - 1) >> (lv - 1)) << (lv - 1); };
    atlas->w = roundUp(std::max(width, 1));
    atlas->h = roundUp(y + rowH);
    if ((long long)atlas->w * atlas->h > 64LL * 1024 * 1024) return nullptr; // far too large: the caller says so

    // The atlas's levels on the processor (RGB floats), filled face by face.
    std::vector<std::vector<float>> levels(lv);
    for (int l = 0; l < lv; ++l) levels[l].assign((size_t)std::max(1, atlas->w >> l) * std::max(1, atlas->h >> l) * 3, 0.0f);

    Grid g;
    g.visible = false;
    for (size_t i = 0; i < probes.size(); ++i) {
        const double t0 = GetTime();
        ProbeAtlas::Slot& s = atlas->slots[i];
        const int S = s.side;
        const int N = S + 2;                 // the face with a 1-pixel margin
        const double k = (double)N / S;      // the margin's reach in tan units
        const int px = N * ss;
        // a float target of this size
        view_->setFloatTarget(true);
        view_->resize(px, px, 1.0f);
        if (!view_->target().id) return nullptr;
        std::array<std::vector<Img>, 6> faces; // each face's picture and its smaller copies
        for (int f = 0; f < 6; ++f) {
            Camera c;
            c.pos = s.pos;
            c.width = c.height = px;
            c.fov = 2 * std::atan(k) * 180 / kPi;
            c.nearDist = 1;
            c.farDist = 100000;
            const Vec3 F = normalize(faceAxis(f, 0, 0));
            const Vec3 R = faceAxis(f, 1, 0) - faceAxis(f, 0, 0);
            const Vec3 D = faceAxis(f, 0, 1) - faceAxis(f, 0, 0);
            Mat3 m;
            m.col = {F, R * -1.0, D * -1.0}; // forward, the picture's left, its up
            setCameraBasis(c, m);
            view_->setLens(c);
            view_->render(g, scene);
            // read it back (half floats, bottom row first) and average ss x ss
            const RenderTexture2D& rt = view_->target();
            auto* raw = (std::uint16_t*)rlReadTexturePixels(rt.texture.id, rt.texture.width, rt.texture.height, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16);
            if (!raw) return nullptr;
            Img img;
            img.w = img.h = N;
            img.px.assign((size_t)N * N * 3, 0);
            const int W = rt.texture.width;
            for (int yy = 0; yy < N; ++yy)
                for (int xx = 0; xx < N; ++xx)
                    for (int a = 0; a < ss; ++a)
                        for (int b = 0; b < ss; ++b) {
                            const int sx = xx * ss + b, sy = (N - 1 - yy) * ss + (ss - 1 - a); // flip: top row first
                            const std::uint16_t* p = &raw[((size_t)sy * W + sx) * 4];
                            for (int ch = 0; ch < 3; ++ch) img.px[((size_t)yy * N + xx) * 3 + ch] += halfToFloat(p[ch]) / (ss * ss);
                        }
            RL_FREE(raw);
            faces[f].push_back(std::move(img));
            for (int l = 1; l < lv; ++l) faces[f].push_back(faces[f].back().half());
        }
        // A direction's colour at level l: its face's picture (margin included).
        auto look = [&](const Vec3& d, int l, float out[3]) {
            double fx, fy;
            const int f = cubeFace(d, fx, fy);
            const Img& im = faces[f][l];
            im.sample((fx / k + 1) * 0.5 * im.w, (fy / k + 1) * 0.5 * im.h, out);
        };
        // Fill this probe's 6 slots at every level: each pixel's centre, as a level-0 point,
        // gives its place on the face (border included) and so its direction.
        for (int l = 0; l < lv; ++l) {
            const int lw = std::max(1, atlas->w >> l), lh = std::max(1, atlas->h >> l);
            const double scale = (double)(1 << l);
            for (int f = 0; f < 6; ++f) {
                const double ox = s.x + (f % 3) * s.slot, oy = s.y + (f / 3) * s.slot;
                const int x0 = (int)std::floor(ox / scale), x1 = (int)std::ceil((ox + s.slot) / scale);
                const int y0 = (int)std::floor(oy / scale), y1 = (int)std::ceil((oy + s.slot) / scale);
                for (int ty = std::max(0, y0); ty < std::min(lh, y1); ++ty)
                    for (int tx = std::max(0, x0); tx < std::min(lw, x1); ++tx) {
                        const double cx = (tx + 0.5) * scale - ox, cy = (ty + 0.5) * scale - oy;
                        if (cx < 0 || cy < 0 || cx > s.slot || cy > s.slot) continue;
                        const double fx = (cx - s.slot * 0.5) / (S * 0.5), fy = (cy - s.slot * 0.5) / (S * 0.5);
                        float c[3];
                        look(cubeDir(f, fx, fy), l, c);
                        float* o = &levels[l][((size_t)ty * lw + tx) * 3];
                        o[0] = c[0];
                        o[1] = c[1];
                        o[2] = c[2];
                    }
            }
        }
        s.seconds = GetTime() - t0;
    }
    if (scene.textures && texWas != smooth) scene.textures->setSmooth(texWas);
    if (scene.models && modelWas != smooth) scene.models->setSmooth(modelWas);

    // Upload: half floats, every level one after another (as rlLoadTexture reads them).
    std::vector<std::uint16_t> half;
    for (int l = 0; l < lv; ++l)
        for (float v : levels[l]) half.push_back(floatToHalf(v));
    atlas->tex = rlLoadTexture(half.data(), atlas->w, atlas->h, PIXELFORMAT_UNCOMPRESSED_R16G16B16, lv);
    if (!atlas->tex) return nullptr;
    rlTextureParameters(atlas->tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
    rlTextureParameters(atlas->tex, RL_TEXTURE_MIN_FILTER, lv > 1 ? RL_TEXTURE_FILTER_MIP_LINEAR : RL_TEXTURE_FILTER_LINEAR);
    rlTextureParameters(atlas->tex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
    rlTextureParameters(atlas->tex, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
    std::vector<float> data;
    for (const ProbeAtlas::Slot& s : atlas->slots) {
        for (float v : {(float)s.pos.x, (float)s.pos.y, (float)s.pos.z, (float)s.side}) data.push_back(v);
        for (float v : {(float)s.x, (float)s.y, (float)s.side, (float)s.slot}) data.push_back(v);
    }
    atlas->data = rlLoadTexture(data.data(), 2, (int)atlas->slots.size(), PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
    if (atlas->data) {
        rlTextureParameters(atlas->data, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
        rlTextureParameters(atlas->data, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    }
    return atlas;
}

} // namespace scal
