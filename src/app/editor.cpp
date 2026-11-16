#include "app/editor.hpp"
#include "io/map_format.hpp"
#include "ui/layout.hpp"

#include "imgui.h"
#include "rlImGui.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace scal {

namespace {

// Physical-key shortcuts (layout independent). On an Italian keyboard the two keys
// right of P ("è" and "+") sit where US "[" and "]" are.
constexpr int kKeyGridSmaller = KEY_LEFT_BRACKET;
constexpr int kKeyGridLarger  = KEY_RIGHT_BRACKET;

bool cmdDown() {
#if defined(__APPLE__)
    return IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
#else
    return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
#endif
}

std::string defaultMapPath() {
    const char* home = std::getenv("HOME");
    if (!home) return "untitled.map";
    return (std::filesystem::path(home) / "Documents" / "untitled.map").string();
}

std::string fileName(const std::string& p) {
    return std::filesystem::path(p).filename().string();
}

int keyFromName(const std::string& n) {
    if (n == "Return" || n == "Enter") return KEY_ENTER;
    if (n == "Esc" || n == "Escape") return KEY_ESCAPE;
    if (n == "Delete") return KEY_DELETE;
    if (n == "Backspace") return KEY_BACKSPACE;
    return 0;
}

} // namespace

Editor::Editor(const EditorOptions& opt) : opt_(opt) {
    // Hammer default arrangement: 3D top-left, top top-right, front bottom-left, side bottom-right.
    views_[0] = std::make_unique<Viewport>(ViewKind::Persp);
    views_[1] = std::make_unique<Viewport>(ViewKind::Top);
    views_[2] = std::make_unique<Viewport>(ViewKind::Front);
    views_[3] = std::make_unique<Viewport>(ViewKind::Side);
    if (const char* vs = std::getenv("SCAL_VIEW_SCALE")) viewScale_ = (float)std::atof(vs); // testing only
    lastBox_.min = {0, 0, 0};
    lastBox_.max = {64, 64, 64};
    if (!opt_.openPath.empty()) openMap(opt_.openPath);
    if (!opt_.scriptPath.empty()) loadScript(opt_.scriptPath);
    updateTitle();
}

Editor::~Editor() = default;

// ------------------------------------------------------------------ input paths

Pointer Editor::pointerFor(int view, Vector2 local) const {
    Pointer p;
    p.view = views_[view].get();
    p.local = local;
    if (p.view->is2D()) p.view->screenToWorld2D(local, p.u, p.v);
    return p;
}

ToolEnv Editor::env() {
    ToolEnv e{doc_, grid_, cache_.get(doc_), lastBox_};
    e.cmd = cmd_;
    e.shift = shift_;
    e.alt = alt_;
    return e;
}

void Editor::onPress(int view, Vector2 local, bool real) {
    if (real) capture_ = view;
    ToolEnv e = env();
    tool_->press(e, pointerFor(view, local));
}

void Editor::onDrag(int view, Vector2 local) {
    ToolEnv e = env();
    tool_->drag(e, pointerFor(view, local));
}

void Editor::onRelease(int view, Vector2 local, bool real) {
    ToolEnv e = env();
    tool_->release(e, pointerFor(view, local));
    if (real) capture_ = -1;
}

void Editor::onKey(int key) {
    ToolEnv e = env();
    tool_->key(e, key);
}

// ------------------------------------------------------------------ frame

