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
    bool addTextureFolder = false, rescanTextures = false;
    bool toolClip = false, clipApply = false, clipPoints = false, carve = false, hollow = false;
    bool toolCurve = false, editCurve = false, bakeCurve = false;
    int clipKeep = -1;
};

// Draws the menu bar and reports clicked items.
MenuActions drawMenuBar(const MenuState& st);

// Status bar along the bottom.
void drawStatusBar(const char* text);

// Creates the dockspace; builds the default layout (tools strip on the left, 2x2 views)
// on first run or when reset. Window names must match the windows' titles.
void dockspace(bool forceReset, const char* tools, const char* topLeft, const char* topRight,
               const char* bottomLeft, const char* bottomRight, const char* rightTop,
               const char* rightBottom, bool withPanels = true);

// Help -> Controls: every shortcut in one place.
void drawControlsWindow(bool* open);

} // namespace scal
