#include "app/editor.hpp"
#include "io/map_format.hpp"
#include "core/hull.hpp"
#include "core/texture.hpp"
#include "app/settings.hpp"
#include "platform/file_dialog.hpp"
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
#include <algorithm>
#include <cctype>

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
    {   // Built-in textures ship inside the app (macOS: Contents/Resources/textures).
        const std::string appDir = GetApplicationDirectory();
#if defined(__APPLE__)
        const std::string builtin = appDir + "../Resources/textures";
#else
        const std::string builtin = appDir + "textures";
#endif
        if (!opt_.automated()) loadTextureFolders();
        textures_.setBuiltinFolder(builtin);
        if (!textures_.exists(currentTexture_) && !textures_.names().empty()) currentTexture_ = textures_.names()[0];
    }
    lastBox_.min = {0, 0, 0};
    lastBox_.max = {64, 64, 64};
    if (!opt_.openPath.empty()) openMap(opt_.openPath);
    if (!opt_.scriptPath.empty()) loadScript(opt_.scriptPath);
    useNative_ = nativeDialogsAvailable() && !opt_.automated();
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
    static Vec3 pivotStore;
    if (pivotMode_ != 0) { pivotStore = pivotPoint(); e.pivot = &pivotStore; }
    e.slopeAngles = slopeAngles_;
    e.textureLock = tfTexLock_;
    e.currentTexture = &currentTexture_;
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
    MenuState st;
    st.gridVisible = grid_.visible;
    st.snapOn = grid_.snap;
    st.canUndo = doc_.canUndo();
    st.canRedo = doc_.canRedo();
    st.hasSelection = !doc_.selection().empty();
    st.selectTool = tool_ == &selectTool_;
    st.blockTool = tool_ == &blockTool_;
    st.blockPreview = st.blockTool && blockTool_.hasPreview();
    st.flying = flying_;
    st.controlsOpen = controlsOpen_;
    st.vertexTool = tool_ == &vertexTool_;
    st.verticesSelected = vertexTool_.selectedCount() > 0;
    st.pivotMode = pivotMode_;
    st.slopeAngles = slopeAngles_;
    st.microgrid = grid_.microgrid;
    st.textureTool = tool_ == &textureTool_;
    st.textureLock = tfTexLock_;
    st.smoothTextures = textures_.smooth();
    st.textured3D = textured3D_;
    st.defaultScale = defaultScale_;
    st.canApplyTexture = !doc_.selection().empty() || (tool_ == &textureTool_ && !textureTool_.faces(doc_).empty());
    const MenuActions m = drawMenuBar(st);
    const std::string status = statusText();
    drawStatusBar(status.c_str());
    dockspace(resetLayout_, "Tools###tools", views_[0]->title(), views_[1]->title(),
              views_[2]->title(), views_[3]->title(), "Textures###textures", "Face###face", showPanels_);
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
    if (m.selectNone) selectNone();
    if (m.duplicate) duplicate();
    if (m.del) deleteSelection();
    if (m.toolSelect) setTool(&selectTool_);
    if (m.toolBlock) setTool(&blockTool_);
    if (m.createBlock) onKey(KEY_ENTER);
    if (m.cancel) onKey(KEY_ESCAPE);
    if (m.transform) openTransformDialog();
    if (m.rotate90Axis >= 0) rotate90(m.rotate90Axis, m.rotate90Sign);
    if (m.flipAxis >= 0) flip(m.flipAxis);
    if (m.centerViews) centerViews();
    if (m.flyMode && !flying_) { flying_ = true; DisableCursor(); skipLookDelta_ = true; }
    else if (m.flyMode && flying_) { flying_ = false; EnableCursor(); }
    if (m.controls) controlsOpen_ = !controlsOpen_;
    if (m.toolVertex) setTool(&vertexTool_);
    if (m.vertexMove) { precisionError_.clear(); askVertexMove_ = true; }
    if (m.snapToGrid) snapSelectionToGrid();
    if (m.pivotMode == 0 || m.pivotMode == 1) pivotMode_ = m.pivotMode;
    if (m.pivotMode == 2) {
        const Vec3 c = pivotMode_ == 2 ? customPivot_ : doc_.selectionBounds().valid() ? doc_.selectionBounds().center() : Vec3{};
        pivotBuf_[0] = c.x; pivotBuf_[1] = c.y; pivotBuf_[2] = c.z;
        askPivot_ = true;
    }
    if (m.rotSnap >= 0) slopeAngles_ = m.rotSnap == 1;
    if (m.toggleMicrogrid) grid_.setMicrogrid(!grid_.microgrid);
    if (m.setGridSize) { gridSizeBuf_ = grid_.size; precisionError_.clear(); askGridSize_ = true; }
    if (m.applyTexture) applyCurrentTexture();
    if (m.applyNoDraw) applyTexture(kNoDrawTexture);
    if (m.toolTexture) setTool(&textureTool_);
    if (m.toggleTextureLock) tfTexLock_ = !tfTexLock_;
    if (m.defaultScale > 0) defaultScale_ = m.defaultScale;
    if (m.smoothTextures >= 0) textures_.setSmooth(m.smoothTextures == 1);
    if (m.textured3D >= 0) textured3D_ = m.textured3D == 1;
    if (m.addTextureFolder) { folderBuf_[0] = 0; askTexFolder_ = true; }
    if (m.rescanTextures) textures_.rescan();
    blockTool_.texture = currentTexture_;
    blockTool_.textureScale = defaultScale_;
    if (m.newMap) guardUnsaved(Pending::New);
    if (m.open) guardUnsaved(Pending::Open);
    if (m.save) save();
    if (m.saveAs) requestSaveAs();
    if (m.openPath) guardUnsaved(Pending::OpenPath);

    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && !io.WantTextInput) handleKeyboard();

    drawToolsPanel();
    if (showPanels_) { drawTextureBrowser(); drawFacePanel(); }
    drawViews();
    drawDialogs();
    drawTransformDialog();
    drawPrecisionDialogs();
    drawControlsWindow(&controlsOpen_);
    closeDialogs_ = false;

    // Remember the last brush size for new blocks' depth.
    if (!doc_.selection().empty()) {
        const AABB sb = doc_.selectionBounds();
        if (sb.valid()) lastBox_ = sb;
    }

    runNativeDialog();
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
        if (IsKeyPressed(KEY_D)) duplicate();
        if (IsKeyPressed(KEY_T)) {
            if (shift_) { if (tool_ == &vertexTool_ && vertexTool_.selectedCount()) { precisionError_.clear(); askVertexMove_ = true; } }
            else openTransformDialog();
        }
        if (IsKeyPressed(KEY_B)) snapSelectionToGrid();
        if (IsKeyPressed(KEY_E)) centerViews();
        if (IsKeyPressed(KEY_S)) { if (shift_) requestSaveAs(); else save(); }
        if (IsKeyPressed(KEY_O)) guardUnsaved(Pending::Open);
        if (IsKeyPressed(KEY_N)) guardUnsaved(Pending::New);
        return;
    }
    if (IsKeyPressed(KEY_F1)) controlsOpen_ = !controlsOpen_;
    if (IsKeyPressed(kKeyGridSmaller)) grid_.halve();
    if (IsKeyPressed(kKeyGridLarger)) grid_.doubleUp();
    if (shift_ && IsKeyPressed(KEY_R)) grid_.visible = !grid_.visible;
    if (shift_ && IsKeyPressed(KEY_W)) grid_.snap = !grid_.snap;
    if (shift_ && IsKeyPressed(KEY_S)) setTool(&selectTool_);
    if (shift_ && IsKeyPressed(KEY_B)) setTool(&blockTool_);
    if (shift_ && IsKeyPressed(KEY_V)) setTool(&vertexTool_);
    if (shift_ && IsKeyPressed(KEY_A)) setTool(&textureTool_);
    if (shift_ && IsKeyPressed(KEY_T)) applyCurrentTexture();

    for (int k : {KEY_ENTER, KEY_KP_ENTER, KEY_ESCAPE, KEY_DELETE, KEY_BACKSPACE})
        if (IsKeyPressed(k)) onKey(k);
}

