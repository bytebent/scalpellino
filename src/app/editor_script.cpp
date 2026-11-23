// Editor (part of the class, split by topic; see editor.hpp): Test scripts (--script): the command interpreter and screenshots.
#include "app/editor.hpp"
#include "io/map_format.hpp"
#include "core/hull.hpp"
#include "core/texture.hpp"
#include "core/csg.hpp"
#include "core/curves.hpp"
#include "app/settings.hpp"
#include "platform/file_dialog.hpp"
#include "ui/layout.hpp"
#include "ui/imgui_checks.hpp"

#include "imgui.h"
#include "rlImGui.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include "app/editor_util.hpp"
#include "app/editor_templates.hpp"

namespace scal {

using namespace editor_util;

//
// One command per frame; lines starting with # are comments. The script language is
// documented in docs/DEV.md (keep that reference in sync when adding commands).
// A failed `expect` prints FAIL and makes the exit code 1.

void Editor::loadScript(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "script: cannot open %s\n", path.c_str());
        ++scriptFailures_;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        script_.push_back(line);
    }
    if (script_.empty()) script_.push_back("quit");
}

void Editor::checkScreenEdges() {
    edgeCheck_ = false;
    // Normally checks the real screen (what the user sees). With a forced view scale
    // (SCAL_VIEW_SCALE, used to exercise Retina rendering on a 1x test display) the
    // screen shows the views squeezed, so each view's own image is checked instead.
    const bool viewImages = viewScale_ > 0;
    Image screen{};
    float fb = 1.0f;
    if (!viewImages) {
        screen = LoadImageFromScreen(); // physical pixels, top-left origin
        fb = screen.width / (float)GetScreenWidth();
    }
    const Scene scene = buildScene();
    int checked = 0, missing = 0;
    for (int i = 1; i < 4; ++i) {
        const Viewport& v = *views_[i];
        Image img = viewImages ? LoadImageFromTexture(v.target().texture) : screen;
        if (viewImages) ImageFlipVertical(&img); // render textures are stored upside down
        const float s = viewImages ? v.scale() : fb;
        const float ox = viewImages ? 0.0f : viewOrigin_[i].x, oy = viewImages ? 0.0f : viewOrigin_[i].y;
        auto bright = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= img.width || y >= img.height) return false;
            const Color c = GetImageColor(img, x, y);
            return c.r > 200 || (c.r + c.g + c.b) > 400; // brush/selection/preview, never grid
        };
        const int U = v.axU(), V = v.axV();
        auto checkSeg = [&](const Vec3& a, const Vec3& b) {
            const Vector2 la = v.worldToLocal2D(a[U], a[V]), lb = v.worldToLocal2D(b[U], b[V]);
            for (float t : {0.3f, 0.7f}) {
                const float lx = la.x + (lb.x - la.x) * t, ly = la.y + (lb.y - la.y) * t;
                if (lx < 4 || ly < 4 || lx > v.width() - 4 || ly > v.height() - 4) continue;
                const int px = (int)((ox + lx) * s), py = (int)((oy + ly) * s);
                bool ok = false;
                for (int dy = -2; dy <= 2 && !ok; ++dy)
                    for (int dx = -2; dx <= 2 && !ok; ++dx) ok = bright(px + dx, py + dy);
                ++checked;
                if (!ok) {
                    ++missing;
                    std::fprintf(stderr, "edge missing: %s view at local %.1f,%.1f\n", v.label(), lx, ly);
                }
            }
        };
        for (const SceneBrush& b : scene.brushes)
            for (const Polygon& p : b.polys)
                for (size_t k = 0; k < p.verts.size(); ++k) checkSeg(p.verts[k], p.verts[(k + 1) % p.verts.size()]);
        if (scene.previewBox) {
            const AABB& bx = *scene.previewBox;
            Vec3 c[4];
            for (int k = 0; k < 4; ++k) c[k] = bx.min;
            c[1][U] = bx.max[U]; c[2][U] = bx.max[U]; c[2][V] = bx.max[V]; c[3][V] = bx.max[V];
            for (int k = 0; k < 4; ++k) checkSeg(c[k], c[(k + 1) % 4]);
        }
        if (viewImages) UnloadImage(img);
    }
    if (!viewImages) UnloadImage(screen);
    if (missing > 0 || checked == 0) {
        std::fprintf(stderr, "script FAIL: %d of %d edge samples not visible\n", missing, checked);
        ++scriptFailures_;
    }
}

std::string Editor::takeScreenshotRequest() {
    if (!opt_.screenshot.empty() && --screenshotFrames_ == 0) {
        exit_ = true;
        return opt_.screenshot;
    }
    std::string s;
    s.swap(shotRequest_);
    return s;
}

void Editor::stepScript() {
    if (script_.empty()) return;
    // Every scripted frame: no two widgets may share an ID (ImGui would mix them up).
    static bool idCheck = false;
    if (!idCheck) { enableImGuiIdCheck(); idCheck = true; }
    for (const std::string& label : takeImGuiIdConflicts()) {
        std::fprintf(stderr, "script FAIL (command %d): two widgets share the ID of '%s'\n", scriptLine_, label.c_str());
        ++scriptFailures_;
    }
    if (warmup_ > 0) { --warmup_; return; } // let views get their sizes first
    if (skyBake_) return; // a Sky Lab bake finishes before the next command
    if (scriptWaitRender_ && renderNeedsFrame()) return; // `render` finishes first
    scriptWaitRender_ = false;
    std::istringstream in(script_.front());
    script_.pop_front();
    std::string cmd;
    in >> cmd;
    ++scriptLine_;
    if (scriptCommands_.empty()) {
        registerScriptCommands();
        registerScriptExpects();
    }
    if (cmd == "expect") {
        // by its second word; anything else ("selected", "brushes") is a count check
        std::string what;
        { std::istringstream peek(in.str()); std::string e; peek >> e >> what; }
        auto it = scriptExpects_.find(what);
        if (it == scriptExpects_.end()) it = scriptExpects_.find("");
        it->second(cmd, in);
        return;
    }
    if (cmd == "refused") {
        // refused <command ...>: the command must report a failure (it was refused); that
        // failure doesn't count against the script.
        std::string inner;
        in >> inner;
        auto it = scriptCommands_.find(inner);
        if (it == scriptCommands_.end()) { std::fprintf(stderr, "script: unknown command '%s'\n", inner.c_str()); return; }
        const int before = scriptFailures_;
        it->second(inner, in);
        if (scriptFailures_ > before) {
            scriptFailures_ = before;
            std::fprintf(stderr, "script: command %d was refused, as expected (the FAIL line above is expected and not counted)\n", scriptLine_);
        } else {
            std::fprintf(stderr, "script FAIL (command %d): '%s' was expected to be refused\n", scriptLine_, inner.c_str());
            ++scriptFailures_;
        }
        return;
    }
    auto it = scriptCommands_.find(cmd);
    if (it != scriptCommands_.end()) { it->second(cmd, in); return; }
    std::fprintf(stderr, "script: unknown command '%s'\n", cmd.c_str());
}

} // namespace scal
