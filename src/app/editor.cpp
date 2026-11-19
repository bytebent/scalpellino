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
#if defined(__APPLE__)
        const std::string skyDir = appDir + "../Resources/skies";
#else
        const std::string skyDir = appDir + "skies";
#endif
        if (!opt_.automated()) loadSkyFolders();
        skies_.setBuiltinFolder(skyDir);
        modelFinder_ = models_.finder();
        if (!opt_.automated()) loadBottegaSetting(); // automated runs never read user settings
        if (!textures_.exists(currentTexture_) && !textures_.names().empty()) currentTexture_ = textures_.names()[0];
    }
    lastBox_.min = {0, 0, 0};
    lastBox_.max = {64, 64, 64};
    if (!opt_.openPath.empty()) openMap(opt_.openPath);
    if (!opt_.scriptPath.empty()) loadScript(opt_.scriptPath);
    useNative_ = nativeDialogsAvailable() && !opt_.automated();
    updateTitle();
}

Editor::~Editor() { models_.unloadAll(); }

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
    e.models = &modelFinder_;
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
    // Multi-Replicate's window stays open while you look around: Return / Esc in the views
    // create or cancel.
    if (replicating_ && !scriptPreview_ && (key == KEY_ENTER || key == KEY_KP_ENTER || key == KEY_ESCAPE)) {
        if (key != KEY_ESCAPE) applyReplicate(repSpec_);
        replicating_ = false;
        return;
    }
    if (tool_ == &terrainTool_ && terrainTool_.picking() && (key == KEY_ENTER || key == KEY_KP_ENTER)) {
        makeTerrainNow(terrainPower_);
        return;
    }
    if (tool_ == &curveTool_ && (key == KEY_ENTER || key == KEY_KP_ENTER) && curveTool_.hasShape()) {
        commitCurve();
        return;
    }
    ToolEnv e = env();
    if (tool_->key(e, key)) return;
    // Delete works in every tool that doesn't use the key itself.
    if (key == KEY_DELETE || key == KEY_BACKSPACE) deleteSelection();
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
    st.hasAnything = st.hasSelection || !doc_.lightSelection().empty() || !doc_.primSelection().empty() || !doc_.propSelection().empty();
    st.propTool = tool_ == &propTool_;
    st.propPending = st.propTool && propTool_.hasPending();
    st.propsSelected = doc_.propSelection().size();
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
    st.clipTool = tool_ == &clipTool_;
    st.clipPlane = clipTool_.hasPlane();
    st.clipKeep = clipTool_.keep == ClipTool::Keep::Both ? 0 : clipTool_.keep == ClipTool::Keep::Front ? 1 : 2;
    st.curveTool = tool_ == &curveTool_;
    st.terrainTool = tool_ == &terrainTool_;
    st.terrainMode = (int)terrainTool_.mode;
    st.terrainSelected = selectionHasTerrain();
    st.canMakeTerrain = true;
    st.terrainPicking = tool_ == &terrainTool_ && terrainTool_.picking();
    st.lightTool = tool_ == &lightTool_;
    st.primTool = tool_ == &primTool_;
    st.primPending = st.primTool && primTool_.hasPending();
    st.primKind = (int)primTool_.templ.kind;
    st.primsSelected = doc_.primSelection().size();
    st.primsInCombo = false;
    for (MathPrimId id : doc_.primSelection())
        if (const MathPrim* p = doc_.findPrim(id)) st.primsInCombo = st.primsInCombo || p->combo != 0;
    st.lightPending = st.lightTool && lightTool_.pending.has_value();
    st.lightKind = (int)lightTool_.templ.kind;
    st.lightsSelected = !doc_.lightSelection().empty();
    st.showSky = showSky_;
    st.lightPreview = lightPreview_;
    st.showFog = showFog_;
    st.showFigure = showFigure_;
    st.groupSelected = selectedGroup() != 0;
    st.facesPicked = tool_ == &textureTool_ && textureTool_.faces(doc_).size() > 1;
    st.canApplyTexture = !doc_.selection().empty() || (tool_ == &textureTool_ && !textureTool_.faces(doc_).empty());
    const MenuActions m = drawMenuBar(st);
    const std::string status = statusText();
    drawStatusBar(status.c_str());
    dockspace(resetLayout_, "Tools###tools", views_[0]->title(), views_[1]->title(),
              views_[2]->title(), views_[3]->title(), "Textures###textures", "Face###face", showPanels_,
              "Visgroups###visgroups", "Props###props");
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
    if (m.toolClip) { if (tool_ == &clipTool_) clipTool_.cycleKeep(); else setTool(&clipTool_); }
    if (m.clipKeep >= 0) clipTool_.keep = m.clipKeep == 0 ? ClipTool::Keep::Both : m.clipKeep == 1 ? ClipTool::Keep::Front : ClipTool::Keep::Back;
    if (m.clipApply) onKey(KEY_ENTER);
    if (m.clipPoints) {
        Vec3 a, b, c;
        if (clipTool_.hasPlane()) clipTool_.points(a, b, c);
        else { const AABB sb = doc_.selectionBounds(); a = sb.center(); b = a + Vec3{0, 64, 0}; c = a + Vec3{0, 0, 64}; }
        const Vec3 pts[3] = {a, b, c};
        for (int i = 0; i < 3; ++i) for (int k = 0; k < 3; ++k) clipBuf_[i][k] = pts[i][k];
        shapingError_.clear();
        askClipPoints_ = true;
    }
    if (m.findOverlaps) findOverlapsNow();
    if (m.viewsToOrigin) allViewsToOrigin();
    if (m.hideSelected) hideSelected();
    if (m.hideUnselected) hideUnselected();
    if (m.showAll) showAll();
    if (m.toolCurve) setTool(&curveTool_);
    if (m.toolTerrain) setTool(&terrainTool_);
    if (m.toolLight) setTool(&lightTool_);
    if (m.toolPrim) setTool(&primTool_);
    if (m.toolProp) setTool(&propTool_);
    if (m.createProp) onKey(KEY_ENTER);
    if (m.primKind >= 0) { primTool_.templ.kind = (PrimKind)m.primKind; setTool(&primTool_); }
    if (m.createPrim) primTool_.create(doc_, currentTexture_, &lastBox_);
    if (m.combine) combinePrims();
    if (m.replicate) openReplicate();
    if (m.snapLand) snapToLand();
    if (m.snapGround) askGround_ = true;
    if (m.separate) separatePrims();
    if (m.primOp >= 0) setPrimOp((PrimOp)m.primOp);
    if (m.lightKind >= 0) { lightTool_.templ.kind = (LightKind)m.lightKind; setTool(&lightTool_); }
    if (m.createLight) lightTool_.create(doc_);
    if (m.environment) { worldError_.clear(); envBuf_ = doc_.environment(); askEnvironment_ = true; }
    if (m.bookmarks) { worldError_.clear(); askBookmarks_ = true; }
    if (m.figurePos) {
        const Vec3 f = doc_.figure() ? *doc_.figure() : Vec3{};
        figureBuf_[0] = f.x; figureBuf_[1] = f.y; figureBuf_[2] = f.z;
        askFigure_ = true;
    }
    if (m.toggleSky) showSky_ = !showSky_;
    if (m.toggleLightPreview) { lightPreview_ = !lightPreview_; if (!lightPreview_) lighting_.clear(); }
    if (m.toggleFog) showFog_ = !showFog_;
    if (m.toggleFigure) {
        showFigure_ = !showFigure_;
        if (showFigure_ && !doc_.figure()) {
            double u = 0, v = 0;
            views_[1]->screenToWorld2D({views_[1]->width() * 0.5f, views_[1]->height() * 0.5f}, u, v);
            doc_.setFigure(groundBelow(grid_.snapValue(u), grid_.snapValue(v)));
        }
    }
    if (m.terrainMode >= 0) { terrainTool_.mode = (SculptMode)m.terrainMode; setTool(&terrainTool_); terrainTool_.stopPicking(); }
    if (m.makeTerrain) startMakeTerrain();
    if (m.terrainGrid) {
        terrainError_.clear();
        for (BrushId id : doc_.selection())
            if (const Brush* b = doc_.find(id)) if (b->terrain) terrainPower_ = b->terrain->power;
        askMakeTerrain_ = true;
    }
    if (m.removeTerrain) removeTerrain();
    if (m.heightmap) { terrainError_.clear(); askHeightmap_ = true; }
    if (m.stitchTerrain) stitchAllTerrain();
    terrainTool_.paintTexture = currentTexture_;
    if (m.editCurve) editCurve();
    if (m.bakeCurve) bakeCurves();
    if (m.carve) carveWithSelection();
    if (m.hollow) { shapingError_.clear(); askHollow_ = true; }
    if (m.applyTexture) applyCurrentTexture();
    if (m.applyNoDraw) applyTexture(kNoDrawTexture);
    if (m.wrapAround) wrapAroundPicked();
    if (m.toolTexture) setTool(&textureTool_);
    if (m.toggleTextureLock) tfTexLock_ = !tfTexLock_;
    if (m.defaultScale > 0) defaultScale_ = m.defaultScale;
    if (m.smoothTextures >= 0) { textures_.setSmooth(m.smoothTextures == 1); models_.setSmooth(m.smoothTextures == 1); }
    if (m.textured3D >= 0) textured3D_ = m.textured3D == 1;
    if (m.addTextureFolder) { folderBuf_[0] = 0; askTexFolder_ = true; }
    if (m.rescanBottega) rescanBottega();
    if (m.bottegaFolder) { std::snprintf(bottegaBuf_, sizeof bottegaBuf_, "%s", bottegaRoot_.c_str()); bottegaMsg_.clear(); askBottega_ = true; }
    if (!curveTool_.hasShape()) { curveTool_.params.texture = currentTexture_; curveTool_.params.texScale = defaultScale_; }
    blockTool_.texture = currentTexture_;
    blockTool_.textureScale = defaultScale_;
    if (m.newMap) guardUnsaved(Pending::New);
    if (m.open) guardUnsaved(Pending::Open);
    if (m.save) save();
    if (m.saveAs) requestSaveAs();
    if (m.openPath) guardUnsaved(Pending::OpenPath);

    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && !io.WantTextInput) handleKeyboard();

    drawToolsPanel();
    if (showPanels_) { drawTextureBrowser(); drawPropsPanel(); drawVisgroupsPanel(); drawFacePanel(); }
    drawViews();
    drawDialogs();
    drawTransformDialog();
    drawPrecisionDialogs();
    drawShapingDialogs();
    drawTerrainDialogs();
    drawWorldDialogs();
    drawReplicateDialog();
    drawBottegaDialog();
    drawControlsWindow(&controlsOpen_);
    drawOverlapsWindow();
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
        if (IsKeyPressed(KEY_D)) { if (shift_) openReplicate(); else duplicate(); }
        if (IsKeyPressed(KEY_J)) snapToLand();
        if (IsKeyPressed(KEY_T)) {
            if (shift_) { if (tool_ == &vertexTool_ && vertexTool_.selectedCount()) { precisionError_.clear(); askVertexMove_ = true; } }
            else openTransformDialog();
        }
        if (IsKeyPressed(KEY_B)) snapSelectionToGrid();
        if (shift_ && IsKeyPressed(KEY_C)) carveWithSelection();
        if (shift_ && IsKeyPressed(KEY_H) && !doc_.selection().empty()) { shapingError_.clear(); askHollow_ = true; }
        if (IsKeyPressed(KEY_E)) centerViews();
        if (IsKeyPressed(KEY_R)) rescanBottega();
        if (IsKeyPressed(KEY_U)) showAll();
        if (IsKeyPressed(KEY_ZERO)) allViewsToOrigin();
        if (shift_ && IsKeyPressed(KEY_O)) { findOverlapsNow(); return; }
        if (IsKeyPressed(KEY_S)) { if (shift_) requestSaveAs(); else save(); }
        if (IsKeyPressed(KEY_O) && !shift_) guardUnsaved(Pending::Open);
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
    if (shift_ && IsKeyPressed(KEY_C)) setTool(&curveTool_);
    if (shift_ && IsKeyPressed(KEY_G)) setTool(&terrainTool_);
    if (shift_ && IsKeyPressed(KEY_L)) setTool(&lightTool_);
    if (shift_ && IsKeyPressed(KEY_M)) setTool(&primTool_);
    if (shift_ && IsKeyPressed(KEY_P)) setTool(&propTool_);
    if (IsKeyPressed(KEY_H)) { if (shift_) hideUnselected(); else hideSelected(); }
    if (shift_ && IsKeyPressed(KEY_X)) { if (tool_ == &clipTool_) clipTool_.cycleKeep(); else setTool(&clipTool_); }
    if (shift_ && IsKeyPressed(KEY_T)) applyCurrentTexture();

    for (int k : {KEY_ENTER, KEY_KP_ENTER, KEY_ESCAPE, KEY_DELETE, KEY_BACKSPACE})
        if (IsKeyPressed(k)) onKey(k);
}

