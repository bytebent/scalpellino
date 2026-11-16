#include "ui/layout.hpp"
#include "imgui_internal.h" // DockBuilder, BeginViewportSideBar
#include <cstdio>
#include <initializer_list>

namespace scal {

MenuActions drawMenuBar(const MenuState& st) {
    MenuActions a;
    if (!ImGui::BeginMainMenuBar()) return a;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", "Cmd+N")) a.newMap = true;
        if (ImGui::MenuItem("Open...", "Cmd+O")) a.open = true;
        if (ImGui::MenuItem("Open by typing a path...")) a.openPath = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Cmd+S")) a.save = true;
        if (ImGui::MenuItem("Save as...", "Cmd+Shift+S")) a.saveAs = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Cmd+Q")) a.quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Cmd+Z", false, st.canUndo)) a.undo = true;
        if (ImGui::MenuItem("Redo", "Cmd+Shift+Z", false, st.canRedo)) a.redo = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Cmd+D", false, st.hasSelection)) a.duplicate = true;
        if (ImGui::MenuItem("Delete", "Delete", false, st.hasSelection)) a.del = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Select all", "Cmd+A")) a.selectAll = true;
        if (ImGui::MenuItem("Select none", "Esc", false, st.hasSelection)) a.selectNone = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Select", "Shift+S", st.selectTool)) a.toolSelect = true;
        if (ImGui::MenuItem("Block", "Shift+B", st.blockTool)) a.toolBlock = true;
        if (ImGui::MenuItem("Vertex", "Shift+V", st.vertexTool)) a.toolVertex = true;
        if (ImGui::MenuItem("Texture", "Shift+A", st.textureTool)) a.toolTexture = true;
        if (ImGui::MenuItem("Clip", "Shift+X", st.clipTool)) a.toolClip = true;
        ImGui::Separator();
        if (ImGui::BeginMenu("Clip: keep")) {
            if (ImGui::MenuItem("Both pieces", nullptr, st.clipKeep == 0)) a.clipKeep = 0;
            if (ImGui::MenuItem("Front", nullptr, st.clipKeep == 1)) a.clipKeep = 1;
            if (ImGui::MenuItem("Back", nullptr, st.clipKeep == 2)) a.clipKeep = 2;
            ImGui::TextDisabled("Shift+X cycles while clipping");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Clip points...", nullptr, false, st.hasSelection)) a.clipPoints = true;
        if (ImGui::MenuItem("Apply clip", "Return", false, st.clipTool && st.clipPlane)) a.clipApply = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Carve with selection", "Cmd+Shift+C", false, st.hasSelection)) a.carve = true;
        if (ImGui::MenuItem("Hollow...", "Cmd+Shift+H", false, st.hasSelection)) a.hollow = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Move selected vertices...", "Cmd+Shift+T", false, st.vertexTool && st.verticesSelected)) a.vertexMove = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Create block", "Return", false, st.blockPreview)) a.createBlock = true;
        if (ImGui::MenuItem("Cancel", "Esc", false, st.blockPreview)) a.cancel = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Transform")) {
        if (ImGui::MenuItem("Move / Rotate / Scale...", "Cmd+T", false, st.hasSelection)) a.transform = true;
        ImGui::Separator();
        static const char* axes[] = {"X", "Y", "Z"};
        for (int ax = 2; ax >= 0; --ax) {
            char label[64];
            std::snprintf(label, sizeof label, "Rotate 90%s around %s", "\xC2\xB0", axes[ax]);
            if (ImGui::BeginMenu(label, st.hasSelection)) {
                if (ImGui::MenuItem("Counter-clockwise (+90)")) { a.rotate90Axis = ax; a.rotate90Sign = +1; }
                if (ImGui::MenuItem("Clockwise (-90)"))         { a.rotate90Axis = ax; a.rotate90Sign = -1; }
                ImGui::EndMenu();
            }
        }
        ImGui::Separator();
        for (int ax = 0; ax < 3; ++ax) {
            char label[32];
            std::snprintf(label, sizeof label, "Flip %s", axes[ax]);
            if (ImGui::MenuItem(label, nullptr, false, st.hasSelection)) a.flipAxis = ax;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Snap vertices to grid", "Cmd+B", false, st.hasSelection)) a.snapToGrid = true;
        ImGui::Separator();
        if (ImGui::BeginMenu("Pivot")) {
            if (ImGui::MenuItem("Selection centre", nullptr, st.pivotMode == 0)) a.pivotMode = 0;
            if (ImGui::MenuItem("World origin", nullptr, st.pivotMode == 1)) a.pivotMode = 1;
            if (ImGui::MenuItem("Point...", nullptr, st.pivotMode == 2)) a.pivotMode = 2;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Drag rotation snapping")) {
            if (ImGui::MenuItem("15\xC2\xB0 steps", nullptr, !st.slopeAngles)) a.rotSnap = 0;
            if (ImGui::MenuItem("Grid-friendly angles (3:4:5, 5:12:13...)", nullptr, st.slopeAngles)) a.rotSnap = 1;
            ImGui::EndMenu();
        }
        ImGui::TextDisabled("Drag: Option+corner handle rotates");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Texture")) {
        if (ImGui::MenuItem("Apply current texture", "Shift+T", false, st.canApplyTexture)) a.applyTexture = true;
        if (ImGui::MenuItem("Apply nodraw (hidden faces)", nullptr, false, st.canApplyTexture)) a.applyNoDraw = true;
        if (ImGui::MenuItem("Texture tool (pick faces)", "Shift+A", st.textureTool)) a.toolTexture = true;
        if (ImGui::MenuItem("Wrap around picked faces", "Option+Shift+click", false, st.facesPicked)) a.wrapAround = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Texture lock", nullptr, st.textureLock)) a.toggleTextureLock = true;
        if (ImGui::BeginMenu("Default scale for new brushes")) {
            for (double sc : {0.125, 0.25, 0.5, 1.0, 2.0}) {
                char b[16];
                std::snprintf(b, sizeof b, "%g", sc);
                if (ImGui::MenuItem(b, nullptr, st.defaultScale == sc)) a.defaultScale = sc;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Filtering")) {
            if (ImGui::MenuItem("Crisp pixels", nullptr, !st.smoothTextures)) a.smoothTextures = 0;
            if (ImGui::MenuItem("Smooth", nullptr, st.smoothTextures)) a.smoothTextures = 1;
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Add texture folder...")) a.addTextureFolder = true;
        if (ImGui::MenuItem("Rescan texture folders")) a.rescanTextures = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        // Shortcut labels are for an Italian keyboard (the two keys right of P).
        if (ImGui::MenuItem("Smaller grid", "\xC3\xA8")) a.gridSmaller = true;
        if (ImGui::MenuItem("Larger grid", "+"))  a.gridLarger = true;
        if (ImGui::MenuItem("Show grid", "Shift+R", st.gridVisible)) a.toggleGrid = true;
        if (ImGui::MenuItem("Snap to grid", "Shift+W", st.snapOn))  a.toggleSnap = true;
        if (ImGui::MenuItem("Set grid size...")) a.setGridSize = true;
        if (ImGui::MenuItem("Microgrid (sizes below 1)", nullptr, st.microgrid)) a.toggleMicrogrid = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Center 2D views on selection", "Cmd+E", false, st.hasSelection)) a.centerViews = true;
        if (ImGui::MenuItem("Fly mode (3D view)", "Z", st.flying)) a.flyMode = true;
        if (ImGui::MenuItem("3D view: textured", nullptr, st.textured3D)) a.textured3D = 1;
        if (ImGui::MenuItem("3D view: flat colours", nullptr, !st.textured3D)) a.textured3D = 0;
        ImGui::Separator();
        if (ImGui::MenuItem("Reset layout")) a.resetLayout = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Controls", "F1", st.controlsOpen)) a.controls = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
    return a;
}

void drawControlsWindow(bool* open) {
    if (!*open) return;
    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Controls", open)) {
        static const char* rows[][2] = {
            {"Select tool / Block tool", "Shift+S / Shift+B"},
            {"Draw a block", "Block tool: drag in a 2D view; Return creates, Esc cancels"},
            {"Select", "click (2D or 3D); Cmd+click adds/removes; Cmd+A all; Esc none"},
            {"Move / resize", "drag the selection / its handles or edges"},
            {"Clone", "Shift+drag the selection, or Cmd+D"},
            {"Rotate by dragging", "Option+drag a corner handle (15\xC2\xB0 steps, +Shift free)"},
            {"Exact move / rotate / scale", "Cmd+T (Transform menu)"},
            {"Vertex tool", "Shift+V; click a corner, Cmd+click adds, drag moves"},
            {"Move vertices exactly", "Cmd+Shift+T (vertex tool)"},
            {"Snap vertices to grid", "Cmd+B"},
            {"Pivot / rotation snapping", "Transform menu"},
            {"Grid size / microgrid", "View menu"},
            {"Texture tool (pick faces)", "Shift+A; 3D: click, Shift+click add/remove face, Cmd+click whole brush, Option+click picks texture"},
            {"Apply current texture", "Shift+T (selected brushes, or picked faces)"},
            {"Wrap texture around a corner", "Texture tool: click a face, Option+Shift+click the next (repeat)"},
            {"Textures / Face panels", "right side: search by name, click to choose; adjust picked faces"},
            {"Clip tool", "Shift+X: drag a line in a 2D view; drag points 1-3 (3 tilts the plane); Return clips"},
            {"Clip: keep both / front / back", "Shift+X again cycles (Tools menu)"},
            {"Carve with selection", "Cmd+Shift+C: cuts the selection's shape out of touching brushes"},
            {"Hollow", "Cmd+Shift+H: turn brushes into walls of a typed thickness"},
            {"Delete", "Delete or Backspace"},
            {"Undo / redo", "Cmd+Z / Cmd+Shift+Z"},
            {"New / open / save / save as", "Cmd+N / Cmd+O / Cmd+S / Cmd+Shift+S"},
            {"Fly mode (3D view)", "Z over the 3D view; Esc or Z to stop"},
            {"Move while flying", "W A S D, Q/E down/up, Shift fast"},
            {"Look without fly mode", "hold right mouse in the 3D view"},
            {"Pan 2D view", "middle mouse, or Space + left mouse"},
            {"Zoom 2D view", "mouse wheel"},
            {"Center 2D views on selection", "Cmd+E"},
            {"Smaller / larger grid", "\xC3\xA8 / + (the two keys right of P)"},
            {"Show grid / snap", "Shift+R / Shift+W"},
            {"This window", "F1"},
        };
        if (ImGui::BeginTable("controls", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupColumn("Keys");
            ImGui::TableHeadersRow();
            for (auto& r : rows) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(r[0]);
                ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", r[1]);
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void drawStatusBar(const char* text) {
    const float h = ImGui::GetFrameHeight();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar("##statusbar", ImGui::GetMainViewport(), ImGuiDir_Down, h, flags)) {
        if (ImGui::BeginMenuBar()) {
            ImGui::TextUnformatted(text);
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

void dockspace(bool forceReset, const char* tools, const char* topLeft, const char* topRight,
               const char* bottomLeft, const char* bottomRight, const char* rightTop,
               const char* rightBottom, bool withPanels) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // Bump the version suffix whenever the default layout changes.
    const ImGuiID id = ImGui::GetID("MainDockspace.v3");

    if (forceReset || ImGui::DockBuilderGetNode(id) == nullptr) {
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(id, vp->WorkPos);
        ImGui::DockBuilderSetNodeSize(id, vp->WorkSize);

        ImGuiID toolsNode = 0, rest0 = 0, rest = 0, panel = 0;
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.07f, &toolsNode, &rest0);
        ImGuiID panelTop = 0, panelBottom = 0;
        if (withPanels) {
            ImGui::DockBuilderSplitNode(rest0, ImGuiDir_Right, 0.22f, &panel, &rest);
            ImGui::DockBuilderSplitNode(panel, ImGuiDir_Up, 0.62f, &panelTop, &panelBottom);
        } else {
            rest = rest0; // "classic" layout (used by older UI test scripts)
        }
        ImGuiID left = 0, right = 0;
        ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, 0.5f, &left, &right);
        ImGuiID tl = 0, bl = 0, tr = 0, br = 0;
        ImGui::DockBuilderSplitNode(left,  ImGuiDir_Up, 0.5f, &tl, &bl);
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.5f, &tr, &br);

        ImGui::DockBuilderDockWindow(tools, toolsNode);
        if (withPanels) {
            ImGui::DockBuilderDockWindow(rightTop, panelTop);
            ImGui::DockBuilderDockWindow(rightBottom, panelBottom);
        }
        ImGui::DockBuilderDockWindow(topLeft, tl);
        ImGui::DockBuilderDockWindow(topRight, tr);
        ImGui::DockBuilderDockWindow(bottomLeft, bl);
        ImGui::DockBuilderDockWindow(bottomRight, br);
        ImGui::DockBuilderFinish(id);
    }

    ImGui::DockSpaceOverViewport(id, vp, ImGuiDockNodeFlags_AutoHideTabBar);
}

} // namespace scal
