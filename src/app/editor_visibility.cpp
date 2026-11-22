// Editor (part of the class, split by topic; see editor.hpp): Views and visibility: centring, overlaps, hide / show, visgroups.
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

void Editor::centerViews() {
    if (doc_.selection().empty()) return;
    const Vec3 c = doc_.selectionBounds().center();
    for (int i = 1; i < 4; ++i) views_[i]->center2D(c[views_[i]->axU()], c[views_[i]->axV()]);
}

void Editor::allViewsToOrigin() {
    for (auto& v : views_) v->resetToOrigin();
}

void Editor::focusRegion(const AABB& region) {
    const Vec3 c = region.center(), s = region.size();
    const double size = std::max({s.x, s.y, s.z, 16.0});
    for (int i = 1; i < 4; ++i) {
        Viewport& v = *views_[i];
        v.center2D(c[v.axU()], c[v.axV()]);
        const double fit = std::min(v.width(), v.height()) / (size * 4.0);
        v.setZoom2D(std::clamp(fit, 0.05, 8.0));
    }
    views_[0]->lookAt(c, size * 3.0 + 96.0);
}

void Editor::findOverlapsNow() {
    std::vector<Brush> visible;
    for (const Brush& b : doc_.brushes()) if (doc_.visible(b)) visible.push_back(b);
    overlaps_ = findOverlaps(visible);
    overlapsOpen_ = true;
    overlapIndex_ = -1;
    if (!overlaps_.empty()) focusOverlap(0);
}

void Editor::focusOverlap(int i) {
    if (overlaps_.empty()) return;
    const int n = (int)overlaps_.size();
    overlapIndex_ = ((i % n) + n) % n;
    const Overlap& o = overlaps_[(size_t)overlapIndex_];
    std::set<BrushId> sel;
    if (doc_.find(o.a)) sel.insert(o.a);
    if (doc_.find(o.b)) sel.insert(o.b);
    tool_->cancel();
    doc_.setSelection(sel);
    focusRegion(o.region);
}

