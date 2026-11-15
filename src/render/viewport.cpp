#include "render/viewport.hpp"
#include "rlgl.h"
#include <algorithm>
#include <cmath>

namespace scal {

namespace {
// Hammer-like palette for 2D views.
const Color kBg2D      {  0,   0,   0, 255};
const Color kGridMinor { 44,  44,  52, 255};
const Color kGrid64    { 72,  72,  84, 255};
const Color kGrid1024  {110,  92,  56, 255};
const Color kAxis      {  0, 110, 110, 255};
const Color kBorder    {120,  40,  40, 255};

const Color kBg3D      { 22,  22,  26, 255};
const Color kGround    { 48,  48,  56, 255};
const Color kGround1024{ 80,  70,  50, 255};

constexpr double kMinZoom = 0.02;  // px per unit
constexpr double kMaxZoom = 32.0;
constexpr double kMinLinePx = 4.0; // hide grid lines closer than this

// Grid lines are drawn as 1px rectangles: GL line rasterization differs between
// drivers (some drop lines entirely), rectangles are exact everywhere.
inline void vline(double x, float y0, float y1, Color c) {
    DrawRectangle((int)std::floor(x), (int)std::floor(y0), 1, (int)std::ceil(y1 - y0), c);
}
inline void hline(double y, float x0, float x1, Color c) {
    DrawRectangle((int)std::floor(x0), (int)std::floor(y), (int)std::ceil(x1 - x0), 1, c);
}
} // namespace

Viewport::Viewport(ViewKind kind) : kind_(kind) {}

Viewport::~Viewport() {
    if (rt_.id) UnloadRenderTexture(rt_);
}

const char* Viewport::title() const {
    switch (kind_) {
        case ViewKind::Persp: return "3D###view3d";
        case ViewKind::Top:   return "Top###viewTop";
        case ViewKind::Front: return "Front###viewFront";
        case ViewKind::Side:  return "Side###viewSide";
    }
    return "?";
}

const char* Viewport::label() const {
    switch (kind_) {
        case ViewKind::Persp: return "3d";
        case ViewKind::Top:   return "top (x/y)";
        case ViewKind::Front: return "front (y/z)";
        case ViewKind::Side:  return "side (x/z)";
    }
    return "";
}

const char* Viewport::axisU() const {
    return kind_ == ViewKind::Front ? "Y" : "X";
}
const char* Viewport::axisV() const {
    return kind_ == ViewKind::Top ? "Y" : "Z";
}

void Viewport::resize(int w, int h) {
    w = std::max(w, 1);
    h = std::max(h, 1);
    if (w == w_ && h == h_ && rt_.id) return;
    if (rt_.id) UnloadRenderTexture(rt_);
    rt_ = LoadRenderTexture(w, h);
    w_ = w;
    h_ = h;
}

void Viewport::render(const Grid& grid) {
    if (!rt_.id) return;
    BeginTextureMode(rt_);
    if (is2D()) render2D(grid); else render3D(grid);
    EndTextureMode();
}

// ------------------------------------------------------------------ 2D

void Viewport::render2D(const Grid& grid) {
    ClearBackground(kBg2D);

    const double hw = w_ * 0.5, hh = h_ * 0.5;
    auto sx = [&](double u) { return hw + (u - panU_) * zoom_; };
    auto sy = [&](double v) { return hh - (v - panV_) * zoom_; };

    // Visible world range, clamped to the world bounds.
    const double uMin = std::max(panU_ - hw / zoom_, -kWorldHalf);
    const double uMax = std::min(panU_ + hw / zoom_,  kWorldHalf);
    const double vMin = std::max(panV_ - hh / zoom_, -kWorldHalf);
    const double vMax = std::min(panV_ + hh / zoom_,  kWorldHalf);
    if (uMin >= uMax || vMin >= vMax) return;

    const float top = (float)sy(vMax), bottom = (float)sy(vMin);
    const float left = (float)sx(uMin), right = (float)sx(uMax);

    auto drawLines = [&](double step, Color c, double skipMultiple) {
        if (step * zoom_ < kMinLinePx) return;
        for (double u = std::ceil(uMin / step) * step; u <= uMax; u += step) {
            if (skipMultiple > 0 && std::fmod(std::fabs(u), skipMultiple) == 0.0) continue;
            vline(sx(u), top, bottom, c);
        }
        for (double v = std::ceil(vMin / step) * step; v <= vMax; v += step) {
            if (skipMultiple > 0 && std::fmod(std::fabs(v), skipMultiple) == 0.0) continue;
            hline(sy(v), left, right, c);
        }
    };

    if (grid.visible) {
        if (grid.size < 64) drawLines(grid.size, kGridMinor, 64.0);
        drawLines(64.0,   kGrid64,   1024.0);
        drawLines(1024.0, kGrid1024, 0.0);
    }

    // Axes through the origin.
    if (uMin <= 0 && uMax >= 0) vline(sx(0), top, bottom, kAxis);
    if (vMin <= 0 && vMax >= 0) hline(sy(0), left, right, kAxis);

    // World border.
    DrawRectangleLinesEx({(float)sx(-kWorldHalf), (float)sy(kWorldHalf),
                          (float)(2 * kWorldHalf * zoom_), (float)(2 * kWorldHalf * zoom_)},
                         1.0f, kBorder);
}

void Viewport::pan2D(Vector2 d) {
    panU_ -= d.x / zoom_;
    panV_ += d.y / zoom_;
    panU_ = std::clamp(panU_, -kWorldHalf, kWorldHalf);
    panV_ = std::clamp(panV_, -kWorldHalf, kWorldHalf);
}

void Viewport::zoom2D(float wheel, Vector2 local) {
    if (wheel == 0.0f) return;
    double u0, v0;
    screenToWorld2D(local, u0, v0);
    zoom_ = std::clamp(zoom_ * std::pow(1.2, (double)wheel), kMinZoom, kMaxZoom);
    // Keep the point under the cursor fixed.
    double u1, v1;
    screenToWorld2D(local, u1, v1);
    panU_ += u0 - u1;
    panV_ += v0 - v1;
}

void Viewport::screenToWorld2D(Vector2 p, double& u, double& v) const {
    u = panU_ + (p.x - w_ * 0.5) / zoom_;
    v = panV_ - (p.y - h_ * 0.5) / zoom_;
}

// ------------------------------------------------------------------ 3D

static Vector3 forwardFrom(float yawDeg, float pitchDeg) {
    const float y = yawDeg * DEG2RAD, p = pitchDeg * DEG2RAD;
    return {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
}

void Viewport::render3D(const Grid& grid) {
    ClearBackground(kBg3D);

    Camera3D cam{};
    cam.position = pos_;
    const Vector3 f = forwardFrom(yaw_, pitch_);
    cam.target = {pos_.x + f.x, pos_.y + f.y, pos_.z + f.z};
    cam.up = {0, 0, 1};
    cam.fovy = 70.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    rlSetClipPlanes(2.0, 32768.0);
    BeginMode3D(cam);

    // Ground grid on z = 0 (64-unit cells, 1024 lines highlighted).
    if (grid.visible) {
        const float ext = 2048.0f;
        for (float t = -ext; t <= ext; t += 64.0f) {
            Color c = (std::fmod(std::fabs(t), 1024.0f) == 0.0f) ? kGround1024 : kGround;
            DrawLine3D({t, -ext, 0}, {t, ext, 0}, c);
            DrawLine3D({-ext, t, 0}, {ext, t, 0}, c);
        }
    }
    // Axes: X red, Y green, Z blue.
    DrawLine3D({0, 0, 0}, {256, 0, 0}, {220, 70, 70, 255});
    DrawLine3D({0, 0, 0}, {0, 256, 0}, {70, 200, 70, 255});
    DrawLine3D({0, 0, 0}, {0, 0, 256}, {80, 120, 230, 255});

    EndMode3D();
}

void Viewport::look(Vector2 d) {
    yaw_   -= d.x * 0.15f;
    pitch_ -= d.y * 0.15f;
    pitch_ = std::clamp(pitch_, -89.0f, 89.0f);
    yaw_ = std::fmod(yaw_, 360.0f);
}

void Viewport::fly(float fwd, float rgt, float up, float dt, bool fast) {
    const float speed = (fast ? 1600.0f : 500.0f) * dt;
    const Vector3 f = forwardFrom(yaw_, pitch_);
    // right = forward x up(0,0,1), flattened.
    Vector3 r{f.y, -f.x, 0.0f};
    const float rl = std::sqrt(r.x * r.x + r.y * r.y);
    if (rl > 1e-5f) { r.x /= rl; r.y /= rl; }
    pos_.x += (f.x * fwd + r.x * rgt) * speed;
    pos_.y += (f.y * fwd + r.y * rgt) * speed;
    pos_.z += (f.z * fwd + up) * speed;
}

} // namespace scal
