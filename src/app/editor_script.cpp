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
    std::istringstream in(script_.front());
    script_.pop_front();
    std::string cmd;
    in >> cmd;
    ++scriptLine_;
    static int dragView = -1;
    if (cmd == "tool") {
        std::string t; in >> t;
        setTool(t == "block" ? (Tool*)&blockTool_ : t == "vertex" ? (Tool*)&vertexTool_
              : t == "texture" ? (Tool*)&textureTool_ : t == "clip" ? (Tool*)&clipTool_
              : t == "curve" ? (Tool*)&curveTool_ : t == "terrain" ? (Tool*)&terrainTool_ : t == "light" ? (Tool*)&lightTool_ : t == "prim" ? (Tool*)&primTool_ : t == "props" ? (Tool*)&propTool_ : (Tool*)&selectTool_);
    } else if (cmd == "press") {
        int v; float x, y; in >> v >> x >> y;
        std::string mod;
        cmd_ = shift_ = alt_ = false;
        while (in >> mod) {
            if (mod == "shift") shift_ = true;
            if (mod == "cmd") cmd_ = true;
            if (mod == "alt") alt_ = true;
        }
        dragView = v;
        onPress(v, {x, y}, false);
    } else if (cmd == "drag") {
        float x, y; in >> x >> y;
        if (dragView >= 0) onDrag(dragView, {x, y});
    } else if (cmd == "release") {
        if (dragView >= 0) onRelease(dragView, {0, 0}, false);
        dragView = -1;
    } else if (cmd == "key") {
        std::string k; in >> k;
        if (int code = keyFromName(k)) onKey(code);
    } else if (cmd == "transform") {
        // transform move x y z | rotate <x|y|z> <deg> [origin] | scale sx sy sz
        std::string kind; in >> kind;
        TransformSpec t;
        t.pivot = doc_.selectionBounds().center();
        if (kind == "move") { t.kind = TransformKind::Move; in >> t.move.x >> t.move.y >> t.move.z; }
        else if (kind == "rotate") {
            std::string ax, piv; t.kind = TransformKind::Rotate; in >> ax >> t.degrees >> piv;
            t.axis = ax == "x" ? 0 : ax == "y" ? 1 : 2;
            if (piv == "origin") t.pivot = {};
        } else { t.kind = TransformKind::Scale; in >> t.scale.x >> t.scale.y >> t.scale.z; }
        if (!applyTransform(t, "Transform")) std::fprintf(stderr, "script: transform failed: %s\n", tfError_.c_str());
    } else if (cmd == "rotate90") { std::string ax; int sg = 1; in >> ax >> sg; rotate90(ax == "x" ? 0 : ax == "y" ? 1 : 2, sg);
    } else if (cmd == "flip") { std::string ax; in >> ax; flip(ax == "x" ? 0 : ax == "y" ? 1 : 2);
    } else if (cmd == "duplicate") { duplicate();
    } else if (cmd == "closedialogs") { closeDialogs_ = true;
    } else if (cmd == "printviews") {
        for (int i = 0; i < 4; ++i)
            std::fprintf(stderr, "view %d origin %.1f,%.1f size %dx%d\n", i, viewOrigin_[i].x, viewOrigin_[i].y, views_[i]->width(), views_[i]->height());
    } else if (cmd == "snapgrid") { snapSelectionToGrid();
    } else if (cmd == "layout") {
        std::string l; in >> l;
        showPanels_ = l != "classic";
        resetLayout_ = true;
        warmup_ = 3; // let the docking settle before the next command
    } else if (cmd == "wpress" || cmd == "wdrag") {
        // World-coordinate input for 2D views: wpress <view> <u> <v> [mods] / wdrag <u> <v>
        static int wview = 1;
        double u = 0, v = 0;
        if (cmd == "wpress") in >> wview;
        in >> u >> v;
        const Vector2 local = views_[wview]->worldToLocal2D(u, v);
        if (cmd == "wpress") {
            std::string mod;
            cmd_ = shift_ = alt_ = false;
            while (in >> mod) { if (mod == "shift") shift_ = true; if (mod == "cmd") cmd_ = true; if (mod == "alt") alt_ = true; }
            dragView = wview;
            onPress(wview, local, false);
        } else if (dragView >= 0) onDrag(dragView, local);
    } else if (cmd == "press3d") {
        // press3d <dx> <dy> [mods]: click relative to the 3D view's centre
        float dx = 0, dy = 0; in >> dx >> dy;
        std::string mod;
        cmd_ = shift_ = alt_ = false;
        while (in >> mod) { if (mod == "shift") shift_ = true; if (mod == "cmd") cmd_ = true; if (mod == "alt") alt_ = true; }
        dragView = 0;
        onPress(0, {views_[0]->width() * 0.5f + dx, views_[0]->height() * 0.5f + dy}, false);
    } else if (cmd == "drag3d") {
        // drag3d <dx> <dy>: drag in the 3D view, relative to its centre (after press3d)
        float dx = 0, dy = 0; in >> dx >> dy;
        if (dragView == 0) onDrag(0, {views_[0]->width() * 0.5f + dx, views_[0]->height() * 0.5f + dy});
    } else if (cmd == "texturedir") { std::string d; std::getline(in >> std::ws, d); textures_.addUserFolder(d);
    } else if (cmd == "texture") { in >> currentTexture_;
    } else if (cmd == "applytexture") { applyCurrentTexture();
    } else if (cmd == "applynodraw") { applyTexture(kNoDrawTexture);
    } else if (cmd == "wraparound") { wrapAroundPicked();
    } else if (cmd == "clippoints") {
        Vec3 a, b, c;
        in >> a.x >> a.y >> a.z >> b.x >> b.y >> b.z >> c.x >> c.y >> c.z;
        setTool(&clipTool_);
        clipTool_.setPoints(a, b, c);
    } else if (cmd == "clipkeep") {
        std::string k; in >> k;
        clipTool_.keep = k == "front" ? ClipTool::Keep::Front : k == "back" ? ClipTool::Keep::Back : ClipTool::Keep::Both;
    } else if (cmd == "carve") { carveWithSelection();
    } else if (cmd == "findoverlaps") { findOverlapsNow();
    } else if (cmd == "nextoverlap") { focusOverlap(overlapIndex_ + 1);
    } else if (cmd == "toorigin") { allViewsToOrigin();
    } else if (cmd == "expect" && in.str().find(" overlaps ") != std::string::npos) {
        std::string w; size_t n = 0; in >> w >> n;
        if (overlaps_.size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu overlaps, got %zu\n", scriptLine_, n, overlaps_.size()); ++scriptFailures_; }
    } else if (cmd == "hide") { hideSelected();
    } else if (cmd == "hideunselected") { hideUnselected();
    } else if (cmd == "showall") { showAll();
    } else if (cmd == "visgroup") {
        // visgroup new <name> | visgroup show <name> | visgroup hide <name>
        std::string op, name; in >> op; std::getline(in >> std::ws, name);
        if (op == "new") {
            std::vector<Visgroup> all = doc_.visgroups();
            const std::uint32_t id = doc_.newVisgroupId();
            all.push_back({id, name, true});
            doc_.setVisgroups(all);
            std::vector<Brush> out;
            for (BrushId bid : doc_.selection()) if (const Brush* b = doc_.find(bid)) { Brush c = *b; c.visgroup = id; out.push_back(c); }
            if (!out.empty()) doc_.apply(doc_.makeModify(out, "New visgroup"));
        } else {
            for (const Visgroup& v : doc_.visgroups()) if (v.name == name) setVisgroupVisible(v.id, op == "show");
        }
    } else if (cmd == "expect" && in.str().find(" bottoms ") != std::string::npos) {
        // expect bottoms <z>: every selected primitive object and prop has its lowest point at z
        std::string w; double z = 0; in >> w >> z;
        int bad = 0;
        for (const auto& obj : selectedObjects(doc_)) bad += std::fabs(objectBottom(obj) - z) > 0.05;
        for (PropId id : doc_.propSelection())
            if (const Prop* q = doc_.findProp(id)) bad += std::fabs(propBottom(*q, models_.find(q->model)) - z) > 0.05;
        if (bad) { std::fprintf(stderr, "script FAIL (command %d): %d selected object(s) not resting at z %g\n", scriptLine_, bad, z); ++scriptFailures_; }
    } else if (cmd == "expect" && in.str().find(" visible ") != std::string::npos) {
        std::string w; size_t n = 0; in >> w >> n;
        size_t got = 0;
        for (const Brush& b : doc_.brushes()) got += doc_.visible(b);
        if (got != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu visible brushes, got %zu\n", scriptLine_, n, got); ++scriptFailures_; }
    } else if (cmd == "terrain") {
        // terrain pick | make <cells per side> | remove | stitch | heightmap <path> <low> <high>
        std::string op; in >> op;
        if (op == "pick") startMakeTerrain();
        else if (op == "make") {
            int cells = 8; in >> cells;
            int pw = 0; while ((1 << pw) < cells) ++pw;
            makeTerrainNow(pw);
            if (!terrainError_.empty()) { std::fprintf(stderr, "script FAIL (command %d): %s\n", scriptLine_, terrainError_.c_str()); ++scriptFailures_; }
        } else if (op == "remove") removeTerrain();
        else if (op == "stitch") stitchAllTerrain();
        else if (op == "heightmap") {
            std::string path; double lo = 0, hi = 0; in >> path >> lo >> hi;
            if (!importHeightmap(path, lo, hi)) { std::fprintf(stderr, "script FAIL (command %d): %s\n", scriptLine_, terrainError_.c_str()); ++scriptFailures_; }
        }
    } else if (cmd == "lightset" || cmd == "lightedit") {
        // lightset <key> <value...>: the next light (light tool); lightedit: the selected lights
        std::string k; in >> k;
        auto setField = [&](Light& l, std::istringstream& vals) {
            if (k == "kind") {
                std::string v; vals >> v;
                for (int i = 0; i < 3; ++i) if (v == lightKindName((LightKind)i)) l.kind = (LightKind)i;
            } else if (k == "color") { vals >> l.color.x >> l.color.y >> l.color.z; }
            else if (k == "brightness") vals >> l.brightness;
            else if (k == "range") vals >> l.range;
            else if (k == "pitch") vals >> l.pitch;
            else if (k == "yaw") vals >> l.yaw;
            else if (k == "inner") vals >> l.inner;
            else if (k == "outer") vals >> l.outer;
            else if (k == "pos") { vals >> l.pos.x >> l.pos.y >> l.pos.z; }
        };
        std::string rest; std::getline(in, rest);
        if (cmd == "lightset") {
            std::istringstream vals(rest);
            setField(lightTool_.templ, vals);
            if (k == "pos") lightTool_.pending = lightTool_.templ.pos;
        } else {
            editLights("Edit light", [&](Light& l) { std::istringstream vals(rest); setField(l, vals); });
        }
    } else if (cmd == "expect" && in.str().find(" lights ") != std::string::npos) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.lights().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu lights, got %zu\n", scriptLine_, n, doc_.lights().size()); ++scriptFailures_; }
    } else if (cmd == "expect" && in.str().find(" lightsel ") != std::string::npos) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.lightSelection().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu selected lights, got %zu\n", scriptLine_, n, doc_.lightSelection().size()); ++scriptFailures_; }
    } else if (cmd == "expect" && (in.str().find(" lightpos ") != std::string::npos || in.str().find(" lightdir ") != std::string::npos)) {
        // expect lightpos x y z (+-0.5) | expect lightdir pitch yaw (+-0.01): the first selected light
        std::string w; in >> w;
        const Light* l = doc_.lightSelection().empty() ? nullptr : doc_.findLight(*doc_.lightSelection().begin());
        if (!l) { std::fprintf(stderr, "script FAIL (command %d): no light selected\n", scriptLine_); ++scriptFailures_; }
        else if (w == "lightpos") {
            Vec3 v; in >> v.x >> v.y >> v.z;
            if (length(l->pos - v) > 0.5) { std::fprintf(stderr, "script FAIL (command %d): light at %g %g %g\n", scriptLine_, l->pos.x, l->pos.y, l->pos.z); ++scriptFailures_; }
        } else {
            double p = 0, y = 0; in >> p >> y;
            if (std::fabs(l->pitch - p) > 0.01 || std::fabs(l->yaw - y) > 0.01) { std::fprintf(stderr, "script FAIL (command %d): light pitch %g yaw %g\n", scriptLine_, l->pitch, l->yaw); ++scriptFailures_; }
        }
    } else if (cmd == "prim") {
        // prim <kind> x y z  sx sy sz  [rx ry rz]: adds a math primitive (current texture)
        std::string k; in >> k;
        MathPrim p;
        if (auto pk = primKindFromName(k)) p.kind = *pk;
        in >> p.pos.x >> p.pos.y >> p.pos.z >> p.size.x >> p.size.y >> p.size.z;
        Vec3 r; if (in >> r.x >> r.y >> r.z) p.rot = r;
        p.texture = currentTexture_;
        doc_.apply(doc_.makeAddPrims({p}, "Create primitive"));
    } else if (cmd == "primedit") {
        // primedit <key> <value...> on the selected primitives:
        //   kind k | pos x y z | rot x y z | size x y z | round r | thickness t | op add|subtract|intersect
        //   | texture <name|none> | color r g b | texscale s
        std::string k; in >> k;
        std::string rest; std::getline(in, rest);
        std::vector<MathPrim> out;
        for (MathPrimId id : doc_.primSelection())
            if (const MathPrim* q = doc_.findPrim(id)) {
                MathPrim p = *q;
                std::istringstream v(rest);
                if (k == "kind") { std::string n; v >> n; if (auto pk = primKindFromName(n)) p.kind = *pk; }
                else if (k == "pos") v >> p.pos.x >> p.pos.y >> p.pos.z;
                else if (k == "rot") v >> p.rot.x >> p.rot.y >> p.rot.z;
                else if (k == "size") v >> p.size.x >> p.size.y >> p.size.z;
                else if (k == "round") v >> p.round;
                else if (k == "thickness") v >> p.thickness;
                else if (k == "blend") v >> p.blend;
                else if (k == "op") { std::string o; v >> o; p.op = o == "subtract" ? PrimOp::Subtract : o == "intersect" ? PrimOp::Intersect : PrimOp::Add; }
                else if (k == "texture") { std::string t; v >> t; p.texture = t == "none" ? "" : t; }
                else if (k == "color") v >> p.color.x >> p.color.y >> p.color.z;
                else if (k == "texscale") v >> p.texScale;
                out.push_back(p);
            }
        if (!out.empty()) doc_.apply(doc_.makeModifyPrims(out, "Edit primitive"));
    } else if (cmd == "primset") {
        // primset kind <k> | round <r> | thickness <t> | flat on|off | color r g b | texscale s: the next primitive
        std::string k; in >> k;
        MathPrim& t = primTool_.templ;
        if (k == "kind") { std::string n; in >> n; if (auto pk = primKindFromName(n)) t.kind = *pk; }
        else if (k == "round") in >> t.round;
        else if (k == "thickness") in >> t.thickness;
        else if (k == "flat") { std::string v; in >> v; primTool_.flat = v == "on"; }
        else if (k == "color") in >> t.color.x >> t.color.y >> t.color.z;
        else if (k == "texscale") in >> t.texScale;
    } else if (cmd == "primbox") {
        // primbox cx cy cz sx sy sz [view axis]: the pending box, typed
        Vec3 c, sz; int ax = 2; in >> c.x >> c.y >> c.z >> sz.x >> sz.y >> sz.z; in >> ax;
        primTool_.setPending(c, sz, ax);
    } else if (cmd == "expect" && (in.str().find(" primpos ") != std::string::npos || in.str().find(" primrot ") != std::string::npos ||
                                   in.str().find(" primsize ") != std::string::npos)) {
        // expect primpos|primrot|primsize x y z: the first selected primitive (+-0.01)
        std::string w; Vec3 v; in >> w >> v.x >> v.y >> v.z;
        const MathPrim* p = doc_.primSelection().empty() ? nullptr : doc_.findPrim(*doc_.primSelection().begin());
        const Vec3 have = !p ? Vec3{} : w == "primpos" ? p->pos : w == "primrot" ? p->rot : p->size;
        if (!p || length(have - v) > 0.01) {
            std::fprintf(stderr, "script FAIL (command %d): %s is %g %g %g\n", scriptLine_, w.c_str(), have.x, have.y, have.z);
            ++scriptFailures_;
        }
    } else if (cmd == "expect" && in.str().find(" primop ") != std::string::npos) {
        std::string w, o; in >> w >> o;
        const MathPrim* p = doc_.primSelection().empty() ? nullptr : doc_.findPrim(*doc_.primSelection().begin());
        if (!p || primOpName(p->op) != o || !p->combo) { std::fprintf(stderr, "script FAIL (command %d): not a '%s' piece of a combination\n", scriptLine_, o.c_str()); ++scriptFailures_; }
    } else if (cmd == "snapland") {
        snapToLand();
    } else if (cmd == "snapground") {
        double h = 0; in >> h; snapToGround(h);
    } else if (cmd == "replicate") {
        // replicate <copies> ox oy oz [rot rx ry rz] [scale sx sy sz] [scaleoffset] [pivot selection|origin|x y z]
        ReplicateSpec r;
        in >> r.copies >> r.offset.x >> r.offset.y >> r.offset.z;
        std::string w;
        while (in >> w) {
            if (w == "rot") in >> r.rotate.x >> r.rotate.y >> r.rotate.z;
            else if (w == "scale") in >> r.scale.x >> r.scale.y >> r.scale.z;
            else if (w == "scaleoffset") r.scaleOffset = true;
            else if (w == "pivot") {
                std::string p; in >> p;
                if (p == "origin") r.pivot = ReplicateSpec::Pivot::Origin;
                else if (p != "selection") { r.pivot = ReplicateSpec::Pivot::Point; r.point.x = std::atof(p.c_str()); in >> r.point.y >> r.point.z; }
            }
        }
        repSpec_ = r;
        applyReplicate(r);
    } else if (cmd == "replicateopen") {
        // replicateopen <copies> ox oy oz: open the Multi-Replicate window with these values
        ReplicateSpec r = repSpec_;
        in >> r.copies >> r.offset.x >> r.offset.y >> r.offset.z;
        openReplicate();
        repSpec_ = r;
    } else if (cmd == "expect" && in.str().find(" replicating ") != std::string::npos) {
        std::string w, v; in >> w >> v;
        if (replicating_ != (v == "on")) { std::fprintf(stderr, "script FAIL (command %d): Multi-Replicate window is %s\n", scriptLine_, replicating_ ? "open" : "closed"); ++scriptFailures_; }
    } else if (cmd == "randreplicate" || cmd == "randreplicateopen") {
        // randreplicate <copies> sx sy sz [turn x y z] [size <min> <max>] [uneven] [seed <n>]
        // (offsets up to +-s per axis); randreplicateopen: the same, opening the window instead
        RandomReplicateSpec r;
        in >> r.copies >> r.spread.x >> r.spread.y >> r.spread.z;
        std::string w;
        while (in >> w) {
            if (w == "turn") in >> r.turn.x >> r.turn.y >> r.turn.z;
            else if (w == "size") in >> r.sizeMin >> r.sizeMax;
            else if (w == "uneven") r.uniform = false;
            else if (w == "seed") in >> r.seed;
        }
        randSpec_ = r;
        if (cmd == "randreplicate") applyRandomReplicate(randSpecNow());
        else openRandomReplicate();
    } else if (cmd == "expect" && in.str().find(" randomizing ") != std::string::npos) {
        std::string w, v; in >> w >> v;
        if (randomizing_ != (v == "on")) { std::fprintf(stderr, "script FAIL (command %d): Random Replicate window is %s\n", scriptLine_, randomizing_ ? "open" : "closed"); ++scriptFailures_; }
    } else if (cmd == "randreplicatepreview") {
        // randreplicatepreview on|off: show the window's preview with the last spec (screenshots)
        std::string v; in >> v; randomizing_ = scriptPreview_ = v == "on";
    } else if (cmd == "replicatepreview") {
        // replicatepreview on|off: show the dialog's preview with the last spec (screenshots)
        std::string v; in >> v; replicating_ = scriptPreview_ = v == "on";
    } else if (cmd == "combine") {
        combinePrims();
    } else if (cmd == "separate") {
        separatePrims();
    } else if (cmd == "primselect") {
        // primselect <index...>: select primitives by their order in the map (0-based)
        std::set<MathPrimId> s; size_t i;
        while (in >> i) if (i < doc_.prims().size()) s.insert(doc_.prims()[i].id);
        doc_.setPrimSelection(s); doc_.setSelection({}); doc_.setLightSelection({});
    } else if (cmd == "expect" && in.str().find(" prims ") != std::string::npos) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.prims().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu primitives, got %zu\n", scriptLine_, n, doc_.prims().size()); ++scriptFailures_; }
    } else if (cmd == "expect" && in.str().find(" primsel ") != std::string::npos) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.primSelection().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu selected primitives, got %zu\n", scriptLine_, n, doc_.primSelection().size()); ++scriptFailures_; }
    } else if (cmd == "propset") {
        // propset model <name> | rot x y z | scale s
        std::string k; in >> k;
        if (k == "model") in >> propTool_.model;
        else if (k == "rot") in >> propTool_.rot.x >> propTool_.rot.y >> propTool_.rot.z;
        else if (k == "scale") in >> propTool_.scale;
    } else if (cmd == "prop") {
        // prop <model> x y z [rx ry rz] [scale]: adds a prop
        Prop p;
        in >> p.model >> p.pos.x >> p.pos.y >> p.pos.z;
        Vec3 r; if (in >> r.x >> r.y >> r.z) p.rot = r;
        double k = 1; if (in >> k) p.scale = {k, k, k};
        doc_.apply(doc_.makeAddProps({p}, "Create prop"));
    } else if (cmd == "propselect") {
        // propselect all | none | <index>...
        std::set<PropId> sel;
        std::string w;
        while (in >> w) {
            if (w == "all") for (const Prop& p : doc_.props()) sel.insert(p.id);
            else if (w != "none") { const size_t i = (size_t)std::atoi(w.c_str()); if (i < doc_.props().size()) sel.insert(doc_.props()[i].id); }
        }
        doc_.setPropSelection(sel); doc_.setSelection({}); doc_.setLightSelection({}); doc_.setPrimSelection({});
    } else if (cmd == "propedit") {
        // propedit pos x y z | rot x y z | scale s | scale x y z | model <name>   (on the selected props)
        std::string k, rest; in >> k; std::getline(in, rest);
        std::vector<Prop> out;
        for (PropId id : doc_.propSelection())
            if (const Prop* q = doc_.findProp(id)) {
                Prop c = *q;
                std::istringstream v(rest);
                if (k == "pos") v >> c.pos.x >> c.pos.y >> c.pos.z;
                else if (k == "rot") v >> c.rot.x >> c.rot.y >> c.rot.z;
                else if (k == "scale") { double a = 1, b2, d; v >> a; if (v >> b2 >> d) c.scale = {a, b2, d}; else c.scale = {a, a, a}; }
                else if (k == "model") v >> c.model;
                out.push_back(c);
            }
        if (!out.empty()) doc_.apply(doc_.makeModifyProps(out, "Edit prop"));
    } else if (cmd == "expect" && in.str().find(" props ") != std::string::npos) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.props().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu props, got %zu\n", scriptLine_, n, doc_.props().size()); ++scriptFailures_; }
    } else if (cmd == "expect" && in.str().find(" propsel ") != std::string::npos) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.propSelection().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu selected props, got %zu\n", scriptLine_, n, doc_.propSelection().size()); ++scriptFailures_; }
    } else if (cmd == "expect" && (in.str().find(" proppos ") != std::string::npos || in.str().find(" proprot ") != std::string::npos ||
                                   in.str().find(" propscale ") != std::string::npos || in.str().find(" propmodel ") != std::string::npos)) {
        // expect proppos|proprot|propscale x y z   |   expect propmodel <name>   (first selected prop)
        std::string w; in >> w;
        const Prop* q = doc_.propSelection().empty() ? nullptr : doc_.findProp(*doc_.propSelection().begin());
        if (!q) { std::fprintf(stderr, "script FAIL (command %d): no prop selected\n", scriptLine_); ++scriptFailures_; }
        else if (w == "propmodel") {
            std::string m; in >> m;
            if (q->model != m) { std::fprintf(stderr, "script FAIL (command %d): prop model %s\n", scriptLine_, q->model.c_str()); ++scriptFailures_; }
        } else {
            Vec3 v; in >> v.x >> v.y >> v.z;
            const Vec3 got = w == "proppos" ? q->pos : w == "proprot" ? q->rot : q->scale;
            if (length(got - v) > (w == "propscale" ? 0.001 : 0.5)) { std::fprintf(stderr, "script FAIL (command %d): %s is %g %g %g\n", scriptLine_, w.c_str(), got.x, got.y, got.z); ++scriptFailures_; }
        }
    } else if (cmd == "expect" && in.str().find(" hasmodel ") != std::string::npos) {
        // expect hasmodel <name> | expect hasmodel not <name>
        std::string w, m; in >> w >> m;
        bool want = true;
        if (m == "not") { want = false; in >> m; }
        if (models_.store().exists(m) != want) { std::fprintf(stderr, "script FAIL (command %d): model %s%s\n", scriptLine_, want ? "" : "not ", m.c_str()); ++scriptFailures_; }
    } else if (cmd == "viewset") {
        // viewset sky|preview|fog|figure|prims|props on|off
        std::string k, v; in >> k >> v;
        const bool on = v == "on";
        if (k == "sky") showSky_ = on;
        else if (k == "preview") { lightPreview_ = on; if (!on) lighting_.clear(); }
        else if (k == "fog") showFog_ = on;
        else if (k == "figure") showFigure_ = on;
        else if (k == "prims") { tool_->cancel(); doc_.setPrimsShown(on); }
        else if (k == "props") { tool_->cancel(); doc_.setPropsShown(on); }
    } else if (cmd == "environment") {
        // environment sky <name|none> | ambient r g b | fog on|off | fogcolor r g b | fogrange <start> <end>
        std::string k; in >> k;
        Environment e = doc_.environment();
        if (k == "sky") { std::string n; in >> n; e.sky = n == "none" ? "" : n; }
        else if (k == "ambient") in >> e.ambient.x >> e.ambient.y >> e.ambient.z;
        else if (k == "fog") { std::string v; in >> v; e.fogOn = v == "on"; }
        else if (k == "fogcolor") in >> e.fog.color.x >> e.fog.color.y >> e.fog.color.z;
        else if (k == "fogrange") in >> e.fog.start >> e.fog.end;
        if (e != doc_.environment()) doc_.apply(doc_.makeEnvironment(e, "Environment"));
    } else if (cmd == "bottega") {
        // bottega <path> | make <path> | none  (in memory only: tests never touch user settings)
        std::string op; in >> op;
        if (op == "none") setBottegaRoot("");
        else if (op == "make") {
            std::string d; std::getline(in >> std::ws, d);
            std::string err;
            if (!bottega::make(d, err)) { std::fprintf(stderr, "script FAIL (command %d): %s\n", scriptLine_, err.c_str()); ++scriptFailures_; }
            setBottegaRoot(d);
        } else setBottegaRoot(op == "set" ? [&] { std::string d; std::getline(in >> std::ws, d); return d; }() : op);
    } else if (cmd == "rescan") {
        rescanBottega();
    } else if (cmd == "bottegadialog") {
        std::snprintf(bottegaBuf_, sizeof bottegaBuf_, "%s", bottegaRoot_.c_str());
        bottegaMsg_.clear();
        askBottega_ = true;
    } else if (cmd == "copy") {
        // copy <from> <to...>  (tests only: add a file to a folder; makes the parent folders)
        std::string from, to; in >> from; std::getline(in >> std::ws, to);
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(to).parent_path(), ec);
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) { std::fprintf(stderr, "script FAIL (command %d): copy %s: %s\n", scriptLine_, from.c_str(), ec.message().c_str()); ++scriptFailures_; }
    } else if (cmd == "skydir") {
        std::string d; std::getline(in >> std::ws, d);
        skies_.addUserFolder(d);
    } else if (cmd == "bookmark") {
        // bookmark add <name> | go <index>
        std::string op; in >> op;
        if (op == "add") { std::string n; std::getline(in >> std::ws, n); addBookmark(n); }
        else if (op == "go") { size_t i = 0; in >> i; goToBookmark(i); }
    } else if (cmd == "figure") {
        Vec3 f; in >> f.x >> f.y >> f.z;
        doc_.setFigure(f);
        showFigure_ = true;
    } else if (cmd == "expect" && in.str().find(" sky ") != std::string::npos) {
        // expect sky <name>: the environment's sky, and that it loads
        std::string w, n; in >> w >> n;
        if (doc_.environment().sky != n || !skies_.get(n)) {
            std::fprintf(stderr, "script FAIL (command %d): sky is '%s' (%s)\n", scriptLine_, doc_.environment().sky.c_str(), skies_.problem(n).c_str());
            ++scriptFailures_;
        }
    } else if (cmd == "expect" && in.str().find(" bookmarks ") != std::string::npos) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.bookmarks().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu bookmarks, got %zu\n", scriptLine_, n, doc_.bookmarks().size()); ++scriptFailures_; }
    } else if (cmd == "expect" && in.str().find(" camera ") != std::string::npos) {
        // expect camera x y z: the 3D view's camera position (+-0.5)
        std::string w; Vec3 v; in >> w >> v.x >> v.y >> v.z;
        const Vector3 c = views_[0]->cameraPos();
        if (length(Vec3{c.x, c.y, c.z} - v) > 0.5) { std::fprintf(stderr, "script FAIL (command %d): camera at %g %g %g\n", scriptLine_, c.x, c.y, c.z); ++scriptFailures_; }
    } else if (cmd == "terrainset") {
        // terrainset mode raise|lower|smooth|flatten|noise|paint|erase | radius <r> | amount <a>
        //            | strength <percent> | falloff soft|linear|hard
        std::string k, v; in >> k >> v;
        if (k == "mode") {
            for (int i = 0; i < 7; ++i) if (v == sculptModeName((SculptMode)i)) terrainTool_.mode = (SculptMode)i;
        } else if (k == "falloff") {
            for (int i = 0; i < 3; ++i) if (v == falloffName((Falloff)i)) terrainTool_.falloff = (Falloff)i;
        } else if (k == "radius") terrainTool_.radius = std::atof(v.c_str());
        else if (k == "amount") terrainTool_.amount = std::atof(v.c_str());
        else if (k == "strength") terrainTool_.strength = std::atof(v.c_str());
        setTool(&terrainTool_);
    } else if (cmd == "camera") {
        // camera <x> <y> <z> <yaw> <pitch>: place the 3D view's camera (degrees)
        double x, y, z, yaw, pitch; in >> x >> y >> z >> yaw >> pitch;
        views_[0]->setCamera({x, y, z}, (float)yaw, (float)pitch);
    } else if (cmd == "whover") {
        // whover <view> <u> <v>: move the mouse there without a button (2D, world coords)
        int vw; double u, v; in >> vw >> u >> v;
        ToolEnv e = env();
        tool_->hover(e, pointerFor(vw, views_[vw]->worldToLocal2D(u, v)));
    } else if (cmd == "expect" && in.str().find(" terrains ") != std::string::npos) {
        std::string w; size_t n = 0; in >> w >> n;
        size_t have = 0;
        for (const Brush& b : doc_.brushes()) if (terrainValid(b)) ++have;
        if (have != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu terrains, got %zu\n", scriptLine_, n, have); ++scriptFailures_; }
    } else if (cmd == "expect" && in.str().find(" terrainheight ") != std::string::npos) {
        // expect terrainheight <x> <y> <z>: the terrain surface straight below x,y is at z (+-0.5)
        std::string w; double x = 0, y = 0, z = 0; in >> w >> x >> y >> z;
        double best = -1e300;
        for (const Brush& b : doc_.brushes()) {
            const double t = rayHitTerrain(b, {x, y, 1e6}, {0, 0, -1});
            if (t >= 0) best = std::max(best, 1e6 - t);
        }
        if (std::fabs(best - z) > 0.5) { std::fprintf(stderr, "script FAIL (command %d): terrain at %g %g is %g, expected %g\n", scriptLine_, x, y, best, z); ++scriptFailures_; }
    } else if (cmd == "blockshape") {
        std::string n; in >> n;
        for (Primitive k : {Primitive::Box, Primitive::Wedge, Primitive::Cylinder, Primitive::Cone, Primitive::Pyramid, Primitive::Sphere}) {
            std::string pn = primitiveName(k);
            for (char& ch : pn) ch = (char)std::tolower((unsigned char)ch);
            if (pn == n) blockTool_.shape = k;
        }
        int sides = 0;
        if (in >> sides) blockTool_.sides = sides;
        setTool(&blockTool_);
    } else if (cmd == "curveshape") {
        std::string n; in >> n;
        CurveShape sh;
        if (curveShapeFromName(n, sh)) curveTool_.params.shape = sh;
        setTool(&curveTool_);
    } else if (cmd == "curveset") {
        // curveset <key> <value...>  (same keys as stored in the .map, e.g. segments 12)
        std::string k, v; in >> k; std::getline(in >> std::ws, v);
        CurveParams& cp = curveTool_.params;
        KeyValues kv{{"shape", curveShapeName(cp.shape)}, {k, v}};
        curveFromKeys(kv, cp);
    } else if (cmd == "editcurve") { editCurve();
    } else if (cmd == "block") {
        // block x0 y0 z0 x1 y1 z1: test helper, creates (and selects) an exact box
        AABB b; Vec3 a, c; in >> a.x >> a.y >> a.z >> c.x >> c.y >> c.z; b.add(a); b.add(c);
        Brush br = makeBox(b, currentTexture_);
        for (Face& f : br.faces) f.scaleU = f.scaleV = defaultScale_;
        doc_.apply(doc_.makeAdd({br}, "Create block"));
    } else if (cmd == "expect" && in.str().find(" bounds ") != std::string::npos) {
        std::string w; Vec3 mn, mx; in >> w >> mn.x >> mn.y >> mn.z >> mx.x >> mx.y >> mx.z;
        const AABB b = doc_.selectionBounds();
        if (!b.valid() || length(b.min - mn) > 0.5 || length(b.max - mx) > 0.5) {
            std::fprintf(stderr, "script FAIL (command %d): selection bounds %g %g %g .. %g %g %g\n", scriptLine_, b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
            ++scriptFailures_;
        }
    } else if (cmd == "bakecurve") { bakeCurves();
    } else if (cmd == "expect" && in.str().find(" groups ") != std::string::npos) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.groups().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu groups, got %zu\n", scriptLine_, n, doc_.groups().size()); ++scriptFailures_; }
    } else if (cmd == "hollow") {
        double t = 16; in >> t;
        shapingError_.clear();
        hollowSelection(t);
        if (!shapingError_.empty()) std::fprintf(stderr, "script: hollow refused: %s\n", shapingError_.c_str());
    } else if (cmd == "expect" && in.str().find(" volume ") != std::string::npos) {
        std::string w; double want = 0; in >> w >> want;
        double got = 0;
        for (const Brush& b : doc_.brushes()) got += brushVolume(b);
        if (std::fabs(got - want) > 0.5) { std::fprintf(stderr, "script FAIL (command %d): total volume %g, expected %g\n", scriptLine_, got, want); ++scriptFailures_; }
    } else if (cmd == "rendermode") { std::string m; in >> m; textured3D_ = m != "flat";
    } else if (cmd == "texscale") { in >> defaultScale_;
    } else if (cmd == "facefit") { editFaces("Fit texture", [&](Face& x, const Polygon& p, TexSize ts, const Vec3&) { fitTexture(x, p, ts); });
    } else if (cmd == "facerotate") { double d = 0; in >> d; editFaces("Texture rotation", [&](Face& x, const Polygon&, TexSize, const Vec3& n) { rotateTexture(x, n, d); });
    } else if (cmd == "facealign") { std::string m; in >> m; editFaces("Align texture", [&](Face& x, const Polygon&, TexSize, const Vec3& n) { if (m == "face") alignTextureToFace(x, n); else alignTextureToWorld(x, n); });
    } else if (cmd == "facejustify") {
        std::string m; in >> m;
        const Justify j = m == "left" ? Justify::Left : m == "right" ? Justify::Right : m == "top" ? Justify::Top : m == "bottom" ? Justify::Bottom : Justify::Center;
        editFaces("Justify texture", [&](Face& x, const Polygon& p, TexSize ts, const Vec3&) { justifyTexture(x, p, ts, j); });
    } else if (cmd == "expect" && in.str().find(" texture ") != std::string::npos) {
        std::string w, name; in >> w >> name;
        int bad = 0, n = 0;
        if (tool_ == &textureTool_) {
            for (const FaceRef& fr : textureTool_.faces(doc_)) { ++n; if (doc_.find(fr.first)->faces[fr.second].texture != name) ++bad; }
        } else {
            for (BrushId id : doc_.selection()) if (const Brush* b = doc_.find(id)) for (const Face& f : b->faces) { ++n; if (f.texture != name) ++bad; }
        }
        if (bad || n == 0) { std::fprintf(stderr, "script FAIL (command %d): %d of %d faces are not '%s'\n", scriptLine_, bad, n, name.c_str()); ++scriptFailures_; }
    } else if (cmd == "expect" && in.str().find(" pickedfaces ") != std::string::npos) {
        std::string w; size_t n = 0; in >> w >> n;
        const size_t got = textureTool_.faces(doc_).size();
        if (got != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu picked faces, got %zu\n", scriptLine_, n, got); ++scriptFailures_; }
    } else if (cmd == "expect" && in.str().find(" current ") != std::string::npos) {
        std::string w, name; in >> w >> name;
        if (currentTexture_ != name) { std::fprintf(stderr, "script FAIL (command %d): current texture is '%s', expected '%s'\n", scriptLine_, currentTexture_.c_str(), name.c_str()); ++scriptFailures_; }
    } else if (cmd == "view") {
        // view <1|2|3> <u> <v> [zoom]  (centre a 2D view on world u,v)
        int vi; double u, v, z = 0; in >> vi >> u >> v >> z;
        if (vi >= 1 && vi <= 3) { views_[vi]->center2D(u, v); if (z > 0) views_[vi]->setZoom2D(z); }
    } else if (cmd == "microgrid") { std::string on; in >> on; grid_.setMicrogrid(on == "on");
    } else if (cmd == "rotsnap") { std::string m; in >> m; slopeAngles_ = m == "slopes";
    } else if (cmd == "pivot") {
        std::string m; in >> m;
        if (m == "center") pivotMode_ = 0;
        else if (m == "origin") pivotMode_ = 1;
        else { customPivot_.x = std::atof(m.c_str()); in >> customPivot_.y >> customPivot_.z; pivotMode_ = 2; }
    } else if (cmd == "vertexraise" || cmd == "vertexheight") {
        double a = 0; in >> a;
        std::string err;
        if (!vertexTool_.moveTerrainSelected(doc_, a, cmd == "vertexheight", err)) {
            std::fprintf(stderr, "script FAIL (command %d): %s\n", scriptLine_, err.c_str());
            ++scriptFailures_;
        }
    } else if (cmd == "expect" && in.str().find(" terrainpoints ") != std::string::npos) {
        std::string w; size_t n = 0; in >> w >> n;
        if (vertexTool_.terrainPointCount() != n) {
            std::fprintf(stderr, "script FAIL (command %d): expected %zu terrain points selected, got %zu\n", scriptLine_, n, vertexTool_.terrainPointCount());
            ++scriptFailures_;
        }
    } else if (cmd == "expect" && in.str().find(" paintlayers ") != std::string::npos) {
        // expect paintlayers <n>: painted textures, counted over every terrain patch
        std::string w; size_t n = 0; in >> w >> n;
        size_t have = 0;
        for (const Brush& b : doc_.brushes()) if (b.terrain) have += b.terrain->layers.size();
        if (have != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu painted textures, got %zu\n", scriptLine_, n, have); ++scriptFailures_; }
    } else if (cmd == "vertexmove") {
        Vec3 d; in >> d.x >> d.y >> d.z;
        std::string err;
        if (!vertexTool_.moveSelected(doc_, d, err)) std::fprintf(stderr, "script: vertexmove refused: %s\n", err.c_str());
    } else if (cmd == "expect" && in.str().find(" ongrid") != std::string::npos) {
        for (BrushId id : doc_.selection())
            if (const Brush* b = doc_.find(id); b && !brushOnGrid(*b, grid_.size)) {
                std::fprintf(stderr, "script FAIL (command %d): brush %u not on grid %g\n", scriptLine_, id, grid_.size);
                ++scriptFailures_;
            }
    } else if (cmd == "expect" && in.str().find(" faces ") != std::string::npos) {
        std::string w; size_t n = 0; in >> w >> n;
        size_t got = 0;
        for (BrushId id : doc_.selection()) if (const Brush* b = doc_.find(id)) got += b->faces.size();
        if (got != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu faces, got %zu\n", scriptLine_, n, got); ++scriptFailures_; }
    } else if (cmd == "dialog") { std::string d; in >> d; if (d == "transform") openTransformDialog(); if (d == "controls") controlsOpen_ = true;
    } else if (cmd == "undo") { undo();
    } else if (cmd == "redo") { redo();
    } else if (cmd == "selectall") { selectAll();
    } else if (cmd == "grid") { double g; in >> g; grid_.setSize(g);
    } else if (cmd == "save") { std::string p; in >> p; saveMap(p);
    } else if (cmd == "open") { std::string p; in >> p; openMap(p);
    } else if (cmd == "shot") { in >> shotRequest_;
    } else if (cmd == "expect" && in.str().find(" size ") != std::string::npos) {
        double x = 0, y = 0, z = 0; std::string what; in >> what >> x >> y >> z;
        const Vec3 s = doc_.selectionBounds().size();
        if (std::fabs(s.x - x) > 0.5 || std::fabs(s.y - y) > 0.5 || std::fabs(s.z - z) > 0.5) {
            std::fprintf(stderr, "script FAIL (command %d): expected selection size %g %g %g, got %g %g %g\n",
                         scriptLine_, x, y, z, s.x, s.y, s.z);
            ++scriptFailures_;
        }
    } else if (cmd == "expect" && in.str().find(" edges") != std::string::npos) {
        edgeCheck_ = true;
    } else if (cmd == "expect" && in.str().find("expect bottega") == 0) {
        // expect bottega ready|empty|notbottega|missing|newer|damaged|none
        std::string w, state; in >> w >> state;
        using S = bottega::State;
        const S s = bottegaCheck_.state;
        const bool ok = (state == "ready" && s == S::Ready) || (state == "empty" && s == S::Empty) ||
                        (state == "notbottega" && s == S::NotBottega) || (state == "missing" && s == S::MissingFolder) ||
                        (state == "newer" && s == S::NewerVersion) || (state == "damaged" && s == S::Damaged) ||
                        (state == "none" && s == S::NoPath);
        if (!ok) { std::fprintf(stderr, "script FAIL (command %d): expected Bottega state %s\n", scriptLine_, state.c_str()); ++scriptFailures_; }
    } else if (cmd == "expect" && (in.str().find("expect hastexture") == 0 || in.str().find("expect hassky") == 0)) {
        // expect hastexture <name> | expect hassky <name>; add "not" before the name to expect it absent
        std::string w, name; in >> w >> name;
        bool want = true;
        if (name == "not") { want = false; in >> name; }
        const bool has = w == "hastexture" ? textures_.exists(name) : skies_.exists(name);
        if (has != want) { std::fprintf(stderr, "script FAIL (command %d): %s %s%s\n", scriptLine_, w.c_str(), want ? "" : "not ", name.c_str()); ++scriptFailures_; }
    } else if (cmd == "expect") {
        std::string what; size_t n = 0; in >> what >> n;
        const size_t got = what == "selected" ? doc_.selection().size() : doc_.brushes().size();
        if (got != n) {
            std::fprintf(stderr, "script FAIL (command %d): expected %zu %s, got %zu\n", scriptLine_, n, what.c_str(), got);
            ++scriptFailures_;
        }
    } else if (cmd == "quit") { exit_ = true;
    } else {
        std::fprintf(stderr, "script: unknown command '%s'\n", cmd.c_str());
    }
}

} // namespace scal
