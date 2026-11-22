#pragma once
// Main window chrome: menu bar, status bar, and the docked layout.
// Rule: every keyboard shortcut also exists as a menu item (mouse-only use must work).
#include "imgui.h"

namespace scal {

struct MenuState {
    bool gridVisible = true, snapOn = true;
    bool canUndo = false, canRedo = false;
    bool hasSelection = false;
    bool selectTool = true, blockTool = false, blockPreview = false;
    bool flying = false, controlsOpen = false;
    bool vertexTool = false, verticesSelected = false;
    int pivotMode = 0;
    bool slopeAngles = false, microgrid = false;
    bool facesPicked = false;
    bool textureTool = false, textureLock = true, smoothTextures = false, textured3D = true;
    bool canApplyTexture = false;
    double defaultScale = 0.25;
    bool clipTool = false, clipPlane = false;
    bool curveTool = false, groupSelected = false;
    int clipKeep = 0; // 0 both, 1 front, 2 back
    bool terrainTool = false, terrainSelected = false, canMakeTerrain = false, terrainPicking = false;
    int terrainMode = 0;
    bool lightTool = false, lightPending = false, lightsSelected = false;
    int lightKind = 0;
    bool showSky = true, lightPreview = false, showFog = false, showFigure = false;
    bool showLightGrid = false, haveLightGrid = false; // the last bake's light grid (v0.12.3)
    bool canRebakeSky = false; // Sky Lab: the sky is out of date or its file is missing
    bool primsShown = true, propsShown = true;
    bool primTool = false, primPending = false;
    int primKind = 0;
    size_t primsSelected = 0;
    bool hasAnything = false;   // brushes, lights, cameras, primitives or props selected
    bool primsInCombo = false;
    bool propTool = false, propPending = false;
    size_t propsSelected = 0;
    bool decalTool = false, decalPending = false, decalsShown = true; // decals (v0.12.5)
    size_t decalsSelected = 0;
    bool cameraTool = false, cameraPending = false, cameraAiming = false;
    bool probeTool = false, probePending = false;           // reflection probes (v0.12.6)
    bool showProbeBalls = false, haveProbeBalls = false, showProbeUsage = false;
    size_t camerasSelected = 0;
    bool selectedCameraActive = false; // the one selected camera is the active one already
    bool hasCameras = false;
    int renderQuality = 1, renderAA = 2; // Render menu (core/render_job.hpp)
    bool renderSmooth = true;
    bool renderPropsQuick = false;
    bool showHelperMaps = false, canMakeVariant = false; // Texture menu (materials, v0.12.4)
};

struct MenuActions {
    bool newMap = false, open = false, openPath = false, save = false, saveAs = false, quit = false;
    bool undo = false, redo = false, duplicate = false, del = false, selectAll = false, selectNone = false;
    bool toolSelect = false, toolBlock = false, createBlock = false, cancel = false;
    bool transform = false;
    int rotate90Axis = -1, rotate90Sign = 0; // axis 0..2, sign +1 = counter-clockwise
    int flipAxis = -1;
    bool resetLayout = false, centerViews = false, flyMode = false;
    bool gridSmaller = false, gridLarger = false, toggleGrid = false, toggleSnap = false;
    bool controls = false;
    bool toolVertex = false, vertexMove = false, snapToGrid = false;
    int pivotMode = -1;          // 0 centre, 1 origin, 2 custom (opens dialog)
    int rotSnap = -1;            // 0 = 15 degree steps, 1 = grid-friendly slopes
    bool toggleMicrogrid = false, setGridSize = false;
    bool applyTexture = false, applyNoDraw = false, wrapAround = false, toolTexture = false, toggleTextureLock = false;
    double defaultScale = 0;     // > 0: new default texture scale
    int smoothTextures = -1;     // 0 crisp, 1 smooth
    int textured3D = -1;         // 0 flat colours, 1 textured
    bool addTextureFolder = false, rescanBottega = false, bottegaFolder = false;
    bool toolClip = false, clipApply = false, clipPoints = false, carve = false, hollow = false;
    bool toolCurve = false, editCurve = false, bakeCurve = false;
    bool hideSelected = false, hideUnselected = false, showAll = false;
    bool findOverlaps = false, viewsToOrigin = false;
    int clipKeep = -1;
    bool toolTerrain = false, makeTerrain = false, terrainGrid = false, removeTerrain = false, heightmap = false, stitchTerrain = false;
    int terrainMode = -1; // SculptMode index; also switches to the terrain tool
    bool toolLight = false, createLight = false, environment = false, bookmarks = false, figurePos = false;
    bool skyLab = false, rebakeSky = false;
    int lightKind = -1;   // LightKind index; also switches to the light tool
    bool toggleSky = false, toggleLightPreview = false, toggleFog = false, toggleFigure = false, toggleLightGrid = false;
    bool togglePrims = false, toggleProps = false;
    bool toolPrim = false, createPrim = false, combine = false, separate = false;
    int primKind = -1;    // PrimKind index; also switches to the primitive tool
    int primOp = -1;      // PrimOp for the selected primitives
    bool replicate = false, randomReplicate = false, snapLand = false, snapGround = false;
    bool toolProp = false, createProp = false;
    bool toolDecal = false, createDecal = false, decalDialog = false, decalForward = false, decalBack = false;
    bool decalPictureSize = false, toggleDecals = false;
    bool toolCamera = false, createCamera = false, activateCamera = false, aimCamera = false;
    bool toolProbe = false, createProbe = false, toggleProbeBalls = false, toggleProbeUsage = false;
    bool updatePreview = false;
    bool render = false;      // Render... (Cmd+Shift+K)
    int renderQuality = -1;   // RenderQuality index
    int renderAA = -1;        // 1..4
    int renderSmooth = -1;    // 0 crisp, 1 smooth
    int renderPropsQuick = -1; // 0 full, 1 quick
    bool selectShownTextures = false, selectFaceTextures = false, toggleHelperMaps = false, makeVariant = false;
};

// Draws the menu bar and reports clicked items.
MenuActions drawMenuBar(const MenuState& st);

// Status bar along the bottom, with the Render button at its left end. True when it was
// clicked. `canRender`: the map has a camera.
bool drawStatusBar(const char* text, bool canRender);

// Creates the dockspace; builds the default layout (tools strip on the left, 2x2 views)
// on first run or when reset. Window names must match the windows' titles.
void dockspace(bool forceReset, const char* tools, const char* topLeft, const char* topRight,
               const char* bottomLeft, const char* bottomRight, const char* rightTop,
               const char* rightBottom, bool withPanels = true, const char* rightTopTab = nullptr, const char* rightTopTab2 = nullptr,
               const char* rightPreview = nullptr);

// Help -> Controls: every shortcut in one place.
void drawControlsWindow(bool* open);

} // namespace scal
