// Editor (part of the class, split by topic; see editor.hpp): Decals (v0.12.5): Properties,
// the Decal... dialog, order, sizes from the picture, and the scene's decal meshes.
#include "app/editor.hpp"
#include "core/decal.hpp"
#include "ui/layout.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace scal {

void Editor::editDecals(const char* name, const std::function<void(Decal&)>& fn) {
    if (doc_.decalSelection().empty()) {
        if (tool_ == &decalTool_)
            if (Decal* d = decalTool_.pending()) {
                fn(*d);
                d->order = std::clamp(d->order, kDecalOrderMin, kDecalOrderMax);
            }
        return;
    }
    std::vector<Decal> out;
    for (DecalId id : doc_.decalSelection())
        if (const Document::DecalRef r = doc_.findDecal(id)) {
            Decal d = *r.decal;
            fn(d);
            d.order = std::clamp(d.order, kDecalOrderMin, kDecalOrderMax); // the file reader's range
            if (d != *r.decal) out.push_back(d);
        }
    if (!out.empty()) doc_.apply(doc_.makeModifyDecals(out, name));
}

void Editor::decalOrder(int step) {
    editDecals(step > 0 ? "Bring decal forward" : "Send decal back", [step](Decal& d) { d.order = decalOrderFrom((double)d.order + step); });
}

void Editor::decalSizeFromPicture() {
    editDecals("Decal size from its picture", [this](Decal& d) {
        int w = 64, h = 64;
        double k = 1;
        if (decalPicture_ && decalPicture_(d.material, w, h, k)) scal::decalSizeFromPicture(d, w, h, k);
    });
}

void Editor::addDecals(Scene& s) {
    if (!doc_.decalsShown()) return;
    if (decalMeshRev_ != doc_.revision()) {
        decalMeshes_.clear();
        decalMeshRev_ = doc_.revision();
    }
    for (const Brush& b : doc_.brushes())
        for (const Decal& d : b.decals) {
            if (!doc_.visible(b, d)) continue;
            Scene::SceneDecal sd;
            sd.decal = d;
            sd.selected = doc_.decalSelection().count(d.id) > 0;
            auto it = decalMeshes_.find(d.id);
            if (it == decalMeshes_.end() || it->second.first != d) {
                auto tris = std::make_shared<const std::vector<DecalTri>>(
                    decalMesh(d, doc_.brushes(), [this](const Brush& x) { return doc_.visible(x); }));
                it = decalMeshes_.insert_or_assign(d.id, std::make_pair(d, tris)).first;
            }
            sd.tris = it->second.second;
            s.decals.push_back(std::move(sd));
        }
    std::stable_sort(s.decals.begin(), s.decals.end(), [](const Scene::SceneDecal& a, const Scene::SceneDecal& b) {
        return a.decal.order != b.decal.order ? a.decal.order < b.decal.order : a.decal.id < b.decal.id;
    });
}

