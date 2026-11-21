#include "render/viewport.hpp"
#include "render/baked_light.hpp"
#include "rlgl.h"
#include "render/texture_library.hpp"
#include "core/sky.hpp"
#include "render/lighting.hpp"
#include "render/mathprim_render.hpp"
#include "render/model_library.hpp"
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
const Color kPrimLine {200, 170, 255, 255};
const Color kPrimCut  {255, 150,  80, 255};
const Color kPrimKeep { 90, 220, 200, 255};
const Color kPropLine { 130, 225, 140, 255};
const Color kPropMissing {255, 170,  60, 255};

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

// ---- lights
const Color kAxisCol[3] = {{235, 80, 80, 255}, {90, 210, 90, 255}, {90, 140, 245, 255}};
Color lightColor(const Light& l) {
    auto c = [](double v) { return (unsigned char)std::clamp((int)std::lround(v), 0, 255); };
    return {c(l.color.x), c(l.color.y), c(l.color.z), 255};
}
Color dim(Color c, float k) {
    return {(unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), c.a};
}
Color cameraColor(const Camera& c) {
    auto k = [](double v) { return (unsigned char)std::clamp((int)std::lround(v), 0, 255); };
    return {k(c.color.x), k(c.color.y), k(c.color.z), 255};
}
const Color kRollRing{240, 200, 60, 255};
constexpr double kCameraLenPx = 56; // a camera's pyramid on screen (logical pixels)

