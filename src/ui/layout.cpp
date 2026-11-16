#include "ui/layout.hpp"
#include "imgui_internal.h" // DockBuilder, BeginViewportSideBar

namespace scal {

MenuActions drawMenuBar(bool gridVisible, bool snapOn, bool canUndo, bool canRedo) {
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
        if (ImGui::MenuItem("Undo", "Cmd+Z", false, canUndo)) a.undo = true;
        if (ImGui::MenuItem("Redo", "Cmd+Shift+Z", false, canRedo)) a.redo = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Select all", "Cmd+A")) a.selectAll = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        // Shortcut labels are for an Italian keyboard (the two keys right of P).
        if (ImGui::MenuItem("Smaller grid", "è")) a.gridSmaller = true;
        if (ImGui::MenuItem("Larger grid", "+"))  a.gridLarger = true;
        if (ImGui::MenuItem("Show grid", "Shift+R", gridVisible)) a.toggleGrid = true;
        if (ImGui::MenuItem("Snap to grid", "Shift+W", snapOn))  a.toggleSnap = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Reset layout")) a.resetLayout = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
    return a;
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
