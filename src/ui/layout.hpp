#pragma once
// Main window chrome: menu bar, status bar, and the docked layout.
#include "imgui.h"

namespace scal {

struct MenuActions {
    bool newMap = false, open = false, save = false, saveAs = false, quit = false;
    bool undo = false, redo = false, selectAll = false;
    bool resetLayout = false;
    bool gridSmaller = false, gridLarger = false, toggleGrid = false, toggleSnap = false;
};

// Draws the menu bar and reports clicked items.
MenuActions drawMenuBar(bool gridVisible, bool snapOn, bool canUndo, bool canRedo);

// Status bar along the bottom.
void drawStatusBar(const char* text);

// Creates the dockspace; builds the default layout (tools strip on the left, 2x2 views)
// on first run or when reset. Window names must match the windows' titles.
void dockspace(bool forceReset, const char* tools, const char* topLeft, const char* topRight,
               const char* bottomLeft, const char* bottomRight);

} // namespace scal