// Lines to draw for a light's shape (world segments): a spot's cone (length `len`), a
// sun's arrow.
std::vector<std::pair<Vec3, Vec3>> lightShape(const Light& l, double len) {
    std::vector<std::pair<Vec3, Vec3>> segs;
    if (!l.hasDirection()) return segs;
    const Vec3 d = lightDirection(l);
    Vec3 a = std::fabs(d.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    a = normalize(cross(d, a));
    const Vec3 b = cross(d, a);
    if (l.kind == LightKind::Spot) {
        const double r = len * std::tan(std::clamp(l.outer, 1.0, 89.0) * 3.14159265358979323846 / 180);
        const Vec3 c = l.pos + d * len;
        Vec3 prev;
        for (int i = 0; i <= 24; ++i) {
            const double t = 2 * 3.14159265358979323846 * i / 24;
            const Vec3 p = c + a * (std::cos(t) * r) + b * (std::sin(t) * r);
            if (i > 0) segs.push_back({prev, p});
            if (i % 6 == 0 && i < 24) segs.push_back({l.pos, p});
            prev = p;
        }
    } else { // sun: an arrow
        const Vec3 tip = l.pos + d * len;
        segs.push_back({l.pos, tip});
        for (const Vec3& s : {a, -a, b, -b}) segs.push_back({tip, tip - d * (len * 0.3) + s * (len * 0.15)});
    }
    return segs;
}

// Scale figure: a blocky person 175 units (cm) tall, feet at `at`, facing +X. The boxes
// below are laid out on a 72-tall body and scaled up.
const Color kFigure{120, 220, 255, 255};
std::vector<AABB> figureBoxes(const Vec3& at) {
    constexpr double k = 175.0 / 72.0;
    auto box = [&](double x0, double y0, double z0, double x1, double y1, double z1) {
        AABB b;
        b.add(at + Vec3{x0, y0, z0} * k);
        b.add(at + Vec3{x1, y1, z1} * k);
        return b;
    };
    return {
        box(-4, -9, 0, 4, -1, 34),   // legs
        box(-4, 1, 0, 4, 9, 34),
        box(-5, -10, 34, 5, 10, 60), // body
        box(-3, -15, 35, 3, -10, 60), // arms
        box(-3, 10, 35, 3, 15, 60),
        box(-5, -5, 62, 5, 5, 72),   // head
    };
}

// Fog preview: textured colour faded to the fog colour by distance from the camera.
// Loaded the first time fog is shown (needs the GL context).
const char* kFogVS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragPos;
void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragPos = vertexPosition; // batched vertices are already in world space
    gl_Position = mvp * vec4(vertexPosition, 1.0);
})";
const char* kFogFS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragPos;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 camPos;
uniform vec3 fogColor;
uniform vec2 fogRange;
out vec4 finalColor;
uniform vec2 fogHeight; // the fog's base and thickness (thickness 0: everywhere)
// How fogged a point is: the stretch of the line of sight inside the fog layer, through the
// fog's start and end distances (as Sky Lab's fog in the sky).
float fogAmount(vec3 p) {
    float d = length(p - camPos);
    float path = d;
    if (fogHeight.y > 0.0) {
        float lo = fogHeight.x, hi = fogHeight.x + fogHeight.y, z0 = camPos.z, z1 = p.z;
        if (abs(z1 - z0) < 0.001) path = (z0 >= lo && z0 <= hi) ? d : 0.0;
        else {
            float a = clamp((lo - z0) / (z1 - z0), 0.0, 1.0), b = clamp((hi - z0) / (z1 - z0), 0.0, 1.0);
            path = abs(b - a) * d;
        }
    }
    return clamp((path - fogRange.x) / max(fogRange.y - fogRange.x, 1.0), 0.0, 1.0);
}
void main() {
    vec4 c = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
    float f = fogAmount(fragPos);
    finalColor = vec4(mix(c.rgb, fogColor, f), c.a);
})";
struct FogShader {
    Shader shader{};
    int camPos = -1, color = -1, range = -1, height = -1;
    bool tried = false, ok = false;
};
FogShader& fogShader() {
    static FogShader f;
    if (!f.tried) {
        f.tried = true;
        f.shader = LoadShaderFromMemory(kFogVS, kFogFS);
        f.ok = f.shader.id != 0 && f.shader.id != rlGetShaderIdDefault();
        if (f.ok) {
            f.camPos = GetShaderLocation(f.shader, "camPos");
            f.color = GetShaderLocation(f.shader, "fogColor");
            f.range = GetShaderLocation(f.shader, "fogRange");
            f.height = GetShaderLocation(f.shader, "fogHeight");
        }
    }
    return f;
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
    // Whether a world box shows in this view at all (v0.10.3: things off screen are skipped).
    auto inView = [&](const AABB& b) {
        const int Uax = axU(), Vax = axV();
        return b.max[Uax] >= uMin && b.min[Uax] <= uMax && b.max[Vax] >= vMin && b.min[Vax] <= vMax;
    };

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
        if (!inView(b.bounds)) return;
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

    // ---- math primitives: outlines (add: lilac, subtract: orange, intersect: teal)
    {
        auto drawPrim = [&](const MathPrim& p, Color c) {
            if (!inView(primBounds(p))) return;
            for (const Polyline& l : primOutline(p, 64))
                for (size_t i = 0; i + 1 < l.pts.size() + (l.closed ? 1 : 0); ++i) {
                    const Vec3 &a = l.pts[i], &b = l.pts[(i + 1) % l.pts.size()];
                    segment(phys(a[U], a[V]), phys(b[U], b[V]), thick, c);
                }
        };
        for (const auto& sp : scene.prims) {
            if (sp.selected) continue;
            const Color c = sp.prim.combo == 0 || sp.prim.op == PrimOp::Add ? kPrimLine
                            : sp.prim.op == PrimOp::Subtract ? kPrimCut : kPrimKeep;
            drawPrim(sp.prim, c);
        }
        for (const auto& sp : scene.prims) if (sp.selected) drawPrim(sp.prim, kSelected);
        for (const MathPrim& p : scene.pendingPrims) drawPrim(p, kPreview);
        // Props: the box of the model (turned and sized), and a cross on its origin.
        auto drawProp = [&](const Scene::SceneProp& sp, Color c) {
            std::vector<Vec3> k;
            for (int i = 0; i < 8; ++i)
                k.push_back(propToWorld(sp.prop, {i & 1 ? sp.local.max.x : sp.local.min.x, i & 2 ? sp.local.max.y : sp.local.min.y,
                                                   i & 4 ? sp.local.max.z : sp.local.min.z}));
            {
                AABB kb;
                for (const Vec3& q : k) kb.add(q);
                if (!inView(kb)) return;
            }
            for (int i = 0; i < 8; ++i)
                for (int b = 0; b < 3; ++b)
                    if (!(i & (1 << b))) {
                        const Vec3 &a = k[i], &e = k[i | (1 << b)];
                        segment(phys(a[U], a[V]), phys(e[U], e[V]), thick, c);
                    }
            const Vector2 o = phys(sp.prop.pos[U], sp.prop.pos[V]);
            const float cs2 = (float)(4 * P);
            segment({o.x - cs2, o.y}, {o.x + cs2, o.y}, thick, c);
            segment({o.x, o.y - cs2}, {o.x, o.y + cs2}, thick, c);
        };
        for (const auto& sp : scene.props)
            if (!sp.selected) drawProp(sp, sp.missing ? kPropMissing : kPropLine);
        for (const auto& sp : scene.props) if (sp.selected) drawProp(sp, kSelected);
        for (const auto& sp : scene.pendingProps) drawProp(sp, kPreview);
        // Rings around the selected primitives / props (the one around this view's axis) and
        // the size handles seen side-on.
        for (const Scene::PrimGizmo& gz : scene.gizmos) {
            const double r = std::max(gz.radius, Scene::kRingPx / zoom_);
            const auto pts = axisCircle(gz.center, axD(), r, 96);
            const Color rc = scene.activeRing == axD() ? WHITE : kAxisCol[axD()];
            for (size_t i = 0; i < pts.size(); ++i) {
                const Vec3 &a = pts[i], &b = pts[(i + 1) % pts.size()];
                segment(phys(a[U], a[V]), phys(b[U], b[V]), thick + 1, rc);
            }
            const float hs = (float)(3 * P);
            for (const auto& [pos, ax] : gz.handles) {
                if (std::hypot(ax[U], ax[V]) < 0.3) continue;
                const Vector2 c = phys(pos[U], pos[V]);
                DrawRectangleRec({std::floor(c.x - hs - (float)P), std::floor(c.y - hs - (float)P), 2 * (hs + (float)P) + 1, 2 * (hs + (float)P) + 1}, BLACK);
                DrawRectangleRec({std::floor(c.x - hs), std::floor(c.y - hs), 2 * hs + 1, 2 * hs + 1}, kHandle);
            }
        }
    }

    // ---- scale figure (outline)
    if (scene.figure)
        for (const AABB& b : figureBoxes(*scene.figure)) {
            const Vector2 a = phys(b.min[U], b.min[V]), c = phys(b.max[U], b.max[V]);
            segment(a, {c.x, a.y}, thick, kFigure);
            segment({c.x, a.y}, c, thick, kFigure);
            segment(c, {a.x, c.y}, thick, kFigure);
            segment({a.x, c.y}, a, thick, kFigure);
        }

    // ---- lights: shape (cone / arrow), reach when selected, marker, rotation rings
    {
        const double wpp = 1.0 / zoom_;
        auto drawLight = [&](const Light& l, bool sel, bool pending) {
            const Color col = lightColor(l);
            for (const auto& [a, b] : lightShape(l, 40 * wpp))
                segment(phys(a[U], a[V]), phys(b[U], b[V]), thick, sel ? kSelected : col);
            if (sel && l.kind != LightKind::Sun) {
                const auto pts = axisCircle(l.pos, axD(), l.range, 96);
                for (size_t i = 0; i < pts.size(); ++i) {
                    const Vec3 &a = pts[i], &b = pts[(i + 1) % pts.size()];
                    segment(phys(a[U], a[V]), phys(b[U], b[V]), thin, dim(col, 0.6f));
                }
            }
            const Vector2 c = phys(l.pos[U], l.pos[V]);
            const float hs = (float)(4 * P);
            const Color frame = pending ? kPreview : sel ? kSelected : BLACK;
            if (l.kind == LightKind::Sun) {
                DrawCircleV(c, hs + (float)(sel || pending ? 2 * P : P), frame);
                DrawCircleV(c, hs, col);
            } else {
                const float f = (float)(sel || pending ? 2 * P : P);
                DrawRectangleRec({std::floor(c.x - hs - f), std::floor(c.y - hs - f), 2 * (hs + f) + 1, 2 * (hs + f) + 1}, frame);
                DrawRectangleRec({std::floor(c.x - hs), std::floor(c.y - hs), 2 * hs + 1, 2 * hs + 1}, col);
            }
        };
        for (const auto& sl : scene.lights) if (!sl.selected) drawLight(sl.light, false, false);
        for (const auto& sl : scene.lights) if (sl.selected) drawLight(sl.light, true, false);
        if (scene.pendingLight) drawLight(*scene.pendingLight, false, true);
        for (const Light& l : scene.previewLights) drawLight(l, false, true);
        for (const auto& sl : scene.lights) {
            if (!sl.selected || !sl.light.hasDirection()) continue;
            const int ax = axD();
            const auto pts = axisCircle(sl.light.pos, ax, Scene::kRingPx * wpp, 64);
            const Color rc = scene.activeRing == ax ? WHITE : kAxisCol[ax];
            for (size_t i = 0; i < pts.size(); ++i) {
                const Vec3 &a = pts[i], &b = pts[(i + 1) % pts.size()];
                segment(phys(a[U], a[V]), phys(b[U], b[V]), thick + 1, rc);
            }
        }
    }

    // ---- cameras: pyramid (box if orthographic), marker (a diamond), rings when selected
    {
        const double wpp = 1.0 / zoom_;
        auto drawCamera = [&](const Camera& c, bool sel, bool pending) {
            const Color col = cameraColor(c);
            const Color lineCol = sel ? kSelected : pending ? kPreview : c.active ? col : dim(col, 0.55f);
            for (const auto& [a, b] : cameraShape(c, kCameraLenPx * wpp))
                segment(phys(a[U], a[V]), phys(b[U], b[V]), thick, lineCol);
            const Vector2 m = phys(c.pos[U], c.pos[V]);
            const float hs = (float)(5 * P), f = (float)(sel || pending ? 2 * P : P);
            const Color frame = pending ? kPreview : sel ? kSelected : BLACK;
            DrawPoly(m, 4, hs + f * 1.5f, 0, frame);
            DrawPoly(m, 4, hs, 0, col);
            if (c.active) DrawCircleV(m, (float)(1.5 * P), WHITE);
        };
        for (const auto& sc : scene.cameras) if (!sc.selected) drawCamera(sc.cam, false, false);
        for (const auto& sc : scene.cameras) if (sc.selected) drawCamera(sc.cam, true, false);
        if (scene.pendingCamera) drawCamera(*scene.pendingCamera, false, true);
        for (const auto& sc : scene.cameras) {
            if (!sc.selected) continue;
            const int ax = axD();
            const double r = Scene::kRingPx * wpp;
            auto ring = [&](const std::vector<Vec3>& pts, Color rc) {
                for (size_t i = 0; i < pts.size(); ++i) {
                    const Vec3 &a = pts[i], &b = pts[(i + 1) % pts.size()];
                    segment(phys(a[U], a[V]), phys(b[U], b[V]), thick + 1, rc);
                }
            };
            ring(axisCircle(sc.cam.pos, ax, r, 64), scene.activeRing == ax ? WHITE : kAxisCol[ax]);
            const Vec3 fwd = cameraForward(sc.cam);
            if (std::fabs(fwd[axD()]) > 0.2) // seen nearly edge-on it can't be grabbed: type the roll
                ring(dirCircle(sc.cam.pos, fwd, r * Scene::kRollRingScale, 64), scene.activeRing == 3 ? WHITE : kRollRing);
        }
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
    if (lens_) {
        const Mat3 b = cameraBasis(*lens_);
        const Vec3 p = lens_->pos, t = p + b.col[0];
        cam.position = {(float)p.x, (float)p.y, (float)p.z};
        cam.target = {(float)t.x, (float)t.y, (float)t.z};
        cam.up = {(float)b.col[2].x, (float)b.col[2].y, (float)b.col[2].z};
        double tx, ty;
        if (lens_->ortho) {
            cameraHalfSize(*lens_, tx, ty);
            cam.fovy = (float)(2 * ty); // orthographic: the height in units
            cam.projection = CAMERA_ORTHOGRAPHIC;
        } else {
            cameraHalfTangents(*lens_, tx, ty);
            cam.fovy = (float)(2 * std::atan(ty) * 180 / 3.14159265358979323846);
            cam.projection = CAMERA_PERSPECTIVE;
        }
        return cam;
    }
    cam.position = pos_;
    const Vector3 f = forwardFrom(yaw_, pitch_);
    cam.target = {pos_.x + f.x, pos_.y + f.y, pos_.z + f.z};
    cam.up = {0, 0, 1};
    cam.fovy = 70.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    return cam;
}

void Viewport::pickRay(Vector2 local, Vec3& origin, Vec3& dir) const {
    rlSetClipPlanes(2.0, 200000.0);
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

double Viewport::worldPerPixel(const Vec3& at) const {
    if (is2D()) return 1.0 / zoom_;
    const Vector3 f = forwardFrom(yaw_, pitch_);
    const double depth = std::max(1.0, (at.x - pos_.x) * f.x + (at.y - pos_.y) * f.y + (at.z - pos_.z) * f.z);
    return 2.0 * depth * std::tan(camera().fovy * 0.5 * DEG2RAD) / std::max(1, h_);
}

bool Viewport::project(const Vec3& p, Vector2& out) const {
    if (is2D()) { out = worldToLocal2D(p); return true; }
    return worldToLocal3D(p, out);
}

void Viewport::render3D(const Grid& grid, const Scene& scene) {
    auto byte = [](double v) { return (unsigned char)std::clamp((int)std::lround(v), 0, 255); };
    const Color fogCol{byte(scene.fogColor.x), byte(scene.fogColor.y), byte(scene.fogColor.z), 255};
    ClearBackground(scene.fog ? fogCol : kBg3D);
    // Through a camera object: its own near / far.
    if (lens_) rlSetClipPlanes(lens_->nearDist, lens_->farDist);
    else rlSetClipPlanes(2.0, 200000.0);
    const Vector3 eye = lens_ ? Vector3{(float)lens_->pos.x, (float)lens_->pos.y, (float)lens_->pos.z} : pos_;
    BeginMode3D(camera());

    // Sky: a big sphere around the camera, behind everything (no depth writes).
    if (scene.skyTexture) {
        // 1 km, inside the far clip (2 km). Through a camera: between its near and far (the
        // sky shows whatever far is), and wider than an orthographic picture.
        double R = 100000;
        if (lens_) {
            R = std::min(R, 0.5 * (lens_->nearDist + lens_->farDist));
            if (lens_->ortho) R = std::min(std::max(R, lens_->orthoSize * 2), lens_->farDist * 0.99);
        }
        const Vec3 c{eye.x, eye.y, eye.z};
        constexpr int NU = 48, NV = 24;
        rlDrawRenderBatchActive();
        rlDisableDepthMask();
        rlDisableBackfaceCulling();
        // Rows of the sphere from v0 to v1 (0 = straight up, 1 = straight down); textured:
        // the texture holds the panorama's top `cover` (the rest is its plain colour).
        const double cover = std::clamp((double)scene.skyCover, 0.05, 1.0);
        auto band = [&](double v0, double v1, int rows, bool textured) {
            if (textured) rlSetTexture(scene.skyTexture);
            rlBegin(RL_QUADS);
            if (textured) rlColor4ub(255, 255, 255, 255);
            else rlColor4ub((unsigned char)scene.skyBelow.x, (unsigned char)scene.skyBelow.y, (unsigned char)scene.skyBelow.z, 255);
            for (int j = 0; j < rows; ++j)
                for (int i = 0; i < NU; ++i) {
                    // Columns go once round (u from 0 to 1 as the yaw falls), rows top to bottom.
                    const int ci[4] = {i, i + 1, i + 1, i}, cj[4] = {j, j, j + 1, j + 1};
                    for (int k = 0; k < 4; ++k) {
                        const double u = (double)ci[k] / NU, v = v0 + (v1 - v0) * cj[k] / rows;
                        const double yaw = (0.5 - u) * 2 * 3.14159265358979323846, lat = (0.5 - v) * 3.14159265358979323846;
                        const Vec3 d{std::cos(lat) * std::cos(yaw), std::cos(lat) * std::sin(yaw), std::sin(lat)};
                        if (textured) rlTexCoord2f((float)u, (float)(v / cover));
                        const Vec3 p = c + d * R;
                        rlVertex3f((float)p.x, (float)p.y, (float)p.z);
                    }
                }
            rlEnd();
            if (textured) rlSetTexture(0);
        };
        const int texRows = std::max(4, (int)std::lround(NV * cover));
        band(0, cover, texRows, true);
        if (cover < 1) {
            rlDrawRenderBatchActive();
            band(cover, 1, std::max(2, NV - texRows), false);
        }
        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlEnableDepthMask();
    }

    if (grid.visible && !lens_) {
        const float ext = 2048.0f;
        for (float t = -ext; t <= ext; t += 64.0f) {
            Color c = (std::fmod(std::fabs(t), 1024.0f) == 0.0f) ? kGround1024 : kGround;
            DrawLine3D({t, -ext, 0}, {t, ext, 0}, c);
            DrawLine3D({-ext, t, 0}, {ext, t, 0}, c);
        }
    }
    if (!lens_) {
        DrawLine3D({0, 0, 0}, {256, 0, 0}, {220, 70, 70, 255});
        DrawLine3D({0, 0, 0}, {0, 256, 0}, {70, 200, 70, 255});
        DrawLine3D({0, 0, 0}, {0, 0, 256}, {80, 120, 230, 255});
    }

    // Baked light (renders): brushes through the lightmap shader (fog included).
    BakedDrawContext bctx;
    bctx.camPos = {eye.x, eye.y, eye.z};
    bctx.exposure = scene.exposure;
    bctx.fog = scene.fog;
    bctx.fogColor = scene.fogColor;
    bctx.fogStart = scene.fogStart;
    bctx.fogEnd = scene.fogEnd;
    bctx.fogBase = scene.fogBase;
    bctx.fogThickness = scene.fogThickness;
    bctx.shaders = scene.shaders;
    if (lens_ && lens_->ortho) {
        bctx.orthoOn = true;
        bctx.viewDir = cameraForward(*lens_);
    }
    bctx.skyTex = scene.skyReflection;
    bctx.skyCover = scene.skyCover;
    bctx.skyBelow = scene.skyBelow;
    bctx.skyFlat = {std::pow(scene.ambient.x / 255.0, 2.2), std::pow(scene.ambient.y / 255.0, 2.2), std::pow(scene.ambient.z / 255.0, 2.2)};
    bctx.problems = scene.problems;
    const bool bakedBrushes = lens_ && scene.baked && scene.textures && drawBakedBrushes(scene.brushes, *scene.baked, *scene.textures, bctx);
    // Fog: everything solid from here on is drawn through the fog shader.
    bool fogging = false;
    if (scene.fog) {
        FogShader& fs = fogShader();
        if (fs.ok) {
            rlDrawRenderBatchActive();
            BeginShaderMode(fs.shader);
            const float cam[3] = {eye.x, eye.y, eye.z};
            const float col[3] = {fogCol.r / 255.0f, fogCol.g / 255.0f, fogCol.b / 255.0f};
            const float range[2] = {(float)scene.fogStart, (float)scene.fogEnd};
            SetShaderValue(fs.shader, fs.camPos, cam, SHADER_UNIFORM_VEC3);
            SetShaderValue(fs.shader, fs.color, col, SHADER_UNIFORM_VEC3);
            SetShaderValue(fs.shader, fs.range, range, SHADER_UNIFORM_VEC2);
            const float height[2] = {(float)scene.fogBase, (float)scene.fogThickness};
            SetShaderValue(fs.shader, fs.height, height, SHADER_UNIFORM_VEC2);
            fogging = true;
        }
    }

    // ---- brushes: textured (default) or flat-shaded per brush colour
    // Terrain cells: smooth per-corner shading; each painted texture is drawn over the
    // ones below it with its blend as transparency (same points, so it sits exactly on it).
    // Rough light preview: the light multipliers of brush `b`, if they match its polygons.
    auto litOf = [&](const SceneBrush& b) -> const std::vector<LitPoly>* {
        if (!scene.lighting) return nullptr;
        auto it = scene.lighting->find(b.id);
        if (it == scene.lighting->end() || it->second.size() != b.polys.size()) return nullptr;
        for (size_t i = b.terrain ? b.terrainFirst : b.polys.size(); i < b.polys.size(); ++i)
            if (it->second[i].light.size() != b.polys[i].verts.size()) return nullptr;
        return &it->second;
    };
    auto litColor = [](Color tint, const Vec3& m) {
        auto ch = [](unsigned char c, double k) { return (unsigned char)std::clamp((int)std::lround(c * k), 0, 255); };
        return Color{ch(tint.r, m.x), ch(tint.g, m.y), ch(tint.b, m.z), 255};
    };
    auto drawTerrain = [&](const SceneBrush& b, Color tint, bool textured) {
        if (!b.terrain) return;
        const auto* lit = litOf(b);
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
                    const Color sc = lit ? litColor(tint, (*lit)[b.terrainFirst + c].light[k]) : shade(tint, b.cellNormals[4 * c + k]);
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
        const auto* lit = litOf(b);
        for (size_t pi = 0; pi < (b.terrain ? b.terrainFirst : b.polys.size()); ++pi) {
            const Polygon& p = b.polys[pi];
            const Color c = shade(base, p.normal);
            if (!lit) {
                for (size_t i = 1; i + 1 < p.verts.size(); ++i)
                    DrawTriangle3D(toRl(p.verts[0]), toRl(p.verts[i]), toRl(p.verts[i + 1]), c);
                continue;
            }
            const LitPoly& lp = (*lit)[pi];
            for (size_t i = 0; i + 2 < lp.pts.size(); i += 3) {
                rlCheckRenderBatchLimit(3);
                rlBegin(RL_TRIANGLES);
                for (size_t k = i; k < i + 3; ++k) {
                    const Color lc = litColor(base, lp.light[k]);
                    rlColor4ub(lc.r, lc.g, lc.b, 255);
                    rlVertex3f((float)lp.pts[k].x, (float)lp.pts[k].y, (float)lp.pts[k].z);
                }
                rlEnd();
            }
        }
    };
    auto drawTextured = [&](const SceneBrush& b, Color tint, bool faceSelection) {
        if (b.terrain) {
            Color t = tint;
            if (faceSelection && scene.selectedFaces.count({b.id, b.terrain->face})) t = {255, 105, 105, 255};
            drawTerrain(b, t, true);
        }
        const auto* lit = litOf(b);
        for (size_t pi = 0; pi < (b.terrain ? b.terrainFirst : b.polys.size()); ++pi) {
            const Polygon& p = b.polys[pi];
            if (p.face < 0 || p.face >= (int)b.faces.size()) continue;
            const Face& f = b.faces[p.face];
            const Texture2D& tex = scene.textures->get(f.texture);
            const TexSize ts{std::max(1, tex.width), std::max(1, tex.height)};
            Color t = tint;
            if (faceSelection && scene.selectedFaces.count({b.id, p.face})) t = {255, 105, 105, 255};
            const Color c = shade(t, p.normal);
            if (lit) { // rough light preview: the face's small lit triangles
                const LitPoly& lp = (*lit)[pi];
                for (size_t i = 0; i + 2 < lp.pts.size(); i += 3) {
                    rlCheckRenderBatchLimit(3);
                    rlSetTexture(tex.id);
                    rlBegin(RL_TRIANGLES);
                    for (size_t k = i; k < i + 3; ++k) {
                        const Color lc = litColor(t, lp.light[k]);
                        rlColor4ub(lc.r, lc.g, lc.b, 255);
                        const UV uv = texCoord(f, lp.pts[k], ts);
                        rlTexCoord2f((float)uv.u, (float)uv.v);
                        rlVertex3f((float)lp.pts[k].x, (float)lp.pts[k].y, (float)lp.pts[k].z);
                    }
                    rlEnd();
                }
                rlSetTexture(0);
                continue;
            }
            rlSetTexture(tex.id);
            rlBegin(RL_TRIANGLES);
            rlColor4ub(c.r, c.g, c.b, 255);
            for (size_t i = 1; i + 1 < p.verts.size(); ++i)
                for (size_t k : {(size_t)0, i, i + 1}) {
                    const Vec3* v = &p.verts[k];
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
        if (bakedBrushes) break;
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

    // Scale figure: see-through blocks.
    if (scene.figure) {
        for (const AABB& b : figureBoxes(*scene.figure)) {
            const Vec3 c = b.center(), s = b.size();
            DrawCube(toRl(c), (float)s.x, (float)s.y, (float)s.z, {kFigure.r, kFigure.g, kFigure.b, 90});
            DrawCubeWires(toRl(c), (float)s.x, (float)s.y, (float)s.z, kFigure);
        }
    }

    if (fogging) {
        rlDrawRenderBatchActive();
        EndShaderMode();
    }

    // Math primitives: exact surfaces (their own shader, fog and light preview included).
    if (!scene.prims.empty()) {
        PrimDrawContext ctx;
        ctx.camPos = {eye.x, eye.y, eye.z};
        if (lens_ && lens_->ortho) ctx.orthoDir = cameraForward(*lens_);
        ctx.textures = scene.textures;
        ctx.lightPreview = scene.lightPreview;
        ctx.ambient = scene.ambient;
        for (const auto& sl : scene.lights) ctx.lights.push_back(sl.light);
        ctx.fog = scene.fog;
        ctx.fogColor = scene.fogColor;
        ctx.fogStart = scene.fogStart;
        ctx.fogEnd = scene.fogEnd;
        ctx.fogBase = scene.fogBase;
        ctx.fogThickness = scene.fogThickness;
        if (lens_ && scene.baked) {
            ctx.baked = scene.baked;
            ctx.exposure = scene.exposure;
        }
        std::vector<MathPrim> ps;
        for (const auto& sp : scene.prims) ps.push_back(sp.prim);
        drawMathPrims3D(ps, ctx);
    }
    // Props: the models, every copy from one upload (fog included).
    if (scene.models && !scene.props.empty()) {
        PropDrawContext pctx;
        pctx.camPos = {eye.x, eye.y, eye.z};
        pctx.fog = scene.fog;
        pctx.fogColor = scene.fogColor;
        pctx.fogStart = scene.fogStart;
        pctx.fogEnd = scene.fogEnd;
        pctx.fogBase = scene.fogBase;
        pctx.fogThickness = scene.fogThickness;
        if (lens_ && scene.baked) {
            pctx.bake = scene.baked->shared();
            pctx.exposure = scene.exposure;
            pctx.material = &bctx;
        }
        std::vector<Prop> ps;
        for (const auto& sp : scene.props) ps.push_back(sp.prop);
        scene.models->draw(ps, pctx);
    }
    // See-through faces (materials with $translucent or $alpha < 1) last, far to near.
    if (bakedBrushes) {
        BakedDrawContext t = bctx;
        t.translucent = true;
        drawBakedBrushes(scene.brushes, *scene.baked, *scene.textures, t);
    }
    // The last bake's light grid (View menu): a small cube per sample, each side coloured by
    // the light from that side (sRGB, clipped). Editor only.
    if (!lens_ && scene.lightGrid) {
        const LightGrid& g = *scene.lightGrid;
        rlDrawRenderBatchActive();
        const float h = 6;
        auto col = [](const Vec3& c) {
            return Color{(unsigned char)std::lround(linearToSrgb(c.x) * 255), (unsigned char)std::lround(linearToSrgb(c.y) * 255),
                         (unsigned char)std::lround(linearToSrgb(c.z) * 255), 255};
        };
        for (int k = 0; k < g.nz; ++k)
            for (int j = 0; j < g.ny; ++j)
                for (int i = 0; i < g.nx; ++i) {
                    const size_t s = ((size_t)k * g.ny + j) * g.nx + i;
                    if (!g.valid[s]) continue;
                    const Vec3 p = g.at(i, j, k);
                    const float x = (float)p.x, y = (float)p.y, z = (float)p.z;
                    rlCheckRenderBatchLimit(36);
                    rlBegin(RL_TRIANGLES);
                    auto quad = [&](const Color& c, Vector3 a, Vector3 b, Vector3 cc, Vector3 d) {
                        rlColor4ub(c.r, c.g, c.b, c.a);
                        for (const Vector3& v : {a, b, cc, a, cc, d}) rlVertex3f(v.x, v.y, v.z);
                    };
                    const Vector3 c000{x - h, y - h, z - h}, c100{x + h, y - h, z - h}, c010{x - h, y + h, z - h}, c110{x + h, y + h, z - h};
                    const Vector3 c001{x - h, y - h, z + h}, c101{x + h, y - h, z + h}, c011{x - h, y + h, z + h}, c111{x + h, y + h, z + h};
                    const Vec3* cb = &g.cube[s * 6];
                    quad(col(cb[0]), c100, c110, c111, c101); // +x
                    quad(col(cb[1]), c000, c001, c011, c010); // -x
                    quad(col(cb[2]), c010, c011, c111, c110); // +y
                    quad(col(cb[3]), c000, c100, c101, c001); // -y
                    quad(col(cb[4]), c001, c101, c111, c011); // +z
                    quad(col(cb[5]), c000, c010, c110, c100); // -z
                    rlEnd();
                }
        rlDrawRenderBatchActive();
    }
    if (lens_) { // a picture: none of the editor's boxes, outlines, markers or labels
        rlDrawRenderBatchActive();
        EndMode3D();
        return;
    }
    // Boxes: selected props in red, missing models in orange (and every prop when the
    // shader can't run), the placement preview in yellow.
    {
        auto box3D = [&](const Scene::SceneProp& sp, Color c) {
            std::vector<Vec3> k;
            for (int i = 0; i < 8; ++i)
                k.push_back(propToWorld(sp.prop, {i & 1 ? sp.local.max.x : sp.local.min.x, i & 2 ? sp.local.max.y : sp.local.min.y,
                                                   i & 4 ? sp.local.max.z : sp.local.min.z}));
            for (int i = 0; i < 8; ++i)
                for (int b = 0; b < 3; ++b)
                    if (!(i & (1 << b))) DrawLine3D(toRl(k[i]), toRl(k[i | (1 << b)]), c);
        };
        const bool modelsOk = scene.models && scene.models->shaderOk();
        for (const auto& sp : scene.props) {
            if (sp.selected) box3D(sp, kSelected);
            else if (sp.missing) box3D(sp, kPropMissing);
            else if (!modelsOk) box3D(sp, kPropLine);
        }
        for (const auto& sp : scene.pendingProps) box3D(sp, kPreview);
    }
    // Their outlines: selected ones in red, the primitive tool's preview in yellow, and
    // all of them if the shader can't run.
    for (const auto& sp : scene.prims) {
        if (!sp.selected && mathPrimShaderOk()) continue;
        for (const Polyline& l : primOutline(sp.prim, 48))
            for (size_t i = 0; i + 1 < l.pts.size() + (l.closed ? 1 : 0); ++i)
                DrawLine3D(toRl(l.pts[i]), toRl(l.pts[(i + 1) % l.pts.size()]), sp.selected ? kSelected : kPrimLine);
    }
    for (const MathPrim& p : scene.pendingPrims)
        for (const Polyline& l : primOutline(p, 48))
            for (size_t i = 0; i + 1 < l.pts.size() + (l.closed ? 1 : 0); ++i)
                DrawLine3D(toRl(l.pts[i]), toRl(l.pts[(i + 1) % l.pts.size()]), kPreview);

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
    // Lights: shapes, reach of selected ones, rotation rings (drawn through walls).
    auto drawLight3D = [&](const Light& l, bool sel) {
        const Color col = lightColor(l);
        for (const auto& [a, b] : lightShape(l, 40 * worldPerPixel(l.pos))) DrawLine3D(toRl(a), toRl(b), sel ? kSelected : col);
        if (sel && l.kind != LightKind::Sun)
            for (int ax = 0; ax < 3; ++ax) {
                const auto pts = axisCircle(l.pos, ax, l.range, 96);
                for (size_t i = 0; i < pts.size(); ++i) DrawLine3D(toRl(pts[i]), toRl(pts[(i + 1) % pts.size()]), dim(col, 0.45f));
            }
    };
    for (const auto& sl : scene.lights) drawLight3D(sl.light, sl.selected);
    if (scene.pendingLight) drawLight3D(*scene.pendingLight, false);
    for (const Light& l : scene.previewLights) drawLight3D(l, false);
    for (const auto& sl : scene.lights) {
        if (!sl.selected || !sl.light.hasDirection()) continue;
        const double r = Scene::kRingPx * worldPerPixel(sl.light.pos);
        for (int ax = 0; ax < 3; ++ax) {
            const auto pts = axisCircle(sl.light.pos, ax, r, 64);
            const Color rc = scene.activeRing == ax ? WHITE : kAxisCol[ax];
            for (size_t i = 0; i < pts.size(); ++i) DrawLine3D(toRl(pts[i]), toRl(pts[(i + 1) % pts.size()]), rc);
        }
    }
    // Cameras: pyramids and rings (drawn through walls, like lights).
    auto drawCamera3D = [&](const Camera& c, bool sel, bool pending) {
        const Color col = cameraColor(c);
        const Color lineCol = sel ? kSelected : pending ? kPreview : c.active ? col : dim(col, 0.55f);
        for (const auto& [a, b] : cameraShape(c, kCameraLenPx * worldPerPixel(c.pos))) DrawLine3D(toRl(a), toRl(b), lineCol);
    };
    for (const auto& sc : scene.cameras) drawCamera3D(sc.cam, sc.selected, false);
    if (scene.pendingCamera) drawCamera3D(*scene.pendingCamera, false, true);
    for (const auto& sc : scene.cameras) {
        if (!sc.selected) continue;
        const double r = Scene::kRingPx * worldPerPixel(sc.cam.pos);
        for (int ax = 0; ax < 3; ++ax) {
            const auto pts = axisCircle(sc.cam.pos, ax, r, 64);
            const Color rc = scene.activeRing == ax ? WHITE : kAxisCol[ax];
            for (size_t i = 0; i < pts.size(); ++i) DrawLine3D(toRl(pts[i]), toRl(pts[(i + 1) % pts.size()]), rc);
        }
        const auto pts = dirCircle(sc.cam.pos, cameraForward(sc.cam), r * Scene::kRollRingScale, 64);
        const Color rc = scene.activeRing == 3 ? WHITE : kRollRing;
        for (size_t i = 0; i < pts.size(); ++i) DrawLine3D(toRl(pts[i]), toRl(pts[(i + 1) % pts.size()]), rc);
    }
    for (const Scene::PrimGizmo& gz : scene.gizmos) {
        const double r = std::max(gz.radius, Scene::kRingPx * worldPerPixel(gz.center));
        for (int ax = 0; ax < 3; ++ax) {
            const auto pts = axisCircle(gz.center, ax, r, 96);
            const Color rc = scene.activeRing == ax ? WHITE : kAxisCol[ax];
            for (size_t i = 0; i < pts.size(); ++i) DrawLine3D(toRl(pts[i]), toRl(pts[(i + 1) % pts.size()]), rc);
        }
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

    // Light markers on top (constant size on screen).
    const float P = scale_;
    auto marker3D = [&](const Light& l, Color frame, float f) {
        Vector2 c;
        if (!worldToLocal3D(l.pos, c)) return;
        c.x *= P;
        c.y *= P;
        const float hs = 4 * P;
        if (l.kind == LightKind::Sun) {
            DrawCircleV(c, hs + f, frame);
            DrawCircleV(c, hs, lightColor(l));
        } else {
            DrawRectangleRec({std::floor(c.x - hs - f), std::floor(c.y - hs - f), 2 * (hs + f) + 1, 2 * (hs + f) + 1}, frame);
            DrawRectangleRec({std::floor(c.x - hs), std::floor(c.y - hs), 2 * hs + 1, 2 * hs + 1}, lightColor(l));
        }
    };
    for (const auto& sl : scene.lights) marker3D(sl.light, sl.selected ? kSelected : BLACK, sl.selected ? 2 * P : P);
    auto cameraMarker3D = [&](const Camera& cam, Color frame, float f) {
        Vector2 c;
        if (!worldToLocal3D(cam.pos, c)) return;
        c.x *= P;
        c.y *= P;
        const float hs = 5 * P;
        DrawPoly(c, 4, hs + f * 1.5f, 0, frame);
        DrawPoly(c, 4, hs, 0, cameraColor(cam));
        if (cam.active) DrawCircleV(c, 1.5f * P, WHITE);
    };
    for (const auto& sc : scene.cameras) cameraMarker3D(sc.cam, sc.selected ? kSelected : BLACK, sc.selected ? 2 * P : P);
    if (scene.pendingCamera) cameraMarker3D(*scene.pendingCamera, kPreview, 2 * P);
    for (const Scene::PrimGizmo& gz : scene.gizmos)
        for (const auto& [pos, ax] : gz.handles) {
            Vector2 c;
            if (!worldToLocal3D(pos, c)) continue;
            c.x *= P;
            c.y *= P;
            const float hs = 3 * P;
            DrawRectangleRec({std::floor(c.x - hs - P), std::floor(c.y - hs - P), 2 * (hs + P) + 1, 2 * (hs + P) + 1}, BLACK);
            DrawRectangleRec({std::floor(c.x - hs), std::floor(c.y - hs), 2 * hs + 1, 2 * hs + 1}, kHandle);
        }
    if (scene.pendingLight) marker3D(*scene.pendingLight, kPreview, 2 * P);
    for (const Light& l : scene.previewLights) marker3D(l, kPreview, 2 * P);
    if (scene.selectRect && scene.selectRect->view == this) { // rectangle select
        const Vector2 a{scene.selectRect->ax, scene.selectRect->ay}, b{scene.selectRect->bx, scene.selectRect->by};
        const Rectangle r{std::floor(std::min(a.x, b.x) * P), std::floor(std::min(a.y, b.y) * P),
                          std::floor(std::fabs(b.x - a.x) * P), std::floor(std::fabs(b.y - a.y) * P)};
        DrawRectangleRec(r, Fade(kSelected, 0.12f));
        DrawRectangleLinesEx(r, std::max(1.0f, P), kSelected);
    }

    // Vertex tool: terrain points as small squares (the only handles picked in 3D).
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
    const float speed = (fast ? 4000.0f : 1250.0f) * dt; // cm per second
    const Vector3 f = forwardFrom(yaw_, pitch_);
    Vector3 r{f.y, -f.x, 0.0f};
    const float rl = std::sqrt(r.x * r.x + r.y * r.y);
    if (rl > 1e-5f) { r.x /= rl; r.y /= rl; }
    pos_.x += (f.x * fwd + r.x * rgt) * speed;
    pos_.y += (f.y * fwd + r.y * rgt) * speed;
    pos_.z += (f.z * fwd + up) * speed;
}

} // namespace scal
