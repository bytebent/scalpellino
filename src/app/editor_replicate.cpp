// Editor (part of the class, split by topic; see editor.hpp): Multi-Replicate and Random Replicate.
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

void Editor::openReplicate() {
    if (doc_.selection().empty() && doc_.primSelection().empty() && doc_.lightSelection().empty() && doc_.propSelection().empty()) return;
    tool_->cancel();
    randomizing_ = false; // one replicate window at a time
    if (repSpec_.offset == Vec3{100, 0, 0} && grid_.size > 0) repSpec_.offset = {grid_.size * 4, 0, 0};
    replicating_ = true;
    replicateFocus_ = true;
}

void Editor::replicateSource(std::vector<Brush>& bs, std::vector<MathPrim>& ps, std::vector<Light>& ls, std::vector<Prop>& qs) const {
    // Groups of the selected brushes come along whole.
    std::set<BrushId> sel = doc_.selection();
    std::set<std::uint32_t> groups;
    for (BrushId id : sel) if (const Brush* b = doc_.find(id)) if (b->group) groups.insert(b->group);
    for (const Brush& b : doc_.brushes()) if (b.group && groups.count(b.group)) sel.insert(b.id);
    for (BrushId id : sel) if (const Brush* b = doc_.find(id)) bs.push_back(*b);
    for (MathPrimId id : doc_.withCombos(doc_.primSelection())) if (const MathPrim* p = doc_.findPrim(id)) ps.push_back(*p);
    for (LightId id : doc_.lightSelection()) if (const Light* l = doc_.findLight(id)) ls.push_back(*l);
    for (PropId id : doc_.propSelection()) if (const Prop* q = doc_.findProp(id)) qs.push_back(*q);
}

bool Editor::applyReplicate(const ReplicateSpec& spec) {
    std::vector<Brush> bs;
    std::vector<MathPrim> ps;
    std::vector<Light> ls;
    std::vector<Prop> qs;
    replicateSource(bs, ps, ls, qs);
    return commitCopies(replicate(bs, ps, ls, spec, qs, modelFinder_), "Multi-Replicate");
}

RandomReplicateSpec Editor::randSpecNow() const {
    RandomReplicateSpec r = randSpec_;
    r.brushGrid = grid_.snap ? grid_.size : 0; // brushes stay on the grid while snapping is on
    return r;
}

void Editor::openRandomReplicate() {
    if (doc_.selection().empty() && doc_.primSelection().empty() && doc_.lightSelection().empty() && doc_.propSelection().empty()) return;
    tool_->cancel();
    replicating_ = false; // one replicate window at a time
    randomizing_ = true;
    randomFocus_ = true;
}

bool Editor::applyRandomReplicate(const RandomReplicateSpec& spec) {
    std::vector<Brush> bs;
    std::vector<MathPrim> ps;
    std::vector<Light> ls;
    std::vector<Prop> qs;
    replicateSource(bs, ps, ls, qs);
    return commitCopies(randomReplicate(bs, ps, ls, spec, qs, modelFinder_), "Random Replicate");
}

bool Editor::commitCopies(const std::vector<ReplicateCopy>& copies, const char* name) {
    if (copies.empty()) return false;
    // One command: every copy, each with its own groups and combinations.
    Command c;
    c.name = name;
    c.selectionBefore = doc_.selection();
    c.lightSelectionBefore = doc_.lightSelection();
    c.primSelectionBefore = doc_.primSelection();
    c.lightSelectionAfter = std::set<LightId>{};
    c.primSelectionAfter = std::set<MathPrimId>{};
    c.propSelectionBefore = doc_.propSelection();
    c.propSelectionAfter = std::set<PropId>{};
    std::uint32_t nextCombo = newComboId(doc_.prims());
    for (const ReplicateCopy& cp : copies) {
        std::map<std::uint32_t, std::uint32_t> newGroup, newCombo;
        for (Brush b : cp.brushes) {
            b.id = doc_.newId();
            if (b.group) {
                auto it = newGroup.find(b.group);
                if (it == newGroup.end()) {
                    const GroupId g = doc_.newGroupId();
                    Group grp;
                    if (auto gi = doc_.groups().find(b.group); gi != doc_.groups().end()) grp = gi->second;
                    grp.id = g;
                    c.groupChanges.push_back({g, std::nullopt, grp});
                    it = newGroup.emplace(b.group, g).first;
                }
                b.group = it->second;
            }
            c.selectionAfter.insert(b.id);
            c.changes.push_back({b.id, std::nullopt, std::move(b)});
        }
        for (MathPrim p : cp.prims) {
            p.id = doc_.newPrimId();
            if (p.combo) {
                auto it = newCombo.find(p.combo);
                if (it == newCombo.end()) it = newCombo.emplace(p.combo, nextCombo++).first;
                p.combo = it->second;
            }
            c.primSelectionAfter->insert(p.id);
            c.primChanges.push_back({p.id, std::nullopt, std::move(p)});
        }
        for (Prop q : cp.props) {
            q.id = doc_.newPropId();
            c.propSelectionAfter->insert(q.id);
            c.propChanges.push_back({q.id, std::nullopt, std::move(q)});
        }
        for (Light l : cp.lights) {
            l.id = doc_.newLightId();
            c.lightSelectionAfter->insert(l.id);
            c.lightChanges.push_back({l.id, std::nullopt, std::move(l)});
        }
    }
    doc_.apply(std::move(c));
    return true;
}