Scene Editor::buildScene() {
    Scene s;
    s.brushes = cache_.get(doc_);
    s.models = &models_;
    // The texture tool works on faces; brush selection tint would compete with it.
    if (tool_ != &textureTool_) s.selected = doc_.selection();
    for (const Light& l : doc_.lights()) s.lights.push_back({l, doc_.lightSelection().count(l.id) > 0});
    for (const MathPrim& p : doc_.prims()) s.prims.push_back({p, doc_.primSelection().count(p.id) > 0});
    for (const Prop& p : doc_.props()) {
        Scene::SceneProp sp;
        sp.prop = p;
        sp.selected = doc_.propSelection().count(p.id) > 0;
        const Model* m = models_.find(p.model);
        sp.local = propLocalBounds(m);
        sp.missing = m == nullptr;
        s.props.push_back(std::move(sp));
    }
    s.lightPreview = lightPreview_;
    s.ambient = doc_.environment().ambient;
    if (replicating_) { // Multi-Replicate preview
        std::vector<Brush> bs;
        std::set<BrushId> sel = doc_.selection();
        std::set<std::uint32_t> groups;
        for (BrushId id : sel) if (const Brush* b = doc_.find(id)) if (b->group) groups.insert(b->group);
        for (const Brush& b : doc_.brushes()) if (b.group && groups.count(b.group)) sel.insert(b.id);
        for (BrushId id : sel) if (const Brush* b = doc_.find(id)) bs.push_back(*b);
        std::vector<MathPrim> ps;
        for (MathPrimId id : doc_.withCombos(doc_.primSelection())) if (const MathPrim* p = doc_.findPrim(id)) ps.push_back(*p);
        std::vector<Light> ls;
        for (LightId id : doc_.lightSelection()) if (const Light* l = doc_.findLight(id)) ls.push_back(*l);
        std::vector<Prop> qs;
        for (PropId id : doc_.propSelection()) if (const Prop* q = doc_.findProp(id)) qs.push_back(*q);
        for (const ReplicateCopy& c : replicate(bs, ps, ls, repSpec_, qs, modelFinder_)) {
            for (const Brush& b : c.brushes) s.ghosts.push_back(makeSceneBrush(b));
            for (const MathPrim& p : c.prims) s.pendingPrims.push_back(p);
            for (const Prop& q : c.props) {
                Scene::SceneProp sp;
                sp.prop = q;
                const Model* m = models_.find(q.model);
                sp.local = propLocalBounds(m);
                sp.missing = m == nullptr;
                s.pendingProps.push_back(std::move(sp));
            }
            for (const Light& l : c.lights) s.previewLights.push_back(l);
        }
    }
    tool_->decorate(doc_, s);
    for (SceneBrush& r : s.replaced)
        for (SceneBrush& b : s.brushes)
            if (b.id == r.id) { b = std::move(r); break; }
    s.replaced.clear();
    if (pivotMode_ != 0) s.pivot = pivotPoint();
    if (textured3D_) s.textures = &textures_;
    if (showSky_ && !doc_.environment().sky.empty())
        if (const Texture2D* t = skies_.get(doc_.environment().sky)) s.skyTexture = t->id;
    if (showFigure_) s.figure = doc_.figure() ? *doc_.figure() : Vec3{};
    if (lightPreview_) s.lighting = &lighting_.get(doc_, cache_.get(doc_));
    const Environment& env = doc_.environment();
    if (showFog_ && env.fogOn) {
        s.fog = true;
        s.fogColor = env.fog.color;
        s.fogStart = env.fog.start;
        s.fogEnd = env.fog.end;
    }
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
            ImGui::SetNextItemAllowOverlap(); // the 'origin' button sits on top of the canvas
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
                else if (capture_ < 0) {
                    const Vector2 md = GetMouseDelta();
                    if (md.x != 0 || md.y != 0) { ToolEnv e = env(); tool_->hover(e, pointerFor(i, local)); }
                }
            }

            const Scene scene = buildScene();
            v.render(grid_, scene);
            ImGui::SetCursorScreenPos(origin);
            const RenderTexture2D& rt = v.target();
            ImGui::Image(ImTextureID(rt.texture.id), ImVec2((float)v.width(), (float)v.height()),
                         ImVec2(0, 1), ImVec2(1, 0)); // render textures are stored upside down
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(origin.x + 6, origin.y + 4), IM_COL32(200, 200, 200, 255), v.label());
            {   // Corner button: this view back to the origin.
                const ImVec2 keep = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(origin.x + v.width() - 52, origin.y + 3));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 1));
                if (ImGui::SmallButton("origin")) v.resetToOrigin();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back to the origin (Cmd+0: all views)");
                ImGui::PopStyleVar();
                ImGui::SetCursorScreenPos(keep);
                ImGui::Dummy(ImVec2(0, 0)); // ImGui needs an item after moving the cursor back
            }
            if (v.is2D() && scene.handleBox) drawSizeLabels(v, *scene.handleBox, origin);
            if (v.is2D() && scene.clipPoints.size() == 3)
                for (int k = 0; k < 3; ++k) {
                    const Vector2 c = v.worldToLocal2D(scene.clipPoints[k][v.axU()], scene.clipPoints[k][v.axV()]);
                    const char num[2] = {(char)('1' + k), 0};
                    dl->AddText(ImVec2(origin.x + c.x + 8, origin.y + c.y - 18 + 6 * k), IM_COL32(255, 255, 255, 255), num);
                }
        }
        ImGui::End();
    }
    ImGui::PopStyleVar();

    if (hovered_ < 0 && capture_ < 0 && !opt_.automated()) { ToolEnv e = env(); tool_->hover(e, Pointer{}); }

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
        toolButton(&clipTool_, "Shift+X");
        toolButton(&curveTool_, "Shift+C");
        toolButton(&terrainTool_, "Shift+G");
        toolButton(&lightTool_, "Shift+L");
        toolButton(&primTool_, "Shift+M");
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
    if (tool_ == &terrainTool_) terrainTool_.stopPicking();
    capture_ = -1;
    tool_ = t;
}

void Editor::undo() { tool_->cancel(); capture_ = -1; doc_.undo(); }
void Editor::redo() { tool_->cancel(); capture_ = -1; doc_.redo(); }

void Editor::selectAll() {
    std::set<BrushId> all;
    for (const Brush& b : doc_.brushes())
        if (doc_.visible(b)) all.insert(b.id); // hidden brushes aren't selectable
    doc_.setSelection(all);
}

void Editor::selectNone() {
    tool_->cancel();
    doc_.setSelection({});
    doc_.setPropSelection({});
}

