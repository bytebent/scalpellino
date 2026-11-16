#include "tools/curve_tool.hpp"
#include <cmath>
#include <cstdio>

namespace scal {

AABB CurveTool::box3D() const {
    AABB b;
    b.min[params.axU] = params.u0; b.max[params.axU] = params.u1;
    b.min[params.axV] = params.v0; b.max[params.axV] = params.v1;
    if (params.shape == CurveShape::SpiralStairs) {
        b.min[params.axD] = params.d0;
        b.max[params.axD] = params.d0 + params.rise * (params.steps - 1) + params.thickness;
    } else if (params.shape == CurveShape::HelixRamp) {
        b.min[params.axD] = params.d0;
        b.max[params.axD] = params.d0 + params.rise * params.steps + params.thickness;
    } else if (params.shape == CurveShape::Dome) {
        b.min[params.axD] = params.d0;
        b.max[params.axD] = params.d0 + std::min(params.u1 - params.u0, params.v1 - params.v0) / 2;
    } else {
        b.min[params.axD] = params.d0; b.max[params.axD] = params.d1;
    }
    return b;
}

void CurveTool::setBox3D(const AABB& b) {
    params.u0 = b.min[params.axU]; params.u1 = b.max[params.axU];
    params.v0 = b.min[params.axV]; params.v1 = b.max[params.axV];
    if (uprightShape()) params.d0 = b.min[params.axD]; // height comes from the shape's own settings
    else { params.d0 = b.min[params.axD]; params.d1 = b.max[params.axD]; }
}

const CurveResult& CurveTool::preview() const {
    std::string key;
    for (const auto& [k, v] : curveToKeys(params)) key += k + "=" + v + ";";
    if (key != cacheKey_) {
        cache_ = generateCurve(params);
        cacheKey_ = key;
    }
    return cache_;
}

void CurveTool::press(ToolEnv& env, const Pointer& p) {
    message_.clear();
    if (!p.view->is2D()) return;
    const bool wall = params.shape == CurveShape::CurvedWall;
    view_ = p.view;
    if (hasShape_) {
        // Curved wall: its three points, in its own view.
        if (wall && viewMatches(*p.view)) {
            const double pts[3][2] = {{params.u0, params.v0}, {params.u1, params.v1}, {params.cu, params.cv}};
            for (int i = 2; i >= 0; --i) {
                const Vector2 s = p.view->worldToLocal2D(pts[i][0], pts[i][1]);
                if (std::hypot(s.x - p.local.x, s.y - p.local.y) < 8) { dragging_ = Drag::Point; point_ = i; return; }
            }
        }
        // Box handles in any 2D view (in other views this also sets the depth).
        if (!wall || !viewMatches(*p.view)) {
            const BoxGrab g = grabBox(*p.view, box3D(), p.local);
            if (!g.none()) {
                dragging_ = Drag::Box;
                grab_ = g;
                startBox_ = box3D();
                u0_ = p.u; v0_ = p.v;
                return;
            }
        }
        if (editing_) return; // editing an existing curve: don't start a new one by accident
    }
    // Start drawing a new shape in this view.
    if (uprightShape() && p.view->kind() != ViewKind::Top) {
        message_ = "draw this shape in the top view (it rises upwards)";
        return;
    }
    params.axU = p.view->axU();
    params.axV = p.view->axV();
    params.axD = p.view->axD();
    au_ = env.grid.snapValue(p.u);
    av_ = env.grid.snapValue(p.v);
    // Depth: through the selected walls for an arched opening, else from the last brush.
    const int D = params.axD;
    const AABB sel = env.doc.selectionBounds();
    if (params.shape == CurveShape::CarveArch && sel.valid()) { params.d0 = sel.min[D]; params.d1 = sel.max[D]; }
    else if (uprightShape()) { params.d0 = env.lastBox.min[D]; }
    else { params.d0 = env.lastBox.min[D]; params.d1 = env.lastBox.max[D]; if (params.d1 <= params.d0) { params.d0 = 0; params.d1 = 16; } }
    params.u0 = params.u1 = au_;
    params.v0 = params.v1 = av_;
    params.cu = au_; params.cv = av_;
    hasShape_ = false;
    editing_ = 0;
    dragging_ = Drag::Drawing;
}

void CurveTool::drag(ToolEnv& env, const Pointer& p) {
    if (dragging_ == Drag::None || p.view != view_) return;
    const double u = env.grid.snapValue(p.u), v = env.grid.snapValue(p.v);
    if (dragging_ == Drag::Drawing) {
        if (params.shape == CurveShape::CurvedWall) {
            params.u0 = au_; params.v0 = av_; params.u1 = u; params.v1 = v;
            // Bend handle: a quarter of the length to the left of the middle.
            const double du = u - au_, dv = v - av_;
            params.cu = env.grid.snapValue((au_ + u) / 2 - dv / 4);
            params.cv = env.grid.snapValue((av_ + v) / 2 + du / 4);
            hasShape_ = std::hypot(du, dv) > 0;
        } else {
            params.u0 = std::min(au_, u); params.u1 = std::max(au_, u);
            params.v0 = std::min(av_, v); params.v1 = std::max(av_, v);
            if (params.shape == CurveShape::Ring || uprightShape()) {
                // Keep it square: a circle needs equal width and height.
                const double s = std::max(params.u1 - params.u0, params.v1 - params.v0);
                params.u0 = u >= au_ ? au_ : au_ - s; params.u1 = params.u0 + s;
                params.v0 = v >= av_ ? av_ : av_ - s; params.v1 = params.v0 + s;
            }
            hasShape_ = params.u1 > params.u0 && params.v1 > params.v0;
        }
    } else if (dragging_ == Drag::Box) {
        setBox3D(dragBox(*view_, startBox_, grab_, u0_, v0_, p.u, p.v, env.grid));
    } else if (dragging_ == Drag::Point) {
        if (point_ == 0) { params.u0 = u; params.v0 = v; }
        else if (point_ == 1) { params.u1 = u; params.v1 = v; }
        else { params.cu = u; params.cv = v; }
    }
}

void CurveTool::release(ToolEnv&, const Pointer&) {
    dragging_ = Drag::None;
    view_ = nullptr;
}

bool CurveTool::key(ToolEnv&, int key) {
    if (key == KEY_ESCAPE) {
        if (!hasShape_ && dragging_ == Drag::None) return false;
        cancel();
        return true;
    }
    return false; // Return is handled by the editor (it creates groups / carves)
}

void CurveTool::decorate(const Document& doc, Scene& scene) const {
    if (!hasShape_) return;
    const CurveResult& r = preview();
    std::vector<SceneBrush>& out = params.shape == CurveShape::CarveArch ? scene.invalidGhosts : scene.ghosts;
    for (const Brush& b : r.brushes) out.push_back(makeSceneBrush(b));
    for (const Brush& b : r.fill) scene.ghosts.push_back(makeSceneBrush(b));
    if (editing_)
        for (BrushId id : doc.groupMembers(editing_)) scene.hidden.insert(id);
    if (params.shape == CurveShape::CurvedWall) {
        Vec3 pts[3];
        const double uv[3][2] = {{params.u0, params.v0}, {params.u1, params.v1}, {params.cu, params.cv}};
        const AABB b = box3D();
        for (int i = 0; i < 3; ++i) {
            pts[i][params.axU] = uv[i][0];
            pts[i][params.axV] = uv[i][1];
            pts[i][params.axD] = (b.min[params.axD] + b.max[params.axD]) / 2;
        }
        scene.clipPoints = {pts[0], pts[1], pts[2]};
    } else {
        scene.previewBox = box3D();
        scene.handleBox = box3D();
    }
}

const char* CurveTool::hint() const {
    if (!message_.empty()) return message_.c_str();
    if (dragging_ == Drag::Drawing) return "drag to size the shape";
    if (hasShape_) {
        const CurveResult& r = preview();
        if (!r.error.empty()) {
            std::snprintf(hintBuf_, sizeof hintBuf_, "can't build: %s", r.error.c_str());
            return hintBuf_;
        }
        std::snprintf(hintBuf_, sizeof hintBuf_, "%zu brushes   Return: %s   Esc: cancel   handles / Properties panel to adjust",
                      r.brushes.size(), params.shape == CurveShape::CarveArch ? "cut the opening" : editing_ ? "update" : "create");
        return hintBuf_;
    }
    if (params.shape == CurveShape::CarveArch) return "select the wall(s), then drag the opening's box in a 2D view";
    if (params.shape == CurveShape::CurvedWall) return "drag from the wall's start to its end in a 2D view";
    return "choose a shape in the Properties panel, then drag a box in a 2D view";
}

void CurveTool::cancel() {
    hasShape_ = false;
    editing_ = 0;
    dragging_ = Drag::None;
    view_ = nullptr;
}

} // namespace scal