Scene Editor::buildScene() {
    Scene s;
    s.brushes = cache_.get(doc_);
    // The texture tool works on faces; brush selection tint would compete with it.
    if (tool_ != &textureTool_) s.selected = doc_.selection();
    tool_->decorate(doc_, s);
    if (pivotMode_ != 0) s.pivot = pivotPoint();
    if (textured3D_) s.textures = &textures_;
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
        toolButton(&vertexTool_, "Shift+V");
        toolButton(&textureTool_, "Shift+A");
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
            if (path_.empty()) requestSaveAs();
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
    char buf[768];
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

    std::snprintf(buf, sizeof buf, "Grid %g  Snap %s  Tex %s  |  %s  |  %s  |  %s  |  %s   [%llu]",
                  grid_.size, grid_.snap ? "on" : "off", currentTexture_.c_str(), tool_->name(), tool_->hint(),
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

void Editor::selectNone() {
    tool_->cancel();
    doc_.setSelection({});
}

void Editor::duplicate() {
    if (doc_.selection().empty()) return;
    tool_->cancel();
    std::vector<Brush> copies;
    const Vec3 offset{grid_.size, grid_.size, 0};
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id)) {
            Brush c = *b;
            c.id = 0;
            c.translate(offset, true);
            copies.push_back(std::move(c));
        }
    doc_.apply(doc_.makeAdd(std::move(copies), "Duplicate"));
}

