#include "ui/imgui_checks.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <cstdarg>
#include <map>
#include <set>

namespace {
int gFrame = -1;
std::set<ImGuiID> gSeen;                 // IDs added this frame
std::set<ImGuiID> gDup;                  // seen twice
std::map<ImGuiID, std::string> gLabels;  // for the report
} // namespace

// Dear ImGui's test-engine hooks (IMGUI_ENABLE_TEST_ENGINE).
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect&, const ImGuiLastItemData* item) {
    if (id == 0) return;
    if (item && (item->ItemFlags & ImGuiItemFlags_AllowDuplicateId)) return;
    if (ctx->FrameCount != gFrame) {
        gFrame = ctx->FrameCount;
        gSeen.clear();
    }
    if (!gSeen.insert(id).second) gDup.insert(id);
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
    if (label && gDup.count(id)) gLabels[id] = label;
    else if (label && gLabels.size() < 4096) gLabels.emplace(id, label);
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID id) {
    auto it = gLabels.find(id);
    return it == gLabels.end() ? nullptr : it->second.c_str();
}

namespace scal {

void enableImGuiIdCheck() { GImGui->TestEngineHookItems = true; }

std::vector<std::string> takeImGuiIdConflicts() {
    std::vector<std::string> out;
    for (ImGuiID id : gDup) {
        auto it = gLabels.find(id);
        out.push_back(it == gLabels.end() ? "(unlabelled)" : it->second);
    }
    gDup.clear();
    return out;
}

} // namespace scal
