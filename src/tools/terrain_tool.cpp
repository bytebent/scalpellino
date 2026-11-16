#include "tools/terrain_tool.hpp"
#include <cmath>

namespace scal {

bool TerrainTool::hit(const ToolEnv& env, const Pointer& p, Vec3& point, Vec3& normal) const {
    if (!p.view) return false;
    // Rays: 3D view = through the pixel; 2D view = straight along the view's depth,
    // tried from both sides.
    std::vector<std::pair<Vec3, Vec3>> rays;
    if (p.view->is2D()) {
        Vec3 o;
        o[p.view->axU()] = p.u;
        o[p.view->axV()] = p.v;
        Vec3 d;
        d[p.view->axD()] = 1;
        rays.push_back({o + d * 1e6, d * -1.0});
        rays.push_back({o - d * 1e6, d});
    } else {
        Vec3 o, d;
        p.view->pickRay(p.local, o, d);
        rays.push_back({o, d});
    }
    double best = -1;
    auto test = [&](const TerrainGrid& g) {
        for (const auto& [o, d] : rays) {
            const double t = rayHitGrid(g, o, d);
            if (t >= 0 && (best < 0 || t < best)) { best = t; point = o + d * t; normal = g.normal; }
        }
    };
    if (stroking_) {
        for (const Brush& b : work_)
            if (auto g = terrainGrid(b)) test(*g);
    } else {
        for (const SceneBrush& b : env.sceneBrushes)
            if (b.terrain) test(*b.terrain);
    }
    return best >= 0;
}

void TerrainTool::setCursor(const ToolEnv& env, const Vec3& at, const Vec3& n) {
    Scene::BrushCursor c{at, n, radius, {}};
    // Each circle point dropped onto the surface along the normal (kept flat where it
    // misses, e.g. past the terrain's edge).
    Vec3 a = std::fabs(n.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    a = normalize(cross(n, a));
    const Vec3 b = cross(n, a);
    std::vector<TerrainGrid> grids;
    if (stroking_) { for (const Brush& br : work_) if (auto g = terrainGrid(br)) grids.push_back(std::move(*g)); }
    else { for (const SceneBrush& sb : env.sceneBrushes) if (sb.terrain) grids.push_back(*sb.terrain); }
    for (int i = 0; i < 48; ++i) {
        const double t = 2 * 3.14159265358979323846 * i / 48;
        const Vec3 flat = at + a * (std::cos(t) * radius) + b * (std::sin(t) * radius);
        const Vec3 o = flat + n * 1e5;
        double best = -1;
        for (const TerrainGrid& g : grids) {
            const double h = rayHitGrid(g, o, n * -1.0);
            if (h >= 0 && (best < 0 || h < best)) best = h;
        }
        c.ring.push_back(best >= 0 ? o - n * (best - 1.0) : flat); // 1 unit above: not hidden by it
    }
    cursor_ = std::move(c);
}

void TerrainTool::dab(const Vec3& at) {
    SculptDab d;
    d.mode = mode;
    d.falloff = falloff;
    d.center = at;
    d.radius = radius;
    d.amount = amount;
    d.strength = strength / 100.0;
    d.flattenTarget = flatten_;
    d.seed = seed_;
    if (mode == SculptMode::Paint) d.texture = paintTexture;
    if (sculpt(work_, d)) { changed_ = true; ++rev_; }
    lastDab_ = at;
}

void TerrainTool::press(ToolEnv& env, const Pointer& p) {
    message_.clear();
    Vec3 at, n;
    if (!hit(env, p, at, n)) {
        bool any = false;
        for (const SceneBrush& b : env.sceneBrushes) if (b.terrain) any = true;
        message_ = any ? "click on a terrain surface" : "no terrain yet: select brushes, then Terrain > Make terrain";
        return;
    }
    work_.clear();
    for (const Brush& b : env.doc.brushes())
        if (b.terrain && env.doc.visible(b) && terrainValid(b)) work_.push_back(b);
    stroking_ = true;
    changed_ = false;
    flatten_ = at; // flatten levels to the height where the stroke started
    ++seed_;
    setCursor(env, at, n);
    dab(at);
}

void TerrainTool::drag(ToolEnv& env, const Pointer& p) {
    if (!stroking_) return;
    Vec3 at, n;
    if (!hit(env, p, at, n)) return;
    setCursor(env, at, n);
    // Dabs every fifth of the radius, so a stroke doesn't depend on the mouse speed.
    if (length(at - lastDab_) >= std::max(1.0, radius * 0.2)) dab(at);
}

void TerrainTool::release(ToolEnv& env, const Pointer&) {
    if (!stroking_) return;
    stroking_ = false;
    if (changed_) {
        std::vector<Brush> out;
        for (const Brush& b : work_) {
            const Brush* old = env.doc.find(b.id);
            if (!old || !old->terrain) continue;
            if (old->terrain->heights != b.terrain->heights || old->terrain->alpha != b.terrain->alpha ||
                old->terrain->texture2 != b.terrain->texture2)
                out.push_back(b);
        }
        if (!out.empty())
            env.doc.apply(env.doc.makeModify(out, std::string("Terrain: ") + sculptModeName(mode)));
    }
    work_.clear();
    changed_ = false;
}

void TerrainTool::hover(ToolEnv& env, const Pointer& p) {
    if (stroking_) return;
    Vec3 at, n;
    if (hit(env, p, at, n)) setCursor(env, at, n);
    else cursor_.reset();
}

bool TerrainTool::key(ToolEnv&, int key) {
    if (key == KEY_ESCAPE && stroking_) {
        cancel();
        message_ = "stroke cancelled";
        return true;
    }
    return false;
}

void TerrainTool::cancel() {
    stroking_ = false;
    changed_ = false;
    work_.clear();
    cursor_.reset();
}

void TerrainTool::decorate(const Document&, Scene& scene) const {
    if (cursor_) scene.brushCursor = *cursor_;
    if (stroking_ && changed_) {
        if (drawnRev_ != rev_) {
            drawn_.clear();
            for (const Brush& b : work_) drawn_.push_back(makeSceneBrush(b));
            drawnRev_ = rev_;
        }
        for (const SceneBrush& b : drawn_) scene.replaced.push_back(b);
    }
}

const char* TerrainTool::hint() const {
    if (!message_.empty()) return message_.c_str();
    if (stroking_) return "drag to keep going; release to finish (one undo step), Esc cancels";
    switch (mode) {
        case SculptMode::Raise: return "drag over terrain to raise it (radius and amount in Properties)";
        case SculptMode::Lower: return "drag over terrain to lower it (radius and amount in Properties)";
        case SculptMode::Smooth: return "drag over terrain to smooth it (radius and strength in Properties)";
        case SculptMode::Flatten: return "drag to level the terrain to the height where you start";
        case SculptMode::Noise: return "drag to roughen the terrain (amount = how much)";
        case SculptMode::Paint: return "drag to paint the current texture onto the terrain";
        case SculptMode::Erase: return "drag to remove painted texture";
    }
    return "";
}

} // namespace scal
