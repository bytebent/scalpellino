// Editor (part of the class, split by topic; see editor.hpp): the test-script checks,
// "expect <what> ...", one entry per <what>. A failed check prints "script FAIL" and
// makes the run fail.
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

void Editor::registerScriptExpects() {
    auto add = [this](std::initializer_list<const char*> names, ScriptFn fn) {
        for (const char* n : names) scriptExpects_[n] = fn;
    };
    add({"overlaps"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        if (overlaps_.size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu overlaps, got %zu\n", scriptLine_, n, overlaps_.size()); ++scriptFailures_; }
    });
    add({"bottoms"}, [this](const std::string&, std::istringstream& in) {
        // expect bottoms <z>: every selected primitive object and prop has its lowest point at z
        std::string w; double z = 0; in >> w >> z;
        int bad = 0;
        for (const auto& obj : selectedObjects(doc_)) bad += std::fabs(objectBottom(obj) - z) > 0.05;
        for (PropId id : doc_.propSelection())
            if (const Prop* q = doc_.findProp(id)) bad += std::fabs(propBottom(*q, models_.find(q->model)) - z) > 0.05;
        if (bad) { std::fprintf(stderr, "script FAIL (command %d): %d selected object(s) not resting at z %g\n", scriptLine_, bad, z); ++scriptFailures_; }
    });
    add({"visible"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        size_t got = 0;
        for (const Brush& b : doc_.brushes()) got += doc_.visible(b);
        if (got != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu visible brushes, got %zu\n", scriptLine_, n, got); ++scriptFailures_; }
    });
    add({"lights"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.lights().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu lights, got %zu\n", scriptLine_, n, doc_.lights().size()); ++scriptFailures_; }
    });
    add({"lightsel"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.lightSelection().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu selected lights, got %zu\n", scriptLine_, n, doc_.lightSelection().size()); ++scriptFailures_; }
    });
    add({"lightpos", "lightdir"}, [this](const std::string&, std::istringstream& in) {
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
    });
    add({"probes", "probesel", "probeatlas"}, [this](const std::string&, std::istringstream& in) {
        // expect probes <n> | probesel <n> | probeatlas <n> (probes in the last built cubemaps)
        std::string w; size_t n = 0; in >> w >> n;
        const size_t got = w == "probes" ? doc_.probes().size() : w == "probesel" ? doc_.probeSelection().size()
                         : lastProbes_ ? lastProbes_->slots.size() : 0;
        if (got != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu %s, got %zu\n", scriptLine_, n, w.c_str(), got); ++scriptFailures_; }
    });
    add({"probepos", "probesize"}, [this](const std::string&, std::istringstream& in) {
        // expect probepos x y z | probesize <n>: the first selected probe (+-0.01)
        std::string w; in >> w;
        const Probe* r = doc_.probeSelection().empty() ? nullptr : doc_.findProbe(*doc_.probeSelection().begin());
        if (!r) { std::fprintf(stderr, "script FAIL (command %d): no probe selected\n", scriptLine_); ++scriptFailures_; return; }
        if (w == "probepos") {
            Vec3 p; in >> p.x >> p.y >> p.z;
            if (length(p - r->pos) > 0.01) {
                std::fprintf(stderr, "script FAIL (command %d): probe at %g %g %g, expected %g %g %g\n", scriptLine_, r->pos.x, r->pos.y, r->pos.z, p.x, p.y, p.z);
                ++scriptFailures_;
            }
        } else {
            int n = 0; in >> n;
            if (n != r->size) { std::fprintf(stderr, "script FAIL (command %d): probe size %d, expected %d\n", scriptLine_, r->size, n); ++scriptFailures_; }
        }
    });
    add({"cameras", "camerasel"}, [this](const std::string&, std::istringstream& in) {
        // expect cameras <n> | expect camerasel <n>
        std::string w; size_t n = 0; in >> w >> n;
        const size_t got = w == "cameras" ? doc_.cameras().size() : doc_.cameraSelection().size();
        if (got != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu %s, got %zu\n", scriptLine_, n, w.c_str(), got); ++scriptFailures_; }
    });
    add({"render"}, [this](const std::string&, std::istringstream& in) {
        // expect render saved|stopped|failed|none: how the last render ended
        std::string w, want; in >> w >> want;
        const char* got = !renderJob_ ? "none" : renderJob_->running ? "running" : renderJob_->done ? "saved"
                          : renderJob_->stopped ? "stopped" : "failed";
        if (want != got) { std::fprintf(stderr, "script FAIL (command %d): render %s, expected %s\n", scriptLine_, got, want.c_str()); ++scriptFailures_; }
    });
    add({"renders"}, [this](const std::string&, std::istringstream& in) {
        // expect renders <n>: PNGs saved so far (and each one is on disk with its log)
        std::string w; int n = 0; in >> w >> n;
        int onDisk = 0;
        std::error_code ec;
        for (const std::string& f : renderFiles_)
            if (std::filesystem::exists(f, ec) && std::filesystem::exists(std::filesystem::path(f).replace_extension(".log"), ec)) ++onDisk;
        if (rendersDone_ != n || onDisk != n) {
            std::fprintf(stderr, "script FAIL (command %d): %d renders saved (%d on disk with a log), expected %d\n", scriptLine_, rendersDone_, onDisk, n);
            ++scriptFailures_;
        }
    });
    add({"renderlog"}, [this](const std::string&, std::istringstream& in) {
        // expect renderlog [not] <text>: a line of the last render's log holds the text
        std::string w, text; in >> w; std::getline(in >> std::ws, text);
        bool neg = false;
        if (text.rfind("not ", 0) == 0) { neg = true; text = text.substr(4); }
        bool found = false;
        if (renderJob_) for (const std::string& l : renderJob_->log) found = found || l.find(text) != std::string::npos;
        if (found == neg) { std::fprintf(stderr, "script FAIL (command %d): render log %s \"%s\"\n", scriptLine_, neg ? "has" : "lacks", text.c_str()); ++scriptFailures_; }
    });
    add({"rendersize"}, [this](const std::string&, std::istringstream& in) {
        // expect rendersize <w> <h>: the last saved PNG's size
        std::string w; int ew = 0, eh = 0; in >> w >> ew >> eh;
        int gw = 0, gh = 0;
        if (!renderFiles_.empty()) {
            Image img = LoadImage(renderFiles_.back().c_str());
            gw = img.width; gh = img.height;
            UnloadImage(img);
        }
        if (gw != ew || gh != eh) { std::fprintf(stderr, "script FAIL (command %d): render is %d x %d, expected %d x %d\n", scriptLine_, gw, gh, ew, eh); ++scriptFailures_; }
    });
    add({"rendersame"}, [this](const std::string&, std::istringstream& in) {
        // expect rendersame yes|no: the last two saved PNGs are the same picture (the
        // average difference under 0.5 of 255), or clearly not (over 2)
        std::string w, want; in >> w >> want;
        double diff = -1;
        if (renderFiles_.size() >= 2) {
            Image a = LoadImage(renderFiles_[renderFiles_.size() - 2].c_str()), b = LoadImage(renderFiles_.back().c_str());
            if (a.data && b.data && a.width == b.width && a.height == b.height) {
                ImageFormat(&a, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
                ImageFormat(&b, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
                const unsigned char* pa = (const unsigned char*)a.data; const unsigned char* pb = (const unsigned char*)b.data;
                double sum = 0;
                const size_t n = (size_t)a.width * a.height * 3;
                for (size_t i = 0; i < n; ++i) sum += std::abs((int)pa[i] - (int)pb[i]);
                diff = sum / (double)n;
            }
            UnloadImage(a); UnloadImage(b);
        }
        const bool ok = diff >= 0 && (want == "yes" ? diff < 0.5 : diff > 2);
        if (!ok) { std::fprintf(stderr, "script FAIL (command %d): the last two renders differ by %.3f on average (rendersame %s)\n", scriptLine_, diff, want.c_str()); ++scriptFailures_; }
    });
    add({"renderwindow"}, [this](const std::string&, std::istringstream& in) {
        // expect renderwindow open|closed
        std::string w, want; in >> w >> want;
        if ((want == "open") != renderOpen_) { std::fprintf(stderr, "script FAIL (command %d): render window %s\n", scriptLine_, renderOpen_ ? "open" : "closed"); ++scriptFailures_; }
    });
    add({"camerapreview"}, [this](const std::string&, std::istringstream& in) {
        // expect camerapreview <times drawn> [stale 0|1]
        std::string w; int n = 0; in >> w >> n;
        std::string k; int stale = -1;
        if (in >> k >> stale) {}
        const Camera* cam = doc_.activeCamera();
        const int isStale = previewDone_ && (previewRev_ != doc_.revision() || !cam || previewCam_ != cam->id) ? 1 : 0;
        if (previewUpdates_ != n || (stale >= 0 && stale != isStale)) {
            std::fprintf(stderr, "script FAIL (command %d): camera preview drawn %d times (stale %d), expected %d\n", scriptLine_, previewUpdates_, isStale, n);
            ++scriptFailures_;
        }
    });
    add({"camerapresets"}, [this](const std::string&, std::istringstream& in) {
        // expect camerapresets <n>: saved presets in the Bottega
        std::string w; size_t n = 0; in >> w >> n;
        if (cameraPresets().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu camera presets, got %zu\n", scriptLine_, n, cameraPresets().size()); ++scriptFailures_; }
    });
    add({"cameraactive"}, [this](const std::string&, std::istringstream& in) {
        // expect cameraactive <index>: that camera (order in the map) is the only active one
        std::string w; size_t i = 0; in >> w >> i;
        int actives = 0;
        for (const Camera& c : doc_.cameras()) actives += c.active;
        if (actives != 1 || i >= doc_.cameras().size() || !doc_.cameras()[i].active) {
            std::fprintf(stderr, "script FAIL (command %d): camera %zu is not the one active camera (%d active)\n", scriptLine_, i, actives);
            ++scriptFailures_;
        }
    });
    add({"cam"}, [this](const std::string&, std::istringstream& in) {
        // expect cam <key> <value...>: the first selected camera, keys as cameraedit
        // (pos, angles, fov, lens, near, far, size, ortho, orthosize, exposure, color, name); +-0.01
        std::string w, k; in >> w >> k;
        const Camera* c = doc_.cameraSelection().empty() ? nullptr : doc_.findCamera(*doc_.cameraSelection().begin());
        if (!c) { std::fprintf(stderr, "script FAIL (command %d): no camera selected\n", scriptLine_); ++scriptFailures_; return; }
        std::vector<double> have;
        if (k == "name") {
            std::string n; std::getline(in >> std::ws, n);
            if (n != c->name) { std::fprintf(stderr, "script FAIL (command %d): camera is called '%s'\n", scriptLine_, c->name.c_str()); ++scriptFailures_; }
            return;
        }
        if (k == "pos") have = {c->pos.x, c->pos.y, c->pos.z};
        else if (k == "angles") have = {c->pitch, c->yaw, c->roll};
        else if (k == "fov") have = {c->fov};
        else if (k == "lens") have = {fovToLens(c->fov)};
        else if (k == "near") have = {c->nearDist};
        else if (k == "far") have = {c->farDist};
        else if (k == "size") have = {(double)c->width, (double)c->height};
        else if (k == "ortho") have = {c->ortho ? 1.0 : 0.0};
        else if (k == "orthosize") have = {c->orthoSize};
        else if (k == "exposure") have = {c->exposure};
        else if (k == "color") have = {c->color.x, c->color.y, c->color.z};
        bool ok = !have.empty();
        std::string got;
        for (double h : have) {
            double want = 0;
            if (!(in >> want) || std::fabs(want - h) > 0.01) ok = false;
            got += " " + std::to_string(h);
        }
        if (!ok) { std::fprintf(stderr, "script FAIL (command %d): camera %s is%s\n", scriptLine_, k.c_str(), got.c_str()); ++scriptFailures_; }
    });
    add({"primpos", "primrot", "primsize"}, [this](const std::string&, std::istringstream& in) {
        // expect primpos|primrot|primsize x y z: the first selected primitive (+-0.01)
        std::string w; Vec3 v; in >> w >> v.x >> v.y >> v.z;
        const MathPrim* p = doc_.primSelection().empty() ? nullptr : doc_.findPrim(*doc_.primSelection().begin());
        const Vec3 have = !p ? Vec3{} : w == "primpos" ? p->pos : w == "primrot" ? p->rot : p->size;
        if (!p || length(have - v) > 0.01) {
            std::fprintf(stderr, "script FAIL (command %d): %s is %g %g %g\n", scriptLine_, w.c_str(), have.x, have.y, have.z);
            ++scriptFailures_;
        }
    });
    add({"primop"}, [this](const std::string&, std::istringstream& in) {
        std::string w, o; in >> w >> o;
        const MathPrim* p = doc_.primSelection().empty() ? nullptr : doc_.findPrim(*doc_.primSelection().begin());
        if (!p || primOpName(p->op) != o || !p->combo) { std::fprintf(stderr, "script FAIL (command %d): not a '%s' piece of a combination\n", scriptLine_, o.c_str()); ++scriptFailures_; }
    });
    add({"replicating"}, [this](const std::string&, std::istringstream& in) {
        std::string w, v; in >> w >> v;
        if (replicating_ != (v == "on")) { std::fprintf(stderr, "script FAIL (command %d): Multi-Replicate window is %s\n", scriptLine_, replicating_ ? "open" : "closed"); ++scriptFailures_; }
    });
    add({"randomizing"}, [this](const std::string&, std::istringstream& in) {
        std::string w, v; in >> w >> v;
        if (randomizing_ != (v == "on")) { std::fprintf(stderr, "script FAIL (command %d): Random Replicate window is %s\n", scriptLine_, randomizing_ ? "open" : "closed"); ++scriptFailures_; }
    });
    add({"prims"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.prims().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu primitives, got %zu\n", scriptLine_, n, doc_.prims().size()); ++scriptFailures_; }
    });
    add({"primsel"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.primSelection().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu selected primitives, got %zu\n", scriptLine_, n, doc_.primSelection().size()); ++scriptFailures_; }
    });
    add({"props"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.props().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu props, got %zu\n", scriptLine_, n, doc_.props().size()); ++scriptFailures_; }
    });
    // Decals (v0.12.5): expect decals <n> | decalsel <n> | decal <field> <value...> (the first
    // selected decal, else the Decal tool's pending one): material, pos x y z, normal x y z,
    // width, height, turn, reach, order, visgroup, brush <index>, drawn <triangles> (> 0: any)
    add({"decals"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.decalCount() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu decals, got %zu\n", scriptLine_, n, doc_.decalCount()); ++scriptFailures_; }
    });
    add({"decalsel"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.decalSelection().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu selected decals, got %zu\n", scriptLine_, n, doc_.decalSelection().size()); ++scriptFailures_; }
    });
    add({"decal"}, [this](const std::string&, std::istringstream& in) {
        std::string w, k; in >> w >> k;
        const Decal* d = nullptr;
        const Brush* holder = nullptr;
        if (!doc_.decalSelection().empty()) {
            const Document::DecalRef r = doc_.findDecal(*doc_.decalSelection().begin());
            d = r.decal;
            holder = r.brush;
        } else if (tool_ == &decalTool_) d = decalTool_.pending();
        auto bad = [&](const std::string& got) { std::fprintf(stderr, "script FAIL (command %d): decal %s is %s\n", scriptLine_, k.c_str(), got.c_str()); ++scriptFailures_; };
        if (!d) { std::fprintf(stderr, "script FAIL (command %d): no decal selected or pending\n", scriptLine_); ++scriptFailures_; return; }
        char b[160];
        if (k == "material") { std::string m; in >> m; if (d->material != m) bad(d->material); }
        else if (k == "pos" || k == "normal") {
            Vec3 v; in >> v.x >> v.y >> v.z;
            const Vec3 got = k == "pos" ? d->pos : d->normal;
            std::snprintf(b, sizeof b, "%g %g %g", got.x, got.y, got.z);
            if (length(got - v) > (k == "pos" ? 0.5 : 1e-3)) bad(b);
        } else if (k == "brush") {
            size_t i = 0; in >> i;
            const size_t at = holder ? (size_t)(holder - doc_.brushes().data()) : (size_t)-1;
            if (at != i) bad(std::to_string(at));
        } else if (k == "drawn") {
            size_t n = 0; in >> n;
            const size_t got = decalMesh(*d, doc_.brushes(), [this](const Brush& x) { return doc_.visible(x); }).size();
            if ((n == 0 && got != 0) || (n > 0 && got == 0)) bad(std::to_string(got) + " triangles");
        } else {
            double v = 0; in >> v;
            const double got = k == "width" ? d->width : k == "height" ? d->height : k == "turn" ? decalRotation(*d) : k == "reach" ? d->reach
                             : k == "order" ? d->order : k == "visgroup" ? (double)d->visgroup : 1e300;
            std::snprintf(b, sizeof b, "%g", got);
            if (std::fabs(got - v) > 0.01) bad(b);
        }
    });
    add({"propsel"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.propSelection().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu selected props, got %zu\n", scriptLine_, n, doc_.propSelection().size()); ++scriptFailures_; }
    });
    add({"proppos", "proprot", "propscale", "propmodel"}, [this](const std::string&, std::istringstream& in) {
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
    });
    add({"hasmodel"}, [this](const std::string&, std::istringstream& in) {
        // expect hasmodel <name> | expect hasmodel not <name>
        std::string w, m; in >> w >> m;
        bool want = true;
        if (m == "not") { want = false; in >> m; }
        if (models_.store().exists(m) != want) { std::fprintf(stderr, "script FAIL (command %d): model %s%s\n", scriptLine_, want ? "" : "not ", m.c_str()); ++scriptFailures_; }
    });
    add({"sky"}, [this](const std::string&, std::istringstream& in) {
        // expect sky <name>: the environment's sky, and that it loads
        std::string w, n; in >> w >> n;
        if (doc_.environment().sky != n || !skies_.get(n)) {
            std::fprintf(stderr, "script FAIL (command %d): sky is '%s' (%s)\n", scriptLine_, doc_.environment().sky.c_str(), skies_.problem(n).c_str());
            ++scriptFailures_;
        }
    });
    add({"skycover"}, [this](const std::string&, std::istringstream& in) {
        // expect skycover <min> <max>: how much of the map's sky picture is kept in memory
        // (v0.11.2: the plain bottom is dropped; 1 = all of it)
        std::string w; double lo = 0, hi = 1; in >> w >> lo >> hi;
        const std::string& n = doc_.environment().sky;
        const double c = skies_.get(n) ? skies_.cut(n).cover : -1;
        if (c < lo - 1e-6 || c > hi + 1e-6) { std::fprintf(stderr, "script FAIL (command %d): sky %s keeps %g of its height\n", scriptLine_, n.c_str(), c); ++scriptFailures_; }
    });
    add({"skylab"}, [this](const std::string&, std::istringstream& in) {
        // expect skylab <field> <value...>: a field of the map's Sky Lab settings, as saved
        std::string w, k, v; in >> w >> k; std::getline(in >> std::ws, v);
        const std::string text = ";" + skyLabToString(doc_.environment().skylab) + ";";
        if (!doc_.environment().skylab.used || text.find(";" + k + "=" + v + ";") == std::string::npos) {
            std::fprintf(stderr, "script FAIL (command %d): skylab %s is not %s (%s)\n", scriptLine_, k.c_str(), v.c_str(), text.c_str());
            ++scriptFailures_;
        }
    });
    add({"skylabwindow"}, [this](const std::string&, std::istringstream& in) {
        // expect skylabwindow open|closed [<preview width>]
        std::string w, st; int width = -1; in >> w >> st >> width;
        const bool bad = (st == "open") != skyLabOpen_ || (width >= 0 && skyLabPreviewW_ != width);
        if (bad) { std::fprintf(stderr, "script FAIL (command %d): Sky Lab window %s, preview %d wide\n", scriptLine_, skyLabOpen_ ? "open" : "closed", skyLabPreviewW_); ++scriptFailures_; }
    });
    add({"fog"}, [this](const std::string&, std::istringstream& in) {
        // expect fog on|off [<base> <thickness>]: the map's fog
        std::string w, v; double base = 0, thick = 0; in >> w >> v;
        const bool withHeight = (bool)(in >> base >> thick);
        const Environment& e = doc_.environment();
        const bool bad = (v == "on") != e.fogOn || (withHeight && (std::fabs(e.fog.base - base) > 1e-6 || std::fabs(e.fog.thickness - thick) > 1e-6));
        if (bad) { std::fprintf(stderr, "script FAIL (command %d): fog %s, base %g thickness %g\n", scriptLine_, e.fogOn ? "on" : "off", e.fog.base, e.fog.thickness); ++scriptFailures_; }
    });
    add({"skyoutofdate"}, [this](const std::string&, std::istringstream& in) {
        // expect skyoutofdate yes|no
        std::string w, v; in >> w >> v;
        if ((v == "yes") != skyOutOfDate()) { std::fprintf(stderr, "script FAIL (command %d): sky out of date: %s\n", scriptLine_, skyOutOfDate() ? "yes" : "no"); ++scriptFailures_; }
    });
    add({"undos"}, [this](const std::string&, std::istringstream& in) {
        // expect undos <n>: how many steps undo can go back
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.undoCount() != n) { std::fprintf(stderr, "script FAIL (command %d): %zu undo steps, expected %zu\n", scriptLine_, doc_.undoCount(), n); ++scriptFailures_; }
    });
    add({"bookmarks"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.bookmarks().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu bookmarks, got %zu\n", scriptLine_, n, doc_.bookmarks().size()); ++scriptFailures_; }
    });
    add({"camera"}, [this](const std::string&, std::istringstream& in) {
        // expect camera x y z: the 3D view's camera position (+-0.5)
        std::string w; Vec3 v; in >> w >> v.x >> v.y >> v.z;
        const Vector3 c = views_[0]->cameraPos();
        if (length(Vec3{c.x, c.y, c.z} - v) > 0.5) { std::fprintf(stderr, "script FAIL (command %d): camera at %g %g %g\n", scriptLine_, c.x, c.y, c.z); ++scriptFailures_; }
    });
    add({"terrains"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        size_t have = 0;
        for (const Brush& b : doc_.brushes()) if (terrainValid(b)) ++have;
        if (have != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu terrains, got %zu\n", scriptLine_, n, have); ++scriptFailures_; }
    });
    add({"terrainheight"}, [this](const std::string&, std::istringstream& in) {
        // expect terrainheight <x> <y> <z>: the terrain surface straight below x,y is at z (+-0.5)
        std::string w; double x = 0, y = 0, z = 0; in >> w >> x >> y >> z;
        double best = -1e300;
        for (const Brush& b : doc_.brushes()) {
            const double t = rayHitTerrain(b, {x, y, 1e6}, {0, 0, -1});
            if (t >= 0) best = std::max(best, 1e6 - t);
        }
        if (std::fabs(best - z) > 0.5) { std::fprintf(stderr, "script FAIL (command %d): terrain at %g %g is %g, expected %g\n", scriptLine_, x, y, best, z); ++scriptFailures_; }
    });
    add({"bounds"}, [this](const std::string&, std::istringstream& in) {
        std::string w; Vec3 mn, mx; in >> w >> mn.x >> mn.y >> mn.z >> mx.x >> mx.y >> mx.z;
        const AABB b = doc_.selectionBounds();
        if (!b.valid() || length(b.min - mn) > 0.5 || length(b.max - mx) > 0.5) {
            std::fprintf(stderr, "script FAIL (command %d): selection bounds %g %g %g .. %g %g %g\n", scriptLine_, b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
            ++scriptFailures_;
        }
    });
    add({"groups"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        if (doc_.groups().size() != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu groups, got %zu\n", scriptLine_, n, doc_.groups().size()); ++scriptFailures_; }
    });
    add({"volume"}, [this](const std::string&, std::istringstream& in) {
        std::string w; double want = 0; in >> w >> want;
        double got = 0;
        for (const Brush& b : doc_.brushes()) got += brushVolume(b);
        if (std::fabs(got - want) > 0.5) { std::fprintf(stderr, "script FAIL (command %d): total volume %g, expected %g\n", scriptLine_, got, want); ++scriptFailures_; }
    });
    add({"texture"}, [this](const std::string&, std::istringstream& in) {
        std::string w, name; in >> w >> name;
        int bad = 0, n = 0;
        if (tool_ == &textureTool_) {
            for (const FaceRef& fr : textureTool_.faces(doc_)) { ++n; if (doc_.find(fr.first)->faces[fr.second].texture != name) ++bad; }
        } else {
            for (BrushId id : doc_.selection()) if (const Brush* b = doc_.find(id)) for (const Face& f : b->faces) { ++n; if (f.texture != name) ++bad; }
        }
        if (bad || n == 0) { std::fprintf(stderr, "script FAIL (command %d): %d of %d faces are not '%s'\n", scriptLine_, bad, n, name.c_str()); ++scriptFailures_; }
    });
    add({"pickedfaces"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        const size_t got = textureTool_.faces(doc_).size();
        if (got != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu picked faces, got %zu\n", scriptLine_, n, got); ++scriptFailures_; }
    });
    add({"current"}, [this](const std::string&, std::istringstream& in) {
        std::string w, name; in >> w >> name;
        if (currentTexture_ != name) { std::fprintf(stderr, "script FAIL (command %d): current texture is '%s', expected '%s'\n", scriptLine_, currentTexture_.c_str(), name.c_str()); ++scriptFailures_; }
    });
    add({"terrainpoints"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        if (vertexTool_.terrainPointCount() != n) {
            std::fprintf(stderr, "script FAIL (command %d): expected %zu terrain points selected, got %zu\n", scriptLine_, n, vertexTool_.terrainPointCount());
            ++scriptFailures_;
        }
    });
    add({"paintlayers"}, [this](const std::string&, std::istringstream& in) {
        // expect paintlayers <n>: painted textures, counted over every terrain patch
        std::string w; size_t n = 0; in >> w >> n;
        size_t have = 0;
        for (const Brush& b : doc_.brushes()) if (b.terrain) have += b.terrain->layers.size();
        if (have != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu painted textures, got %zu\n", scriptLine_, n, have); ++scriptFailures_; }
    });
    add({"ongrid"}, [this](const std::string&, std::istringstream&) {
        for (BrushId id : doc_.selection())
            if (const Brush* b = doc_.find(id); b && !brushOnGrid(*b, grid_.size)) {
                std::fprintf(stderr, "script FAIL (command %d): brush %u not on grid %g\n", scriptLine_, id, grid_.size);
                ++scriptFailures_;
            }
    });
    add({"faces"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        size_t got = 0;
        for (BrushId id : doc_.selection()) if (const Brush* b = doc_.find(id)) got += b->faces.size();
        if (got != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu faces, got %zu\n", scriptLine_, n, got); ++scriptFailures_; }
    });
    add({"size"}, [this](const std::string&, std::istringstream& in) {
        double x = 0, y = 0, z = 0; std::string what; in >> what >> x >> y >> z;
        const Vec3 s = doc_.selectionBounds().size();
        if (std::fabs(s.x - x) > 0.5 || std::fabs(s.y - y) > 0.5 || std::fabs(s.z - z) > 0.5) {
            std::fprintf(stderr, "script FAIL (command %d): expected selection size %g %g %g, got %g %g %g\n",
                         scriptLine_, x, y, z, s.x, s.y, s.z);
            ++scriptFailures_;
        }
    });
    add({"edges"}, [this](const std::string&, std::istringstream&) {
        edgeCheck_ = true;
    });
    add({"bottega"}, [this](const std::string&, std::istringstream& in) {
        // expect bottega ready|empty|notbottega|missing|newer|damaged|none
        std::string w, state; in >> w >> state;
        using S = bottega::State;
        const S s = bottegaCheck_.state;
        const bool ok = (state == "ready" && s == S::Ready) || (state == "empty" && s == S::Empty) ||
                        (state == "notbottega" && s == S::NotBottega) || (state == "missing" && s == S::MissingFolder) ||
                        (state == "newer" && s == S::NewerVersion) || (state == "damaged" && s == S::Damaged) ||
                        (state == "none" && s == S::NoPath);
        if (!ok) { std::fprintf(stderr, "script FAIL (command %d): expected Bottega state %s\n", scriptLine_, state.c_str()); ++scriptFailures_; }
    });
    // ---- materials (v0.12.4)
    add({"matsel"}, [this](const std::string&, std::istringstream& in) {
        std::string w; size_t n = 0; in >> w >> n;
        const size_t got = materialTargets().size();
        if (got != n) { std::fprintf(stderr, "script FAIL (command %d): expected %zu materials selected, got %zu\n", scriptLine_, n, got); ++scriptFailures_; }
    });
    add({"mat", "matfile", "matkind", "matshader", "matparent"}, [this](const std::string&, std::istringstream& in) {
        // expect mat <name> <key> <value...|none>   expect matfile <name> yes|no
        // expect matkind <name> texture|variant|helper|parent   expect matshader <name> <shader>
        // expect matparent <name> <parent|none>
        syncMaterialFiles();
        std::string w, name; in >> w >> name;
        const MaterialIndex::Entry* e = textures_.material(name);
        std::string want, got;
        if (w == "mat") {
            std::string key; in >> key; std::getline(in >> std::ws, want);
            got = e && e->mat.has(key) ? e->mat.str(key) : "none";
            if (want == "\"\"") want.clear();
        } else if (w == "matfile") {
            in >> want;
            got = e && !e->vmt.empty() ? "yes" : "no";
        } else if (w == "matkind") {
            in >> want;
            got = !e ? "none" : e->kind == MaterialIndex::Kind::Variant ? "variant" : e->kind == MaterialIndex::Kind::Helper ? "helper"
                  : e->kind == MaterialIndex::Kind::Parent ? "parent" : "texture";
        } else if (w == "matshader") {
            in >> want;
            got = e ? e->mat.shader : "none";
        } else {
            in >> want;
            const MaterialIndex::Entry* p = e && !e->mat.parentFile.empty() ? textures_.materials().byVmt(e->mat.parentFile) : nullptr;
            got = !e ? "?" : p ? p->name : e->mat.parentFile.empty() ? "none" : e->mat.parentFile;
        }
        if (got != want) { std::fprintf(stderr, "script FAIL (command %d): %s %s: expected \"%s\", got \"%s\"\n", scriptLine_, w.c_str(), name.c_str(), want.c_str(), got.c_str()); ++scriptFailures_; }
    });
    add({"hastexture", "hassky"}, [this](const std::string&, std::istringstream& in) {
        // expect hastexture <name> | expect hassky <name>; add "not" before the name to expect it absent
        std::string w, name; in >> w >> name;
        bool want = true;
        if (name == "not") { want = false; in >> name; }
        const bool has = w == "hastexture" ? textures_.exists(name) : skies_.exists(name);
        if (has != want) { std::fprintf(stderr, "script FAIL (command %d): %s %s%s\n", scriptLine_, w.c_str(), want ? "" : "not ", name.c_str()); ++scriptFailures_; }
    });
    add({""}, [this](const std::string&, std::istringstream& in) {
        std::string what; size_t n = 0; in >> what >> n;
        const size_t got = what == "selected" ? doc_.selection().size() : doc_.brushes().size();
        if (got != n) {
            std::fprintf(stderr, "script FAIL (command %d): expected %zu %s, got %zu\n", scriptLine_, n, what.c_str(), got);
            ++scriptFailures_;
        }
    });
}

} // namespace scal
