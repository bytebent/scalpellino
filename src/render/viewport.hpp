#pragma once
// One editor viewport: a 2D orthographic view (top/front/side) or the 3D view.
// Owns its render texture; draws only when asked.
#include "raylib.h"
#include "core/grid.hpp"
#include "core/math.hpp"
#include "render/scene.hpp"
#include <optional>

namespace scal {

enum class ViewKind { Persp, Top, Front, Side };

class Viewport {
public:
    explicit Viewport(ViewKind kind);
    ~Viewport();
    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;

    ViewKind kind() const { return kind_; }
    bool is2D() const { return kind_ != ViewKind::Persp; }
    const char* title() const;      // window title (unique ImGui id)
    const char* label() const;      // short overlay label, e.g. "top (x/y)"
    const char* axisU() const;      // horizontal axis name for 2D views
    const char* axisV() const;      // vertical axis name for 2D views

    // World axis index (0=x, 1=y, 2=z) shown horizontally / vertically / along depth.
    int axU() const;
    int axV() const;
    int axD() const;

    // Keeps the render texture matching the on-screen size. w/h are in logical (point)
    // units; `scale` is the display's pixels per point (2 on Retina). The texture is
    // w*scale x h*scale so drawing is 1:1 with real screen pixels.
    void resize(int w, int h, float scale);
    float scale() const { return scale_; }
    const RenderTexture2D& target() const { return rt_; }
    int width()  const { return w_; }
    int height() const { return h_; }

    void render(const Grid& grid, const Scene& scene);

    // Camera Preview (v0.11.1): look through a camera object instead of the fly camera
    // (its lens, near / far, orthographic) and draw only what a picture shows: no grid,
    // axes, markers or editor outlines. nullopt: the editor's own 3D camera.
    void setLens(const std::optional<scal::Camera>& c) { lens_ = c; }

    // --- 2D navigation and mapping
    void pan2D(Vector2 deltaPx);
    void center2D(double u, double v) { panU_ = u; panV_ = v; }
    void setZoom2D(double pxPerUnit) { zoom_ = pxPerUnit; }
    // Back to the start: 2D views centred on the origin at the default zoom; the 3D
    // camera at its default spot looking at the origin.
    void resetToOrigin();
    // 3D: keep the viewing direction, move back from `target` by `distance`.
    void lookAt(const Vec3& target, double distance);
    // 3D camera position and direction (degrees), e.g. for scripted screenshots.
    void setCamera(const Vec3& pos, float yaw, float pitch) {
        pos_ = {(float)pos.x, (float)pos.y, (float)pos.z}; yaw_ = yaw; pitch_ = pitch;
    }
    void zoom2D(float wheel, Vector2 localPx);
    void screenToWorld2D(Vector2 localPx, double& u, double& v) const;
    Vector2 worldToLocal2D(double u, double v) const;
    Vector2 worldToLocal2D(const Vec3& p) const { return worldToLocal2D(p[axU()], p[axV()]); }
    double zoom() const { return zoom_; }

    // --- 3D navigation and picking
    void look(Vector2 deltaPx);
    void fly(float forward, float right, float up, float dt, bool fast);
    Vector3 cameraPos() const { return pos_; }
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
    Camera3D camera() const;
    void pickRay(Vector2 localPx, Vec3& origin, Vec3& dir) const;
    // Where a world point appears in the 3D view (logical pixels); false if it is behind
    // the camera.
    bool worldToLocal3D(const Vec3& p, Vector2& out) const;
    // World units covered by one logical pixel at `at` (2D: everywhere the same).
    double worldPerPixel(const Vec3& at) const;
    // Where a world point appears in this view (2D or 3D), logical pixels.
    bool project(const Vec3& p, Vector2& out) const;

private:
    void render2D(const Grid& grid, const Scene& scene);
    void render3D(const Grid& grid, const Scene& scene);

    ViewKind kind_;
    RenderTexture2D rt_{};
    int w_ = 0, h_ = 0;      // logical size
    float scale_ = 1.0f;     // physical pixels per logical pixel

    // 2D: world point at the view center, and pixels per world unit.
    double panU_ = 0.0, panV_ = 0.0;
    double zoom_ = 0.5;

    // 3D: position + yaw/pitch in degrees. Z is up.
    Vector3 pos_{-384.0f, -384.0f, 256.0f};
    float yaw_ = 45.0f, pitch_ = -25.0f;
    std::optional<scal::Camera> lens_;
};

} // namespace scal