void Editor::duplicate() {
    if (doc_.selection().empty() && !doc_.primSelection().empty()) {
        tool_->cancel();
        // Copies keep their combinations together, as new combinations.
        std::map<std::uint32_t, std::uint32_t> combos;
        std::uint32_t next = newComboId(doc_.prims());
        std::vector<MathPrim> copies;
        for (MathPrimId id : doc_.primSelection())
            if (const MathPrim* p = doc_.findPrim(id)) {
                MathPrim c = *p;
                c.id = 0;
                c.pos += Vec3{grid_.size, grid_.size, 0};
                if (c.combo) {
                    auto it = combos.find(c.combo);
                    if (it == combos.end()) it = combos.emplace(c.combo, next++).first;
                    c.combo = it->second;
                }
                copies.push_back(std::move(c));
            }
        doc_.apply(doc_.makeAddPrims(std::move(copies), "Duplicate primitive"));
        return;
    }
    if (doc_.selection().empty() && !doc_.propSelection().empty()) {
        tool_->cancel();
        std::vector<Prop> copies;
        for (PropId id : doc_.propSelection())
            if (const Prop* p = doc_.findProp(id)) {
                Prop c = *p;
                c.id = 0;
                c.pos += Vec3{grid_.size, grid_.size, 0};
                copies.push_back(std::move(c));
            }
        doc_.apply(doc_.makeAddProps(std::move(copies), "Duplicate prop"));
        return;
    }
    if (doc_.selection().empty() && !doc_.lightSelection().empty()) {
        tool_->cancel();
        std::vector<Light> copies;
        for (LightId id : doc_.lightSelection())
            if (const Light* l = doc_.findLight(id)) {
                Light c = *l;
                c.id = 0;
                c.pos += Vec3{grid_.size, grid_.size, 0};
                copies.push_back(std::move(c));
            }
        doc_.apply(doc_.makeAddLights(std::move(copies), "Duplicate light"));
        return;
    }
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
    if (doc_.selection().empty() && doc_.lightSelection().empty() && doc_.primSelection().empty() && doc_.propSelection().empty()) return;
    tool_->cancel();
    Command c = doc_.makeDelete(doc_.selection(), "Delete");
    doc_.addLightDeletes(c, doc_.lightSelection());
    doc_.addPrimDeletes(c, doc_.primSelection());
    doc_.addPropDeletes(c, doc_.propSelection());
    doc_.apply(std::move(c));
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
        if (r.ok && b->terrain) {
            const double g = grid_.size;
            carryTerrain(*b, r.brush, [g](const Vec3& v) {
                return Vec3{std::round(v.x / g) * g, std::round(v.y / g) * g, std::round(v.z / g) * g};
            });
        }
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
        if (vertexTool_.terrainPointCount() > 0) {
            // Terrain points move along the terrain's normal only.
            double h = 0;
            const bool sameH = vertexTool_.singleHeight(doc_, h);
            ImGui::Text("%zu terrain point(s) selected", vertexTool_.terrainPointCount());
            ImGui::RadioButton("Raise by", &vmoveMode_, 0);
            ImGui::SameLine();
            if (ImGui::RadioButton("Set height", &vmoveMode_, 1)) vmove_[0] = sameH ? h : 0;
            ImGui::SetNextItemWidth(160);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::InputDouble("units", &vmove_[0], 0, 0, "%g");
            ImGui::TextDisabled("heights are measured from the face, along its normal");
            if (!precisionError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", precisionError_.c_str());
            buttons(apply, cancel);
            if (apply) {
                std::string err;
                if (vertexTool_.moveTerrainSelected(doc_, vmove_[0], vmoveMode_ == 1, err)) ImGui::CloseCurrentPopup();
                else precisionError_ = "Not applied: " + err;
            } else if (cancel) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
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

// ------------------------------------------------------------------ shaping

void Editor::carveWithSelection() {
    if (doc_.selection().empty()) return;
    tool_->cancel();
    const std::set<BrushId> cutters = doc_.selection();
    std::vector<Brush> modified, added;
    std::set<BrushId> deleted;
    int touched = 0;
    for (const Brush& b : doc_.brushes()) {
        if (cutters.count(b.id)) continue;
        std::vector<Brush> pieces{b};
        bool changed = false;
        for (BrushId cid : cutters) {
            const Brush* cutter = doc_.find(cid);
            std::vector<Brush> next;
            for (const Brush& p : pieces) {
                std::vector<Brush> r = carveBrush(p, *cutter);
                if (r.size() != 1 || r[0].faces.size() != p.faces.size()) changed = true;
                for (Brush& q : r) next.push_back(std::move(q));
            }
            pieces = std::move(next);
        }
        if (!changed) continue;
        ++touched;
        if (pieces.empty()) { deleted.insert(b.id); continue; }
        pieces[0].id = b.id;
        modified.push_back(pieces[0]);
        for (size_t i = 1; i < pieces.size(); ++i) added.push_back(pieces[i]);
    }
    if (touched == 0) { errorText_ = "Carve: the selection doesn't touch any other brush."; return; }
    doc_.apply(doc_.makeChange(modified, std::move(added), deleted, "Carve", &cutters));
}

void Editor::hollowSelection(double t) {
    if (doc_.selection().empty()) return;
    tool_->cancel();
    std::vector<Brush> modified, added;
    for (BrushId id : doc_.selection()) {
        const Brush* b = doc_.find(id);
        if (!b) continue;
        std::string err;
        std::vector<Brush> walls = hollowBrush(*b, t, err);
        if (walls.empty()) { shapingError_ = err; return; }
        walls[0].id = id;
        modified.push_back(walls[0]);
        for (size_t i = 1; i < walls.size(); ++i) added.push_back(walls[i]);
    }
    Command c = doc_.makeChange(modified, std::move(added), {}, "Hollow");
    doc_.apply(std::move(c));
}

void Editor::drawShapingDialogs() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    if (askClipPoints_) { ImGui::OpenPopup("Clip points"); askClipPoints_ = false; }
    if (askHollow_) { ImGui::OpenPopup("Hollow"); askHollow_ = false; }
    auto buttons = [&](bool& apply, bool& cancel) {
        apply = ImGui::Button("Apply (Return)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        ImGui::SameLine();
        cancel = ImGui::Button("Cancel (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_;
    };
    bool apply = false, cancel = false;

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Clip points", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The clip plane goes through these three points:");
        ImGui::PushItemWidth(260);
        ImGui::InputScalarN("Point 1", ImGuiDataType_Double, clipBuf_[0], 3, nullptr, nullptr, "%g");
        ImGui::InputScalarN("Point 2", ImGuiDataType_Double, clipBuf_[1], 3, nullptr, nullptr, "%g");
        ImGui::InputScalarN("Point 3", ImGuiDataType_Double, clipBuf_[2], 3, nullptr, nullptr, "%g");
        ImGui::PopItemWidth();
        ImGui::TextDisabled("Then Return in the views clips (Tools -> Clip: keep chooses the side).");
        if (!shapingError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", shapingError_.c_str());
        buttons(apply, cancel);
        if (apply) {
            const Vec3 a{clipBuf_[0][0], clipBuf_[0][1], clipBuf_[0][2]};
            const Vec3 b{clipBuf_[1][0], clipBuf_[1][1], clipBuf_[1][2]};
            const Vec3 c{clipBuf_[2][0], clipBuf_[2][1], clipBuf_[2][2]};
            if (length(cross(c - a, b - a)) < 1e-9) shapingError_ = "The three points are on one line: no plane.";
            else { setTool(&clipTool_); clipTool_.setPoints(a, b, c); ImGui::CloseCurrentPopup(); }
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Hollow", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Turn the selected brushes into walls around an empty inside.");
        ImGui::SetNextItemWidth(120);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputDouble("wall thickness", &hollowThickness_, 0, 0, "%g");
        for (double t : {4.0, 8.0, 16.0, 32.0, 64.0}) {
            char b[16]; std::snprintf(b, sizeof b, "%g", t);
            if (ImGui::SmallButton(b)) hollowThickness_ = t;
            ImGui::SameLine();
        }
        ImGui::NewLine();
        if (!shapingError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", shapingError_.c_str());
        buttons(apply, cancel);
        if (apply) {
            shapingError_.clear();
            hollowSelection(hollowThickness_);
            if (shapingError_.empty()) ImGui::CloseCurrentPopup();
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ------------------------------------------------------------------ overlaps + navigation

void Editor::allViewsToOrigin() {
    for (auto& v : views_) v->resetToOrigin();
}

void Editor::focusRegion(const AABB& region) {
    const Vec3 c = region.center(), s = region.size();
    const double size = std::max({s.x, s.y, s.z, 16.0});
    for (int i = 1; i < 4; ++i) {
        Viewport& v = *views_[i];
        v.center2D(c[v.axU()], c[v.axV()]);
        const double fit = std::min(v.width(), v.height()) / (size * 4.0);
        v.setZoom2D(std::clamp(fit, 0.05, 8.0));
    }
    views_[0]->lookAt(c, size * 3.0 + 96.0);
}

void Editor::findOverlapsNow() {
    std::vector<Brush> visible;
    for (const Brush& b : doc_.brushes()) if (doc_.visible(b)) visible.push_back(b);
    overlaps_ = findOverlaps(visible);
    overlapsOpen_ = true;
    overlapIndex_ = -1;
    if (!overlaps_.empty()) focusOverlap(0);
}

void Editor::focusOverlap(int i) {
    if (overlaps_.empty()) return;
    const int n = (int)overlaps_.size();
    overlapIndex_ = ((i % n) + n) % n;
    const Overlap& o = overlaps_[(size_t)overlapIndex_];
    std::set<BrushId> sel;
    if (doc_.find(o.a)) sel.insert(o.a);
    if (doc_.find(o.b)) sel.insert(o.b);
    tool_->cancel();
    doc_.setSelection(sel);
    focusRegion(o.region);
}

void Editor::drawOverlapsWindow() {
    if (!overlapsOpen_) return;
    ImGui::SetNextWindowSize(ImVec2(380, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Overlapping brushes", &overlapsOpen_)) {
        if (ImGui::Button("Search again")) findOverlapsNow();
        ImGui::SameLine();
        if (overlaps_.empty()) {
            ImGui::TextUnformatted("No brushes cut into each other.");
        } else {
            if (ImGui::Button("< Prev")) focusOverlap(overlapIndex_ - 1);
            ImGui::SameLine();
            if (ImGui::Button("Next >")) focusOverlap(overlapIndex_ + 1);
            ImGui::SameLine();
            ImGui::Text("%d / %zu", overlapIndex_ + 1, overlaps_.size());
            ImGui::TextDisabled("Touching faces don't count. Click a row to jump there.");
            if (ImGui::BeginChild("##ov")) {
                for (size_t i = 0; i < overlaps_.size(); ++i) {
                    const Overlap& o = overlaps_[i];
                    char row[128];
                    std::snprintf(row, sizeof row, "brush %u  x  brush %u    shared %.0f units\xC2\xB3", o.a, o.b, o.volume);
                    if (ImGui::Selectable(row, (int)i == overlapIndex_)) focusOverlap((int)i);
                }
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

// ------------------------------------------------------------------ visibility

void Editor::dropInvisibleFromSelection() {
    std::set<BrushId> sel;
    for (BrushId id : doc_.selection())
        if (doc_.visible(id)) sel.insert(id);
    if (sel.size() != doc_.selection().size()) doc_.setSelection(sel);
}

void Editor::hideSelected() {
    if (doc_.selection().empty()) return;
    tool_->cancel();
    std::vector<Brush> out;
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id)) { Brush c = *b; c.hidden = true; out.push_back(std::move(c)); }
    Command c = doc_.makeModify(out, "Hide");
    c.selectionAfter.clear();
    doc_.apply(std::move(c));
}

void Editor::hideUnselected() {
    if (doc_.selection().empty()) return;
    tool_->cancel();
    std::vector<Brush> out;
    for (const Brush& b : doc_.brushes())
        if (!doc_.selection().count(b.id) && doc_.visible(b)) { Brush c = b; c.hidden = true; out.push_back(std::move(c)); }
    if (!out.empty()) doc_.apply(doc_.makeModify(out, "Hide unselected"));
}

void Editor::showAll() {
    std::vector<Brush> out;
    for (const Brush& b : doc_.brushes())
        if (b.hidden) { Brush c = b; c.hidden = false; out.push_back(std::move(c)); }
    if (!out.empty()) doc_.apply(doc_.makeModify(out, "Show all"));
    std::vector<Visgroup> vg = doc_.visgroups();
    bool changed = false;
    for (Visgroup& v : vg) if (!v.visible) { v.visible = true; changed = true; }
    if (changed) doc_.setVisgroups(vg);
}

void Editor::setVisgroupVisible(std::uint32_t id, bool visible) {
    std::vector<Visgroup> vg = doc_.visgroups();
    for (Visgroup& v : vg) if (v.id == id) v.visible = visible;
    doc_.setVisgroups(vg);
    dropInvisibleFromSelection();
}

void Editor::drawPropsPanel() {
    if (ImGui::Begin("Props###props", nullptr, ImGuiWindowFlags_NoCollapse)) {
        const auto& names = models_.store().names();
        ImGui::Text("model: %s", propTool_.model.empty() ? "(none picked)" : propTool_.model.c_str());
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##propsearch", "search by name", propSearch_, sizeof propSearch_);
        std::string needle = propSearch_;
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        const float footer = ImGui::GetFrameHeightWithSpacing() * 1.4f;
        if (ImGui::BeginChild("##proplist", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
            size_t shown = 0;
            for (const std::string& name : names) {
                std::string low = name;
                std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (!needle.empty() && low.find(needle) == std::string::npos) continue;
                ++shown;
                if (ImGui::Selectable(name.c_str(), name == propTool_.model)) {
                    propTool_.model = name;
                    if (tool_ != &propTool_) setTool(&propTool_);
                }
                if (ImGui::IsItemHovered()) {
                    const std::string why = models_.store().problem(name);
                    if (!why.empty()) ImGui::SetTooltip("%s\n%s", name.c_str(), why.c_str());
                }
            }
            if (shown == 0) {
                if (bottegaRoot_.empty()) ImGui::TextWrapped("No Bottega folder yet. File > Bottega folder... then put .glb models in its models folder.");
                else if (names.empty()) ImGui::TextWrapped("No models found in the Bottega's models folder (.glb files).");
                else ImGui::TextDisabled("No model matches.");
            }
        }
        ImGui::EndChild();
        if (ImGui::Button("Rescan")) rescanBottega();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu models", names.size());
    }
    ImGui::End();
}

void Editor::drawPropPanel() {
    auto vec3Edit = [](const char* label, Vec3& v) {
        double a[3] = {v.x, v.y, v.z};
        ImGui::SetNextItemWidth(220);
        ImGui::InputScalarN(label, ImGuiDataType_Double, a, 3, nullptr, nullptr, "%g");
        v = {a[0], a[1], a[2]};
        return ImGui::IsItemDeactivatedAfterEdit();
    };
    ImGui::PushID("props");
    const std::set<PropId>& sel = doc_.propSelection();
    if (!sel.empty()) {
        if ((sel != propBufFor_ || doc_.revision() != propBufRev_) && !ImGui::IsAnyItemActive()) {
            if (const Prop* p = doc_.findProp(*sel.begin())) propBuf_ = *p;
            propBufFor_ = sel;
            propBufRev_ = doc_.revision();
        }
        Prop& b = propBuf_;
        if (sel.size() == 1) ImGui::Text("prop: %s", b.model.c_str());
        else ImGui::Text("%zu props selected", sel.size());
        if (const Model* m = models_.find(b.model)) {
            if (m->triangles() > Model::kTriangleBudget)
                ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "heavy model: %zu triangles", (size_t)m->triangles());
        } else ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "model not found (shown as a box)");
        std::vector<std::function<void(Prop&)>> fns;
        if (sel.size() == 1) {
            if (vec3Edit("position", b.pos)) { const Vec3 v = b.pos; fns.push_back([v](Prop& p) { p.pos = v; }); }
            if (vec3Edit("rotation X Y Z", b.rot)) { const Vec3 v = b.rot; fns.push_back([v](Prop& p) { p.rot = v; }); }
            ImGui::Checkbox("scale per axis", &propPerAxis_);
            if (propPerAxis_) {
                if (vec3Edit("scale", b.scale)) { const Vec3 v = b.scale; fns.push_back([v](Prop& p) { p.scale = v; }); }
            } else {
                double k = b.scale.x;
                ImGui::SetNextItemWidth(220);
                ImGui::InputDouble("scale", &k, 0, 0, "%g");
                if (ImGui::IsItemDeactivatedAfterEdit() && k > 0) fns.push_back([k](Prop& p) { p.scale = {k, k, k}; });
            }
        } else {
            double k = 1;
            ImGui::SetNextItemWidth(220);
            ImGui::InputDouble("scale all by", &k, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit() && k > 0) fns.push_back([k](Prop& p) { p.scale = p.scale * k; });
        }
        if (!propTool_.model.empty() && ImGui::SmallButton("use the picked model")) {
            const std::string m = propTool_.model;
            fns.push_back([m](Prop& p) { p.model = m; });
        }
        if (!fns.empty()) {
            std::vector<Prop> out;
            for (PropId id : sel)
                if (const Prop* p = doc_.findProp(id)) {
                    Prop c = *p;
                    for (auto& f : fns) f(c);
                    out.push_back(c);
                }
            doc_.apply(doc_.makeModifyProps(out, "Edit prop"));
        }
        ImGui::Separator();
    }
    if (tool_ == &propTool_) {
        ImGui::TextDisabled("next prop");
        ImGui::Text("model: %s", propTool_.model.empty() ? "(pick one in the Props panel)" : propTool_.model.c_str());
        ImGui::PushItemWidth(220);
        vec3Edit("turn X Y Z", propTool_.rot);
        ImGui::InputDouble("scale##next", &propTool_.scale, 0, 0, "%g");
        if (propTool_.scale <= 0) propTool_.scale = 1;
        if (propTool_.hasPending()) {
            if (auto pp = propTool_.pending()) {
                if (!ImGui::IsAnyItemActive()) { propPend_[0] = pp->pos.x; propPend_[1] = pp->pos.y; propPend_[2] = pp->pos.z; }
                ImGui::InputScalarN("place at", ImGuiDataType_Double, propPend_, 3, nullptr, nullptr, "%g");
                if (ImGui::IsItemDeactivatedAfterEdit()) propTool_.setPending({propPend_[0], propPend_[1], propPend_[2]});
            }
            if (ImGui::Button("Place (Return)")) propTool_.create(doc_);
            ImGui::SameLine();
            if (ImGui::Button("Cancel (Esc)")) propTool_.cancel();
        } else {
            ImGui::TextDisabled("click a surface in 3D or a spot in a 2D view");
            if (ImGui::Button("Start at the origin") && !propTool_.model.empty()) propTool_.setPending({0, 0, 0});
        }
        ImGui::PopItemWidth();
    }
    ImGui::PopID();
}

void Editor::drawVisgroupsPanel() {
    if (ImGui::Begin("Visgroups###visgroups", nullptr, ImGuiWindowFlags_NoCollapse)) {
        size_t hidden = 0;
        for (const Brush& b : doc_.brushes()) hidden += b.hidden;
        if (ImGui::Button("Hide selected (H)")) hideSelected();
        ImGui::SameLine();
        if (ImGui::Button("Hide others (Shift+H)")) hideUnselected();
        if (ImGui::Button("Show all (Cmd+U)")) showAll();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu hidden", hidden);
        ImGui::Separator();
        ImGui::TextUnformatted("Visgroups");
        std::vector<Visgroup> vg = doc_.visgroups();
        int removeIdx = -1;
        bool changed = false;
        for (size_t i = 0; i < vg.size(); ++i) {
            Visgroup& v = vg[i];
            ImGui::PushID((int)v.id);
            size_t count = 0;
            for (const Brush& b : doc_.brushes()) count += b.visgroup == v.id;
            if (ImGui::Checkbox("##vis", &v.visible)) { setVisgroupVisible(v.id, v.visible); vg = doc_.visgroups(); ImGui::PopID(); break; }
            ImGui::SameLine();
            char name[64];
            std::snprintf(name, sizeof name, "%s", v.name.c_str());
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150);
            if (ImGui::InputText("##name", name, sizeof name, ImGuiInputTextFlags_EnterReturnsTrue)) { v.name = name; changed = true; }
            ImGui::SameLine();
            ImGui::TextDisabled("%zu", count);
            ImGui::SameLine();
            if (ImGui::SmallButton("Select")) {
                std::set<BrushId> sel;
                for (const Brush& b : doc_.brushes()) if (b.visgroup == v.id && doc_.visible(b)) sel.insert(b.id);
                doc_.setSelection(sel);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select its visible brushes");
            ImGui::SameLine();
            if (ImGui::SmallButton("+")) {
                std::vector<Brush> out;
                for (BrushId id : doc_.selection()) if (const Brush* b = doc_.find(id)) { Brush c = *b; c.visgroup = v.id; out.push_back(c); }
                if (!out.empty()) doc_.apply(doc_.makeModify(out, "Add to visgroup"));
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add the selection to this visgroup");
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removeIdx = (int)i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete the visgroup (its brushes stay, ungrouped)");
            ImGui::PopID();
        }
        if (removeIdx >= 0) {
            const std::uint32_t id = vg[(size_t)removeIdx].id;
            std::vector<Brush> out;
            for (const Brush& b : doc_.brushes()) if (b.visgroup == id) { Brush c = b; c.visgroup = 0; out.push_back(c); }
            if (!out.empty()) doc_.apply(doc_.makeModify(out, "Remove visgroup"));
            vg.erase(vg.begin() + removeIdx);
            changed = true;
        }
        if (changed) doc_.setVisgroups(vg);
        if (vg.empty()) ImGui::TextDisabled("none yet");
        ImGui::Separator();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 140);
        ImGui::InputTextWithHint("##newvg", "name", newVisgroupName_, sizeof newVisgroupName_);
        ImGui::SameLine();
        ImGui::BeginDisabled(doc_.selection().empty() || !newVisgroupName_[0]);
        if (ImGui::Button("New from selection")) {
            std::vector<Visgroup> all = doc_.visgroups();
            const std::uint32_t id = doc_.newVisgroupId();
            all.push_back({id, newVisgroupName_, true});
            doc_.setVisgroups(all);
            std::vector<Brush> out;
            for (BrushId bid : doc_.selection()) if (const Brush* b = doc_.find(bid)) { Brush c = *b; c.visgroup = id; out.push_back(c); }
            doc_.apply(doc_.makeModify(out, "New visgroup"));
            newVisgroupName_[0] = 0;
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
}

// ------------------------------------------------------------------ curves

GroupId Editor::selectedGroup() const {
    GroupId g = 0;
    for (BrushId id : doc_.selection()) {
        const Brush* b = doc_.find(id);
        if (!b || b->group == 0) return 0;
        if (g && b->group != g) return 0;
        g = b->group;
    }
    return g && doc_.group(g) ? g : 0;
}

void Editor::commitCurve() {
    const CurveParams p = curveTool_.params;
    CurveResult r = generateCurve(p);
    if (!r.ok()) { curveTool_.setMessage("can't build: " + r.error); return; }

    if (p.shape == CurveShape::CarveArch) {
        // Cut the opening out of the selected brushes (or everything it touches).
        std::set<BrushId> targets = doc_.selection();
        if (targets.empty()) for (const Brush& b : doc_.brushes()) targets.insert(b.id);
        std::vector<Brush> modified, added;
        std::set<BrushId> deleted;
        int touched = 0;
        for (BrushId id : targets) {
            const Brush* b = doc_.find(id);
            if (!b) continue;
            std::vector<Brush> pieces{*b};
            bool changed = false;
            for (const Brush& cut : r.brushes) {
                std::vector<Brush> next;
                for (const Brush& q : pieces) {
                    std::vector<Brush> c = carveBrush(q, cut);
                    if (c.size() != 1 || c[0].faces.size() != q.faces.size()) changed = true;
                    for (Brush& x : c) next.push_back(std::move(x));
                }
                pieces = std::move(next);
            }
            if (!changed) continue;
            ++touched;
            for (Brush& q : pieces) q.group = 0;
            if (pieces.empty()) { deleted.insert(id); continue; }
            pieces[0].id = id;
            modified.push_back(pieces[0]);
            for (size_t i = 1; i < pieces.size(); ++i) added.push_back(pieces[i]);
        }
        if (!touched) { curveTool_.setMessage("the opening doesn't cut through any selected brush"); return; }
        // Refill above the curve with arch slices, in the wall's texture.
        std::string wallTex = p.texture;
        if (const Brush* w = doc_.find(*targets.begin()); w && !w->faces.empty()) wallTex = w->faces[0].texture;
        for (Brush f : r.fill) {
            for (Face& face : f.faces) face.texture = wallTex;
            added.push_back(std::move(f));
        }
        Command c = doc_.makeChange(modified, std::move(added), deleted, "Carve arch");
        for (const BrushChange& ch : c.changes) if (ch.after) c.selectionAfter.insert(ch.id);
        doc_.apply(std::move(c));
        curveTool_.finished();
        return;
    }

    // A (re)generated group.
    const GroupId old = curveTool_.editingGroup();
    const GroupId gid = old ? old : doc_.newGroupId();
    for (Brush& b : r.brushes) b.group = gid;
    const std::set<BrushId> oldMembers = old ? doc_.groupMembers(old) : std::set<BrushId>{};
    Command c = doc_.makeChange({}, std::move(r.brushes), oldMembers, old ? "Edit curve" : "Create curve");
    std::set<BrushId> sel;
    for (const BrushChange& ch : c.changes) if (ch.after) sel.insert(ch.id);
    c.selectionAfter = sel;
    std::optional<Group> before;
    if (old) before = *doc_.group(old);
    c.groupChanges.push_back({gid, before, Group{gid, curveToKeys(p)}});
    doc_.apply(std::move(c));
    curveTool_.finished();
}

void Editor::editCurve() {
    const GroupId g = selectedGroup();
    if (!g) return;
    CurveParams p;
    if (!curveFromKeys(doc_.group(g)->keys, p)) { errorText_ = "This group's curve settings can't be read."; return; }
    // If the group was moved since it was made, follow it.
    const CurveResult fresh = generateCurve(p);
    AABB now, then;
    for (BrushId id : doc_.groupMembers(g)) now.add(doc_.find(id)->bounds());
    for (const Brush& b : fresh.brushes) then.add(b.bounds());
    if (now.valid() && then.valid() && length(now.size() - then.size()) < 1e-6) shiftCurve(p, now.min - then.min);
    setTool(&curveTool_);
    curveTool_.startEditing(p, g);
}

void Editor::bakeCurves() {
    std::set<GroupId> groups;
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id); b && b->group) groups.insert(b->group);
    if (groups.empty()) return;
    tool_->cancel();
    std::vector<Brush> modified;
    for (GroupId g : groups)
        for (BrushId id : doc_.groupMembers(g)) {
            Brush b = *doc_.find(id);
            b.group = 0;
            modified.push_back(std::move(b));
        }
    Command c = doc_.makeModify(modified, "Bake curve");
    for (GroupId g : groups) c.groupChanges.push_back({g, *doc_.group(g), std::nullopt});
    doc_.apply(std::move(c));
}

void Editor::drawBlockPanel() {
    ImGui::TextUnformatted("Block tool");
    static const char* shapes[] = {"Box", "Wedge", "Cylinder", "Cone", "Pyramid", "Sphere"};
    int shape = (int)blockTool_.shape;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##blockshape", &shape, shapes, 6)) blockTool_.shape = (Primitive)shape;
    const Primitive k = blockTool_.shape;
    if (k == Primitive::Cylinder || k == Primitive::Cone || k == Primitive::Sphere) {
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt("sides", &blockTool_.sides)) blockTool_.sides = std::clamp(blockTool_.sides, 3, 64);
        ImGui::Checkbox("round corners to whole units", &blockTool_.roundToWhole);
    }
    ImGui::TextDisabled(k == Primitive::Wedge ? "slopes down to the right in the view you draw it in"
                        : k == Primitive::Box ? "drag in a 2D view, Return creates"
                        : "stands along the view's depth (top view = upright)");
    ImGui::Text("texture: %s (scale %g)", currentTexture_.c_str(), defaultScale_);
    ImGui::Separator();
    ImGui::BeginDisabled(!blockTool_.hasPreview());
    if (ImGui::Button("Create (Return)")) onKey(KEY_ENTER);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel (Esc)")) onKey(KEY_ESCAPE);
}

void Editor::drawCurvePanel() {
    CurveParams& p = curveTool_.params;
    const bool editing = curveTool_.editingGroup() != 0;
    ImGui::TextUnformatted(editing ? "Editing a generated curve" : "Curve tool");
    static const char* shapes[] = {"Arch (doorway)", "Ring / pipe / cylinder", "Curved wall", "Spiral stairs",
                                   "Arched opening (cut)", "Dome", "Helix ramp", "Straight stairs"};
    int shape = (int)p.shape;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##shape", &shape, shapes, 8) && !editing) {
        p.shape = (CurveShape)shape;
        if (p.shape == CurveShape::SpiralStairs || p.shape == CurveShape::HelixRamp) { p.thickness = 8; }
    }
    ImGui::Separator();
    ImGui::PushItemWidth(120);
    auto intField = [&](const char* label, int& v, int lo, int hi) {
        if (ImGui::InputInt(label, &v)) v = std::clamp(v, lo, hi);
    };
    auto dblField = [&](const char* label, double& v) { ImGui::InputDouble(label, &v, 0, 0, "%g"); };
    switch (p.shape) {
        case CurveShape::Arch:
            intField("segments", p.segments, 2, 64);
            dblField("thickness", p.thickness);
            ImGui::Checkbox("square top", &p.flatTop);
            ImGui::SameLine();
            ImGui::Checkbox("legs", &p.legs);
            break;
        case CurveShape::Ring:
            intField("segments", p.segments, 1, 128);
            dblField("thickness", p.thickness);
            ImGui::TextDisabled("thickness = radius: solid cylinder");
            dblField("start degrees", p.startDeg);
            dblField("end degrees", p.endDeg);
            break;
        case CurveShape::CurvedWall:
            intField("segments", p.segments, 1, 128);
            dblField("thickness", p.thickness);
            ImGui::TextDisabled("drag points 1 (start), 2 (end), 3 (bend)");
            dblField("bend u", p.cu);
            dblField("bend v", p.cv);
            break;
        case CurveShape::SpiralStairs:
            intField("steps", p.steps, 1, 256);
            dblField("rise per step", p.rise);
            dblField("step thickness", p.thickness);
            dblField("total turn (deg)", p.turnDeg);
            dblField("inner radius", p.innerRadius);
            dblField("start degrees", p.startDeg);
            ImGui::Checkbox("clockwise", &p.clockwise);
            ImGui::SameLine();
            ImGui::Checkbox("centre column", &p.column);
            ImGui::TextDisabled("draw in the top view");
            break;
        case CurveShape::HelixRamp:
            intField("segments", p.steps, 1, 512);
            dblField("rise per segment", p.rise);
            dblField("thickness", p.thickness);
            dblField("total turn (deg)", p.turnDeg);
            dblField("inner radius", p.innerRadius);
            dblField("start degrees", p.startDeg);
            ImGui::Checkbox("clockwise", &p.clockwise);
            ImGui::TextDisabled("draw in the top view");
            break;
        case CurveShape::Dome:
            intField("segments around", p.segments, 3, 64);
            intField("rings", p.rings, 1, 32);
            dblField("shell thickness", p.thickness);
            ImGui::TextDisabled("draw in the top view");
            break;
        case CurveShape::StraightStairs:
            intField("steps", p.steps, 1, 256);
            ImGui::Checkbox("solid to the floor", &p.solid);
            if (!p.solid) dblField("step thickness", p.thickness);
            ImGui::Checkbox("rise to the left", &p.flip);
            ImGui::TextDisabled("the box = the staircase's side view");
            break;
        case CurveShape::CarveArch:
            intField("segments", p.segments, 2, 64);
            ImGui::TextDisabled("select the wall(s), draw the opening's box,");
            ImGui::TextDisabled("Return cuts it out.");
            break;
    }
    ImGui::Separator();
    static const char* ax = "XYZ";
    ImGui::Text("Box (%c %c) and depth (%c)", ax[p.axU], ax[p.axV], ax[p.axD]);
    double box[4] = {p.u0, p.v0, p.u1, p.v1};
    ImGui::SetNextItemWidth(240);
    if (ImGui::InputScalarN("##box", ImGuiDataType_Double, box, 4, nullptr, nullptr, "%g")) { p.u0 = box[0]; p.v0 = box[1]; p.u1 = box[2]; p.v1 = box[3]; }
    double depth[2] = {p.d0, p.d1};
    ImGui::SetNextItemWidth(240);
    const bool upright = p.shape == CurveShape::SpiralStairs || p.shape == CurveShape::HelixRamp || p.shape == CurveShape::Dome;
    if (ImGui::InputScalarN("##depth", ImGuiDataType_Double, depth, upright ? 1 : 2, nullptr, nullptr, "%g")) { p.d0 = depth[0]; p.d1 = depth[1]; }
    ImGui::PopItemWidth();
    ImGui::Checkbox("round corners to whole units", &p.roundToWhole);
    if (p.shape != CurveShape::CarveArch) {
        ImGui::Text("texture: %s", p.texture.c_str());
        if (editing) { ImGui::SameLine(); if (ImGui::SmallButton("use current")) { p.texture = currentTexture_; p.texScale = defaultScale_; } }
    }
    ImGui::Separator();
    const CurveResult& r = curveTool_.preview();
    if (!curveTool_.hasShape()) ImGui::TextDisabled("drag in a 2D view to place the shape");
    else if (!r.error.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "%s", r.error.c_str());
    else ImGui::Text("%zu brushes", r.brushes.size());
    ImGui::BeginDisabled(!curveTool_.hasShape() || !r.ok());
    if (ImGui::Button(p.shape == CurveShape::CarveArch ? "Cut opening (Return)" : editing ? "Update (Return)" : "Create (Return)")) commitCurve();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel (Esc)")) curveTool_.cancel();
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

void Editor::loadSkyFolders() {
    const std::string dir = settingsDir();
    if (dir.empty()) return;
    std::ifstream f(dir + "/sky_folders.txt");
    std::string line;
    while (std::getline(f, line))
        if (!line.empty()) skies_.addUserFolder(line);
}

void Editor::saveSkyFolders() const {
    if (opt_.automated()) return; // tests never touch user settings
    const std::string dir = settingsDir();
    if (dir.empty()) return;
    std::ofstream f(dir + "/sky_folders.txt");
    for (const std::string& d : skies_.userFolders()) f << d << '\n';
}

void Editor::saveTextureFolders() const {
    if (opt_.automated()) return; // tests never touch user settings
    const std::string dir = settingsDir();
    if (dir.empty()) return;
    std::ofstream f(dir + "/texture_folders.txt");
    for (const std::string& d : textures_.userFolders()) f << d << '\n';
}

// ------------------------------------------------------------------ Bottega

void Editor::loadBottegaSetting() {
    const std::string dir = settingsDir();
    if (dir.empty()) return;
    std::ifstream f(dir + "/bottega.txt");
    std::string line;
    if (std::getline(f, line) && !line.empty()) setBottegaRoot(line);
}

void Editor::saveBottegaSetting() const {
    if (opt_.automated()) return; // tests never touch user settings
    const std::string dir = settingsDir();
    if (dir.empty()) return;
    std::ofstream f(dir + "/bottega.txt");
    f << bottegaRoot_ << '\n';
}

void Editor::setBottegaRoot(const std::string& root) {
    bottegaRoot_ = root;
    bottegaCheck_ = bottega::inspect(root);
    const std::string use = bottega::usable(bottegaCheck_) ? root : std::string();
    textures_.setBottegaRoot(use);
    skies_.setBottegaRoot(use);
    models_.setBottegaRoot(use);
    if (!textures_.exists(currentTexture_) && !textures_.names().empty()) currentTexture_ = textures_.names()[0];
}

void Editor::rescanBottega() {
    // Never automatic (idle must stay free): the user asks, by menu, button or Cmd+R.
    bottegaCheck_ = bottega::inspect(bottegaRoot_);
    const std::string use = bottega::usable(bottegaCheck_) ? bottegaRoot_ : std::string();
    textures_.setBottegaRoot(use);
    textures_.rescan();
    skies_.setBottegaRoot(use);
    skies_.rescan();
    models_.setBottegaRoot(use); // forgets every loaded model; they load again when next drawn
}

void Editor::drawBottegaDialog() {
    if (askBottega_) { ImGui::OpenPopup("Bottega folder"); askBottega_ = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Bottega folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextWrapped("The Bottega is one folder shared by Scalpellino, Mazzuola and other tools: textures, skies, models, "
                       "shaders, maps and renders live in its sub-folders. Choose it once.");
    ImGui::Separator();
    ImGui::SetNextItemWidth(520);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputTextWithHint("##bottega", "path of the Bottega folder", bottegaBuf_, sizeof bottegaBuf_,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    if (useNative_) {
        if (ImGui::Button("Browse...")) { native_ = Native::Bottega; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
    }
    const bottega::Check typed = bottega::inspect(bottegaBuf_);
    ImGui::TextWrapped("%s", bottega::describe(typed).c_str());
    if (!bottegaMsg_.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", bottegaMsg_.c_str());

    const bool canMake = typed.state == bottega::State::MissingFolder || typed.state == bottega::State::Empty ||
                         typed.state == bottega::State::NotBottega || (typed.state == bottega::State::Ready && !typed.missing.empty());
    const bool canUse = typed.state == bottega::State::Ready && typed.missing.empty();
    if (canMake) {
        const char* label = typed.state == bottega::State::Ready ? "Add missing folders" : "Make this a Bottega and use it";
        if (ImGui::Button(label)) {
            if (bottega::make(bottegaBuf_, bottegaMsg_)) {
                bottegaMsg_.clear();
                setBottegaRoot(bottegaBuf_);
                saveBottegaSetting();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(!canUse);
    if (ImGui::Button("Use this folder") || (enter && canUse)) {
        bottegaMsg_.clear();
        setBottegaRoot(bottegaBuf_);
        saveBottegaSetting();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (!bottegaRoot_.empty() && ImGui::Button("Stop using the Bottega")) {
        setBottegaRoot("");
        saveBottegaSetting();
        ImGui::CloseCurrentPopup();
    }
    if (!bottegaRoot_.empty()) { ImGui::SameLine(); }
    if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_) ImGui::CloseCurrentPopup();
    ImGui::TextDisabled("Current: %s", bottegaRoot_.empty() ? "(none)" : bottegaRoot_.c_str());
    ImGui::EndPopup();
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

void Editor::wrapAroundPicked() {
    const std::vector<FaceRef> picked = textureTool_.faces(doc_);
    if (picked.size() < 2) return;
    FaceRef start = textureTool_.primary(doc_);
    if (std::find(picked.begin(), picked.end(), start) == picked.end()) start = picked[0];
    std::vector<Brush> work; // copies of touched brushes
    auto brushOf = [&](BrushId id) -> Brush& {
        for (Brush& b : work) if (b.id == id) return b;
        work.push_back(*doc_.find(id));
        return work.back();
    };
    auto polyOf = [](const Brush& b, int face) {
        for (const Polygon& p : b.polygons()) if (p.face == face) return p;
        return Polygon{};
    };
    std::vector<FaceRef> done{start};
    bool progress = true;
    while (progress) {
        progress = false;
        for (const FaceRef& f : picked) {
            if (std::find(done.begin(), done.end(), f) != done.end()) continue;
            for (const FaceRef& d : done) {
                Brush& db = brushOf(d.first);
                const Face src = db.faces[d.second];
                const Polygon sp = polyOf(db, d.second);
                Brush& fb = brushOf(f.first);
                if (wrapTexture(src, sp, fb.faces[f.second], polyOf(fb, f.second))) {
                    done.push_back(f);
                    progress = true;
                    break;
                }
            }
        }
    }
    if (done.size() < 2) { errorText_ = "Wrap around: none of the picked faces touch the first one."; return; }
    doc_.apply(doc_.makeModify(work, "Wrap texture"));
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
                    const bool inBottega = !bottegaRoot_.empty() && name.rfind("textures/", 0) == 0;
                    ImGui::SetTooltip("%s\n%d x %d\n%s", name.c_str(), t.width, t.height, inBottega ? "Bottega" : user ? "your folder" : "built-in");
                }
                std::string label = name.substr(name.find_last_of('/') == std::string::npos ? 0 : name.find_last_of('/') + 1);
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
        if (ImGui::Button("Rescan")) rescanBottega();
        ImGui::SameLine();
        if (bottegaRoot_.empty()) ImGui::TextDisabled("%zu textures, no Bottega (File menu)", textures_.names().size());
        else ImGui::TextDisabled("%zu textures", textures_.names().size());
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

// ------------------------------------------------------------------ terrain (milestone 7)

bool Editor::selectionHasTerrain() const {
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id)) if (b->terrain) return true;
    return false;
}

void Editor::applyTerrainEdit(std::vector<Brush> edited, const char* name) {
    // Stitch with every other terrain, then keep only brushes that actually changed.
    std::vector<Brush> all = edited;
    for (const Brush& b : doc_.brushes()) {
        if (!b.terrain) continue;
        bool mine = false;
        for (const Brush& e : edited) if (e.id == b.id) mine = true;
        if (!mine) all.push_back(b);
    }
    stitchTerrain(all);
    std::vector<Brush> out;
    for (const Brush& b : all) {
        const Brush* old = doc_.find(b.id);
        if (!old) continue;
        const bool had = old->terrain.has_value(), has = b.terrain.has_value();
        if (had != has || (has && *old->terrain != *b.terrain))
            out.push_back(b);
    }
    if (!out.empty()) doc_.apply(doc_.makeModify(out, name));
}

void Editor::startMakeTerrain() {
    terrainError_.clear();
    // Faces already picked with the Texture tool: straight to the grid size dialog.
    if (tool_ == &textureTool_ && !textureTool_.faces(doc_).empty()) { askMakeTerrain_ = true; return; }
    setTool(&terrainTool_);
    terrainTool_.startPicking();
}

void Editor::makeTerrainNow(int power) {
    terrainError_.clear();
    std::vector<std::pair<BrushId, int>> targets; // brush, face
    auto addFaces = [&](const std::vector<FaceRef>& faces) {
        for (const FaceRef& f : faces) {
            for (const auto& t : targets)
                if (t.first == f.first) { terrainError_ = "pick only one face per brush (a brush has one terrain face)"; return false; }
            targets.push_back(f);
        }
        return true;
    };
    const bool picking = tool_ == &terrainTool_ && terrainTool_.picking();
    if (picking) {
        if (!addFaces(terrainTool_.pickedFaces(doc_))) return;
    } else if (tool_ == &textureTool_ && !textureTool_.faces(doc_).empty()) {
        if (!addFaces(textureTool_.faces(doc_))) return;
    } else {
        // No faces picked: change the grid size of the selected terrain.
        for (BrushId id : doc_.selection())
            if (const Brush* b = doc_.find(id))
                if (b->terrain && terrainValid(*b)) targets.push_back({id, b->terrain->face});
    }
    if (targets.empty()) {
        terrainError_ = picking ? "click the faces to turn into terrain first"
                                : "no faces picked: use Terrain > Make terrain and click faces (or select terrain to change its grid size)";
        return;
    }
    std::vector<Brush> edited;
    int failed = 0;
    std::string why;
    for (const auto& [id, face] : targets) {
        Brush b = *doc_.find(id);
        const std::string err = makeTerrain(b, face, power);
        if (!err.empty()) { ++failed; why = err; continue; }
        edited.push_back(std::move(b));
    }
    if (edited.empty()) { terrainError_ = why; return; }
    std::set<BrushId> made;
    for (const Brush& b : edited) made.insert(b.id);
    applyTerrainEdit(std::move(edited), "Make terrain");
    doc_.setSelection(made);
    if (picking) terrainTool_.stopPicking();
    if (failed) terrainError_ = std::to_string(failed) + " face(s) skipped: " + why;
}

void Editor::removeTerrain() {
    std::vector<Brush> out;
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id))
            if (b->terrain) { Brush c = *b; c.terrain.reset(); out.push_back(std::move(c)); }
    if (!out.empty()) doc_.apply(doc_.makeModify(out, "Remove terrain"));
}

void Editor::stitchAllTerrain() { applyTerrainEdit({}, "Stitch terrain"); }

bool Editor::importHeightmap(const std::string& path, double low, double high) {
    terrainError_.clear();
    std::vector<Brush> sel;
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id)) if (b->terrain && terrainValid(*b)) sel.push_back(*b);
    if (sel.empty()) { terrainError_ = "select terrain brushes first"; return false; }
    if (!FileExists(path.c_str())) { terrainError_ = "file not found"; return false; }
    Image img = LoadImage(path.c_str());
    if (!img.data) { terrainError_ = "could not read that image (PNG, JPG, BMP or TGA)"; return false; }
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
    std::vector<float> grey((size_t)img.width * img.height);
    const unsigned char* px = (const unsigned char*)img.data;
    for (size_t i = 0; i < grey.size(); ++i) grey[i] = px[i] / 255.0f;
    const int w = img.width, h = img.height;
    UnloadImage(img);
    if (!applyHeightmap(sel, grey, w, h, low, high)) { terrainError_ = "could not lay the image over the terrain"; return false; }
    applyTerrainEdit(std::move(sel), "Import heightmap");
    return true;
}

void Editor::drawTerrainPanel() {
    TerrainTool& t = terrainTool_;
    if (t.picking()) {
        ImGui::TextUnformatted("Make terrain");
        ImGui::TextWrapped("Click faces in the 3D view (Shift+click adds or removes), or click brushes in a 2D view to pick "
                           "their face toward you. Any face with 3 or 4 corners; one per brush.");
        const size_t n = t.pickedFaces(doc_).size();
        ImGui::Text("%zu face(s) picked", n);
        ImGui::TextUnformatted("Cells per side:");
        for (int pw = kMinTerrainPower; pw <= kMaxTerrainPower; ++pw) {
            char b[8]; std::snprintf(b, sizeof b, "%d", 1 << pw);
            ImGui::SameLine();
            if (ImGui::RadioButton(b, terrainPower_ == pw)) terrainPower_ = pw;
        }
        ImGui::TextDisabled("a face that is already terrain gets the new size");
        ImGui::BeginDisabled(n == 0);
        if (ImGui::Button("Make terrain (Return)")) makeTerrainNow(terrainPower_);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Stop (Esc)")) t.stopPicking();
        if (!terrainError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", terrainError_.c_str());
        return;
    }
    ImGui::TextUnformatted("Terrain tool");
    static const char* modes[] = {"Raise", "Lower", "Smooth", "Flatten", "Noise", "Paint texture", "Erase paint"};
    int mode = (int)t.mode;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##terrainmode", &mode, modes, 7)) t.mode = (SculptMode)mode;
    ImGui::PushItemWidth(120);
    if (ImGui::InputDouble("radius", &t.radius, 0, 0, "%g")) t.radius = std::clamp(t.radius, 1.0, 8192.0);
    const bool usesAmount = t.mode == SculptMode::Raise || t.mode == SculptMode::Lower || t.mode == SculptMode::Noise;
    if (usesAmount) {
        if (ImGui::InputDouble("amount per dab", &t.amount, 0, 0, "%g")) t.amount = std::clamp(t.amount, 0.0, 4096.0);
    } else {
        if (ImGui::InputDouble("strength %", &t.strength, 0, 0, "%g")) t.strength = std::clamp(t.strength, 0.0, 100.0);
    }
    static const char* falloffs[] = {"soft", "linear", "hard"};
    int f = (int)t.falloff;
    if (ImGui::Combo("falloff", &f, falloffs, 3)) t.falloff = (Falloff)f;
    ImGui::PopItemWidth();
    for (double r : {32.0, 64.0, 128.0, 256.0, 512.0}) {
        char b[16]; std::snprintf(b, sizeof b, "%g", r);
        if (ImGui::SmallButton(b)) t.radius = r;
        ImGui::SameLine();
    }
    ImGui::TextDisabled("radius");
    if (t.mode == SculptMode::Paint) ImGui::Text("paints: %s", currentTexture_.c_str());
    ImGui::TextDisabled(t.mode == SculptMode::Flatten ? "levels to the height where a stroke starts" : "one drag = one undo step");
    // Painted textures of the selected terrain (bottom first).
    for (BrushId id : doc_.selection()) {
        const Brush* b = doc_.find(id);
        if (!b || !b->terrain || !terrainValid(*b)) continue;
        std::string list = b->faces[b->terrain->face].texture + " (face)";
        for (const TerrainLayer& l : b->terrain->layers) list += ", " + l.texture;
        ImGui::TextWrapped("Textures on the selected patch: %s", list.c_str());
        ImGui::TextDisabled("up to %d painted ones; paint the face's texture to uncover it", kMaxTerrainLayers);
        break;
    }
    if (!terrainError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", terrainError_.c_str());
    ImGui::Separator();
    if (ImGui::Button("Make terrain...")) startMakeTerrain();
    ImGui::SameLine();
    ImGui::BeginDisabled(!selectionHasTerrain());
    if (ImGui::Button("Grid size...")) {
        terrainError_.clear();
        for (BrushId id : doc_.selection())
            if (const Brush* b = doc_.find(id)) if (b->terrain) terrainPower_ = b->terrain->power;
        askMakeTerrain_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Heightmap...")) { terrainError_.clear(); askHeightmap_ = true; }
    ImGui::EndDisabled();
}

// ------------------------------------------------------------------ world (milestone 8)

void Editor::combinePrims() {
    if (doc_.primSelection().size() < 2) { worldError_ = "select two or more primitives to combine"; return; }
    const std::uint32_t id = newComboId(doc_.prims());
    std::vector<MathPrim> out;
    for (MathPrimId pid : doc_.primSelection())
        if (const MathPrim* p = doc_.findPrim(pid)) {
            MathPrim c = *p;
            c.combo = id;
            out.push_back(c);
        }
    tool_->cancel();
    doc_.apply(doc_.makeModifyPrims(out, "Combine"));
}

// ------------------------------------------------------------------ v0.9.2: land, replicate

namespace {
// The selected primitives as objects: a combination is one object.
std::vector<std::vector<const MathPrim*>> selectedObjects(const Document& doc) {
    std::vector<std::vector<const MathPrim*>> out;
    std::set<std::uint32_t> combos;
    for (MathPrimId id : doc.primSelection()) {
        const MathPrim* p = doc.findPrim(id);
        if (!p) continue;
        if (!p->combo) { out.push_back({p}); continue; }
        if (combos.insert(p->combo).second) out.push_back(comboMembers(doc.prims(), p->combo));
    }
    return out;
}
} // namespace

void Editor::snapToLand() {
    auto objects = selectedObjects(doc_);
    if (objects.empty() && doc_.propSelection().empty()) { landMessage_ = "select primitives or props to snap to land"; return; }
    tool_->cancel();
    // Lowest first, so an object stacked on another selected one lands on its new spot.
    std::sort(objects.begin(), objects.end(), [](const auto& a, const auto& b) { return objectBottom(a) < objectBottom(b); });
    std::vector<const Brush*> brushes;
    for (const Brush& b : doc_.brushes())
        if (doc_.visible(b)) brushes.push_back(&b);
    std::vector<MathPrim> work = doc_.prims();
    std::vector<MathPrim> out;
    int nothing = 0;
    for (const auto& obj : objects) {
        std::set<MathPrimId> ids;
        for (const MathPrim* p : obj) ids.insert(p->id);
        std::vector<const MathPrim*> me, others;
        for (const MathPrim& p : work) (ids.count(p.id) ? me : others).push_back(&p);
        const auto mv = landingMove(me, brushes, others);
        if (!mv) { ++nothing; continue; }
        for (MathPrim& p : work)
            if (ids.count(p.id)) {
                p.pos.z = cleanNumber(p.pos.z + *mv, 1e-6);
                out.push_back(p);
            }
    }
    // Props land after the primitives, each on what is below it (including the props already landed).
    std::vector<Prop> propsWork = doc_.props();
    std::vector<Prop> propsOut;
    {
        std::vector<PropId> ids(doc_.propSelection().begin(), doc_.propSelection().end());
        auto bottomOf = [&](PropId id) {
            for (const Prop& q : propsWork) if (q.id == id) return propBottom(q, models_.find(q.model));
            return 0.0;
        };
        std::sort(ids.begin(), ids.end(), [&](PropId a, PropId b) { return bottomOf(a) < bottomOf(b); });
        for (PropId id : ids) {
            Prop* me = nullptr;
            std::vector<PropRef> others;
            for (Prop& q : propsWork) {
                if (q.id == id) me = &q;
                else others.push_back({&q, models_.find(q.model)});
            }
            if (!me) continue;
            std::vector<const MathPrim*> prims;
            for (const MathPrim& m : work) prims.push_back(&m);
            const auto mv = propLandingMove({me, models_.find(me->model)}, brushes, prims, others);
            if (!mv) { ++nothing; continue; }
            me->pos.z = cleanNumber(me->pos.z + *mv, 1e-6);
            propsOut.push_back(*me);
        }
    }
    landMessage_ = nothing ? std::to_string(nothing) + " object(s) had nothing below them" : "";
    if (out.empty() && propsOut.empty()) return;
    Command c = out.empty() ? doc_.makeModifyProps(propsOut, "Snap to land") : doc_.makeModifyPrims(out, "Snap to land");
    if (!out.empty() && !propsOut.empty()) {
        Command pc = doc_.makeModifyProps(propsOut, "Snap to land");
        c.propChanges = std::move(pc.propChanges);
    }
    doc_.apply(std::move(c));
}

void Editor::snapToGround(double height) {
    std::vector<MathPrim> out;
    for (const auto& obj : selectedObjects(doc_)) {
        const double dz = height - objectBottom(obj);
        for (const MathPrim* p : obj) {
            MathPrim c = *p;
            c.pos.z = cleanNumber(c.pos.z + dz, 1e-6);
            out.push_back(c);
        }
    }
    std::vector<Prop> propsOut;
    for (PropId id : doc_.propSelection())
        if (const Prop* q = doc_.findProp(id)) {
            Prop c = *q;
            c.pos.z = cleanNumber(c.pos.z + height - propBottom(*q, models_.find(q->model)), 1e-6);
            propsOut.push_back(c);
        }
    if (out.empty() && propsOut.empty()) return;
    tool_->cancel();
    Command c = out.empty() ? doc_.makeModifyProps(propsOut, "Snap to ground") : doc_.makeModifyPrims(out, "Snap to ground");
    if (!out.empty() && !propsOut.empty()) c.propChanges = doc_.makeModifyProps(propsOut, "").propChanges;
    doc_.apply(std::move(c));
}

void Editor::openReplicate() {
    if (doc_.selection().empty() && doc_.primSelection().empty() && doc_.lightSelection().empty() && doc_.propSelection().empty()) return;
    tool_->cancel();
    if (repSpec_.offset == Vec3{100, 0, 0} && grid_.size > 0) repSpec_.offset = {grid_.size * 4, 0, 0};
    replicating_ = true;
    replicateFocus_ = true;
}

bool Editor::applyReplicate(const ReplicateSpec& spec) {
    // Groups of the selected brushes come along whole.
    std::set<BrushId> sel = doc_.selection();
    std::set<std::uint32_t> groups;
    for (BrushId id : sel) if (const Brush* b = doc_.find(id)) if (b->group) groups.insert(b->group);
    for (const Brush& b : doc_.brushes()) if (b.group && groups.count(b.group)) sel.insert(b.id);
    std::vector<Brush> bs;
    for (BrushId id : sel) if (const Brush* b = doc_.find(id)) bs.push_back(*b);
    std::vector<MathPrim> ps;
    for (MathPrimId id : doc_.withCombos(doc_.primSelection())) if (const MathPrim* p = doc_.findPrim(id)) ps.push_back(*p);
    std::vector<Light> ls;
    for (LightId id : doc_.lightSelection()) if (const Light* l = doc_.findLight(id)) ls.push_back(*l);
    std::vector<Prop> qs;
    for (PropId id : doc_.propSelection()) if (const Prop* q = doc_.findProp(id)) qs.push_back(*q);
    const auto copies = replicate(bs, ps, ls, spec, qs, modelFinder_);
    if (copies.empty()) return false;
    // One command: every copy, each with its own groups and combinations.
    Command c;
    c.name = "Multi-Replicate";
    c.selectionBefore = doc_.selection();
    c.lightSelectionBefore = doc_.lightSelection();
    c.primSelectionBefore = doc_.primSelection();
    c.lightSelectionAfter = std::set<LightId>{};
    c.primSelectionAfter = std::set<MathPrimId>{};
    c.propSelectionBefore = doc_.propSelection();
    c.propSelectionAfter = std::set<PropId>{};
    std::uint32_t nextCombo = newComboId(doc_.prims());
    for (const ReplicateCopy& cp : copies) {
        std::map<std::uint32_t, std::uint32_t> newGroup, newCombo;
        for (Brush b : cp.brushes) {
            b.id = doc_.newId();
            if (b.group) {
                auto it = newGroup.find(b.group);
                if (it == newGroup.end()) {
                    const GroupId g = doc_.newGroupId();
                    Group grp;
                    if (auto gi = doc_.groups().find(b.group); gi != doc_.groups().end()) grp = gi->second;
                    grp.id = g;
                    c.groupChanges.push_back({g, std::nullopt, grp});
                    it = newGroup.emplace(b.group, g).first;
                }
                b.group = it->second;
            }
            c.selectionAfter.insert(b.id);
            c.changes.push_back({b.id, std::nullopt, std::move(b)});
        }
        for (MathPrim p : cp.prims) {
            p.id = doc_.newPrimId();
            if (p.combo) {
                auto it = newCombo.find(p.combo);
                if (it == newCombo.end()) it = newCombo.emplace(p.combo, nextCombo++).first;
                p.combo = it->second;
            }
            c.primSelectionAfter->insert(p.id);
            c.primChanges.push_back({p.id, std::nullopt, std::move(p)});
        }
        for (Prop q : cp.props) {
            q.id = doc_.newPropId();
            c.propSelectionAfter->insert(q.id);
            c.propChanges.push_back({q.id, std::nullopt, std::move(q)});
        }
        for (Light l : cp.lights) {
            l.id = doc_.newLightId();
            c.lightSelectionAfter->insert(l.id);
            c.lightChanges.push_back({l.id, std::nullopt, std::move(l)});
        }
    }
    doc_.apply(std::move(c));
    return true;
}

void Editor::drawReplicateDialog() {
    if (askGround_) { ImGui::OpenPopup("Snap to ground"); askGround_ = false; }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Snap to ground", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Put the selected primitives' lowest point at this height");
        ImGui::SetNextItemWidth(150);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputDouble("Z (units)", &groundHeight_, 0, 0, "%g");
        const bool apply = ImGui::Button("Apply (Return)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_;
        if (apply) { snapToGround(groundHeight_); ImGui::CloseCurrentPopup(); }
        else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // Multi-Replicate: a window, not a dialog, so the views stay free to look around
    // (pan, zoom, orbit, fly) before the copies are made.
    if (!replicating_ || scriptPreview_) return;
    ImGui::SetNextWindowPos(ImVec2(center.x, center.y * 0.4f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
    if (replicateFocus_) { ImGui::SetNextWindowFocus(); }
    bool open = true;
    if (ImGui::Begin("Multi-Replicate", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ReplicateSpec& r = repSpec_;
        ImGui::TextUnformatted("Copies of the selection, each one step further than the last");
        ImGui::TextDisabled("look around in the views freely; Return creates, Esc cancels");
        ImGui::SetNextItemWidth(120);
        if (replicateFocus_) ImGui::SetKeyboardFocusHere();
        ImGui::InputInt("copies", &r.copies);
        r.copies = std::clamp(r.copies, 1, 1000);
        auto vec = [](const char* label, Vec3& v) {
            double a[3] = {v.x, v.y, v.z};
            ImGui::SetNextItemWidth(240);
            ImGui::InputScalarN(label, ImGuiDataType_Double, a, 3, nullptr, nullptr, "%g");
            v = {a[0], a[1], a[2]};
        };
        vec("offset per copy (units)", r.offset);
        vec("turn per copy (degrees X Y Z)", r.rotate);
        vec("scale per copy (%)", r.scale);
        for (int a = 0; a < 3; ++a) r.scale[a] = std::clamp(r.scale[a], 1.0, 1000.0);
        ImGui::Checkbox("scale the offset too", &r.scaleOffset);
        ImGui::TextUnformatted("turn and scale about:");
        ImGui::RadioButton("the selection (moves with the copies)", &repPivot_, 0);
        ImGui::RadioButton("the world origin", &repPivot_, 1);
        ImGui::RadioButton("a point", &repPivot_, 2);
        if (repPivot_ == 2) vec("point", r.point);
        r.pivot = (ReplicateSpec::Pivot)repPivot_;
        ImGui::TextDisabled("brushes only move (they stay on the grid); primitives and");
        ImGui::TextDisabled("lights also turn; primitives also scale");
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const bool apply = ImGui::Button("Create (Return)") ||
                           (focused && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)));
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel (Esc)") || (focused && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) || closeDialogs_;
        if (apply) { applyReplicate(r); replicating_ = false; }
        else if (cancel) replicating_ = false;
    }
    ImGui::End();
    replicateFocus_ = false;
    if (!open) replicating_ = false;
}

void Editor::setPrimOp(PrimOp op) {
    std::vector<MathPrim> out;
    for (MathPrimId pid : doc_.primSelection())
        if (const MathPrim* p = doc_.findPrim(pid))
            if (p->combo && p->op != op) {
                MathPrim c = *p;
                c.op = op;
                out.push_back(c);
            }
    if (!out.empty()) doc_.apply(doc_.makeModifyPrims(out, "Combine mode"));
}

void Editor::drawPrimPanel() {
    static const char* kindNames[] = {"sphere", "cube", "cylinder", "cone", "torus", "capsule", "disc"};
    auto texturePicker = [&](std::string& tex, bool& changed) {
        ImGui::Text("texture: %s", tex.empty() ? "(flat colour)" : tex.c_str());
        if (ImGui::SmallButton("use current texture")) { tex = currentTexture_; changed = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("flat colour")) { tex.clear(); changed = true; }
    };
    auto colorEdit = [](Vec3& c) {
        float f[3] = {(float)(c.x / 255), (float)(c.y / 255), (float)(c.z / 255)};
        if (ImGui::ColorEdit3("colour", f, ImGuiColorEditFlags_Uint8)) c = {std::round(f[0] * 255), std::round(f[1] * 255), std::round(f[2] * 255)};
        return ImGui::IsItemDeactivatedAfterEdit();
    };
    auto vec3Edit = [](const char* label, Vec3& v) {
        double a[3] = {v.x, v.y, v.z};
        ImGui::SetNextItemWidth(220);
        ImGui::InputScalarN(label, ImGuiDataType_Double, a, 3, nullptr, nullptr, "%g");
        v = {a[0], a[1], a[2]};
        return ImGui::IsItemDeactivatedAfterEdit();
    };
    const std::set<MathPrimId>& sel = doc_.primSelection();
    if (!sel.empty()) {
        if ((sel != primBufFor_ || doc_.revision() != primBufRev_) && !ImGui::IsAnyItemActive()) {
            if (const MathPrim* p = doc_.findPrim(*sel.begin())) primBuf_ = *p;
            primBufFor_ = sel;
            primBufRev_ = doc_.revision();
        }
        MathPrim& b = primBuf_;
        const bool one = sel.size() == 1;
        // Own ID scope: the "new primitive" fields below use the same labels.
        ImGui::PushID("selected primitives");
        std::set<std::uint32_t> combos;
        for (MathPrimId id : sel) if (const MathPrim* p = doc_.findPrim(id)) combos.insert(p->combo);
        if (one) ImGui::Text("%s", kindNames[(int)b.kind]);
        else ImGui::Text("%zu primitives selected", sel.size());
        // What changed is applied to every selected primitive; place, turn and size only
        // when a single one is selected.
        std::vector<std::function<void(MathPrim&)>> fns;
        const char* what = "Edit primitive";
        ImGui::PushItemWidth(150);
        if (one) {
            if (vec3Edit("position", b.pos)) { const Vec3 v = b.pos; fns.push_back([v](MathPrim& p) { p.pos = v; }); }
            if (vec3Edit("rotation X Y Z", b.rot)) { const Vec3 v = b.rot; fns.push_back([v](MathPrim& p) { p.rot = v; }); }
            if (vec3Edit("size", b.size)) {
                Vec3 v = b.size;
                for (int a = 0; a < 3; ++a) v[a] = std::max(b.kind == PrimKind::Disc && a == 2 ? 0.0 : 1.0, std::fabs(v[a]));
                fns.push_back([v](MathPrim& p) { p.size = v; });
            }
        }
        if (b.kind == PrimKind::Cube) {
            ImGui::InputDouble("rounded corners", &b.round, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit()) { const double v = std::max(0.0, b.round); fns.push_back([v](MathPrim& p) { if (p.kind == PrimKind::Cube) p.round = v; }); }
        }
        if (b.kind == PrimKind::Torus) {
            ImGui::InputDouble("tube thickness (0.05-0.5)", &b.thickness, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit()) { const double v = std::clamp(b.thickness, 0.05, 0.5); fns.push_back([v](MathPrim& p) { if (p.kind == PrimKind::Torus) p.thickness = v; }); }
        }
        bool texChanged = false;
        texturePicker(b.texture, texChanged);
        if (texChanged) { const std::string t = b.texture; fns.push_back([t](MathPrim& p) { p.texture = t; }); }
        if (colorEdit(b.color)) { const Vec3 c = b.color; fns.push_back([c](MathPrim& p) { p.color = c; }); }
        ImGui::InputDouble("texture repeats", &b.texScale, 0, 0, "%g");
        if (ImGui::IsItemDeactivatedAfterEdit()) { const double v = std::clamp(b.texScale, 0.01, 1000.0); fns.push_back([v](MathPrim& p) { p.texScale = v; }); }
        ImGui::PopItemWidth();
        if (!fns.empty()) {
            std::vector<MathPrim> out;
            for (MathPrimId id : sel)
                if (const MathPrim* p = doc_.findPrim(id)) {
                    MathPrim c = *p;
                    for (auto& f : fns) f(c);
                    if (c != *p) out.push_back(c);
                }
            if (!out.empty()) doc_.apply(doc_.makeModifyPrims(out, what));
        }
        ImGui::Separator();
        // Combining.
        const bool inCombo = !combos.count(0);
        if (inCombo && combos.size() == 1) {
            const auto members = comboMembers(doc_.prims(), *combos.begin());
            ImGui::Text("combination of %zu", members.size());
            ImGui::SetNextItemWidth(150);
            ImGui::InputDouble("blend (units, 0 = sharp)", &b.blend, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const double v = std::max(0.0, b.blend);
                std::vector<MathPrim> out;
                for (MathPrimId id : sel)
                    if (const MathPrim* p = doc_.findPrim(id)) {
                        MathPrim c = *p;
                        c.blend = v;
                        if (c != *p) out.push_back(c);
                    }
                if (!out.empty()) doc_.apply(doc_.makeModifyPrims(out, "Blend"));
            }
            ImGui::TextDisabled("how softly the selected pieces melt into the others");
            int op = (int)b.op;
            if (ImGui::RadioButton("add", one && op == 0)) setPrimOp(PrimOp::Add);
            ImGui::SameLine();
            if (ImGui::RadioButton("subtract", one && op == 1)) setPrimOp(PrimOp::Subtract);
            ImGui::SameLine();
            if (ImGui::RadioButton("intersect", one && op == 2)) setPrimOp(PrimOp::Intersect);
            ImGui::TextDisabled("pick one piece:");
            for (const MathPrim* m : members) {
                char label[64];
                std::snprintf(label, sizeof label, "%s (%s)##%u", kindNames[(int)m->kind], primOpName(m->op), m->id);
                if (ImGui::Selectable(label, one && sel.count(m->id))) doc_.setPrimSelection({m->id});
            }
            if (ImGui::SmallButton("whole combination")) doc_.setPrimSelection(doc_.withCombos(sel));
        }
        ImGui::BeginDisabled(sel.size() < 2);
        if (ImGui::Button("Combine")) combinePrims();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!inCombo && combos.size() == 1);
        if (ImGui::Button("Separate")) separatePrims();
        ImGui::EndDisabled();
        if (!worldError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", worldError_.c_str());
        if (!landMessage_.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", landMessage_.c_str());
        if (ImGui::SmallButton("Snap to land (Cmd+J)")) snapToLand();
        ImGui::TextDisabled("Delete removes, Cmd+D duplicates");
        ImGui::PopID();
        if (tool_ != &primTool_) return;
        ImGui::Separator();
    }
    PrimTool& t = primTool_;
    ImGui::PushID("new primitive");
    ImGui::TextUnformatted("New primitive");
    int kind = (int)t.templ.kind;
    for (int i = 0; i < kPrimKinds; ++i) {
        if (i % 4) ImGui::SameLine();
        ImGui::RadioButton(kindNames[i], &kind, i);
    }
    t.templ.kind = (PrimKind)kind;
    ImGui::PushItemWidth(150);
    if (t.templ.kind == PrimKind::Cube) ImGui::InputDouble("rounded corners", &t.templ.round, 0, 0, "%g");
    if (t.templ.kind == PrimKind::Torus) {
        ImGui::InputDouble("tube thickness (0.05-0.5)", &t.templ.thickness, 0, 0, "%g");
        t.templ.thickness = std::clamp(t.templ.thickness, 0.05, 0.5);
    }
    ImGui::Checkbox("flat colour (else the current texture)", &t.flat);
    colorEdit(t.templ.color);
    ImGui::InputDouble("texture repeats", &t.templ.texScale, 0, 0, "%g");
    ImGui::PopItemWidth();
    if (auto p = t.pending()) {
        ImGui::TextDisabled("box (centre, size):");
        double c[3], s[3];
        // the box as drawn (before a disc is turned to face its view)
        if (!ImGui::IsAnyItemActive()) {
            const Vec3 cc = p->pos;
            const Vec3 ss = primBounds(*p).size();
            primPend_[0] = cc.x; primPend_[1] = cc.y; primPend_[2] = cc.z;
            primPend_[3] = ss.x; primPend_[4] = ss.y; primPend_[5] = ss.z;
        }
        for (int i = 0; i < 3; ++i) { c[i] = primPend_[i]; s[i] = primPend_[3 + i]; }
        ImGui::SetNextItemWidth(220);
        ImGui::InputScalarN("centre", ImGuiDataType_Double, c, 3, nullptr, nullptr, "%g");
        const bool e1 = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetNextItemWidth(220);
        ImGui::InputScalarN("box size", ImGuiDataType_Double, s, 3, nullptr, nullptr, "%g");
        const bool e2 = ImGui::IsItemDeactivatedAfterEdit();
        for (int i = 0; i < 3; ++i) { primPend_[i] = c[i]; primPend_[3 + i] = std::fabs(s[i]); }
        if (e1 || e2) {
            int ax = 2;
            if (p->kind == PrimKind::Disc) ax = std::fabs(p->rot.y) > 1 ? 0 : std::fabs(p->rot.x) > 1 ? 1 : 2;
            t.setPending({c[0], c[1], c[2]}, {primPend_[3], primPend_[4], primPend_[5]}, ax);
        }
        if (ImGui::Button("Create (Return)")) t.create(doc_, currentTexture_, &lastBox_);
    } else {
        ImGui::TextDisabled("drag a box in a 2D view");
    }
    ImGui::PopID();
}

void Editor::separatePrims() {
    std::vector<MathPrim> out;
    for (MathPrimId pid : doc_.primSelection())
        if (const MathPrim* p = doc_.findPrim(pid))
            if (p->combo) {
                MathPrim c = *p;
                c.combo = 0;
                c.op = PrimOp::Add;
                out.push_back(c);
            }
    if (out.empty()) return;
    tool_->cancel();
    doc_.apply(doc_.makeModifyPrims(out, "Separate"));
}

template <class Fn> void Editor::editLights(const char* name, Fn fn) {
    std::vector<Light> out;
    for (LightId id : doc_.lightSelection())
        if (const Light* l = doc_.findLight(id)) {
            Light c = *l;
            fn(c);
            if (c != *l) out.push_back(std::move(c));
        }
    if (!out.empty()) doc_.apply(doc_.makeModifyLights(out, name));
}

namespace {
// The fields shared by "new light" and "selected lights". Returns a bit per edited field
// (only when an edit is finished, so one change = one undo step).
enum LightField : unsigned {
    LF_Color = 1, LF_Bright = 2, LF_Range = 4, LF_Pitch = 8, LF_Yaw = 16, LF_Inner = 32, LF_Outer = 64, LF_Pos = 128
};
unsigned lightFields(Light& l, bool withPos) {
    unsigned done = 0;
    float col[3] = {(float)(l.color.x / 255), (float)(l.color.y / 255), (float)(l.color.z / 255)};
    ImGui::PushItemWidth(150);
    if (ImGui::ColorEdit3("colour", col, ImGuiColorEditFlags_Uint8)) l.color = {std::round(col[0] * 255), std::round(col[1] * 255), std::round(col[2] * 255)};
    if (ImGui::IsItemDeactivatedAfterEdit()) done |= LF_Color;
    auto num = [&](const char* label, double& v, unsigned bit, double lo, double hi) {
        ImGui::InputDouble(label, &v, 0, 0, "%g");
        if (ImGui::IsItemDeactivatedAfterEdit()) { v = std::clamp(v, lo, hi); done |= bit; }
    };
    num("brightness", l.brightness, LF_Bright, 0, 100000);
    if (l.kind != LightKind::Sun) num("reach (units)", l.range, LF_Range, 1, 1 << 20);
    if (l.hasDirection()) {
        num("pitch (-90 = down)", l.pitch, LF_Pitch, -90, 90);
        num("yaw", l.yaw, LF_Yaw, -360, 360);
    }
    if (l.kind == LightKind::Spot) {
        num("inner angle", l.inner, LF_Inner, 0, 89);
        num("outer angle", l.outer, LF_Outer, 1, 89);
    }
    if (withPos) {
        double p[3] = {l.pos.x, l.pos.y, l.pos.z};
        ImGui::SetNextItemWidth(220);
        ImGui::InputScalarN("position", ImGuiDataType_Double, p, 3, nullptr, nullptr, "%g");
        if (ImGui::IsItemDeactivatedAfterEdit()) { l.pos = {p[0], p[1], p[2]}; done |= LF_Pos; }
        else l.pos = {p[0], p[1], p[2]};
    }
    ImGui::PopItemWidth();
    return done;
}
} // namespace

void Editor::drawLightPanel() {
    const std::set<LightId>& sel = doc_.lightSelection();
    if (!sel.empty()) {
        // Fields show the first selected light; an edit goes to every selected light.
        if (sel != lightBufFor_ || doc_.revision() != lightBufRev_) {
            if (!ImGui::IsAnyItemActive()) {
                if (const Light* l = doc_.findLight(*sel.begin())) lightBuf_ = *l;
                lightBufFor_ = sel;
                lightBufRev_ = doc_.revision();
            }
        }
        // Own ID scope: the "new light" fields below use the same labels.
        ImGui::PushID("selected lights");
        ImGui::Text("%zu %s light(s) selected", sel.size(), lightKindName(lightBuf_.kind));
        const unsigned done = lightFields(lightBuf_, sel.size() == 1);
        if (done) {
            const Light v = lightBuf_;
            editLights("Edit light", [&](Light& l) {
                if (done & LF_Color) l.color = v.color;
                if (done & LF_Bright) l.brightness = v.brightness;
                if (done & LF_Range) l.range = v.range;
                if ((done & LF_Pitch) && l.hasDirection()) l.pitch = v.pitch;
                if ((done & LF_Yaw) && l.hasDirection()) l.yaw = v.yaw;
                if (done & LF_Inner) l.inner = std::min(v.inner, l.outer);
                if (done & LF_Outer) { l.outer = v.outer; l.inner = std::min(l.inner, l.outer); }
                if (done & LF_Pos) l.pos = v.pos;
            });
        }
        if (lightBuf_.hasDirection()) {
            if (ImGui::SmallButton("point down")) editLights("Aim light", [](Light& l) { if (l.hasDirection()) l.pitch = -90; });
            ImGui::SameLine();
            ImGui::TextDisabled("or drag the rings");
        }
        ImGui::TextDisabled("Delete removes, Cmd+D duplicates");
        ImGui::PopID();
        if (tool_ != &lightTool_) return;
        ImGui::Separator();
    }
    LightTool& t = lightTool_;
    ImGui::PushID("new light");
    ImGui::TextUnformatted("New light");
    int kind = (int)t.templ.kind;
    ImGui::RadioButton("point", &kind, 0); ImGui::SameLine();
    ImGui::RadioButton("spot", &kind, 1); ImGui::SameLine();
    ImGui::RadioButton("sun", &kind, 2);
    t.templ.kind = (LightKind)kind;
    if (t.pending) t.templ.pos = *t.pending;
    lightFields(t.templ, t.pending.has_value());
    if (t.pending) t.pending = t.templ.pos;
    ImGui::BeginDisabled(!t.pending);
    if (ImGui::Button("Create (Return)")) t.create(doc_);
    ImGui::EndDisabled();
    if (!t.pending) ImGui::TextDisabled("click in a view to place it");
    ImGui::PopID();
}

Vec3 Editor::groundBelow(double x, double y) const {
    double best = -1;
    for (const Brush& b : doc_.brushes()) {
        if (!doc_.visible(b)) continue;
        for (double t : {rayHit(b, {x, y, 1e5}, {0, 0, -1}), rayHitTerrain(b, {x, y, 1e5}, {0, 0, -1})})
            if (t >= 0 && (best < 0 || t < best)) best = t;
    }
    return {x, y, best >= 0 ? 1e5 - best : 0};
}

void Editor::addBookmark(const std::string& name) {
    std::vector<CameraBookmark> b = doc_.bookmarks();
    const Vector3 c = views_[0]->cameraPos();
    std::string n = name.empty() ? "view " + std::to_string(b.size() + 1) : name;
    b.push_back({n, {c.x, c.y, c.z}, views_[0]->yaw(), views_[0]->pitch()});
    doc_.setBookmarks(std::move(b));
}

void Editor::goToBookmark(size_t i) {
    if (i >= doc_.bookmarks().size()) return;
    const CameraBookmark& b = doc_.bookmarks()[i];
    views_[0]->setCamera(b.pos, (float)b.yaw, (float)b.pitch);
}

void Editor::drawWorldDialogs() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    if (askEnvironment_) { ImGui::OpenPopup("Environment"); askEnvironment_ = false; }
    if (askBookmarks_) { ImGui::OpenPopup("Camera bookmarks"); askBookmarks_ = false; }
    if (askFigure_) { ImGui::OpenPopup("Scale figure"); askFigure_ = false; }
    auto buttons = [&](bool& apply, bool& cancel, const char* ok = "Apply (Return)") {
        apply = ImGui::Button(ok) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        ImGui::SameLine();
        cancel = ImGui::Button("Cancel (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_;
    };
    bool apply = false, cancel = false;
    auto color3 = [](const char* label, Vec3& c) {
        float f[3] = {(float)(c.x / 255), (float)(c.y / 255), (float)(c.z / 255)};
        if (ImGui::ColorEdit3(label, f, ImGuiColorEditFlags_Uint8))
            c = {std::round(f[0] * 255), std::round(f[1] * 255), std::round(f[2] * 255)};
    };

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Environment", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Environment& e = envBuf_;
        ImGui::TextUnformatted("Sky");
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo("##sky", e.sky.empty() ? "(none)" : e.sky.c_str())) {
            if (ImGui::Selectable("(none)", e.sky.empty())) e.sky.clear();
            for (const std::string& n : skies_.names())
                if (ImGui::Selectable(n.c_str(), n == e.sky)) e.sky = n;
            ImGui::EndCombo();
        }
        if (!e.sky.empty() && !skies_.exists(e.sky)) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "not found in the sky folders");
        else if (!e.sky.empty() && !skies_.get(e.sky)) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", skies_.problem(e.sky).c_str());
        ImGui::TextDisabled("2:1 panoramas or 4:3 cube crosses; shown in the 3D view (View menu)");
        if (ImGui::TreeNode("Sky folders")) {
            int remove = -1;
            for (size_t i = 0; i < skies_.userFolders().size(); ++i) {
                ImGui::PushID((int)i);
                if (ImGui::SmallButton("remove")) remove = (int)i;
                ImGui::SameLine();
                ImGui::TextUnformatted(skies_.userFolders()[i].c_str());
                ImGui::PopID();
            }
            if (remove >= 0) { skies_.removeUserFolder((size_t)remove); saveSkyFolders(); }
            if (skies_.userFolders().empty()) ImGui::TextDisabled("(none yet - the built-in skies are always there)");
            ImGui::SetNextItemWidth(260);
            ImGui::InputTextWithHint("##skydir", "folder path", skyFolderBuf_, sizeof skyFolderBuf_);
            ImGui::SameLine();
            if (ImGui::Button("Add") && skyFolderBuf_[0]) { skies_.addUserFolder(skyFolderBuf_); saveSkyFolders(); skyFolderBuf_[0] = 0; }
            if (useNative_) {
                ImGui::SameLine();
                if (ImGui::Button("Browse...")) { native_ = Native::SkyFolder; ImGui::CloseCurrentPopup(); }
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
        color3("ambient light", e.ambient);
        ImGui::TextDisabled("lights everything a little, even where no light reaches");
        ImGui::Separator();
        ImGui::Checkbox("fog", &e.fogOn);
        ImGui::BeginDisabled(!e.fogOn);
        color3("fog colour", e.fog.color);
        ImGui::PushItemWidth(120);
        ImGui::InputDouble("clear up to (units)", &e.fog.start, 0, 0, "%g");
        ImGui::InputDouble("fully fogged from", &e.fog.end, 0, 0, "%g");
        ImGui::PopItemWidth();
        ImGui::EndDisabled();
        ImGui::TextDisabled("the 3D view shows fog only when View > 3D view: fog is on");
        if (!worldError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", worldError_.c_str());
        buttons(apply, cancel);
        if (apply) {
            e.fog.start = std::max(0.0, e.fog.start);
            if (e.fog.end <= e.fog.start) worldError_ = "fog: 'fully fogged from' must be farther than 'clear up to'";
            else {
                worldError_.clear();
                if (e != doc_.environment()) doc_.apply(doc_.makeEnvironment(e, "Environment"));
                ImGui::CloseCurrentPopup();
            }
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Camera bookmarks", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Saved 3D viewpoints (kept in the map, editor only)");
        int go = -1, del = -1;
        for (size_t i = 0; i < doc_.bookmarks().size(); ++i) {
            ImGui::PushID((int)i);
            if (ImGui::SmallButton("go")) go = (int)i;
            ImGui::SameLine();
            if (ImGui::SmallButton("delete")) del = (int)i;
            ImGui::SameLine();
            ImGui::TextUnformatted(doc_.bookmarks()[i].name.c_str());
            ImGui::PopID();
        }
        if (doc_.bookmarks().empty()) ImGui::TextDisabled("(none yet)");
        if (go >= 0) goToBookmark((size_t)go);
        if (del >= 0) { auto b = doc_.bookmarks(); b.erase(b.begin() + del); doc_.setBookmarks(std::move(b)); }
        ImGui::Separator();
        ImGui::SetNextItemWidth(200);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##bm", "name", bookmarkName_, sizeof bookmarkName_);
        ImGui::SameLine();
        if (ImGui::Button("Save current 3D view (Return)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            addBookmark(bookmarkName_);
            bookmarkName_[0] = 0;
        }
        if (ImGui::Button("Close (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Scale figure", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Where the 175-unit (1.75 m) figure stands (its feet)");
        ImGui::SetNextItemWidth(260);
        ImGui::InputScalarN("X Y Z", ImGuiDataType_Double, figureBuf_, 3, nullptr, nullptr, "%g");
        if (ImGui::Button("Under the 3D view's centre")) {
            Vec3 o, d;
            views_[0]->pickRay({views_[0]->width() * 0.5f, views_[0]->height() * 0.5f}, o, d);
            double best = -1;
            for (const Brush& b : doc_.brushes()) {
                if (!doc_.visible(b)) continue;
                for (double t : {rayHit(b, o, d), rayHitTerrain(b, o, d)})
                    if (t >= 0 && (best < 0 || t < best)) best = t;
            }
            const Vec3 h = o + d * (best >= 0 ? best : 256.0);
            figureBuf_[0] = grid_.snapValue(h.x); figureBuf_[1] = grid_.snapValue(h.y); figureBuf_[2] = h.z;
        }
        ImGui::TextDisabled("View > Scale figure shows or hides it");
        buttons(apply, cancel);
        if (apply) {
            doc_.setFigure(Vec3{figureBuf_[0], figureBuf_[1], figureBuf_[2]});
            showFigure_ = true;
            ImGui::CloseCurrentPopup();
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Editor::drawTerrainDialogs() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    if (askMakeTerrain_) { ImGui::OpenPopup("Terrain grid size"); askMakeTerrain_ = false; }
    if (askHeightmap_) { ImGui::OpenPopup("Import heightmap"); askHeightmap_ = false; }
    auto buttons = [&](bool& apply, bool& cancel) {
        apply = ImGui::Button("Apply (Return)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        ImGui::SameLine();
        cancel = ImGui::Button("Cancel (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_;
    };
    bool apply = false, cancel = false;

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Terrain grid size", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Grid size of the selected terrain");
        ImGui::TextDisabled("(with the Texture tool: makes the picked faces terrain).");
        ImGui::TextDisabled("Terrain keeps its shape and paint at the new size.");
        ImGui::TextUnformatted("Cells per side:");
        for (int pw = kMinTerrainPower; pw <= kMaxTerrainPower; ++pw) {
            char b[8]; std::snprintf(b, sizeof b, "%d", 1 << pw);
            ImGui::SameLine();
            if (ImGui::RadioButton(b, terrainPower_ == pw)) terrainPower_ = pw;
        }
        if (!terrainError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", terrainError_.c_str());
        buttons(apply, cancel);
        if (apply) {
            makeTerrainNow(terrainPower_);
            if (terrainError_.empty()) { ImGui::CloseCurrentPopup(); setTool(&terrainTool_); }
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Import heightmap", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Set the selected terrain's heights from a grey image");
        ImGui::TextDisabled("(black = low, white = high; the image covers all selected");
        ImGui::TextDisabled("terrain, top of the image = +Y in the top view).");
        ImGui::SetNextItemWidth(360);
        ImGui::InputTextWithHint("##hm", "image path (PNG, JPG, BMP, TGA)", heightmapPath_, sizeof heightmapPath_);
        if (useNative_) {
            ImGui::SameLine();
            if (ImGui::Button("Browse...")) { native_ = Native::Heightmap; ImGui::CloseCurrentPopup(); }
        }
        ImGui::PushItemWidth(120);
        ImGui::InputDouble("height of black", &heightLow_, 0, 0, "%g");
        ImGui::InputDouble("height of white", &heightHigh_, 0, 0, "%g");
        ImGui::PopItemWidth();
        if (!terrainError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", terrainError_.c_str());
        buttons(apply, cancel);
        if (apply) {
            if (importHeightmap(heightmapPath_, heightLow_, heightHigh_)) ImGui::CloseCurrentPopup();
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Editor::drawFacePanel() {
    if (tool_ == &blockTool_) {
        if (ImGui::Begin("Properties###face", nullptr, ImGuiWindowFlags_NoCollapse)) drawBlockPanel();
        ImGui::End();
        return;
    }
    if (tool_ == &curveTool_) {
        if (ImGui::Begin("Properties###face", nullptr, ImGuiWindowFlags_NoCollapse)) drawCurvePanel();
        ImGui::End();
        return;
    }
    if (tool_ == &terrainTool_) {
        if (ImGui::Begin("Properties###face", nullptr, ImGuiWindowFlags_NoCollapse)) drawTerrainPanel();
        ImGui::End();
        return;
    }
    if (tool_ == &propTool_ || ((tool_ == &selectTool_ || tool_ == &lightTool_ || tool_ == &primTool_) && doc_.primSelection().empty() && !doc_.propSelection().empty())) {
        if (ImGui::Begin("Properties###face", nullptr, ImGuiWindowFlags_NoCollapse)) drawPropPanel();
        ImGui::End();
        return;
    }
    if (tool_ == &primTool_ || ((tool_ == &selectTool_ || tool_ == &lightTool_) && !doc_.primSelection().empty())) {
        if (ImGui::Begin("Properties###face", nullptr, ImGuiWindowFlags_NoCollapse)) drawPrimPanel();
        ImGui::End();
        return;
    }
    if (tool_ == &lightTool_ || (tool_ == &selectTool_ && !doc_.lightSelection().empty())) {
        if (ImGui::Begin("Properties###face", nullptr, ImGuiWindowFlags_NoCollapse)) drawLightPanel();
        ImGui::End();
        return;
    }
    if (ImGui::Begin("Properties###face", nullptr, ImGuiWindowFlags_NoCollapse)) {
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
            if (faces.size() > 1) {
                if (ImGui::Button("Wrap around")) wrapAroundPicked();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Continue the first clicked face's texture around all connected picked faces");
            } else {
                ImGui::TextDisabled("Wrap: Option+Shift+click the next face");
            }
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
    doc_.reset(loaded.worldKeys, loaded.brushes(), loaded.otherEntities, loaded.groups(), loaded.visgroups(),
               WorldData{loaded.lights(), loaded.environment(), loaded.bookmarks(), loaded.figure(), loaded.prims(), loaded.props()});
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
        case Native::Bottega:
            if (chooseFolderDialog(docs, picked)) std::snprintf(bottegaBuf_, sizeof bottegaBuf_, "%s", picked.c_str());
            askBottega_ = true; // back to the Bottega dialog
            break;
        case Native::SkyFolder:
            if (chooseFolderDialog(docs, picked)) { skies_.addUserFolder(picked); saveSkyFolders(); }
            askEnvironment_ = true; // back to the environment dialog
            break;
        case Native::Heightmap:
            if (openFileDialog(docs, {"png", "jpg", "jpeg", "bmp", "tga"}, picked))
                std::snprintf(heightmapPath_, sizeof heightmapPath_, "%s", picked.c_str());
            askHeightmap_ = true; // back to the heightmap dialog
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
        // viewset sky|preview|fog|figure on|off
        std::string k, v; in >> k >> v;
        const bool on = v == "on";
        if (k == "sky") showSky_ = on;
        else if (k == "preview") { lightPreview_ = on; if (!on) lighting_.clear(); }
        else if (k == "fog") showFog_ = on;
        else if (k == "figure") showFigure_ = on;
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