bool Editor::frame(float dt) {
    ++redraws_;
    cmd_ = cmdDown();
    shift_ = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    alt_ = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    ImGuiIO& io = ImGui::GetIO();

    stepScript();

    // ---- chrome
    const MenuActions m = drawMenuBar(grid_.visible, grid_.snap, doc_.canUndo(), doc_.canRedo());
    const std::string status = statusText();
    drawStatusBar(status.c_str());
    dockspace(resetLayout_, "Tools###tools", views_[0]->title(), views_[1]->title(),
              views_[2]->title(), views_[3]->title());
    resetLayout_ = false;

    if (m.quit) requestQuit();
    if (m.resetLayout) resetLayout_ = true;
    if (m.gridSmaller) grid_.halve();
    if (m.gridLarger) grid_.doubleUp();
    if (m.toggleGrid) grid_.visible = !grid_.visible;
    if (m.toggleSnap) grid_.snap = !grid_.snap;
    if (m.undo) undo();
    if (m.redo) redo();
    if (m.selectAll) selectAll();
    if (m.newMap) guardUnsaved(Pending::New);
    if (m.open) guardUnsaved(Pending::Open);
    if (m.save) save();
    if (m.saveAs) { std::snprintf(pathBuf_, sizeof pathBuf_, "%s", path_.empty() ? defaultMapPath().c_str() : path_.c_str()); askSaveAs_ = true; }

    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && !io.WantTextInput) handleKeyboard();

    drawToolsPanel();
    drawViews();
    drawDialogs();

    // Remember the last brush size for new blocks' depth.
    if (!doc_.selection().empty()) {
        const AABB sb = doc_.selectionBounds();
        if (sb.valid()) lastBox_ = sb;
    }

    updateTitle();
    return flying_ || rmbLook_ || !script_.empty() || !opt_.screenshot.empty();
}

void Editor::handleKeyboard() {
    // Fly mode: Z over the 3D view toggles; Esc also leaves.
    if (IsKeyPressed(KEY_Z) && !cmd_ && (flying_ || hovered_ == 0)) {
        flying_ = !flying_;
        if (flying_) { DisableCursor(); skipLookDelta_ = true; }
        else EnableCursor();
    }
    if (flying_ && IsKeyPressed(KEY_ESCAPE)) { flying_ = false; EnableCursor(); return; }
    if (flying_) return; // WASD etc. belong to the camera

    if (cmd_) {
        if (IsKeyPressed(KEY_Z)) { if (shift_) redo(); else undo(); }
        if (IsKeyPressed(KEY_A)) selectAll();
        if (IsKeyPressed(KEY_S)) { if (shift_) { std::snprintf(pathBuf_, sizeof pathBuf_, "%s", path_.empty() ? defaultMapPath().c_str() : path_.c_str()); askSaveAs_ = true; } else save(); }
        if (IsKeyPressed(KEY_O)) guardUnsaved(Pending::Open);
        if (IsKeyPressed(KEY_N)) guardUnsaved(Pending::New);
        return;
    }
    if (IsKeyPressed(kKeyGridSmaller)) grid_.halve();
    if (IsKeyPressed(kKeyGridLarger)) grid_.doubleUp();
    if (shift_ && IsKeyPressed(KEY_R)) grid_.visible = !grid_.visible;
    if (shift_ && IsKeyPressed(KEY_W)) grid_.snap = !grid_.snap;
    if (shift_ && IsKeyPressed(KEY_S)) setTool(&selectTool_);
    if (shift_ && IsKeyPressed(KEY_B)) setTool(&blockTool_);

    for (int k : {KEY_ENTER, KEY_KP_ENTER, KEY_ESCAPE, KEY_DELETE, KEY_BACKSPACE})
        if (IsKeyPressed(k)) onKey(k);
}

Scene Editor::buildScene() {
    Scene s;
    s.brushes = cache_.get(doc_);
    s.selected = doc_.selection();
    tool_->decorate(doc_, s);
    return s;
}

