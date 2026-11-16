#include "render/viewport.hpp"
#include "rlgl.h"
#include "render/texture_library.hpp"
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
const Color kBrush2D   {205, 205, 205, 255};
const Color kSelected  {255,  64,  64, 255};
const Color kPreview   {255, 220,  80, 255};
const Color kHandle    {255, 255, 255, 255};
const Color kInvalid   {255,  60, 220, 255};
const Color kPivot     { 80, 230, 255, 255};
const Color kTerrainPoint{120, 190, 255, 255};

const Color kBg3D      { 22,  22,  26, 255};
const Color kGround    { 48,  48,  56, 255};
const Color kGround1024{ 80,  70,  50, 255};

constexpr double kMinZoom = 0.02;  // px per unit
constexpr double kViewLimit = 1 << 20; // how far 2D views can pan (the grid is endless)
constexpr double kMaxZoom = 32.0;
constexpr double kMinLinePx = 4.0; // hide grid lines closer than this

// All 2D lines are drawn as filled rectangles/quads in physical pixels: exact and
// identical on every driver (GL line rasterization drops or shifts lines on some).
inline void vline(double x, double y0, double y1, int t, Color c) {
    if (y1 < y0) std::swap(y0, y1);
    DrawRectangle((int)std::floor(x), (int)std::floor(y0), t, std::max(1, (int)std::lround(y1 - y0) + 1), c);
}
inline void hline(double y, double x0, double x1, int t, Color c) {
    if (x1 < x0) std::swap(x0, x1);
    DrawRectangle((int)std::floor(x0), (int)std::floor(y), std::max(1, (int)std::lround(x1 - x0) + 1), t, c);
}
inline void segment(Vector2 a, Vector2 b, int t, Color c) {
    if (std::fabs(a.x - b.x) < 0.5f) vline(a.x, a.y, b.y, t, c);
    else if (std::fabs(a.y - b.y) < 0.5f) hline(a.y, a.x, b.x, t, c);
    else DrawLineEx(a, b, (float)t, c);
}

Vector3 toRl(const Vec3& v) { return {(float)v.x, (float)v.y, (float)v.z}; }

