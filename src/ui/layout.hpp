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
};

struct MenuActions {
    bool newMap = false, open = false, save = false, saveAs = false, quit = false;
    bool undo = false, redo = false, duplicate = false, del = false, selectAll = false, selectNone = false;
    bool toolSelect = false, toolBlock = false, createBlock = false, cancel = false;
    bool transform = false;
    int rotate90Axis = -1, rotate90Sign = 0; // axis 0..2, sign +1 = counter-clockwise
    int flipAxis = -1;
    bool resetLayout = false, centerViews = false, flyMode = false;
    bool gridSmaller = false, gridLarger = false, toggleGrid = false, toggleSnap = false;
    bool controls = false;
};

// Draws the menu bar and reports clicked items.
MenuActions drawMenuBar(const MenuState& st);

// Status bar along the bottom.
void drawStatusBar(const char* text);

// Creates the dockspace; builds the default layout (tools strip on the left, 2x2 views)
// on first run or when reset. Window names must match the windows' titles.
void dockspace(bool forceReset, const char* tools, const char* topLeft, const char* topRight,
               const char* bottomLeft, const char* bottomRight);

// Help -> Controls: every shortcut in one place.
void drawControlsWindow(bool* open);

} // namespace scal