void Editor::drawReplicateDialog() {
    if (askGround_) { ImGui::OpenPopup("Snap to ground"); askGround_ = false; }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Snap to ground", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Put the selected primitives' lowest point at this height");
        ImGui::SetNextItemWidth(150);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputDouble("Z (units)", &groundHeight_, 0, 0, "%g");
        const bool apply = ImGui::Button("Apply (Return)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_;
        if (apply) { snapToGround(groundHeight_); ImGui::CloseCurrentPopup(); }
        else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // Multi-Replicate: a window, not a dialog, so the views stay free to look around
    // (pan, zoom, orbit, fly) before the copies are made.
    if (!replicating_ || scriptPreview_) return;
    ImGui::SetNextWindowPos(ImVec2(center.x, center.y * 0.4f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
    if (replicateFocus_) { ImGui::SetNextWindowFocus(); }
    bool open = true;
    if (ImGui::Begin("Multi-Replicate", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ReplicateSpec& r = repSpec_;
        ImGui::TextUnformatted("Copies of the selection, each one step further than the last");
        ImGui::TextDisabled("look around in the views freely; Return creates, Esc cancels");
        ImGui::SetNextItemWidth(120);
        if (replicateFocus_) ImGui::SetKeyboardFocusHere();
        ImGui::InputInt("copies", &r.copies);
        r.copies = std::clamp(r.copies, 1, 1000);
        auto vec = [](const char* label, Vec3& v) {
            double a[3] = {v.x, v.y, v.z};
            ImGui::SetNextItemWidth(240);
            ImGui::InputScalarN(label, ImGuiDataType_Double, a, 3, nullptr, nullptr, "%g");
            v = {a[0], a[1], a[2]};
        };
        vec("offset per copy (units)", r.offset);
        vec("turn per copy (degrees X Y Z)", r.rotate);
        vec("scale per copy (%)", r.scale);
        for (int a = 0; a < 3; ++a) r.scale[a] = std::clamp(r.scale[a], 1.0, 1000.0);
        ImGui::Checkbox("scale the offset too", &r.scaleOffset);
        ImGui::TextUnformatted("turn and scale about:");
        ImGui::RadioButton("the selection (moves with the copies)", &repPivot_, 0);
        ImGui::RadioButton("the world origin", &repPivot_, 1);
        ImGui::RadioButton("a point", &repPivot_, 2);
        if (repPivot_ == 2) vec("point", r.point);
        r.pivot = (ReplicateSpec::Pivot)repPivot_;
        ImGui::TextDisabled("brushes only move (they stay on the grid); primitives, props");
        ImGui::TextDisabled("and lights also turn; primitives and props also scale");
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const bool apply = ImGui::Button("Create (Return)") ||
                           (focused && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)));
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel (Esc)") || (focused && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) || closeDialogs_;
        if (apply) { applyReplicate(r); replicating_ = false; }
        else if (cancel) replicating_ = false;
    }
    ImGui::End();
    replicateFocus_ = false;
    if (!open) replicating_ = false;
}

void Editor::drawRandomReplicateWindow() {
    // Random Replicate: a window like Multi-Replicate's, so the views stay free to look around.
    if (!randomizing_ || scriptPreview_) return;
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(ImVec2(center.x, center.y * 0.4f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
    if (randomFocus_) ImGui::SetNextWindowFocus();
    bool open = true;
    if (ImGui::Begin("Random Replicate", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        RandomReplicateSpec& r = randSpec_;
        ImGui::TextUnformatted("Copies scattered around the selection, each with its own random");
        ImGui::TextUnformatted("offset, turn and size inside these ranges");
        ImGui::TextDisabled("look around in the views freely; Return creates, Esc cancels");
        ImGui::SetNextItemWidth(120);
        if (randomFocus_) ImGui::SetKeyboardFocusHere();
        ImGui::InputInt("copies", &r.copies);
        r.copies = std::clamp(r.copies, 1, 2000);
        auto vec = [](const char* label, Vec3& v) {
            double a[3] = {v.x, v.y, v.z};
            ImGui::SetNextItemWidth(240);
            ImGui::InputScalarN(label, ImGuiDataType_Double, a, 3, nullptr, nullptr, "%g");
            v = {std::fabs(a[0]), std::fabs(a[1]), std::fabs(a[2])};
        };
        vec("offset up to +- (units X Y Z)", r.spread);
        vec("turn up to +- (degrees X Y Z)", r.turn);
        for (int a = 0; a < 3; ++a) r.turn[a] = std::min(r.turn[a], 180.0);
        double sz[2] = {r.sizeMin, r.sizeMax};
        ImGui::SetNextItemWidth(160);
        ImGui::InputScalarN("size from / to (%)", ImGuiDataType_Double, sz, 2, nullptr, nullptr, "%g");
        r.sizeMin = std::clamp(sz[0], 1.0, 1000.0);
        r.sizeMax = std::clamp(sz[1], 1.0, 1000.0);
        bool uneven = !r.uniform;
        ImGui::Checkbox("each axis its own size", &uneven);
        r.uniform = !uneven;
        int seed = (int)r.seed;
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt("seed", &seed);
        r.seed = (std::uint32_t)std::max(0, seed);
        ImGui::SameLine();
        if (ImGui::Button("New seed")) r.seed = (r.seed * 1103515245u + 12345u) % 1000000u; // the same seed gives the same copies
        ImGui::TextDisabled("brushes only move (by whole grid steps while snapping is on);");
        ImGui::TextDisabled("primitives and props also turn and size; lights also turn.");
        ImGui::TextDisabled("The copies stay selected: Cmd+J then drops them onto the land.");
        const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const bool apply = ImGui::Button("Create (Return)") ||
                           (focused && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)));
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel (Esc)") || (focused && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) || closeDialogs_;
        if (apply) { applyRandomReplicate(randSpecNow()); randomizing_ = false; }
        else if (cancel) randomizing_ = false;
    }
    ImGui::End();
    randomFocus_ = false;
    if (!open) randomizing_ = false;
}

} // namespace scal