void Editor::drawViews() {
    ImGuiIO& io = ImGui::GetIO();
    const bool mouseCaptured = flying_ || rmbLook_;
    if (mouseCaptured) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    else io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;

    const Vector2 mouse = GetMousePosition();
    const bool space = IsKeyDown(KEY_SPACE);
    hovered_ = -1;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    for (int i = 0; i < 4; ++i) {
        Viewport& v = *views_[i];
        const ImGuiWindowFlags wf = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                    ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin(v.title(), nullptr, wf)) {
            // Place the view on whole physical pixels so its texture maps 1:1 to the
            // screen. (Fractional placement made the sampler skip rows/columns, which
            // is what made 1-pixel lines vanish at some positions.)
            const float fb = io.DisplayFramebufferScale.x > 0 ? io.DisplayFramebufferScale.x : 1.0f;
            ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 avail0 = ImGui::GetContentRegionAvail();
            const ImVec2 snapped(std::ceil(origin.x * fb) / fb, std::ceil(origin.y * fb) / fb);
            const ImVec2 avail(avail0.x - (snapped.x - origin.x), avail0.y - (snapped.y - origin.y));
            origin = snapped;
            ImGui::SetCursorScreenPos(origin);
            viewOrigin_[i] = {origin.x, origin.y};
            v.resize((int)std::floor(avail.x), (int)std::floor(avail.y), viewScale_ > 0 ? viewScale_ : fb);

            // Invisible button over the canvas: owns mouse clicks so ImGui never drags windows.
            ImGui::InvisibleButton("##canvas", ImVec2((float)v.width(), (float)v.height()),
                                   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);
            const bool hovered = ImGui::IsItemHovered();
            viewHovered_[i] = hovered;
            if (hovered) hovered_ = i;
            const Vector2 local{mouse.x - origin.x, mouse.y - origin.y};

            if (hovered && !mouseCaptured) {
                if (v.is2D()) {
                    v.zoom2D(GetMouseWheelMove(), local);
                    v.screenToWorld2D(local, cursorU_, cursorV_);
                    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) || (space && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))
                        panning_ = i;
                } else if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                    rmbLook_ = true;
                    DisableCursor();
                    skipLookDelta_ = true;
                }
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !space && panning_ < 0 && capture_ < 0)
                    onPress(i, local);
            }

            const Scene scene = buildScene();
            v.render(grid_, scene);
            ImGui::SetCursorScreenPos(origin);
            const RenderTexture2D& rt = v.target();
            ImGui::Image(ImTextureID(rt.texture.id), ImVec2((float)v.width(), (float)v.height()),
                         ImVec2(0, 1), ImVec2(1, 0)); // render textures are stored upside down
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(origin.x + 6, origin.y + 4), IM_COL32(200, 200, 200, 255), v.label());
            if (v.is2D() && scene.handleBox) drawSizeLabels(v, *scene.handleBox, origin);
        }
        ImGui::End();
    }
    ImGui::PopStyleVar();

    // ---- left-button drags continue even outside the view
    if (capture_ >= 0) {
        const Vector2 local{mouse.x - viewOrigin_[capture_].x, mouse.y - viewOrigin_[capture_].y};
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) onRelease(capture_, local);
        else {
            const Vector2 d = GetMouseDelta();
            if (d.x != 0 || d.y != 0) onDrag(capture_, local);
        }
    }

    // ---- 2D panning
    if (panning_ >= 0) {
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            views_[panning_]->pan2D(GetMouseDelta());
        else panning_ = -1;
    }

    // ---- 3D look / fly
    if (rmbLook_ && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        rmbLook_ = false;
        if (!flying_) EnableCursor();
    }
    if (flying_ || rmbLook_) {
        Viewport& v3 = *views_[0];
        if (skipLookDelta_) skipLookDelta_ = false;
        else v3.look(GetMouseDelta());
        const float dt = GetFrameTime() > 0.05f ? 0.05f : GetFrameTime();
        const float f = (float)IsKeyDown(KEY_W) - (float)IsKeyDown(KEY_S);
        const float r = (float)IsKeyDown(KEY_D) - (float)IsKeyDown(KEY_A);
        const float u = (float)IsKeyDown(KEY_E) - (float)IsKeyDown(KEY_Q);
        v3.fly(f, r, u, dt, shift_);
    }
}

void Editor::drawSizeLabels(const Viewport& v, const AABB& box, ImVec2 origin) {
    const int U = v.axU(), V = v.axV();
    const Vector2 a = v.worldToLocal2D(box.min[U], box.max[V]); // top-left
    const Vector2 b = v.worldToLocal2D(box.max[U], box.min[V]); // bottom-right
    char buf[32];
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = IM_COL32(255, 200, 200, 255);
    std::snprintf(buf, sizeof buf, "%g", box.max[U] - box.min[U]);
    const ImVec2 su = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(origin.x + (a.x + b.x) * 0.5f - su.x * 0.5f, origin.y + a.y - su.y - 6), col, buf);
    std::snprintf(buf, sizeof buf, "%g", box.max[V] - box.min[V]);
    const ImVec2 sv = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(origin.x + b.x + 9, origin.y + (a.y + b.y) * 0.5f - sv.y * 0.5f), col, buf);
}

