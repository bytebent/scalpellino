#pragma once
// One editor viewport: a 2D orthographic view (top/front/side) or the 3D view.
// Owns its render texture; draws only when asked.
#include "raylib.h"
#include "core/grid.hpp"

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

    // Keeps the render texture matching the on-screen size.
    void resize(int w, int h);
    const RenderTexture2D& target() const { return rt_; }
    int width()  const { return w_; }
    int height() const { return h_; }

    void render(const Grid& grid);

    // --- 2D navigation
    void pan2D(Vector2 deltaPx);
    void zoom2D(float wheel, Vector2 localPx);
    // Local pixel -> world (u, v) on this view's plane.
    void screenToWorld2D(Vector2 localPx, double& u, double& v) const;
    double zoom() const { return zoom_; }

    // --- 3D navigation
    void look(Vector2 deltaPx);
    void fly(float forward, float right, float up, float dt, bool fast);
    Vector3 cameraPos() const { return pos_; }

private:
    void render2D(const Grid& grid);
    void render3D(const Grid& grid);

    ViewKind kind_;
    RenderTexture2D rt_{};
    int w_ = 0, h_ = 0;

    // 2D: world point at the view center, and pixels per world unit.
    double panU_ = 0.0, panV_ = 0.0;
    double zoom_ = 0.5;

    // 3D: position + yaw/pitch in degrees. Z is up.
    Vector3 pos_{-384.0f, -384.0f, 256.0f};
    float yaw_ = 45.0f, pitch_ = -25.0f;
};

} // namespace scal