void Editor::drawDecalPanel() {
    ImGui::PushID("decals");
    const std::set<DecalId>& sel = doc_.decalSelection();
    const Decal* pend = sel.empty() && tool_ == &decalTool_ ? decalTool_.pending() : nullptr;
    const Decal* first = nullptr;
    if (!sel.empty())
        if (const Document::DecalRef r = doc_.findDecal(*sel.begin())) first = r.decal;
    if (pend) first = pend;
    if (!first) {
        ImGui::TextWrapped("Decal tool: pick a picture in the Textures panel (now: %s), then click a surface in the 3D "
                           "view; Return places it. Click a decal to select it.",
                           currentTexture_.empty() ? "none" : currentTexture_.c_str());
        ImGui::PopID();
        return;
    }
    // the fields hold their own copy while being typed in
    const std::set<DecalId> key = pend ? std::set<DecalId>{0} : sel;
    if ((key != decalPanelFor_ || doc_.revision() != decalPanelRev_ || pend) && !ImGui::IsAnyItemActive()) {
        decalPanel_ = *first;
        decalPanelFor_ = key;
        decalPanelRev_ = doc_.revision();
    }
    Decal& b = decalPanel_;
    if (pend) ImGui::Text("next decal: %s", b.material.c_str());
    else if (sel.size() == 1) ImGui::Text("decal: %s", b.material.c_str());
    else ImGui::Text("%zu decals selected", sel.size());
    if (!currentTexture_.empty() && currentTexture_ != b.material && ImGui::SmallButton("use the current texture")) {
        const std::string m = currentTexture_;
        editDecals("Decal picture", [m](Decal& d) { d.material = m; });
    }
    const bool one = pend || sel.size() == 1;
    ImGui::PushItemWidth(220);
    if (one) {
        double p[3] = {b.pos.x, b.pos.y, b.pos.z};
        ImGui::InputScalarN("position", ImGuiDataType_Double, p, 3, nullptr, nullptr, "%g");
        b.pos = {p[0], p[1], p[2]};
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const Vec3 v = b.pos;
            editDecals("Move decal", [v](Decal& d) { d.pos = v; });
        }
        ImGui::InputDouble("width", &b.width, 0, 0, "%g");
        if (ImGui::IsItemDeactivatedAfterEdit() && b.width > 0) {
            const double v = b.width;
            editDecals("Size decal", [v](Decal& d) { d.width = v; });
        }
        ImGui::InputDouble("height", &b.height, 0, 0, "%g");
        if (ImGui::IsItemDeactivatedAfterEdit() && b.height > 0) {
            const double v = b.height;
            editDecals("Size decal", [v](Decal& d) { d.height = v; });
        }
        double rot = decalRotation(b);
        ImGui::InputDouble("turn", &rot, 0, 0, "%g");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Degrees, counter-clockwise seen from the front");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            setDecalRotation(b, rot);
            editDecals("Turn decal", [rot](Decal& d) { setDecalRotation(d, rot); });
        }
    }
    ImGui::InputDouble("reach", &b.reach, 0, 0, "%g");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How far in front of and behind its surface the decal lands (cm): surfaces in that\n"
                          "box that face it take it. Default %g (4 Hammer units).", kDefaultDecalReach);
    if (ImGui::IsItemDeactivatedAfterEdit() && b.reach >= 0) {
        const double v = b.reach;
        editDecals("Decal reach", [v](Decal& d) { d.reach = v; });
    }
    if (one) {
        ImGui::InputInt("order", &b.order);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Higher is drawn on top; equal ones in the order they were placed.\n"
                              "Bring forward / Send back change it by 1. From %d to %d.", kDecalOrderMin, kDecalOrderMax);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const int v = b.order;
            editDecals("Decal order", [v](Decal& d) { d.order = v; });
        }
    }
    ImGui::PopItemWidth();
    if (ImGui::Button("Bring forward")) decalOrder(+1);
    ImGui::SameLine();
    if (ImGui::Button("Send back")) decalOrder(-1);
    if (ImGui::Button("Size from its picture")) decalSizeFromPicture();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Texels x $decalscale x 1 Hammer unit, as Source sizes an infodecal");
    if (pend) {
        if (ImGui::Button("Place (Return)")) decalTool_.create(doc_);
        ImGui::SameLine();
        if (ImGui::Button("Cancel (Esc)")) decalTool_.cancel();
    }
    if (const MaterialIndex::Entry* e = textures_.material(b.material))
        if (e->mat.shader == "DecalModulate") ImGui::TextDisabled("DecalModulate: darkens / lightens what is under it");
    ImGui::PopID();
}

void Editor::drawDecalDialog() {
    if (askDecal_) {
        const Decal* d = nullptr;
        if (doc_.decalSelection().size() == 1) d = doc_.findDecal(*doc_.decalSelection().begin()).decal;
        else if (tool_ == &decalTool_) d = decalTool_.pending();
        askDecal_ = false;
        if (!d) return;
        const double v[8] = {d->pos.x, d->pos.y, d->pos.z, d->width, d->height, decalRotation(*d), d->reach, (double)d->order};
        std::copy(v, v + 8, decalBuf_);
        ImGui::OpenPopup("Decal");
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Decal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushItemWidth(240);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputScalarN("position X Y Z", ImGuiDataType_Double, decalBuf_, 3, nullptr, nullptr, "%g");
        ImGui::InputScalarN("width, height", ImGuiDataType_Double, decalBuf_ + 3, 2, nullptr, nullptr, "%g");
        ImGui::InputDouble("turn (degrees)", &decalBuf_[5], 0, 0, "%g");
        ImGui::InputDouble("reach", &decalBuf_[6], 0, 0, "%g");
        ImGui::InputDouble("order", &decalBuf_[7], 0, 0, "%g");
        ImGui::PopItemWidth();
        const bool ok = decalBuf_[3] > 0 && decalBuf_[4] > 0 && decalBuf_[6] >= 0;
        if (!ok) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "width and height must be above zero, reach not below");
        const bool apply = ImGui::Button("Apply (Return)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_;
        if (apply && ok) {
            const double* v = decalBuf_;
            editDecals("Decal", [v](Decal& d) {
                d.pos = {v[0], v[1], v[2]};
                d.width = v[3];
                d.height = v[4];
                setDecalRotation(d, v[5]);
                d.reach = v[6];
                d.order = decalOrderFrom(v[7]);
            });
            ImGui::CloseCurrentPopup();
        } else if (cancel)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace scal
