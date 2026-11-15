#pragma once
// Main window chrome: menu bar, status bar, and the docked 4-viewport layout.
#include "imgui.h"

namespace scal {

struct MenuActions {
    bool quit = false;
    bool resetLayout = false;
    bool gridSmaller = false;
    bool gridLarger = false;
    bool toggleGrid = false;
    bool toggleSnap = false;
};

// Draws the menu bar and reports clicked items.
MenuActions drawMenuBar(bool gridVisible, bool snapOn);

// Status bar along the bottom. `text` is shown left-aligned.
void drawStatusBar(const char* text);

// Creates the dockspace; builds the default 2x2 layout on first run or when reset.
// Window titles must match the viewports' title() strings.
void dockspace(bool forceReset, const char* topLeft, const char* topRight,
               const char* bottomLeft, const char* bottomRight);

} // namespace scal
