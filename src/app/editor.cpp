// Editor (part of the class, split by topic; see editor.hpp): The main loop: input, the frame, building the scene, drawing the views, status.
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
#if defined(__APPLE__)
        moonBuiltinPath_ = appDir + "../Resources/moon/moon_lroc_color.jpg";
#else
        moonBuiltinPath_ = appDir + "moon/moon_lroc_color.jpg";
#endif
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
    if (randomizing_ && !scriptPreview_ && (key == KEY_ENTER || key == KEY_KP_ENTER || key == KEY_ESCAPE)) {
        if (key != KEY_ESCAPE) applyRandomReplicate(randSpecNow());
        randomizing_ = false;
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
    st.hasAnything = st.hasSelection || !doc_.lightSelection().empty() || !doc_.primSelection().empty() || !doc_.propSelection().empty() ||
                     !doc_.cameraSelection().empty();
    st.cameraTool = tool_ == &cameraTool_;
    st.cameraPending = st.cameraTool && cameraTool_.pending.has_value();
    st.cameraAiming = st.cameraTool && cameraTool_.aiming();
    st.camerasSelected = doc_.cameraSelection().size();
    st.hasCameras = !doc_.cameras().empty();
    if (st.camerasSelected == 1)
        if (const Camera* c = doc_.findCamera(*doc_.cameraSelection().begin())) st.selectedCameraActive = c->active;
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
    st.canRebakeSky = !skyBake_ && (skyOutOfDate() || skyFileMissing());
    st.primsShown = doc_.primsShown();
    st.propsShown = doc_.propsShown();
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
              "Visgroups###visgroups", "Props###props", "Camera Preview###camerapreview");
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
    if (m.toolCamera) setTool(&cameraTool_);
    if (m.createCamera) cameraTool_.create(doc_);
    if (m.activateCamera && doc_.cameraSelection().size() == 1) activateCamera(*doc_.cameraSelection().begin());
    if (m.aimCamera) startCameraAim();
    if (m.updatePreview && !doc_.cameras().empty()) requestCameraPreview();
    if (m.createProp) onKey(KEY_ENTER);
    if (m.primKind >= 0) { primTool_.templ.kind = (PrimKind)m.primKind; setTool(&primTool_); }
    if (m.createPrim) primTool_.create(doc_, currentTexture_, &lastBox_);
    if (m.combine) combinePrims();
    if (m.replicate) openReplicate();
    if (m.randomReplicate) openRandomReplicate();
    if (m.snapLand) snapToLand();
    if (m.snapGround) askGround_ = true;
    if (m.separate) separatePrims();
    if (m.primOp >= 0) setPrimOp((PrimOp)m.primOp);
    if (m.lightKind >= 0) { lightTool_.templ.kind = (LightKind)m.lightKind; setTool(&lightTool_); }
    if (m.createLight) lightTool_.create(doc_);
    if (m.skyLab) { if (skyLabOpen_) skyLabFocus_ = true; else openSkyLab(); }
    if (m.rebakeSky) rebakeSky();
    if (m.environment) { worldError_.clear(); envBuf_ = doc_.environment(); askEnvironment_ = true; }
    if (m.bookmarks) { worldError_.clear(); askBookmarks_ = true; }
    if (m.figurePos) {
        const Vec3 f = doc_.figure() ? *doc_.figure() : Vec3{};
        figureBuf_[0] = f.x; figureBuf_[1] = f.y; figureBuf_[2] = f.z;
        askFigure_ = true;
    }
    if (m.toggleSky) showSky_ = !showSky_;
    if (m.togglePrims) { tool_->cancel(); doc_.setPrimsShown(!doc_.primsShown()); }
    if (m.toggleProps) { tool_->cancel(); doc_.setPropsShown(!doc_.propsShown()); }
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
    if (showPanels_) { drawTextureBrowser(); drawPropsPanel(); drawVisgroupsPanel(); drawFacePanel(); drawCameraPreview(); }
    drawViews();
    drawDialogs();
    drawTransformDialog();
    drawPrecisionDialogs();
    drawShapingDialogs();
    drawTerrainDialogs();
    drawWorldDialogs();
    drawReplicateDialog();
    drawRandomReplicateWindow();
    drawSkyLabWindow();
    stepSkyBake();
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
    return flying_ || rmbLook_ || !script_.empty() || !opt_.screenshot.empty() || skyLabNeedsFrame();
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
        if (IsKeyPressed(KEY_R)) { if (shift_) openRandomReplicate(); else rescanBottega(); }
        if (IsKeyPressed(KEY_U)) showAll();
        if (IsKeyPressed(KEY_K) && !shift_ && !doc_.cameras().empty()) requestCameraPreview();
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
    if (shift_ && IsKeyPressed(KEY_K)) setTool(&cameraTool_);
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
    for (const Camera& c : doc_.cameras()) s.cameras.push_back({c, doc_.cameraSelection().count(c.id) > 0});
    if (doc_.primsShown())
        for (const MathPrim& p : doc_.prims()) s.prims.push_back({p, doc_.primSelection().count(p.id) > 0});
    if (doc_.propsShown()) for (const Prop& p : doc_.props()) {
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
    if (replicating_ || randomizing_) { // Multi- / Random Replicate preview
        std::vector<Brush> bs;
        std::vector<MathPrim> ps;
        std::vector<Light> ls;
        std::vector<Prop> qs;
        replicateSource(bs, ps, ls, qs);
        const std::vector<ReplicateCopy> copies = replicating_ ? replicate(bs, ps, ls, repSpec_, qs, modelFinder_)
                                                               : randomReplicate(bs, ps, ls, randSpecNow(), qs, modelFinder_);
        for (const ReplicateCopy& c : copies) {
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
    if (skyLabOpen_ && skyLabTex_.id) s.skyTexture = skyLabTex_.id; // Sky Lab's live preview
    else if (showSky_ && !doc_.environment().sky.empty())
        if (const Texture2D* t = skies_.get(doc_.environment().sky)) {
            s.skyTexture = t->id;
            const SkyLibrary::Cut cut = skies_.cut(doc_.environment().sky);
            s.skyCover = cut.cover;
            s.skyBelow = {(double)cut.below[0], (double)cut.below[1], (double)cut.below[2]};
        }
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
        toolButton(&cameraTool_, "Shift+K");
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

void Editor::setTool(Tool* t) {
    if (t == tool_) return;
    tool_->cancel();
    if (tool_ == &terrainTool_) terrainTool_.stopPicking();
    capture_ = -1;
    tool_ = t;
}

} // namespace scal
