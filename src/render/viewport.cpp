#include "render/viewport.hpp"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace scal {

namespace {
// Hammer-like palette for 2D views.
const Color kBg2D      {  0,   0,   0, 255};
const Color kGridMinor { 44,  44,  52, 255};
const Color kGrid64    { 72,  72,  84, 255};
const Color kGrid1024  {110,  92,  56, 255};
const Color kAxis      {  0, 110, 110, 255};
const Color kBorder    {120,  40,  40, 255};
const Color kBrush2D   {205, 205, 205, 255};
const Color kSelected  {255,  64,  64, 255};
const Color kPreview   {255, 220,  80, 255};
const Color kHandle    {255, 255, 255, 255};
const Color kLabel     {255, 200, 200, 255};

const Color kBg3D      { 22,  22,  26, 255};
const Color kGround    { 48,  48,  56, 255};
const Color kGround1024{ 80,  70,  50, 255};

constexpr double kMinZoom = 0.02;  // px per unit
constexpr double kMaxZoom = 32.0;
constexpr double kMinLinePx = 4.0; // hide grid lines closer than this

// Grid lines are drawn as 1px rectangles: exact and identical on every driver.
inline void vline(double x, float y0, float y1, Color c) {
    DrawRectangle((int)std::floor(x), (int)std::floor(y0), 1, (int)std::ceil(y1 - y0), c);
}
inline void hline(double y, float x0, float x1, Color c) {
    DrawRectangle((int)std::floor(x0), (int)std::floor(y), (int)std::ceil(x1 - x0), 1, c);
}

Vector3 toRl(const Vec3& v) { return {(float)v.x, (float)v.y, (float)v.z}; }

// Stable pastel color per brush (flat-shaded 3D mode, like Hammer's "flat" view).
Color brushColor(BrushId id) {
    unsigned h = id * 2654435761u;
    const float hue = (float)(h % 360);
    return ColorFromHSV(hue, 0.35f, 0.85f);
}

Color shade(Color c, const Vec3& n) {
    const Vec3 l = normalize(Vec3{0.35, 0.55, 0.85});
    double k = 0.5 + 0.5 * std::max(0.0, dot(n, l));
    if (n.z < -0.5) k = 0.42; // undersides
    return {(unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), 255};
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

int Viewport::axU() const { return kind_ == ViewKind::Front ? 1 : 0; }
int Viewport::axV() const { return kind_ == ViewKind::Top ? 1 : 2; }
int Viewport::axD() const {
    switch (kind_) {
        case ViewKind::Top:   return 2;
        case ViewKind::Front: return 0;
        default:              return 1;
    }
}
const char* Viewport::axisU() const { static const char* n[] = {"X", "Y", "Z"}; return n[axU()]; }
const char* Viewport::axisV() const { static const char* n[] = {"X", "Y", "Z"}; return n[axV()]; }

void Viewport::resize(int w, int h) {
    w = std::max(w, 1);
    h = std::max(h, 1);
    if (w == w_ && h == h_ && rt_.id) return;
    if (rt_.id) UnloadRenderTexture(rt_);
    rt_ = LoadRenderTexture(w, h);
    w_ = w;
    h_ = h;
}

void Viewport::render(const Grid& grid, const Scene& scene) {
    if (!rt_.id) return;
    BeginTextureMode(rt_);
    if (is2D()) render2D(grid, scene); else render3D(grid, scene);
    EndTextureMode();
}

// ------------------------------------------------------------------ 2D

Vector2 Viewport::worldToLocal2D(double u, double v) const {
    return {(float)(w_ * 0.5 + (u - panU_) * zoom_), (float)(h_ * 0.5 - (v - panV_) * zoom_)};
}

void Viewport::render2D(const Grid& grid, const Scene& scene) {
    ClearBackground(kBg2D);

    const double hw = w_ * 0.5, hh = h_ * 0.5;
    auto sx = [&](double u) { return hw + (u - panU_) * zoom_; };
    auto sy = [&](double v) { return hh - (v - panV_) * zoom_; };

    // Visible world range, clamped to the world bounds.
    const double uMin = std::max(panU_ - hw / zoom_, -kWorldHalf);
    const double uMax = std::min(panU_ + hw / zoom_,  kWorldHalf);
    const double vMin = std::max(panV_ - hh / zoom_, -kWorldHalf);
    const double vMax = std::min(panV_ + hh / zoom_,  kWorldHalf);

    if (uMin < uMax && vMin < vMax) {
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
        if (uMin <= 0 && uMax >= 0) vline(sx(0), top, bottom, kAxis);
        if (vMin <= 0 && vMax >= 0) hline(sy(0), left, right, kAxis);
    }
    DrawRectangleLinesEx({(float)sx(-kWorldHalf), (float)sy(kWorldHalf),
                          (float)(2 * kWorldHalf * zoom_), (float)(2 * kWorldHalf * zoom_)},
                         1.0f, kBorder);

    // ---- brushes: edges of every face polygon, projected.
    const int U = axU(), V = axV();
    auto drawBrush = [&](const SceneBrush& b, Color c) {
        for (const Polygon& p : b.polys) {
            const size_t n = p.verts.size();
            for (size_t i = 0; i < n; ++i) {
                const Vec3& a = p.verts[i];
                const Vec3& e = p.verts[(i + 1) % n];
                DrawLineV(worldToLocal2D(a[U], a[V]), worldToLocal2D(e[U], e[V]), c);
            }
        }
    };
    for (const SceneBrush& b : scene.brushes)
        if (!scene.selected.count(b.id)) drawBrush(b, kBrush2D);
    if (!scene.hideSelected)
        for (const SceneBrush& b : scene.brushes)
            if (scene.selected.count(b.id)) drawBrush(b, kSelected);
    for (const SceneBrush& g : scene.ghosts) drawBrush(g, kSelected);

    // ---- block tool preview
    if (scene.previewBox) {
        const AABB& pb = *scene.previewBox;
        const Vector2 a = worldToLocal2D(pb.min[U], pb.max[V]);
        const Vector2 b = worldToLocal2D(pb.max[U], pb.min[V]);
        DrawRectangleLinesEx({a.x, a.y, b.x - a.x, b.y - a.y}, 1.0f, kPreview);
    }

    // ---- handles + size labels
    if (scene.handleBox) {
        const AABB& hb = *scene.handleBox;
        const Vector2 a = worldToLocal2D(hb.min[U], hb.max[V]); // top-left
        const Vector2 b = worldToLocal2D(hb.max[U], hb.min[V]); // bottom-right
        const float mx = (a.x + b.x) * 0.5f, my = (a.y + b.y) * 0.5f;
        const float hs = 3.0f;
        // Middle handles only when there is room for them (small boxes stay readable).
        const bool midU = (b.x - a.x) > 40.0f, midV = (b.y - a.y) > 40.0f;
        Vector2 pts[8];
        int n = 0;
        pts[n++] = {a.x, a.y}; pts[n++] = {b.x, a.y}; pts[n++] = {a.x, b.y}; pts[n++] = {b.x, b.y};
        if (midU) { pts[n++] = {mx, a.y}; pts[n++] = {mx, b.y}; }
        if (midV) { pts[n++] = {a.x, my}; pts[n++] = {b.x, my}; }
        for (int i = 0; i < n; ++i) {
            const Vector2 p = pts[i];
            DrawRectangle((int)(p.x - hs - 1), (int)(p.y - hs - 1), (int)(hs * 2 + 3), (int)(hs * 2 + 3), BLACK);
            DrawRectangle((int)(p.x - hs), (int)(p.y - hs), (int)(hs * 2 + 1), (int)(hs * 2 + 1), kHandle);
        }
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", hb.max[U] - hb.min[U]);
        DrawText(buf, (int)(mx - MeasureText(buf, 10) / 2), (int)(a.y - 16), 10, kLabel);
        std::snprintf(buf, sizeof buf, "%g", hb.max[V] - hb.min[V]);
        DrawText(buf, (int)(b.x + 8), (int)(my - 5), 10, kLabel);
    }
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

Camera3D Viewport::camera() const {
    Camera3D cam{};
    cam.position = pos_;
    const Vector3 f = forwardFrom(yaw_, pitch_);
    cam.target = {pos_.x + f.x, pos_.y + f.y, pos_.z + f.z};
    cam.up = {0, 0, 1};
    cam.fovy = 70.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    return cam;
}

void Viewport::pickRay(Vector2 local, Vec3& origin, Vec3& dir) const {
    rlSetClipPlanes(2.0, 32768.0);
    const Ray r = GetScreenToWorldRayEx(local, camera(), w_, h_);
    origin = {r.position.x, r.position.y, r.position.z};
    dir = normalize(Vec3{r.direction.x, r.direction.y, r.direction.z});
}

void Viewport::render3D(const Grid& grid, const Scene& scene) {
    ClearBackground(kBg3D);
    rlSetClipPlanes(2.0, 32768.0);
    BeginMode3D(camera());

    if (grid.visible) {
        const float ext = 2048.0f;
        for (float t = -ext; t <= ext; t += 64.0f) {
            Color c = (std::fmod(std::fabs(t), 1024.0f) == 0.0f) ? kGround1024 : kGround;
            DrawLine3D({t, -ext, 0}, {t, ext, 0}, c);
            DrawLine3D({-ext, t, 0}, {ext, t, 0}, c);
        }
    }
    DrawLine3D({0, 0, 0}, {256, 0, 0}, {220, 70, 70, 255});
    DrawLine3D({0, 0, 0}, {0, 256, 0}, {70, 200, 70, 255});
    DrawLine3D({0, 0, 0}, {0, 0, 256}, {80, 120, 230, 255});

    // ---- flat-shaded brushes
    auto drawSolid = [&](const SceneBrush& b, Color base) {
        for (const Polygon& p : b.polys) {
            const Color c = shade(base, p.normal);
            for (size_t i = 1; i + 1 < p.verts.size(); ++i)
                DrawTriangle3D(toRl(p.verts[0]), toRl(p.verts[i]), toRl(p.verts[i + 1]), c);
        }
    };
    auto drawEdges = [&](const SceneBrush& b, Color c) {
        for (const Polygon& p : b.polys)
            for (size_t i = 0; i < p.verts.size(); ++i)
                DrawLine3D(toRl(p.verts[i]), toRl(p.verts[(i + 1) % p.verts.size()]), c);
    };
    const Color selTint{235, 110, 110, 255};
    for (const SceneBrush& b : scene.brushes) {
        const bool sel = scene.selected.count(b.id) > 0;
        if (sel && scene.hideSelected) continue;
        drawSolid(b, sel ? selTint : brushColor(b.id));
    }
    for (const SceneBrush& g : scene.ghosts) drawSolid(g, selTint);

    // Selection outlines on top (visible through other brushes, like Hammer).
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    if (!scene.hideSelected)
        for (const SceneBrush& b : scene.brushes)
            if (scene.selected.count(b.id)) drawEdges(b, kSelected);
    for (const SceneBrush& g : scene.ghosts) drawEdges(g, kSelected);
    if (scene.previewBox) {
        const AABB& pb = *scene.previewBox;
        const Vec3 c = pb.center(), s = pb.size();
        DrawCubeWires(toRl(c), (float)s.x, (float)s.y, (float)s.z, kPreview);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthTest();

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
    Vector3 r{f.y, -f.x, 0.0f};
    const float rl = std::sqrt(r.x * r.x + r.y * r.y);
    if (rl > 1e-5f) { r.x /= rl; r.y /= rl; }
    pos_.x += (f.x * fwd + r.x * rgt) * speed;
    pos_.y += (f.y * fwd + r.y * rgt) * speed;
    pos_.z += (f.z * fwd + up) * speed;
}

} // namespace scal