void Editor::deleteSelection() {
    if (doc_.selection().empty()) return;
    tool_->cancel();
    doc_.apply(doc_.makeDelete(doc_.selection(), "Delete"));
}

bool Editor::applyTransform(const TransformSpec& t, const char* name) {
    std::vector<Brush> sel;
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id)) sel.push_back(*b);
    if (sel.empty()) return false;
    std::string err;
    std::vector<Brush> out = scal::applyTransform(sel, t, tfTexLock_, err);
    if (out.empty()) {
        tfError_ = err;
        return false;
    }
    tool_->cancel();
    doc_.apply(doc_.makeModify(out, name));
    return true;
}

void Editor::rotate90(int axis, int sign) {
    TransformSpec t;
    t.kind = TransformKind::Rotate;
    t.axis = axis;
    t.degrees = 90.0 * sign;
    t.pivot = doc_.selectionBounds().center();
    if (!applyTransform(t, "Rotate 90")) errorText_ = "Could not rotate: " + tfError_;
}

void Editor::flip(int axis) {
    TransformSpec t;
    t.kind = TransformKind::Scale;
    t.scale[axis] = -1;
    t.pivot = doc_.selectionBounds().center();
    if (!applyTransform(t, "Flip")) errorText_ = "Could not flip: " + tfError_;
}

void Editor::centerViews() {
    if (doc_.selection().empty()) return;
    const Vec3 c = doc_.selectionBounds().center();
    for (int i = 1; i < 4; ++i) views_[i]->center2D(c[views_[i]->axU()], c[views_[i]->axV()]);
}

void Editor::openTransformDialog() {
    if (doc_.selection().empty()) return;
    tfError_.clear();
    tfPivot_ = pivotMode_;
    if (pivotMode_ == 2) { tfCustom_[0] = customPivot_.x; tfCustom_[1] = customPivot_.y; tfCustom_[2] = customPivot_.z; }
    askTransform_ = true;
}

