#include "tools/terrain_tool.hpp"
#include "tools/texture_tool.hpp"
#include <cmath>

namespace scal {

std::vector<FaceRef> TerrainTool::pickedFaces(const Document& doc) const {
    std::vector<FaceRef> out;
    for (const FaceRef& f : picked_)
        if (const Brush* b = doc.find(f.first))
            if (f.second >= 0 && f.second < (int)b->faces.size()) out.push_back(f);
    return out;
}

void TerrainTool::pickPress(ToolEnv& env, const Pointer& p) {
    const Document& doc = env.doc;
    FaceRef hit{0, -1};
    if (p.view->is2D()) {
        // The face of the clicked brush that looks toward you.
        if (const Brush* b = doc.find(pick2D(env.sceneBrushes, *p.view, p.local))) {
            Vec3 eu, ev;
            eu[p.view->axU()] = 1;
            ev[p.view->axV()] = 1;
            hit = {b->id, faceFacing(*b, cross(eu, ev))};
        }
    } else {
        hit = pickFace3D(doc, *p.view, p.local);
    }
    if (hit.second < 0) {
        if (!env.shift) picked_.clear();
        message_ = "click a face (3D view) or a brush (2D view)";
        return;
    }
    const Brush* b = doc.find(hit.first);
    size_t corners = 0;
    for (const Polygon& pl : b->polygons()) if (pl.face == hit.second) corners = pl.verts.size();
    if (corners != 3 && corners != 4) {
        message_ = "that face has " + std::to_string(corners) + " corners: terrain needs 3 or 4 (clip it into smaller pieces)";
        return;
    }
    message_.clear();
    if (env.shift && picked_.erase(hit)) return;
    if (!env.shift) picked_.clear();
    // One terrain face per brush: a new pick on the same brush replaces the old one.
    for (auto it = picked_.begin(); it != picked_.end();)
        it = it->first == hit.first ? picked_.erase(it) : std::next(it);
    picked_.insert(hit);
}

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
    bool full = false;
    if (sculpt(work_, d, &full)) { changed_ = true; ++rev_; }
    if (full) message_ = "a patch already has 8 textures: erase one before painting a new one";
    lastDab_ = at;
}

void TerrainTool::press(ToolEnv& env, const Pointer& p) {
    if (picking_) { pickPress(env, p); return; }
    message_.clear();
    Vec3 at, n;
    if (!hit(env, p, at, n)) {
        bool any = false;
        for (const SceneBrush& b : env.sceneBrushes) if (b.terrain) any = true;
        message_ = any ? "click on a terrain surface" : "no terrain yet: Terrain > Make terrain, then click faces";
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
        for (Brush& b : work_) {
            const Brush* old = env.doc.find(b.id);
            if (!old || !old->terrain) continue;
            pruneTerrainLayers(*b.terrain); // textures erased everywhere are dropped
            if (*old->terrain != *b.terrain) out.push_back(b);
        }
        if (!out.empty())
            env.doc.apply(env.doc.makeModify(out, std::string("Terrain: ") + sculptModeName(mode)));
    }
    work_.clear();
    changed_ = false;
}

void TerrainTool::hover(ToolEnv& env, const Pointer& p) {
    if (stroking_) return;
    if (picking_) { cursor_.reset(); return; }
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
    if (key == KEY_ESCAPE && picking_) {
        stopPicking();
        message_.clear();
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

void TerrainTool::decorate(const Document& doc, Scene& scene) const {
    if (picking_) {
        for (const FaceRef& f : pickedFaces(doc)) {
            scene.selectedFaces.insert(f);
            if (const Brush* b = doc.find(f.first))
                for (const Polygon& p : b->polygons())
                    if (p.face == f.second) scene.outlines.push_back(p.verts);
        }
        return;
    }
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
    if (picking_)
        return picked_.empty() ? "make terrain: click a face in 3D (Shift+click adds), or a brush in 2D (its face toward you); Esc stops"
                               : "Return: make the picked faces terrain (grid size in Properties)   Shift+click: add/remove   Esc: stop";
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
