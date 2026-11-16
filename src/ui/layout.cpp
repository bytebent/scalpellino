#include "ui/layout.hpp"
#include "imgui_internal.h" // DockBuilder, BeginViewportSideBar
#include <cstdio>

namespace scal {

MenuActions drawMenuBar(const MenuState& st) {
    MenuActions a;
    if (!ImGui::BeginMainMenuBar()) return a;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", "Cmd+N")) a.newMap = true;
        if (ImGui::MenuItem("Open...", "Cmd+O")) a.open = true;
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
        ImGui::TextDisabled("Drag: Option+corner handle rotates");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        // Shortcut labels are for an Italian keyboard (the two keys right of P).
        if (ImGui::MenuItem("Smaller grid", "\xC3\xA8")) a.gridSmaller = true;
        if (ImGui::MenuItem("Larger grid", "+"))  a.gridLarger = true;
        if (ImGui::MenuItem("Show grid", "Shift+R", st.gridVisible)) a.toggleGrid = true;
        if (ImGui::MenuItem("Snap to grid", "Shift+W", st.snapOn))  a.toggleSnap = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Center 2D views on selection", "Cmd+E", false, st.hasSelection)) a.centerViews = true;
        if (ImGui::MenuItem("Fly mode (3D view)", "Z", st.flying)) a.flyMode = true;
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
               const char* bottomLeft, const char* bottomRight) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // Bump the version suffix whenever the default layout changes.
    const ImGuiID id = ImGui::GetID("MainDockspace.v2");

    if (forceReset || ImGui::DockBuilderGetNode(id) == nullptr) {
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(id, vp->WorkPos);
        ImGui::DockBuilderSetNodeSize(id, vp->WorkSize);

        ImGuiID toolsNode = 0, rest = 0;
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.07f, &toolsNode, &rest);
        ImGuiID left = 0, right = 0;
        ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, 0.5f, &left, &right);
        ImGuiID tl = 0, bl = 0, tr = 0, br = 0;
        ImGui::DockBuilderSplitNode(left,  ImGuiDir_Up, 0.5f, &tl, &bl);
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.5f, &tr, &br);

        ImGui::DockBuilderDockWindow(tools, toolsNode);
        ImGui::DockBuilderDockWindow(topLeft, tl);
        ImGui::DockBuilderDockWindow(topRight, tr);
        ImGui::DockBuilderDockWindow(bottomLeft, bl);
        ImGui::DockBuilderDockWindow(bottomRight, br);
        ImGui::DockBuilderFinish(id);
    }

    ImGui::DockSpaceOverViewport(id, vp, ImGuiDockNodeFlags_AutoHideTabBar);
}

} // namespace scal