void Editor::drawTransformDialog() {
    if (askTransform_) { ImGui::OpenPopup("Transform"); askTransform_ = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Transform", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::RadioButton("Move", &tfMode_, 0); ImGui::SameLine();
    ImGui::RadioButton("Rotate", &tfMode_, 1); ImGui::SameLine();
    ImGui::RadioButton("Scale", &tfMode_, 2);
    ImGui::Separator();
    ImGui::PushItemWidth(260);
    if (tfMode_ == 0) {
        ImGui::InputScalarN("Offset X Y Z", ImGuiDataType_Double, tfMove_, 3, nullptr, nullptr, "%g");
    } else if (tfMode_ == 1) {
        static const char* axes[] = {"X", "Y", "Z"};
        ImGui::Combo("Axis", &tfAxis_, axes, 3);
        ImGui::InputDouble("Degrees", &tfDegrees_, 0, 0, "%.6g");
        ImGui::TextDisabled("positive = counter-clockwise, looking down the axis");
        for (double d : {15.0, 22.5, 30.0, 45.0, 90.0, -90.0}) {
            char b[16];
            std::snprintf(b, sizeof b, "%g", d);
            if (ImGui::SmallButton(b)) tfDegrees_ = d;
            ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::TextDisabled("grid-friendly angles: corners stay on whole numbers when sizes");
        ImGui::TextDisabled("(from the pivot) are multiples of 5, 13, 17 or 25:");
        const struct { const char* label; double rise, run; } slopes[] = {
            {"3:4:5", 3, 4}, {"4:3:5", 4, 3}, {"5:12:13", 5, 12}, {"12:5:13", 12, 5},
            {"8:15:17", 8, 15}, {"15:8:17", 15, 8}, {"7:24:25", 7, 24}, {"24:7:25", 24, 7}};
        for (const auto& sl : slopes) {
            if (ImGui::SmallButton(sl.label)) tfDegrees_ = std::atan2(sl.rise, sl.run) * 180.0 / 3.14159265358979323846;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%.4f\xC2\xB0", std::atan2(sl.rise, sl.run) * 180.0 / 3.14159265358979323846);
            ImGui::SameLine();
        }
        ImGui::NewLine();
    } else {
        ImGui::InputScalarN("Factor X Y Z", ImGuiDataType_Double, tfScale_, 3, nullptr, nullptr, "%g");
        ImGui::TextDisabled("1 = unchanged, 2 = double, -1 = mirror");
    }
    if (tfMode_ != 0) {
        ImGui::Separator();
        ImGui::TextUnformatted("Pivot");
        ImGui::RadioButton("Selection centre", &tfPivot_, 0); ImGui::SameLine();
        ImGui::RadioButton("World origin", &tfPivot_, 1); ImGui::SameLine();
        ImGui::RadioButton("Point", &tfPivot_, 2);
        if (tfPivot_ == 2) ImGui::InputScalarN("Point X Y Z", ImGuiDataType_Double, tfCustom_, 3, nullptr, nullptr, "%g");
    }
    ImGui::PopItemWidth();
    ImGui::Separator();
    ImGui::Checkbox("Texture lock (also Texture menu)", &tfTexLock_);
    if (!tfError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", tfError_.c_str());

    const bool apply = ImGui::Button("Apply (Return)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                       ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_;
    if (apply) {
        TransformSpec t;
        t.kind = tfMode_ == 0 ? TransformKind::Move : tfMode_ == 1 ? TransformKind::Rotate : TransformKind::Scale;
        t.move = {tfMove_[0], tfMove_[1], tfMove_[2]};
        t.axis = tfAxis_;
        t.degrees = tfDegrees_;
        t.scale = {tfScale_[0], tfScale_[1], tfScale_[2]};
        t.pivot = tfPivot_ == 0 ? doc_.selectionBounds().center()
                : tfPivot_ == 1 ? Vec3{} : Vec3{tfCustom_[0], tfCustom_[1], tfCustom_[2]};
        static const char* names[] = {"Move", "Rotate", "Scale"};
        if (applyTransform(t, names[tfMode_])) ImGui::CloseCurrentPopup();
    } else if (cancel) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

Vec3 Editor::pivotPoint() const {
    if (pivotMode_ == 1) return {};
    if (pivotMode_ == 2) return customPivot_;
    const AABB b = doc_.selectionBounds();
    return b.valid() ? b.center() : Vec3{};
}

void Editor::snapSelectionToGrid() {
    if (doc_.selection().empty()) return;
    tool_->cancel();
    std::vector<Brush> out;
    for (BrushId id : doc_.selection()) {
        const Brush* b = doc_.find(id);
        if (!b) continue;
        if (brushOnGrid(*b, grid_.size)) continue;
        HullResult r = snapBrushToGrid(*b, grid_.size);
        if (!r.ok) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%g", grid_.size);
            errorText_ = "Snap to grid (" + std::string(buf) + "): " + r.error;
            return;
        }
        out.push_back(std::move(r.brush));
    }
    if (!out.empty()) doc_.apply(doc_.makeModify(out, "Snap to grid"));
}

void Editor::drawPrecisionDialogs() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    if (askGridSize_)   { ImGui::OpenPopup("Grid size"); askGridSize_ = false; }
    if (askPivot_)      { ImGui::OpenPopup("Pivot point"); askPivot_ = false; }
    if (askVertexMove_) { ImGui::OpenPopup("Move vertices"); askVertexMove_ = false; }
    auto buttons = [&](bool& apply, bool& cancel) {
        apply = ImGui::Button("Apply (Return)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        ImGui::SameLine();
        cancel = ImGui::Button("Cancel (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_;
    };
    bool apply = false, cancel = false;

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Grid size", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(160);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputDouble("units", &gridSizeBuf_, 0, 0, "%g");
        ImGui::TextDisabled("any size, e.g. 80; below 1 turns on the microgrid");
        for (double g : {0.125, 0.25, 0.5, 1.0, 8.0, 16.0, 32.0, 64.0}) {
            char b[16]; std::snprintf(b, sizeof b, "%g", g);
            if (ImGui::SmallButton(b)) gridSizeBuf_ = g;
            ImGui::SameLine();
        }
        ImGui::NewLine();
        buttons(apply, cancel);
        if (apply && gridSizeBuf_ > 0) { grid_.setSize(gridSizeBuf_); ImGui::CloseCurrentPopup(); }
        else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Pivot point", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(260);
        ImGui::InputScalarN("X Y Z", ImGuiDataType_Double, pivotBuf_, 3, nullptr, nullptr, "%g");
        ImGui::TextDisabled("rotations and scaling (drag and Transform dialog) use this point");
        buttons(apply, cancel);
        if (apply) {
            customPivot_ = {pivotBuf_[0], pivotBuf_[1], pivotBuf_[2]};
            pivotMode_ = 2;
            ImGui::CloseCurrentPopup();
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Move vertices", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Vec3 single;
        const bool one = vertexTool_.singlePosition(single);
        if (ImGui::IsWindowAppearing()) {
            vmoveMode_ = 0;
            vmove_[0] = vmove_[1] = vmove_[2] = 0;
        }
        ImGui::Text("%zu corner(s) selected", vertexTool_.selectedCount());
        ImGui::RadioButton("Move by", &vmoveMode_, 0);
        if (one) {
            ImGui::SameLine();
            if (ImGui::RadioButton("Set position", &vmoveMode_, 1)) { vmove_[0] = single.x; vmove_[1] = single.y; vmove_[2] = single.z; }
        } else if (vmoveMode_ == 1) vmoveMode_ = 0;
        ImGui::SetNextItemWidth(260);
        ImGui::InputScalarN("X Y Z", ImGuiDataType_Double, vmove_, 3, nullptr, nullptr, "%g");
        if (!precisionError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", precisionError_.c_str());
        buttons(apply, cancel);
        if (apply) {
            Vec3 d{vmove_[0], vmove_[1], vmove_[2]};
            if (vmoveMode_ == 1 && one) d = d - single;
            std::string err;
            if (vertexTool_.moveSelected(doc_, d, err)) ImGui::CloseCurrentPopup();
            else precisionError_ = "Not applied: " + err;
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ------------------------------------------------------------------ textures

void Editor::loadTextureFolders() {
    const std::string dir = settingsDir();
    if (dir.empty()) return;
    std::ifstream f(dir + "/texture_folders.txt");
    std::string line;
    while (std::getline(f, line))
        if (!line.empty()) textures_.addUserFolder(line);
}

void Editor::saveTextureFolders() const {
    if (opt_.automated()) return; // tests never touch user settings
    const std::string dir = settingsDir();
    if (dir.empty()) return;
    std::ofstream f(dir + "/texture_folders.txt");
    for (const std::string& d : textures_.userFolders()) f << d << '\n';
}

template <class Fn> void Editor::editFaces(const char* name, Fn fn) {
    const std::vector<FaceRef> faces = textureTool_.faces(doc_);
    if (faces.empty()) return;
    std::vector<Brush> out;
    for (const FaceRef& fr : faces) {
        auto it = std::find_if(out.begin(), out.end(), [&](const Brush& b) { return b.id == fr.first; });
        if (it == out.end()) {
            out.push_back(*doc_.find(fr.first));
            it = out.end() - 1;
        }
        Face& f = it->faces[fr.second];
        Polygon poly;
        for (const Polygon& p : it->polygons()) if (p.face == fr.second) { poly = p; break; }
        const Vec3 n = f.plane().n;
        fn(f, poly, textures_.size(f.texture), n);
    }
    doc_.apply(doc_.makeModify(out, name));
}

void Editor::applyTexture(const std::string& tex) {
    if (tool_ == &textureTool_ && !textureTool_.faces(doc_).empty()) {
        editFaces("Apply texture", [&](Face& f, const Polygon&, TexSize, const Vec3&) { f.texture = tex; });
        return;
    }
    if (doc_.selection().empty()) return;
    std::vector<Brush> out;
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id)) {
            Brush c = *b;
            for (Face& f : c.faces) f.texture = tex;
            out.push_back(std::move(c));
        }
    doc_.apply(doc_.makeModify(out, "Apply texture"));
}

void Editor::drawTextureBrowser() {
    if (ImGui::Begin("Textures###textures", nullptr, ImGuiWindowFlags_NoCollapse)) {
        // Current texture with a small preview.
        const Texture2D& cur = textures_.get(currentTexture_);
        ImGui::Image(ImTextureID(cur.id), ImVec2(40, 40));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(currentTexture_.c_str());
        ImGui::TextDisabled("%d x %d", cur.width, cur.height);
        ImGui::EndGroup();
        if (ImGui::Button("Apply to selection (Shift+T)")) applyCurrentTexture();

        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##search", "search by name", texSearch_, sizeof texSearch_);
        std::string needle = texSearch_;
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });

        const float footer = ImGui::GetFrameHeightWithSpacing() * 1.4f;
        if (ImGui::BeginChild("##grid", ImVec2(0, -footer), ImGuiChildFlags_None)) {
            const float cell = 76.0f;
            const float avail = ImGui::GetContentRegionAvail().x;
            const int cols = std::max(1, (int)(avail / (cell + ImGui::GetStyle().ItemSpacing.x)));
            int col = 0, shown = 0;
            for (const std::string& name : textures_.names()) {
                std::string low = name;
                std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (!needle.empty() && low.find(needle) == std::string::npos) continue;
                ++shown;
                if (col > 0) ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::PushID(name.c_str());
                const Texture2D& t = textures_.get(name);
                const bool selected = name == currentTexture_;
                if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::ImageButton("##t", ImTextureID(t.id), ImVec2(cell - 8, cell - 8))) currentTexture_ = name;
                if (selected) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                {
                    bool user = false;
                    for (const std::string& d : textures_.userFolders()) user = user || d == textures_.folderOf(name);
                    ImGui::SetTooltip("%s\n%d x %d\n%s", name.c_str(), t.width, t.height, user ? "your folder" : "built-in");
                }
                std::string label = name;
                while (label.size() > 3 && ImGui::CalcTextSize(label.c_str()).x > cell) label = label.substr(0, label.size() - 4) + "..";
                ImGui::TextUnformatted(label.c_str());
                ImGui::PopID();
                ImGui::EndGroup();
                if (++col >= cols) col = 0;
            }
            if (shown == 0) ImGui::TextDisabled(textures_.names().empty() ? "No textures. Texture -> Add texture folder..." : "No texture matches.");
        }
        ImGui::EndChild();
        if (ImGui::Button("Add folder...")) { folderBuf_[0] = 0; askTexFolder_ = true; }
        ImGui::SameLine();
        if (ImGui::Button("Rescan")) textures_.rescan();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu textures", textures_.names().size());
    }
    ImGui::End();

    // Add-folder dialog + folder list.
    if (askTexFolder_) { ImGui::OpenPopup("Texture folders"); askTexFolder_ = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Texture folders", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Your texture folders (PNG, TGA, JPG, BMP; subfolders included):");
        int remove = -1;
        for (size_t i = 0; i < textures_.userFolders().size(); ++i) {
            ImGui::PushID((int)i);
            if (ImGui::SmallButton("Remove")) remove = (int)i;
            ImGui::SameLine();
            ImGui::TextUnformatted(textures_.userFolders()[i].c_str());
            ImGui::PopID();
        }
        if (remove >= 0) { textures_.removeUserFolder((size_t)remove); saveTextureFolders(); }
        if (textures_.userFolders().empty()) ImGui::TextDisabled("(none yet - the built-in textures are always there)");
        ImGui::Separator();
        ImGui::SetNextItemWidth(520);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputTextWithHint("##folder", "folder path to add", folderBuf_, sizeof folderBuf_,
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        if (useNative_) {
            if (ImGui::Button("Browse...")) { native_ = Native::Folder; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
        }
        if ((ImGui::Button("Add") || enter) && folderBuf_[0]) {
            textures_.addUserFolder(folderBuf_);
            saveTextureFolders();
            folderBuf_[0] = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Editor::drawFacePanel() {
    if (ImGui::Begin("Face###face", nullptr, ImGuiWindowFlags_NoCollapse)) {
        const std::vector<FaceRef> faces = textureTool_.faces(doc_);
        if (tool_ != &textureTool_) {
            ImGui::TextWrapped("Pick faces with the Texture tool (Shift+A) to adjust how textures sit on them.");
        } else if (faces.empty()) {
            ImGui::TextWrapped("Click faces in the 3D view. Shift+click adds or removes a face, Cmd+click takes the whole brush, Option+click picks up a face's texture.");
        } else {
            const Face& f = doc_.find(faces[0].first)->faces[faces[0].second];
            ImGui::Text("%zu face(s)   %s", faces.size(), f.texture.c_str());
            ImGui::PushItemWidth(90);
            double su = f.u.offset, sv = f.v.offset;
            ImGui::TextUnformatted("Shift"); ImGui::SameLine(70);
            ImGui::InputDouble("##su", &su, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit()) editFaces("Texture shift", [&](Face& x, const Polygon&, TexSize, const Vec3&) { x.u.offset = su; });
            ImGui::SameLine();
            ImGui::InputDouble("##sv", &sv, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit()) editFaces("Texture shift", [&](Face& x, const Polygon&, TexSize, const Vec3&) { x.v.offset = sv; });
            double cu = f.scaleU, cv = f.scaleV;
            ImGui::TextUnformatted("Scale"); ImGui::SameLine(70);
            ImGui::InputDouble("##cu", &cu, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit() && cu != 0) editFaces("Texture scale", [&](Face& x, const Polygon&, TexSize, const Vec3&) { x.scaleU = cu; });
            ImGui::SameLine();
            ImGui::InputDouble("##cv", &cv, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit() && cv != 0) editFaces("Texture scale", [&](Face& x, const Polygon&, TexSize, const Vec3&) { x.scaleV = cv; });
            double rot = f.rotation;
            ImGui::TextUnformatted("Rotation"); ImGui::SameLine(70);
            ImGui::InputDouble("##rot", &rot, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const double delta = rot - f.rotation;
                editFaces("Texture rotation", [&](Face& x, const Polygon&, TexSize, const Vec3& n) { rotateTexture(x, n, delta); });
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("-90")) editFaces("Texture rotation", [&](Face& x, const Polygon&, TexSize, const Vec3& n) { rotateTexture(x, n, -90); });
            ImGui::SameLine();
            if (ImGui::SmallButton("+90")) editFaces("Texture rotation", [&](Face& x, const Polygon&, TexSize, const Vec3& n) { rotateTexture(x, n, 90); });
            ImGui::PopItemWidth();
            ImGui::Separator();
            if (ImGui::Button("Fit")) editFaces("Fit texture", [&](Face& x, const Polygon& p, TexSize ts, const Vec3&) { fitTexture(x, p, ts, fitTimes_[0], fitTimes_[1]); });
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            ImGui::InputInt2("times", fitTimes_);
            fitTimes_[0] = std::max(1, fitTimes_[0]);
            fitTimes_[1] = std::max(1, fitTimes_[1]);
            ImGui::TextUnformatted("Justify");
            ImGui::SameLine();
            const struct { const char* l; Justify j; } js[] = {{"L", Justify::Left}, {"R", Justify::Right}, {"T", Justify::Top},
                                                              {"B", Justify::Bottom}, {"C", Justify::Center}};
            for (const auto& j : js) {
                if (ImGui::SmallButton(j.l)) editFaces("Justify texture", [&](Face& x, const Polygon& p, TexSize ts, const Vec3&) { justifyTexture(x, p, ts, j.j); });
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::TextUnformatted("Align");
            ImGui::SameLine();
            if (ImGui::SmallButton("World")) editFaces("Align texture", [&](Face& x, const Polygon&, TexSize, const Vec3& n) { alignTextureToWorld(x, n); });
            ImGui::SameLine();
            if (ImGui::SmallButton("Face")) editFaces("Align texture", [&](Face& x, const Polygon&, TexSize, const Vec3& n) { alignTextureToFace(x, n); });
            ImGui::Separator();
            if (ImGui::Button("Apply current texture")) applyCurrentTexture();
            ImGui::SameLine();
            if (ImGui::Button("Use this texture")) currentTexture_ = f.texture;
        }
        ImGui::Checkbox("Texture lock", &tfTexLock_);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Textures stay put on surfaces when brushes move, rotate or scale");
    }
    ImGui::End();
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

void Editor::requestSaveAs() {
    if (useNative_) { native_ = Native::SaveAs; return; }
    std::snprintf(pathBuf_, sizeof pathBuf_, "%s", path_.empty() ? defaultMapPath().c_str() : path_.c_str());
    askSaveAs_ = true;
}

void Editor::requestOpen() {
    if (useNative_) { native_ = Native::Open; return; }
    std::snprintf(pathBuf_, sizeof pathBuf_, "%s", path_.empty() ? defaultMapPath().c_str() : path_.c_str());
    askOpen_ = true;
}

void Editor::runNativeDialog() {
    if (native_ == Native::None) return;
    // Wait until no modifier key or mouse button is held (see editor.hpp).
    for (int k : {KEY_LEFT_SUPER, KEY_RIGHT_SUPER, KEY_LEFT_SHIFT, KEY_RIGHT_SHIFT, KEY_LEFT_ALT, KEY_RIGHT_ALT,
                  KEY_LEFT_CONTROL, KEY_RIGHT_CONTROL})
        if (IsKeyDown(k)) return;
    for (int b : {MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE})
        if (IsMouseButtonDown(b)) return;
    const Native what = native_;
    native_ = Native::None;
    namespace fs = std::filesystem;
    const std::string docs = fs::path(defaultMapPath()).parent_path().string();
    const std::string startDir = path_.empty() ? docs : fs::path(path_).parent_path().string();
    std::string picked;
    switch (what) {
        case Native::Open:
            if (openFileDialog(startDir, {"map"}, picked)) openMap(picked);
            break;
        case Native::SaveAs:
            if (saveFileDialog(startDir, path_.empty() ? "untitled.map" : fileName(path_), {"map"}, picked)) {
                if (fs::path(picked).extension() != ".map") picked += ".map";
                if (saveMap(picked)) runPending();
                else pending_ = Pending::None;
            } else {
                pending_ = Pending::None; // cancelled: e.g. don't quit
            }
            break;
        case Native::Folder:
            if (chooseFolderDialog(docs, picked)) { textures_.addUserFolder(picked); saveTextureFolders(); }
            break;
        case Native::None: break;
    }
}

void Editor::save() {
    if (path_.empty()) {
        requestSaveAs();
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
        case Pending::Open: requestOpen(); break;
        case Pending::OpenPath:
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
//   transform move x y z | transform rotate x|y|z <deg> [origin] | transform scale sx sy sz
//   rotate90 x|y|z <+1|-1>  flip x|y|z  duplicate  dialog transform|controls  closedialogs
//   tool vertex   vertexmove dx dy dz   snapgrid   microgrid on|off   rotsnap slopes|15
//   pivot center|origin|<x y z>   expect ongrid   expect faces <n>
//   view <1|2|3> <u> <v> [zoom]   (centre a 2D view; zoom = pixels per unit)
//   layout classic|default   (classic = no right-hand panels; older scripts use it)
//   wpress <view> <u> <v> [mods] / wdrag <u> <v>   (2D input in world coordinates)
//   press3d <dx> <dy> [mods]   (3D view click relative to its centre)
//   tool texture   texturedir <path>   texture <name>   applytexture   applynodraw   rendermode flat|textured
//   texscale <s>   facefit   facerotate <deg>   facealign world|face
//   facejustify left|right|top|bottom|center
//   expect texture <name>   expect pickedfaces <n>   expect current <name>
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
        setTool(t == "block" ? (Tool*)&blockTool_ : t == "vertex" ? (Tool*)&vertexTool_
              : t == "texture" ? (Tool*)&textureTool_ : (Tool*)&selectTool_);
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
    } else if (cmd == "texturedir") { std::string d; std::getline(in >> std::ws, d); textures_.addUserFolder(d);
    } else if (cmd == "texture") { in >> currentTexture_;
    } else if (cmd == "applytexture") { applyCurrentTexture();
    } else if (cmd == "applynodraw") { applyTexture(kNoDrawTexture);
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