void Editor::drawToolsPanel() {
    if (ImGui::Begin("Tools###tools", nullptr, ImGuiWindowFlags_NoCollapse)) {
        const float w = ImGui::GetContentRegionAvail().x;
        auto toolButton = [&](Tool* t, const char* shortcut) {
            const bool active = tool_ == t;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(t->name(), ImVec2(w, 0))) setTool(t);
            if (active) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", shortcut);
        };
        toolButton(&selectTool_, "Shift+S");
        toolButton(&blockTool_, "Shift+B");
    }
    ImGui::End();
}

void Editor::drawDialogs() {
    if (askSaveAs_) { ImGui::OpenPopup("Save map as"); askSaveAs_ = false; }
    if (askOpen_)   { ImGui::OpenPopup("Open map");    askOpen_ = false; }
    if (askUnsaved_){ ImGui::OpenPopup("Unsaved changes"); askUnsaved_ = false; }
    if (!errorText_.empty() && !ImGui::IsPopupOpen("Error")) ImGui::OpenPopup("Error");

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    auto pathDialog = [&](const char* id, const char* button, auto&& onOk) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("File path (.map):");
            ImGui::SetNextItemWidth(560);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##path", pathBuf_, sizeof pathBuf_, ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button(button) || enter) { onOk(std::string(pathBuf_)); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                if (pending_ != Pending::None) pending_ = Pending::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    };
    pathDialog("Save map as", "Save", [&](const std::string& p) {
        if (saveMap(p)) runPending();
        else pending_ = Pending::None;
    });
    pathDialog("Open map", "Open", [&](const std::string& p) { openMap(p); });

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save changes to %s first?", path_.empty() ? "this map" : fileName(path_).c_str());
        if (ImGui::Button("Save")) {
            ImGui::CloseCurrentPopup();
            if (path_.empty()) { std::snprintf(pathBuf_, sizeof pathBuf_, "%s", defaultMapPath().c_str()); askSaveAs_ = true; }
            else if (saveMap(path_)) runPending();
            else pending_ = Pending::None;
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save")) { ImGui::CloseCurrentPopup(); runPending(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { pending_ = Pending::None; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(errorText_.c_str());
        if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            errorText_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

std::string Editor::statusText() const {
    char buf[512];
    std::string where;
    if (flying_ || rmbLook_) {
        const Vector3 p = views_[0]->cameraPos();
        std::snprintf(buf, sizeof buf, "cam %.0f %.0f %.0f   WASD move, Q/E down/up, Shift fast%s",
                      p.x, p.y, p.z, flying_ ? ", Z or Esc to stop" : "");
        where = buf;
    } else if (hovered_ > 0) {
        const Viewport& v = *views_[hovered_];
        std::snprintf(buf, sizeof buf, "%s %.0f  %s %.0f", v.axisU(), grid_.snapValue(cursorU_),
                      v.axisV(), grid_.snapValue(cursorV_));
        where = buf;
    } else if (hovered_ == 0) {
        where = "Z: fly   hold right mouse: look";
    }

    std::string sel;
    if (!doc_.selection().empty()) {
        const AABB b = doc_.selectionBounds();
        const Vec3 s = b.size();
        std::snprintf(buf, sizeof buf, "%zu selected  %g x %g x %g", doc_.selection().size(), s.x, s.y, s.z);
        sel = buf;
    }

    std::snprintf(buf, sizeof buf, "Grid %d  Snap %s  |  %s  |  %s  |  %s  |  %s   [%llu]",
                  grid_.size, grid_.snap ? "on" : "off", tool_->name(), tool_->hint(),
                  sel.c_str(), where.c_str(), redraws_);
    return buf;
}

void Editor::updateTitle() {
    std::string t = "Scalpellino - " + (path_.empty() ? std::string("untitled") : fileName(path_));
    if (doc_.dirty()) t += " *";
    if (t != lastTitle_) {
        SetWindowTitle(t.c_str());
        lastTitle_ = t;
    }
}

// ------------------------------------------------------------------ commands

void Editor::setTool(Tool* t) {
    if (t == tool_) return;
    tool_->cancel();
    capture_ = -1;
    tool_ = t;
}

void Editor::undo() { tool_->cancel(); capture_ = -1; doc_.undo(); }
void Editor::redo() { tool_->cancel(); capture_ = -1; doc_.redo(); }

void Editor::selectAll() {
    std::set<BrushId> all;
    for (const Brush& b : doc_.brushes()) all.insert(b.id);
    doc_.setSelection(all);
}

void Editor::newMap() {
    tool_->cancel();
    doc_.reset({{"classname", "worldspawn"}, {"mapversion", "220"}}, {}, {});
    path_.clear();
}

void Editor::openMap(const std::string& path) {
    Document loaded;
    const MapReadResult r = loadMapFile(path, loaded);
    if (!r.ok) {
        errorText_ = "Could not open " + fileName(path) + ":\n" + r.error;
        return;
    }
    tool_->cancel();
    doc_.reset(loaded.worldKeys, loaded.brushes(), loaded.otherEntities);
    path_ = path;
    if (r.skippedBrushes > 0)
        errorText_ = "Opened, but skipped " + std::to_string(r.skippedBrushes) + " invalid brush(es).";
}

bool Editor::saveMap(const std::string& path) {
    std::string err;
    if (!saveMapFile(doc_, path, err)) {
        errorText_ = "Could not save " + fileName(path) + ":\n" + err;
        return false;
    }
    doc_.markSaved();
    path_ = path;
    return true;
}

void Editor::save() {
    if (path_.empty()) {
        std::snprintf(pathBuf_, sizeof pathBuf_, "%s", defaultMapPath().c_str());
        askSaveAs_ = true;
    } else {
        saveMap(path_);
    }
}

void Editor::requestQuit() { guardUnsaved(Pending::Quit); }

void Editor::guardUnsaved(Pending action) {
    pending_ = action;
    if (doc_.dirty() && !opt_.automated()) askUnsaved_ = true;
    else runPending();
}

void Editor::runPending() {
    const Pending p = pending_;
    pending_ = Pending::None;
    switch (p) {
        case Pending::New:  newMap(); break;
        case Pending::Open:
            std::snprintf(pathBuf_, sizeof pathBuf_, "%s", path_.empty() ? defaultMapPath().c_str() : path_.c_str());
            askOpen_ = true;
            break;
        case Pending::Quit: exit_ = true; break;
        case Pending::None: break;
    }
}

// ------------------------------------------------------------------ scripting
//
// One command per frame. Views: 0=3d 1=top 2=front 3=side. Coordinates are pixels
// inside the view. Lines starting with # are comments.
//   tool select|block       press <view> <x> <y> [shift] [cmd] [alt]
//   drag <x> <y>            release
//   key Return|Esc|Delete   undo | redo | selectall
//   grid <n>                save <path> | open <path>
//   shot <file.png>         quit
//   expect brushes|selected <n>  |  expect size <x> <y> <z>  (selection bounds, +-0.5)
//   expect edges   (every brush/preview edge in the 2D views is visible on screen)
//   A failed expect prints FAIL and makes the exit code 1.

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
    if (warmup_ > 0) { --warmup_; return; } // let views get their sizes first
    std::istringstream in(script_.front());
    script_.pop_front();
    std::string cmd;
    in >> cmd;
    ++scriptLine_;
    static int dragView = -1;
    if (cmd == "tool") {
        std::string t; in >> t;
        setTool(t == "block" ? (Tool*)&blockTool_ : (Tool*)&selectTool_);
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
    } else if (cmd == "undo") { undo();
    } else if (cmd == "redo") { redo();
    } else if (cmd == "selectall") { selectAll();
    } else if (cmd == "grid") { int g; in >> g; grid_.size = g;
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
