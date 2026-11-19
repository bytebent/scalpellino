// Editor (part of the class, split by topic; see editor.hpp): the test-script commands,
// one entry each (see docs/DEV.md for the list). "expect" checks are in
// editor_script_expects.cpp.
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

void Editor::registerScriptCommands() {
    auto add = [this](std::initializer_list<const char*> names, ScriptFn fn) {
        for (const char* n : names) scriptCommands_[n] = fn;
    };
    add({"tool"}, [this](const std::string&, std::istringstream& in) {
        std::string t; in >> t;
        setTool(t == "block" ? (Tool*)&blockTool_ : t == "vertex" ? (Tool*)&vertexTool_
              : t == "texture" ? (Tool*)&textureTool_ : t == "clip" ? (Tool*)&clipTool_
              : t == "curve" ? (Tool*)&curveTool_ : t == "terrain" ? (Tool*)&terrainTool_ : t == "light" ? (Tool*)&lightTool_ : t == "prim" ? (Tool*)&primTool_ : t == "props" ? (Tool*)&propTool_ : (Tool*)&selectTool_);
    });
    add({"press"}, [this](const std::string& cmd, std::istringstream& in) {
        int v; float x, y; in >> v >> x >> y;
        std::string mod;
        cmd_ = shift_ = alt_ = false;
        while (in >> mod) {
            if (mod == "shift") shift_ = true;
            if (mod == "cmd") cmd_ = true;
            if (mod == "alt") alt_ = true;
        }
        scriptDragView_ = v;
        onPress(v, {x, y}, false);
    });
    add({"drag"}, [this](const std::string&, std::istringstream& in) {
        float x, y; in >> x >> y;
        if (scriptDragView_ >= 0) onDrag(scriptDragView_, {x, y});
    });
    add({"release"}, [this](const std::string&, std::istringstream&) {
        if (scriptDragView_ >= 0) onRelease(scriptDragView_, {0, 0}, false);
        scriptDragView_ = -1;
    });
    add({"key"}, [this](const std::string&, std::istringstream& in) {
        std::string k; in >> k;
        if (int code = keyFromName(k)) onKey(code);
    });
    add({"transform"}, [this](const std::string&, std::istringstream& in) {
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
    });
    add({"rotate90"}, [this](const std::string&, std::istringstream& in) {
        std::string ax; int sg = 1; in >> ax >> sg; rotate90(ax == "x" ? 0 : ax == "y" ? 1 : 2, sg);
    });
    add({"flip"}, [this](const std::string&, std::istringstream& in) {
        std::string ax; in >> ax; flip(ax == "x" ? 0 : ax == "y" ? 1 : 2);
    });
    add({"duplicate"}, [this](const std::string&, std::istringstream&) {
        duplicate();
    });
    add({"closedialogs"}, [this](const std::string&, std::istringstream&) {
        closeDialogs_ = true;
    });
    add({"printviews"}, [this](const std::string&, std::istringstream&) {
        for (int i = 0; i < 4; ++i)
            std::fprintf(stderr, "view %d origin %.1f,%.1f size %dx%d\n", i, viewOrigin_[i].x, viewOrigin_[i].y, views_[i]->width(), views_[i]->height());
    });
    add({"snapgrid"}, [this](const std::string&, std::istringstream&) {
        snapSelectionToGrid();
    });
    add({"layout"}, [this](const std::string&, std::istringstream& in) {
        std::string l; in >> l;
        showPanels_ = l != "classic";
        resetLayout_ = true;
        warmup_ = 3; // let the docking settle before the next command
    });
    add({"wpress", "wdrag"}, [this](const std::string& cmd, std::istringstream& in) {
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
            scriptDragView_ = wview;
            onPress(wview, local, false);
        } else if (scriptDragView_ >= 0) onDrag(scriptDragView_, local);
    });
    add({"press3d"}, [this](const std::string& cmd, std::istringstream& in) {
        // press3d <dx> <dy> [mods]: click relative to the 3D view's centre
        float dx = 0, dy = 0; in >> dx >> dy;
        std::string mod;
        cmd_ = shift_ = alt_ = false;
        while (in >> mod) { if (mod == "shift") shift_ = true; if (mod == "cmd") cmd_ = true; if (mod == "alt") alt_ = true; }
        scriptDragView_ = 0;
        onPress(0, {views_[0]->width() * 0.5f + dx, views_[0]->height() * 0.5f + dy}, false);
    });
    add({"drag3d"}, [this](const std::string&, std::istringstream& in) {
        // drag3d <dx> <dy>: drag in the 3D view, relative to its centre (after press3d)
        float dx = 0, dy = 0; in >> dx >> dy;
        if (scriptDragView_ == 0) onDrag(0, {views_[0]->width() * 0.5f + dx, views_[0]->height() * 0.5f + dy});
    });
    add({"texturedir"}, [this](const std::string&, std::istringstream& in) {
        std::string d; std::getline(in >> std::ws, d); textures_.addUserFolder(d);
    });
    add({"texture"}, [this](const std::string&, std::istringstream& in) {
        in >> currentTexture_;
    });
    add({"applytexture"}, [this](const std::string&, std::istringstream&) {
        applyCurrentTexture();
    });
    add({"applynodraw"}, [this](const std::string&, std::istringstream&) {
        applyTexture(kNoDrawTexture);
    });
    add({"wraparound"}, [this](const std::string&, std::istringstream&) {
        wrapAroundPicked();
    });
    add({"clippoints"}, [this](const std::string&, std::istringstream& in) {
        Vec3 a, b, c;
        in >> a.x >> a.y >> a.z >> b.x >> b.y >> b.z >> c.x >> c.y >> c.z;
        setTool(&clipTool_);
        clipTool_.setPoints(a, b, c);
    });
    add({"clipkeep"}, [this](const std::string&, std::istringstream& in) {
        std::string k; in >> k;
        clipTool_.keep = k == "front" ? ClipTool::Keep::Front : k == "back" ? ClipTool::Keep::Back : ClipTool::Keep::Both;
    });
    add({"carve"}, [this](const std::string&, std::istringstream&) {
        carveWithSelection();
    });
    add({"findoverlaps"}, [this](const std::string&, std::istringstream&) {
        findOverlapsNow();
    });
    add({"nextoverlap"}, [this](const std::string&, std::istringstream&) {
        focusOverlap(overlapIndex_ + 1);
    });
    add({"toorigin"}, [this](const std::string&, std::istringstream&) {
        allViewsToOrigin();
    });
    add({"hide"}, [this](const std::string&, std::istringstream&) {
        hideSelected();
    });
    add({"hideunselected"}, [this](const std::string&, std::istringstream&) {
        hideUnselected();
    });
    add({"showall"}, [this](const std::string&, std::istringstream&) {
        showAll();
    });
    add({"visgroup"}, [this](const std::string&, std::istringstream& in) {
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
    });
    add({"terrain"}, [this](const std::string&, std::istringstream& in) {
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
    });
    add({"lightset", "lightedit"}, [this](const std::string& cmd, std::istringstream& in) {
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
    });
    add({"prim"}, [this](const std::string&, std::istringstream& in) {
        // prim <kind> x y z  sx sy sz  [rx ry rz]: adds a math primitive (current texture)
        std::string k; in >> k;
        MathPrim p;
        if (auto pk = primKindFromName(k)) p.kind = *pk;
        in >> p.pos.x >> p.pos.y >> p.pos.z >> p.size.x >> p.size.y >> p.size.z;
        Vec3 r; if (in >> r.x >> r.y >> r.z) p.rot = r;
        p.texture = currentTexture_;
        doc_.apply(doc_.makeAddPrims({p}, "Create primitive"));
    });
    add({"primedit"}, [this](const std::string&, std::istringstream& in) {
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
    });
    add({"primset"}, [this](const std::string&, std::istringstream& in) {
        // primset kind <k> | round <r> | thickness <t> | flat on|off | color r g b | texscale s: the next primitive
        std::string k; in >> k;
        MathPrim& t = primTool_.templ;
        if (k == "kind") { std::string n; in >> n; if (auto pk = primKindFromName(n)) t.kind = *pk; }
        else if (k == "round") in >> t.round;
        else if (k == "thickness") in >> t.thickness;
        else if (k == "flat") { std::string v; in >> v; primTool_.flat = v == "on"; }
        else if (k == "color") in >> t.color.x >> t.color.y >> t.color.z;
        else if (k == "texscale") in >> t.texScale;
    });
    add({"primbox"}, [this](const std::string&, std::istringstream& in) {
        // primbox cx cy cz sx sy sz [view axis]: the pending box, typed
        Vec3 c, sz; int ax = 2; in >> c.x >> c.y >> c.z >> sz.x >> sz.y >> sz.z; in >> ax;
        primTool_.setPending(c, sz, ax);
    });
    add({"snapland"}, [this](const std::string&, std::istringstream&) {
        snapToLand();
    });
    add({"snapground"}, [this](const std::string&, std::istringstream& in) {
        double h = 0; in >> h; snapToGround(h);
    });
    add({"replicate"}, [this](const std::string&, std::istringstream& in) {
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
    });
    add({"replicateopen"}, [this](const std::string&, std::istringstream& in) {
        // replicateopen <copies> ox oy oz: open the Multi-Replicate window with these values
        ReplicateSpec r = repSpec_;
        in >> r.copies >> r.offset.x >> r.offset.y >> r.offset.z;
        openReplicate();
        repSpec_ = r;
    });
    add({"randreplicate", "randreplicateopen"}, [this](const std::string& cmd, std::istringstream& in) {
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
    });
    add({"randreplicatepreview"}, [this](const std::string&, std::istringstream& in) {
        // randreplicatepreview on|off: show the window's preview with the last spec (screenshots)
        std::string v; in >> v; randomizing_ = scriptPreview_ = v == "on";
    });
    add({"replicatepreview"}, [this](const std::string&, std::istringstream& in) {
        // replicatepreview on|off: show the dialog's preview with the last spec (screenshots)
        std::string v; in >> v; replicating_ = scriptPreview_ = v == "on";
    });
    add({"combine"}, [this](const std::string&, std::istringstream&) {
        combinePrims();
    });
    add({"separate"}, [this](const std::string&, std::istringstream&) {
        separatePrims();
    });
    add({"primselect"}, [this](const std::string&, std::istringstream& in) {
        // primselect <index...>: select primitives by their order in the map (0-based)
        std::set<MathPrimId> s; size_t i;
        while (in >> i) if (i < doc_.prims().size()) s.insert(doc_.prims()[i].id);
        doc_.setPrimSelection(s); doc_.setSelection({}); doc_.setLightSelection({});
    });
    add({"propset"}, [this](const std::string&, std::istringstream& in) {
        // propset model <name> | rot x y z | scale s
        std::string k; in >> k;
        if (k == "model") in >> propTool_.model;
        else if (k == "rot") in >> propTool_.rot.x >> propTool_.rot.y >> propTool_.rot.z;
        else if (k == "scale") in >> propTool_.scale;
    });
    add({"prop"}, [this](const std::string&, std::istringstream& in) {
        // prop <model> x y z [rx ry rz] [scale]: adds a prop
        Prop p;
        in >> p.model >> p.pos.x >> p.pos.y >> p.pos.z;
        Vec3 r; if (in >> r.x >> r.y >> r.z) p.rot = r;
        double k = 1; if (in >> k) p.scale = {k, k, k};
        doc_.apply(doc_.makeAddProps({p}, "Create prop"));
    });
    add({"propselect"}, [this](const std::string&, std::istringstream& in) {
        // propselect all | none | <index>...
        std::set<PropId> sel;
        std::string w;
        while (in >> w) {
            if (w == "all") for (const Prop& p : doc_.props()) sel.insert(p.id);
            else if (w != "none") { const size_t i = (size_t)std::atoi(w.c_str()); if (i < doc_.props().size()) sel.insert(doc_.props()[i].id); }
        }
        doc_.setPropSelection(sel); doc_.setSelection({}); doc_.setLightSelection({}); doc_.setPrimSelection({});
    });
    add({"propedit"}, [this](const std::string&, std::istringstream& in) {
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
    });
    add({"viewset"}, [this](const std::string&, std::istringstream& in) {
        // viewset sky|preview|fog|figure|prims|props on|off
        std::string k, v; in >> k >> v;
        const bool on = v == "on";
        if (k == "sky") showSky_ = on;
        else if (k == "preview") { lightPreview_ = on; if (!on) lighting_.clear(); }
        else if (k == "fog") showFog_ = on;
        else if (k == "figure") showFigure_ = on;
        else if (k == "prims") { tool_->cancel(); doc_.setPrimsShown(on); }
        else if (k == "props") { tool_->cancel(); doc_.setPropsShown(on); }
    });
    add({"environment"}, [this](const std::string&, std::istringstream& in) {
        // environment sky <name|none> | ambient r g b | fog on|off | fogcolor r g b | fogrange <start> <end>
        std::string k; in >> k;
        Environment e = doc_.environment();
        if (k == "sky") { std::string n; in >> n; e.sky = n == "none" ? "" : n; }
        else if (k == "ambient") in >> e.ambient.x >> e.ambient.y >> e.ambient.z;
        else if (k == "fog") { std::string v; in >> v; e.fogOn = v == "on"; }
        else if (k == "fogcolor") in >> e.fog.color.x >> e.fog.color.y >> e.fog.color.z;
        else if (k == "fogrange") in >> e.fog.start >> e.fog.end;
        if (e != doc_.environment()) doc_.apply(doc_.makeEnvironment(e, "Environment"));
    });
    add({"bottega"}, [this](const std::string&, std::istringstream& in) {
        // bottega <path> | make <path> | none  (in memory only: tests never touch user settings)
        std::string op; in >> op;
        if (op == "none") setBottegaRoot("");
        else if (op == "make") {
            std::string d; std::getline(in >> std::ws, d);
            std::string err;
            if (!bottega::make(d, err)) { std::fprintf(stderr, "script FAIL (command %d): %s\n", scriptLine_, err.c_str()); ++scriptFailures_; }
            setBottegaRoot(d);
        } else setBottegaRoot(op == "set" ? [&] { std::string d; std::getline(in >> std::ws, d); return d; }() : op);
    });
    add({"rescan"}, [this](const std::string&, std::istringstream&) {
        rescanBottega();
    });
    add({"bottegadialog"}, [this](const std::string&, std::istringstream&) {
        std::snprintf(bottegaBuf_, sizeof bottegaBuf_, "%s", bottegaRoot_.c_str());
        bottegaMsg_.clear();
        askBottega_ = true;
    });
    add({"copy"}, [this](const std::string&, std::istringstream& in) {
        // copy <from> <to...>  (tests only: add a file to a folder; makes the parent folders)
        std::string from, to; in >> from; std::getline(in >> std::ws, to);
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(to).parent_path(), ec);
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) { std::fprintf(stderr, "script FAIL (command %d): copy %s: %s\n", scriptLine_, from.c_str(), ec.message().c_str()); ++scriptFailures_; }
    });
    add({"skydir"}, [this](const std::string&, std::istringstream& in) {
        std::string d; std::getline(in >> std::ws, d);
        skies_.addUserFolder(d);
    });
    add({"bookmark"}, [this](const std::string&, std::istringstream& in) {
        // bookmark add <name> | go <index>
        std::string op; in >> op;
        if (op == "add") { std::string n; std::getline(in >> std::ws, n); addBookmark(n); }
        else if (op == "go") { size_t i = 0; in >> i; goToBookmark(i); }
    });
    add({"figure"}, [this](const std::string&, std::istringstream& in) {
        Vec3 f; in >> f.x >> f.y >> f.z;
        doc_.setFigure(f);
        showFigure_ = true;
    });
    add({"terrainset"}, [this](const std::string&, std::istringstream& in) {
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
    });
    add({"camera"}, [this](const std::string&, std::istringstream& in) {
        // camera <x> <y> <z> <yaw> <pitch>: place the 3D view's camera (degrees)
        double x, y, z, yaw, pitch; in >> x >> y >> z >> yaw >> pitch;
        views_[0]->setCamera({x, y, z}, (float)yaw, (float)pitch);
    });
    add({"whover"}, [this](const std::string&, std::istringstream& in) {
        // whover <view> <u> <v>: move the mouse there without a button (2D, world coords)
        int vw; double u, v; in >> vw >> u >> v;
        ToolEnv e = env();
        tool_->hover(e, pointerFor(vw, views_[vw]->worldToLocal2D(u, v)));
    });
    add({"blockshape"}, [this](const std::string&, std::istringstream& in) {
        std::string n; in >> n;
        for (Primitive k : {Primitive::Box, Primitive::Wedge, Primitive::Cylinder, Primitive::Cone, Primitive::Pyramid, Primitive::Sphere}) {
            std::string pn = primitiveName(k);
            for (char& ch : pn) ch = (char)std::tolower((unsigned char)ch);
            if (pn == n) blockTool_.shape = k;
        }
        int sides = 0;
        if (in >> sides) blockTool_.sides = sides;
        setTool(&blockTool_);
    });
    add({"curveshape"}, [this](const std::string&, std::istringstream& in) {
        std::string n; in >> n;
        CurveShape sh;
        if (curveShapeFromName(n, sh)) curveTool_.params.shape = sh;
        setTool(&curveTool_);
    });
    add({"curveset"}, [this](const std::string&, std::istringstream& in) {
        // curveset <key> <value...>  (same keys as stored in the .map, e.g. segments 12)
        std::string k, v; in >> k; std::getline(in >> std::ws, v);
        CurveParams& cp = curveTool_.params;
        KeyValues kv{{"shape", curveShapeName(cp.shape)}, {k, v}};
        curveFromKeys(kv, cp);
    });
    add({"editcurve"}, [this](const std::string&, std::istringstream&) {
        editCurve();
    });
    add({"block"}, [this](const std::string&, std::istringstream& in) {
        // block x0 y0 z0 x1 y1 z1: test helper, creates (and selects) an exact box
        AABB b; Vec3 a, c; in >> a.x >> a.y >> a.z >> c.x >> c.y >> c.z; b.add(a); b.add(c);
        Brush br = makeBox(b, currentTexture_);
        for (Face& f : br.faces) f.scaleU = f.scaleV = defaultScale_;
        doc_.apply(doc_.makeAdd({br}, "Create block"));
    });
    add({"bakecurve"}, [this](const std::string&, std::istringstream&) {
        bakeCurves();
    });
    add({"hollow"}, [this](const std::string&, std::istringstream& in) {
        double t = 16; in >> t;
        shapingError_.clear();
        hollowSelection(t);
        if (!shapingError_.empty()) std::fprintf(stderr, "script: hollow refused: %s\n", shapingError_.c_str());
    });
    add({"rendermode"}, [this](const std::string&, std::istringstream& in) {
        std::string m; in >> m; textured3D_ = m != "flat";
    });
    add({"texscale"}, [this](const std::string&, std::istringstream& in) {
        in >> defaultScale_;
    });
    add({"facefit"}, [this](const std::string&, std::istringstream&) {
        editFaces("Fit texture", [&](Face& x, const Polygon& p, TexSize ts, const Vec3&) { fitTexture(x, p, ts); });
    });
    add({"facerotate"}, [this](const std::string&, std::istringstream& in) {
        double d = 0; in >> d; editFaces("Texture rotation", [&](Face& x, const Polygon&, TexSize, const Vec3& n) { rotateTexture(x, n, d); });
    });
    add({"facealign"}, [this](const std::string&, std::istringstream& in) {
        std::string m; in >> m; editFaces("Align texture", [&](Face& x, const Polygon&, TexSize, const Vec3& n) { if (m == "face") alignTextureToFace(x, n); else alignTextureToWorld(x, n); });
    });
    add({"facejustify"}, [this](const std::string&, std::istringstream& in) {
        std::string m; in >> m;
        const Justify j = m == "left" ? Justify::Left : m == "right" ? Justify::Right : m == "top" ? Justify::Top : m == "bottom" ? Justify::Bottom : Justify::Center;
        editFaces("Justify texture", [&](Face& x, const Polygon& p, TexSize ts, const Vec3&) { justifyTexture(x, p, ts, j); });
    });
    add({"view"}, [this](const std::string&, std::istringstream& in) {
        // view <1|2|3> <u> <v> [zoom]  (centre a 2D view on world u,v)
        int vi; double u, v, z = 0; in >> vi >> u >> v >> z;
        if (vi >= 1 && vi <= 3) { views_[vi]->center2D(u, v); if (z > 0) views_[vi]->setZoom2D(z); }
    });
    add({"microgrid"}, [this](const std::string&, std::istringstream& in) {
        std::string on; in >> on; grid_.setMicrogrid(on == "on");
    });
    add({"rotsnap"}, [this](const std::string&, std::istringstream& in) {
        std::string m; in >> m; slopeAngles_ = m == "slopes";
    });
    add({"pivot"}, [this](const std::string&, std::istringstream& in) {
        std::string m; in >> m;
        if (m == "center") pivotMode_ = 0;
        else if (m == "origin") pivotMode_ = 1;
        else { customPivot_.x = std::atof(m.c_str()); in >> customPivot_.y >> customPivot_.z; pivotMode_ = 2; }
    });
    add({"vertexraise", "vertexheight"}, [this](const std::string& cmd, std::istringstream& in) {
        double a = 0; in >> a;
        std::string err;
        if (!vertexTool_.moveTerrainSelected(doc_, a, cmd == "vertexheight", err)) {
            std::fprintf(stderr, "script FAIL (command %d): %s\n", scriptLine_, err.c_str());
            ++scriptFailures_;
        }
    });
    add({"vertexmove"}, [this](const std::string&, std::istringstream& in) {
        Vec3 d; in >> d.x >> d.y >> d.z;
        std::string err;
        if (!vertexTool_.moveSelected(doc_, d, err)) std::fprintf(stderr, "script: vertexmove refused: %s\n", err.c_str());
    });
    add({"dialog"}, [this](const std::string&, std::istringstream& in) {
        std::string d; in >> d; if (d == "transform") openTransformDialog(); if (d == "controls") controlsOpen_ = true;
    });
    add({"undo"}, [this](const std::string&, std::istringstream&) {
        undo();
    });
    add({"redo"}, [this](const std::string&, std::istringstream&) {
        redo();
    });
    add({"selectall"}, [this](const std::string&, std::istringstream&) {
        selectAll();
    });
    add({"grid"}, [this](const std::string&, std::istringstream& in) {
        double g; in >> g; grid_.setSize(g);
    });
    add({"save"}, [this](const std::string&, std::istringstream& in) {
        std::string p; in >> p; saveMap(p);
    });
    add({"open"}, [this](const std::string&, std::istringstream& in) {
        std::string p; in >> p; openMap(p);
    });
    add({"shot"}, [this](const std::string&, std::istringstream& in) {
        in >> shotRequest_;
    });
    add({"quit"}, [this](const std::string&, std::istringstream&) {
        exit_ = true;
    });
}

} // namespace scal