void Editor::drawOverlapsWindow() {
    if (!overlapsOpen_) return;
    ImGui::SetNextWindowSize(ImVec2(380, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Overlapping brushes", &overlapsOpen_)) {
        if (ImGui::Button("Search again")) findOverlapsNow();
        ImGui::SameLine();
        if (overlaps_.empty()) {
            ImGui::TextUnformatted("No brushes cut into each other.");
        } else {
            if (ImGui::Button("< Prev")) focusOverlap(overlapIndex_ - 1);
            ImGui::SameLine();
            if (ImGui::Button("Next >")) focusOverlap(overlapIndex_ + 1);
            ImGui::SameLine();
            ImGui::Text("%d / %zu", overlapIndex_ + 1, overlaps_.size());
            ImGui::TextDisabled("Touching faces don't count. Click a row to jump there.");
            if (ImGui::BeginChild("##ov")) {
                for (size_t i = 0; i < overlaps_.size(); ++i) {
                    const Overlap& o = overlaps_[i];
                    char row[128];
                    std::snprintf(row, sizeof row, "brush %u  x  brush %u    shared %.0f units\xC2\xB3", o.a, o.b, o.volume);
                    if (ImGui::Selectable(row, (int)i == overlapIndex_)) focusOverlap((int)i);
                }
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

void Editor::dropInvisibleFromSelection() {
    std::set<BrushId> sel;
    for (BrushId id : doc_.selection())
        if (doc_.visible(id)) sel.insert(id);
    if (sel.size() != doc_.selection().size()) doc_.setSelection(sel);
}

void Editor::hideSelected() {
    if (doc_.selection().empty()) return;
    tool_->cancel();
    std::vector<Brush> out;
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id)) { Brush c = *b; c.hidden = true; out.push_back(std::move(c)); }
    Command c = doc_.makeModify(out, "Hide");
    c.selectionAfter.clear();
    doc_.apply(std::move(c));
}

void Editor::hideUnselected() {
    if (doc_.selection().empty()) return;
    tool_->cancel();
    std::vector<Brush> out;
    for (const Brush& b : doc_.brushes())
        if (!doc_.selection().count(b.id) && doc_.visible(b)) { Brush c = b; c.hidden = true; out.push_back(std::move(c)); }
    if (!out.empty()) doc_.apply(doc_.makeModify(out, "Hide unselected"));
}

void Editor::showAll() {
    std::vector<Brush> out;
    for (const Brush& b : doc_.brushes())
        if (b.hidden) { Brush c = b; c.hidden = false; out.push_back(std::move(c)); }
    if (!out.empty()) doc_.apply(doc_.makeModify(out, "Show all"));
    std::vector<Visgroup> vg = doc_.visgroups();
    bool changed = false;
    for (Visgroup& v : vg) if (!v.visible) { v.visible = true; changed = true; }
    if (changed) doc_.setVisgroups(vg);
    doc_.setPrimsShown(true); // View > Show primitives / props are part of "everything"
    doc_.setPropsShown(true);
}

void Editor::setVisgroupVisible(std::uint32_t id, bool visible) {
    std::vector<Visgroup> vg = doc_.visgroups();
    for (Visgroup& v : vg) if (v.id == id) v.visible = visible;
    doc_.setVisgroups(vg);
    dropInvisibleFromSelection();
}

void Editor::drawVisgroupsPanel() {
    if (ImGui::Begin("Visgroups###visgroups", nullptr, ImGuiWindowFlags_NoCollapse)) {
        size_t hidden = 0;
        for (const Brush& b : doc_.brushes()) hidden += b.hidden;
        if (ImGui::Button("Hide selected (H)")) hideSelected();
        ImGui::SameLine();
        if (ImGui::Button("Hide others (Shift+H)")) hideUnselected();
        if (ImGui::Button("Show all (Cmd+U)")) showAll();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu hidden", hidden);
        ImGui::Separator();
        ImGui::TextUnformatted("Visgroups");
        // The selected brushes and decals (v0.12.5) into visgroup `id` (0: none), one step.
        auto assign = [&](std::uint32_t id, const char* what) {
            std::map<BrushId, Brush> out;
            for (BrushId bid : doc_.selection())
                if (const Brush* b = doc_.find(bid)) { out[bid] = *b; out[bid].visgroup = id; }
            for (DecalId did : doc_.decalSelection())
                if (const Document::DecalRef r = doc_.findDecal(did)) {
                    auto it = out.find(r.brush->id);
                    if (it == out.end()) it = out.emplace(r.brush->id, *r.brush).first;
                    for (Decal& d : it->second.decals) if (d.id == did) d.visgroup = id;
                }
            std::vector<Brush> v;
            for (auto& [k, b] : out) v.push_back(std::move(b));
            if (!v.empty()) doc_.apply(doc_.makeModify(v, what));
        };
        std::vector<Visgroup> vg = doc_.visgroups();
        int removeIdx = -1;
        bool changed = false;
        for (size_t i = 0; i < vg.size(); ++i) {
            Visgroup& v = vg[i];
            ImGui::PushID((int)v.id);
            size_t count = 0;
            for (const Brush& b : doc_.brushes()) {
                count += b.visgroup == v.id;
                for (const Decal& d : b.decals) count += d.visgroup == v.id;
            }
            if (ImGui::Checkbox("##vis", &v.visible)) { setVisgroupVisible(v.id, v.visible); vg = doc_.visgroups(); ImGui::PopID(); break; }
            ImGui::SameLine();
            char name[64];
            std::snprintf(name, sizeof name, "%s", v.name.c_str());
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150);
            if (ImGui::InputText("##name", name, sizeof name, ImGuiInputTextFlags_EnterReturnsTrue)) { v.name = name; changed = true; }
            ImGui::SameLine();
            ImGui::TextDisabled("%zu", count);
            ImGui::SameLine();
            if (ImGui::SmallButton("Select")) {
                std::set<BrushId> sel;
                std::set<DecalId> ds;
                for (const Brush& b : doc_.brushes()) {
                    if (b.visgroup == v.id && doc_.visible(b)) sel.insert(b.id);
                    for (const Decal& d : b.decals) if (d.visgroup == v.id && doc_.visible(b, d)) ds.insert(d.id);
                }
                doc_.setSelection(sel);
                doc_.setDecalSelection(ds);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select its visible brushes and decals");
            ImGui::SameLine();
            if (ImGui::SmallButton("+")) assign(v.id, "Add to visgroup");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add the selection to this visgroup");
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removeIdx = (int)i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete the visgroup (its brushes stay, ungrouped)");
            ImGui::PopID();
        }
        if (removeIdx >= 0) {
            const std::uint32_t id = vg[(size_t)removeIdx].id;
            std::vector<Brush> out;
            for (const Brush& b : doc_.brushes()) {
                bool touched = false;
                Brush c = b;
                if (c.visgroup == id) { c.visgroup = 0; touched = true; }
                for (Decal& d : c.decals) if (d.visgroup == id) { d.visgroup = 0; touched = true; }
                if (touched) out.push_back(c);
            }
            if (!out.empty()) doc_.apply(doc_.makeModify(out, "Remove visgroup"));
            vg.erase(vg.begin() + removeIdx);
            changed = true;
        }
        if (changed) doc_.setVisgroups(vg);
        if (vg.empty()) ImGui::TextDisabled("none yet");
        ImGui::Separator();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 140);
        ImGui::InputTextWithHint("##newvg", "name", newVisgroupName_, sizeof newVisgroupName_);
        ImGui::SameLine();
        ImGui::BeginDisabled((doc_.selection().empty() && doc_.decalSelection().empty()) || !newVisgroupName_[0]);
        if (ImGui::Button("New from selection")) {
            std::vector<Visgroup> all = doc_.visgroups();
            const std::uint32_t id = doc_.newVisgroupId();
            all.push_back({id, newVisgroupName_, true});
            doc_.setVisgroups(all);
            assign(id, "New visgroup");
            newVisgroupName_[0] = 0;
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
}

} // namespace scal