// Terrain tool circle: 64 points around the centre, in the plane across the normal.
std::vector<Vec3> cursorCircle(const Scene::BrushCursor& c) {
    Vec3 a = std::fabs(c.normal.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    a = normalize(cross(c.normal, a));
    const Vec3 b = cross(c.normal, a);
    std::vector<Vec3> pts;
    for (int i = 0; i < 64; ++i) {
        const double t = 2 * 3.14159265358979323846 * i / 64;
        pts.push_back(c.center + a * (std::cos(t) * c.radius) + b * (std::sin(t) * c.radius));
    }
    return pts;
}

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

void Viewport::resize(int w, int h, float scale) {
    w = std::max(w, 1);
    h = std::max(h, 1);
    if (scale <= 0) scale = 1;
    if (w == w_ && h == h_ && scale == scale_ && rt_.id) return;
    if (rt_.id) UnloadRenderTexture(rt_);
    rt_ = LoadRenderTexture((int)std::lround(w * scale), (int)std::lround(h * scale));
    SetTextureFilter(rt_.texture, TEXTURE_FILTER_POINT);
    w_ = w;
    h_ = h;
    scale_ = scale;
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

    // Physical-pixel mapping (the texture is w_*scale_ x h_*scale_).
    const double P = scale_;
    const double hw = w_ * 0.5, hh = h_ * 0.5;
    auto sx = [&](double u) { return (hw + (u - panU_) * zoom_) * P; };
    auto sy = [&](double v) { return (hh - (v - panV_) * zoom_) * P; };
    auto phys = [&](double u, double v) { return Vector2{(float)sx(u), (float)sy(v)}; };
    const int thin = 1;                                   // grid: one physical pixel
    const int thick = std::max(1, (int)std::lround(P));   // geometry: one logical pixel

    // Visible world range. The grid is endless (builds can go past the classic
    // +-4096 area); the red border only marks that area for reference.
    const double uMin = panU_ - hw / zoom_;
    const double uMax = panU_ + hw / zoom_;
    const double vMin = panV_ - hh / zoom_;
    const double vMax = panV_ + hh / zoom_;

    if (uMin < uMax && vMin < vMax) {
        const double top = sy(vMax), bottom = sy(vMin);
        const double left = sx(uMin), right = sx(uMax);

        auto drawLines = [&](double step, Color c, double skipMultiple) {
            if (step * zoom_ < kMinLinePx) return;
            for (double u = std::ceil(uMin / step) * step; u <= uMax; u += step) {
                if (skipMultiple > 0 && std::fmod(std::fabs(u), skipMultiple) == 0.0) continue;
                vline(sx(u), top, bottom, thin, c);
            }
            for (double v = std::ceil(vMin / step) * step; v <= vMax; v += step) {
                if (skipMultiple > 0 && std::fmod(std::fabs(v), skipMultiple) == 0.0) continue;
                hline(sy(v), left, right, thin, c);
            }
        };

        if (grid.visible) {
            if (std::fmod(64.0, grid.size) == 0.0) {
                // Power-of-two style grid: minor lines + highlighted 64 / 1024 lines.
                if (grid.size < 64) drawLines(grid.size, kGridMinor, 64.0);
                drawLines(64.0,   kGrid64,   1024.0);
                drawLines(1024.0, kGrid1024, 0.0);
            } else {
                // Custom size (e.g. 80): its own lines, highlighted every 8 cells.
                drawLines(grid.size, kGridMinor, grid.size * 8);
                drawLines(grid.size * 8, kGrid64, 0.0);
            }
        }
        if (uMin <= 0 && uMax >= 0) vline(sx(0), top, bottom, thin, kAxis);
        if (vMin <= 0 && vMax >= 0) hline(sy(0), left, right, thin, kAxis);
    }
    {   // world border
        const Vector2 a = phys(-kWorldHalf, kWorldHalf), b = phys(kWorldHalf, -kWorldHalf);
        hline(a.y, a.x, b.x, thin, kBorder); hline(b.y, a.x, b.x, thin, kBorder);
        vline(a.x, a.y, b.y, thin, kBorder); vline(b.x, a.y, b.y, thin, kBorder);
    }

    // ---- brushes: edges of every face polygon, projected.
    const int U = axU(), V = axV();
    auto drawBrush = [&](const SceneBrush& b, Color c) {
        for (const Polygon& p : b.polys) {
            const size_t n = p.verts.size();
            for (size_t i = 0; i < n; ++i) {
                const Vec3& a = p.verts[i];
                const Vec3& e = p.verts[(i + 1) % n];
                segment(phys(a[U], a[V]), phys(e[U], e[V]), thick, c);
            }
        }
    };
    for (const SceneBrush& b : scene.brushes)
        if (!scene.selected.count(b.id) && !scene.hidden.count(b.id)) drawBrush(b, kBrush2D);
    if (!scene.hideSelected)
        for (const SceneBrush& b : scene.brushes)
            if (scene.selected.count(b.id) && !scene.hidden.count(b.id)) drawBrush(b, kSelected);
    for (const SceneBrush& g : scene.ghosts) drawBrush(g, kSelected);
    for (const SceneBrush& g : scene.invalidGhosts) drawBrush(g, kInvalid);
    if (scene.brushCursor) {
        const auto pts = cursorCircle(*scene.brushCursor);
        for (size_t i = 0; i < pts.size(); ++i) {
            const Vec3 &a = pts[i], &e = pts[(i + 1) % pts.size()];
            segment(phys(a[U], a[V]), phys(e[U], e[V]), thick, kPreview);
        }
    }
    for (const auto& o : scene.outlines)
        for (size_t i = 0; i < o.size(); ++i) {
            const Vec3 &a = o[i], &e = o[(i + 1) % o.size()];
            segment(phys(a[U], a[V]), phys(e[U], e[V]), thick + 1, kPreview);
        }

    // ---- vertex tool corners: white, selected red (drawn on top); terrain points
    //      smaller and blue under them
    {
        const float pad = (float)P;
        for (int pass = 0; pass < 4; ++pass)
            for (const auto& h : scene.vertexHandles) {
                if (h.selected != (pass >= 2) || h.terrain != (pass % 2 == 0)) continue;
                const float hs = (float)((h.terrain ? 1.5 : 2.5) * P);
                const Vector2 c = phys(h.pos[U], h.pos[V]);
                DrawRectangleRec({std::floor(c.x - hs - pad), std::floor(c.y - hs - pad), 2 * (hs + pad) + 1, 2 * (hs + pad) + 1}, BLACK);
                DrawRectangleRec({std::floor(c.x - hs), std::floor(c.y - hs), 2 * hs + 1, 2 * hs + 1},
                                 h.selected ? kSelected : h.terrain ? kTerrainPoint : kHandle);
            }
    }

    // ---- clip tool: line 1-2, guide 1-3, numbered point handles
    if (scene.clipPoints.size() == 3) {
        const Vector2 c[3] = {phys(scene.clipPoints[0][U], scene.clipPoints[0][V]),
                              phys(scene.clipPoints[1][U], scene.clipPoints[1][V]),
                              phys(scene.clipPoints[2][U], scene.clipPoints[2][V])};
        const Color line{255, 255, 255, 255};
        segment(c[0], c[1], thick, line);
        if (std::hypot(c[2].x - c[0].x, c[2].y - c[0].y) > 2) segment(c[0], c[2], thin, {150, 150, 150, 255});
        const Color cols[3] = {{90, 230, 120, 255}, {90, 170, 255, 255}, {255, 210, 70, 255}};
        const float r = (float)(5 * P);
        for (int i = 2; i >= 0; --i) {
            DrawCircleV(c[i], r + (float)P, BLACK);
            DrawCircleV(c[i], r, cols[i]);
        }
    }

    // ---- custom rotation pivot: cyan circle with a cross
    if (scene.pivot) {
        const Vector2 c = phys((*scene.pivot)[U], (*scene.pivot)[V]);
        const float r = (float)(6 * P);
        DrawRing(c, r - thick, r, 0, 360, 32, kPivot);
        hline(c.y, c.x - r - 3 * P, c.x + r + 3 * P, thick, kPivot);
        vline(c.x, c.y - r - 3 * P, c.y + r + 3 * P, thick, kPivot);
    }

    auto drawBox = [&](const AABB& bx, Color c) {
        const Vector2 a = phys(bx.min[U], bx.max[V]), b = phys(bx.max[U], bx.min[V]);
        hline(a.y, a.x, b.x, thick, c); hline(b.y, a.x, b.x, thick, c);
        vline(a.x, a.y, b.y, thick, c); vline(b.x, a.y, b.y, thick, c);
    };
    if (scene.previewBox) drawBox(*scene.previewBox, kPreview);

    // ---- handles (size labels are drawn by the UI layer, in the UI font)
    if (scene.handleBox) {
        const AABB& hb = *scene.handleBox;
        const Vector2 a = phys(hb.min[U], hb.max[V]); // top-left
        const Vector2 b = phys(hb.max[U], hb.min[V]); // bottom-right
        const float mx = (a.x + b.x) * 0.5f, my = (a.y + b.y) * 0.5f;
        const float hs = (float)(3.0 * P);
        const float pad = (float)P;
        // Middle handles only when there is room for them (small boxes stay readable).
        const bool midU = (b.x - a.x) > 40.0f * P, midV = (b.y - a.y) > 40.0f * P;
        Vector2 pts[8];
        int n = 0;
        pts[n++] = {a.x, a.y}; pts[n++] = {b.x, a.y}; pts[n++] = {a.x, b.y}; pts[n++] = {b.x, b.y};
        if (midU) { pts[n++] = {mx, a.y}; pts[n++] = {mx, b.y}; }
        if (midV) { pts[n++] = {a.x, my}; pts[n++] = {b.x, my}; }
        for (int i = 0; i < n; ++i) {
            const Vector2 p = pts[i];
            DrawRectangleRec({std::floor(p.x - hs - pad), std::floor(p.y - hs - pad), 2 * (hs + pad) + 1, 2 * (hs + pad) + 1}, BLACK);
            DrawRectangleRec({std::floor(p.x - hs), std::floor(p.y - hs), 2 * hs + 1, 2 * hs + 1}, kHandle);
        }
    }
}

void Viewport::pan2D(Vector2 d) {
    panU_ -= d.x / zoom_;
    panV_ += d.y / zoom_;
    panU_ = std::clamp(panU_, -kViewLimit, kViewLimit);
    panV_ = std::clamp(panV_, -kViewLimit, kViewLimit);
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

bool Viewport::worldToLocal3D(const Vec3& p, Vector2& out) const {
    const Vector3 f = forwardFrom(yaw_, pitch_);
    const Vec3 rel{p.x - pos_.x, p.y - pos_.y, p.z - pos_.z};
    if (rel.x * f.x + rel.y * f.y + rel.z * f.z < 1.0) return false;
    out = GetWorldToScreenEx({(float)p.x, (float)p.y, (float)p.z}, camera(), w_, h_);
    return true;
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

    // ---- brushes: textured (default) or flat-shaded per brush colour
    // Terrain cells: smooth per-corner shading; each painted texture is drawn over the
    // ones below it with its blend as transparency (same points, so it sits exactly on it).
    auto drawTerrain = [&](const SceneBrush& b, Color tint, bool textured) {
        if (!b.terrain) return;
        const Face& f = b.faces[b.terrain->face];
        const int passes = textured ? 1 + (int)b.layers.size() : 1;
        for (int pass = 0; pass < passes; ++pass) {
            const std::string& texName = pass == 0 ? f.texture : b.layers[pass - 1].texture;
            unsigned texId = 0;
            TexSize ts{1, 1};
            if (textured) {
                const Texture2D& tex = scene.textures->get(texName);
                texId = tex.id;
                ts = {std::max(1, tex.width), std::max(1, tex.height)};
            }
            const size_t cells = b.polys.size() - b.terrainFirst;
            for (size_t c = 0; c < cells; ++c) {
                const Polygon& p = b.polys[b.terrainFirst + c];
                const float* al = pass == 0 ? nullptr : &b.layers[pass - 1].cellAlpha[4 * c];
                if (pass > 0 && al[0] <= 0 && al[1] <= 0 && al[2] <= 0 && al[3] <= 0) continue;
                rlCheckRenderBatchLimit(6);
                if (texId) rlSetTexture(texId);
                rlBegin(RL_TRIANGLES);
                for (int k : {0, 1, 2, 0, 2, 3}) {
                    const Color sc = shade(tint, b.cellNormals[4 * c + k]);
                    const unsigned char a = pass == 0 ? 255 : (unsigned char)std::lround(std::clamp(al[k], 0.0f, 1.0f) * 255);
                    rlColor4ub(sc.r, sc.g, sc.b, a);
                    if (texId) {
                        const UV uv = texCoord(f, p.verts[k], ts);
                        rlTexCoord2f((float)uv.u, (float)uv.v);
                    }
                    rlVertex3f((float)p.verts[k].x, (float)p.verts[k].y, (float)p.verts[k].z);
                }
                rlEnd();
                if (texId) rlSetTexture(0);
            }
        }
    };
    auto drawSolid = [&](const SceneBrush& b, Color base) {
        if (b.terrain) drawTerrain(b, base, false);
        for (size_t pi = 0; pi < (b.terrain ? b.terrainFirst : b.polys.size()); ++pi) {
            const Polygon& p = b.polys[pi];
            const Color c = shade(base, p.normal);
            for (size_t i = 1; i + 1 < p.verts.size(); ++i)
                DrawTriangle3D(toRl(p.verts[0]), toRl(p.verts[i]), toRl(p.verts[i + 1]), c);
        }
    };
    auto drawTextured = [&](const SceneBrush& b, Color tint, bool faceSelection) {
        if (b.terrain) {
            Color t = tint;
            if (faceSelection && scene.selectedFaces.count({b.id, b.terrain->face})) t = {255, 105, 105, 255};
            drawTerrain(b, t, true);
        }
        for (size_t pi = 0; pi < (b.terrain ? b.terrainFirst : b.polys.size()); ++pi) {
            const Polygon& p = b.polys[pi];
            if (p.face < 0 || p.face >= (int)b.faces.size()) continue;
            const Face& f = b.faces[p.face];
            const Texture2D& tex = scene.textures->get(f.texture);
            const TexSize ts{std::max(1, tex.width), std::max(1, tex.height)};
            Color t = tint;
            if (faceSelection && scene.selectedFaces.count({b.id, p.face})) t = {255, 105, 105, 255};
            const Color c = shade(t, p.normal);
            rlSetTexture(tex.id);
            rlBegin(RL_TRIANGLES);
            rlColor4ub(c.r, c.g, c.b, 255);
            for (size_t i = 1; i + 1 < p.verts.size(); ++i)
                for (const Vec3* v : {&p.verts[0], &p.verts[i], &p.verts[i + 1]}) {
                    const UV uv = texCoord(f, *v, ts);
                    rlTexCoord2f((float)uv.u, (float)uv.v);
                    rlVertex3f((float)v->x, (float)v->y, (float)v->z);
                }
            rlEnd();
            rlSetTexture(0);
        }
    };
    auto drawEdges = [&](const SceneBrush& b, Color c) {
        const size_t plain = b.terrain ? b.terrainFirst : b.polys.size();
        for (size_t pi = 0; pi < plain; ++pi) {
            const Polygon& p = b.polys[pi];
            for (size_t i = 0; i < p.verts.size(); ++i)
                DrawLine3D(toRl(p.verts[i]), toRl(p.verts[(i + 1) % p.verts.size()]), c);
        }
        if (b.terrain) { // terrain: just its outline (the grid would hide the shape)
            const TerrainGrid& g = *b.terrain;
            for (int k = 0; k + 1 < g.n; ++k) {
                DrawLine3D(toRl(g.at(k, 0)), toRl(g.at(k + 1, 0)), c);
                DrawLine3D(toRl(g.at(k, g.n - 1)), toRl(g.at(k + 1, g.n - 1)), c);
                DrawLine3D(toRl(g.at(0, k)), toRl(g.at(0, k + 1)), c);
                DrawLine3D(toRl(g.at(g.n - 1, k)), toRl(g.at(g.n - 1, k + 1)), c);
            }
        }
    };
    const Color selTint{235, 110, 110, 255};
    const bool textured = scene.textures != nullptr;
    for (const SceneBrush& b : scene.brushes) {
        const bool sel = scene.selected.count(b.id) > 0;
        if (sel && scene.hideSelected) continue;
        if (scene.hidden.count(b.id)) continue;
        if (textured) drawTextured(b, sel ? Color{255, 150, 150, 255} : WHITE, true);
        else drawSolid(b, sel ? selTint : brushColor(b.id));
    }
    for (const SceneBrush& g : scene.ghosts) {
        if (textured) drawTextured(g, {255, 150, 150, 255}, false);
        else drawSolid(g, selTint);
    }
    for (const SceneBrush& g : scene.invalidGhosts) drawSolid(g, kInvalid);

    // Selection outlines on top (visible through other brushes, like Hammer).
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    if (!scene.hideSelected)
        for (const SceneBrush& b : scene.brushes)
            if (scene.selected.count(b.id)) drawEdges(b, kSelected);
    for (const SceneBrush& g : scene.ghosts) drawEdges(g, kSelected);
    for (const SceneBrush& g : scene.invalidGhosts) drawEdges(g, kInvalid);
    for (const SceneBrush& b : scene.brushes)
        for (const Polygon& p : b.polys)
            if (scene.selectedFaces.count({b.id, p.face}))
                for (size_t i = 0; i < p.verts.size(); ++i)
                    DrawLine3D(toRl(p.verts[i]), toRl(p.verts[(i + 1) % p.verts.size()]), {255, 230, 80, 255});
    for (const auto& o : scene.outlines)
        for (size_t i = 0; i < o.size(); ++i) DrawLine3D(toRl(o[i]), toRl(o[(i + 1) % o.size()]), kPreview);
    if (scene.previewBox) {
        const AABB& pb = *scene.previewBox;
        const Vec3 c = pb.center(), s = pb.size();
        DrawCubeWires(toRl(c), (float)s.x, (float)s.y, (float)s.z, kPreview);
    }
    if (scene.brushCursor) {
        const auto pts = scene.brushCursor->ring.empty() ? cursorCircle(*scene.brushCursor) : scene.brushCursor->ring;
        for (size_t i = 0; i < pts.size(); ++i) DrawLine3D(toRl(pts[i]), toRl(pts[(i + 1) % pts.size()]), kPreview);
        const Vec3 tip = scene.brushCursor->center + scene.brushCursor->normal * (scene.brushCursor->radius * 0.25);
        DrawLine3D(toRl(scene.brushCursor->center), toRl(tip), kPreview);
    }
    rlDrawRenderBatchActive();
    rlEnableDepthTest();

    EndMode3D();

    // Vertex tool: terrain points as small squares (the only handles picked in 3D).
    const float P = scale_;
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& h : scene.vertexHandles) {
            if (!h.terrain || h.selected != (pass == 1)) continue;
            Vector2 c;
            if (!worldToLocal3D(h.pos, c)) continue;
            c.x *= P;
            c.y *= P;
            const float hs = (h.selected ? 2.0f : 1.5f) * P;
            DrawRectangleRec({std::floor(c.x - hs - P), std::floor(c.y - hs - P), 2 * (hs + P) + 1, 2 * (hs + P) + 1}, BLACK);
            DrawRectangleRec({std::floor(c.x - hs), std::floor(c.y - hs), 2 * hs + 1, 2 * hs + 1},
                             h.selected ? kSelected : kTerrainPoint);
        }
}

void Viewport::resetToOrigin() {
    panU_ = panV_ = 0;
    zoom_ = 0.5;
    pos_ = {-384.0f, -384.0f, 256.0f};
    yaw_ = 45.0f;
    pitch_ = -25.0f;
}

void Viewport::lookAt(const Vec3& t, double dist) {
    const Vector3 f = forwardFrom(yaw_, pitch_);
    pos_ = {(float)(t.x - f.x * dist), (float)(t.y - f.y * dist), (float)(t.z - f.z * dist)};
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
