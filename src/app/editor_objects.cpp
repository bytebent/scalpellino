// Editor (part of the class, split by topic; see editor.hpp): Primitives and props: panels, combining, Snap to Land / Ground.
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

void Editor::drawPropsPanel() {
    if (ImGui::Begin("Props###props", nullptr, ImGuiWindowFlags_NoCollapse)) {
        const auto& names = models_.store().names();
        ImGui::Text("model: %s", propTool_.model.empty() ? "(none picked)" : propTool_.model.c_str());
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##propsearch", "search by name", propSearch_, sizeof propSearch_);
        std::string needle = propSearch_;
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        const float footer = ImGui::GetFrameHeightWithSpacing() * 1.4f;
        if (ImGui::BeginChild("##proplist", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
            size_t shown = 0;
            for (const std::string& name : names) {
                std::string low = name;
                std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (!needle.empty() && low.find(needle) == std::string::npos) continue;
                ++shown;
                if (ImGui::Selectable(name.c_str(), name == propTool_.model)) {
                    propTool_.model = name;
                    if (tool_ != &propTool_) setTool(&propTool_);
                }
                if (ImGui::IsItemHovered()) {
                    const std::string why = models_.store().problem(name);
                    if (!why.empty()) ImGui::SetTooltip("%s\n%s", name.c_str(), why.c_str());
                }
            }
            if (shown == 0) {
                if (bottegaRoot_.empty()) ImGui::TextWrapped("No Bottega folder yet. File > Bottega folder... then put .glb models in its models folder.");
                else if (names.empty()) ImGui::TextWrapped("No models found in the Bottega's models folder (.glb files).");
                else ImGui::TextDisabled("No model matches.");
            }
        }
        ImGui::EndChild();
        if (ImGui::Button("Rescan")) rescanBottega();
        ImGui::SameLine();
        ImGui::TextDisabled("%zu models", names.size());
        const auto& clashes = models_.store().clashes();
        if (!clashes.empty()) {
            // Two files with the same model name: the first keeps it, the rest are listed.
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%zu file(s) left out: name already used (hover)", clashes.size());
            if (ImGui::IsItemHovered()) {
                std::string tip = "Rename these so each model has its own name, then Rescan:";
                for (const std::string& c : clashes) tip += "\n" + c;
                ImGui::SetTooltip("%s", tip.c_str());
            }
        }
    }
    ImGui::End();
}

void Editor::drawPropPanel() {
    auto vec3Edit = [](const char* label, Vec3& v) {
        double a[3] = {v.x, v.y, v.z};
        ImGui::SetNextItemWidth(220);
        ImGui::InputScalarN(label, ImGuiDataType_Double, a, 3, nullptr, nullptr, "%g");
        v = {a[0], a[1], a[2]};
        return ImGui::IsItemDeactivatedAfterEdit();
    };
    ImGui::PushID("props");
    const std::set<PropId>& sel = doc_.propSelection();
    if (!sel.empty()) {
        if ((sel != propBufFor_ || doc_.revision() != propBufRev_) && !ImGui::IsAnyItemActive()) {
            if (const Prop* p = doc_.findProp(*sel.begin())) propBuf_ = *p;
            propBufFor_ = sel;
            propBufRev_ = doc_.revision();
        }
        Prop& b = propBuf_;
        if (sel.size() == 1) ImGui::Text("prop: %s", b.model.c_str());
        else ImGui::Text("%zu props selected", sel.size());
        if (const Model* m = models_.find(b.model)) {
            if (m->triangles() > Model::kTriangleBudget)
                ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "heavy model: %zu triangles", (size_t)m->triangles());
        } else ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "model not found (shown as a box)");
        std::vector<std::function<void(Prop&)>> fns;
        if (sel.size() == 1) {
            if (vec3Edit("position", b.pos)) { const Vec3 v = b.pos; fns.push_back([v](Prop& p) { p.pos = v; }); }
            if (vec3Edit("rotation X Y Z", b.rot)) { const Vec3 v = b.rot; fns.push_back([v](Prop& p) { p.rot = v; }); }
            ImGui::Checkbox("scale per axis", &propPerAxis_);
            if (propPerAxis_) {
                if (vec3Edit("scale", b.scale)) { const Vec3 v = b.scale; fns.push_back([v](Prop& p) { p.scale = v; }); }
            } else {
                double k = b.scale.x;
                ImGui::SetNextItemWidth(220);
                ImGui::InputDouble("scale", &k, 0, 0, "%g");
                if (ImGui::IsItemDeactivatedAfterEdit() && k > 0) fns.push_back([k](Prop& p) { p.scale = {k, k, k}; });
            }
        } else {
            double k = 1;
            ImGui::SetNextItemWidth(220);
            ImGui::InputDouble("scale all by", &k, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit() && k > 0) fns.push_back([k](Prop& p) { p.scale = p.scale * k; });
        }
        if (!propTool_.model.empty() && ImGui::SmallButton("use the picked model")) {
            const std::string m = propTool_.model;
            fns.push_back([m](Prop& p) { p.model = m; });
        }
        if (!fns.empty()) {
            std::vector<Prop> out;
            for (PropId id : sel)
                if (const Prop* p = doc_.findProp(id)) {
                    Prop c = *p;
                    for (auto& f : fns) f(c);
                    out.push_back(c);
                }
            doc_.apply(doc_.makeModifyProps(out, "Edit prop"));
        }
        ImGui::Separator();
    }
    if (tool_ == &propTool_) {
        ImGui::TextDisabled("next prop");
        ImGui::Text("model: %s", propTool_.model.empty() ? "(pick one in the Props panel)" : propTool_.model.c_str());
        ImGui::PushItemWidth(220);
        vec3Edit("turn X Y Z", propTool_.rot);
        ImGui::InputDouble("scale##next", &propTool_.scale, 0, 0, "%g");
        if (propTool_.scale <= 0) propTool_.scale = 1;
        if (propTool_.hasPending()) {
            if (auto pp = propTool_.pending()) {
                if (!ImGui::IsAnyItemActive()) { propPend_[0] = pp->pos.x; propPend_[1] = pp->pos.y; propPend_[2] = pp->pos.z; }
                ImGui::InputScalarN("place at", ImGuiDataType_Double, propPend_, 3, nullptr, nullptr, "%g");
                if (ImGui::IsItemDeactivatedAfterEdit()) propTool_.setPending({propPend_[0], propPend_[1], propPend_[2]});
            }
            if (ImGui::Button("Place (Return)")) propTool_.create(doc_);
            ImGui::SameLine();
            if (ImGui::Button("Cancel (Esc)")) propTool_.cancel();
        } else {
            ImGui::TextDisabled("click a surface in 3D or a spot in a 2D view");
            if (ImGui::Button("Start at the origin") && !propTool_.model.empty()) propTool_.setPending({0, 0, 0});
        }
        ImGui::PopItemWidth();
    }
    ImGui::PopID();
}

void Editor::combinePrims() {
    if (doc_.primSelection().size() < 2) { worldError_ = "select two or more primitives to combine"; return; }
    const std::uint32_t id = newComboId(doc_.prims());
    std::vector<MathPrim> out;
    for (MathPrimId pid : doc_.primSelection())
        if (const MathPrim* p = doc_.findPrim(pid)) {
            MathPrim c = *p;
            c.combo = id;
            out.push_back(c);
        }
    tool_->cancel();
    doc_.apply(doc_.makeModifyPrims(out, "Combine"));
}

void Editor::snapToLand() {
    auto objects = selectedObjects(doc_);
    if (objects.empty() && doc_.propSelection().empty()) { landMessage_ = "select primitives or props to snap to land"; return; }
    tool_->cancel();
    // Several selected objects land lowest first. One lands on another selected object only
    // if it started entirely above it (a stack stays a stack); selected objects that
    // overlap side by side, like scattered copies, all go down to what is below them
    // instead of piling up on each other (v0.10.2).
    struct Item {
        bool isProp = false;
        std::set<MathPrimId> prims; // the primitive object's pieces
        PropId prop = 0;
        double bottom = 0, top = 0;  // where it started
        bool landed = false;
    };
    std::vector<Item> items;
    std::set<MathPrimId> selPrims;
    for (const auto& obj : objects) {
        Item it;
        AABB b;
        for (const MathPrim* p : obj) {
            it.prims.insert(p->id);
            selPrims.insert(p->id);
            const AABB pb = primBounds(*p);
            b.add(pb.min);
            b.add(pb.max);
        }
        it.bottom = objectBottom(obj);
        it.top = b.max.z;
        items.push_back(std::move(it));
    }
    for (PropId id : doc_.propSelection())
        if (const Prop* q = doc_.findProp(id)) {
            Item it;
            it.isProp = true;
            it.prop = id;
            const Model* mdl = models_.find(q->model);
            it.bottom = propBottom(*q, mdl);
            it.top = propBounds(*q, mdl).max.z;
            items.push_back(std::move(it));
        }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.bottom < b.bottom; });

    std::vector<const Brush*> brushes;
    for (const Brush& b : doc_.brushes())
        if (doc_.visible(b)) brushes.push_back(&b);
    std::vector<MathPrim> work = doc_.prims();
    std::vector<Prop> propsWork = doc_.props();
    std::set<MathPrimId> movedPrims;
    std::set<PropId> movedProps;
    std::map<MathPrimId, const Item*> primItem;
    std::map<PropId, const Item*> propItem;
    for (const Item& it : items) {
        if (it.isProp) propItem[it.prop] = &it;
        else for (MathPrimId id : it.prims) primItem[id] = &it;
    }
    // Whether `other` is something `me` may land on.
    auto counts = [](const Item& me, const Item* other) {
        if (!other) return true;                      // not selected
        if (other == &me) return false;
        return other->landed && other->top <= me.bottom + 1e-6; // selected: only if it started below
    };
    int nothing = 0;
    for (Item& it : items) {
        std::vector<const MathPrim*> me, otherPrims;
        for (const MathPrim& p : work) {
            if (it.prims.count(p.id)) me.push_back(&p);
            else if (doc_.primsShown()) { // hidden ones are not landed on
                auto f = primItem.find(p.id);
                if (counts(it, f == primItem.end() ? nullptr : f->second)) otherPrims.push_back(&p);
            }
        }
        std::vector<PropRef> otherProps;
        Prop* myProp = nullptr;
        for (Prop& q : propsWork) {
            if (it.isProp && q.id == it.prop) { myProp = &q; continue; }
            if (!doc_.propsShown()) continue; // hidden ones are not landed on
            auto f = propItem.find(q.id);
            if (counts(it, f == propItem.end() ? nullptr : f->second)) otherProps.push_back({&q, models_.find(q.model)});
        }
        std::optional<double> mv;
        if (it.isProp) {
            if (!myProp) continue;
            mv = propLandingMove({myProp, models_.find(myProp->model)}, brushes, otherPrims, otherProps);
        } else {
            mv = landingMove(me, brushes, otherPrims, otherProps);
        }
        it.landed = true;
        if (!mv) { ++nothing; continue; }
        if (it.isProp) {
            myProp->pos.z = cleanNumber(myProp->pos.z + *mv, 1e-6);
            movedProps.insert(it.prop);
        } else {
            for (MathPrim& p : work)
                if (it.prims.count(p.id)) {
                    p.pos.z = cleanNumber(p.pos.z + *mv, 1e-6);
                    movedPrims.insert(p.id);
                }
        }
    }
    std::vector<MathPrim> out;
    for (const MathPrim& p : work) if (movedPrims.count(p.id)) out.push_back(p);
    std::vector<Prop> propsOut;
    for (const Prop& q : propsWork) if (movedProps.count(q.id)) propsOut.push_back(q);
    landMessage_ = nothing ? std::to_string(nothing) + " object(s) had nothing below them" : "";
    if (out.empty() && propsOut.empty()) return;
    Command c = out.empty() ? doc_.makeModifyProps(propsOut, "Snap to land") : doc_.makeModifyPrims(out, "Snap to land");
    if (!out.empty() && !propsOut.empty()) {
        Command pc = doc_.makeModifyProps(propsOut, "Snap to land");
        c.propChanges = std::move(pc.propChanges);
    }
    doc_.apply(std::move(c));
}

void Editor::snapToGround(double height) {
    std::vector<MathPrim> out;
    for (const auto& obj : selectedObjects(doc_)) {
        const double dz = height - objectBottom(obj);
        for (const MathPrim* p : obj) {
            MathPrim c = *p;
            c.pos.z = cleanNumber(c.pos.z + dz, 1e-6);
            out.push_back(c);
        }
    }
    std::vector<Prop> propsOut;
    for (PropId id : doc_.propSelection())
        if (const Prop* q = doc_.findProp(id)) {
            Prop c = *q;
            c.pos.z = cleanNumber(c.pos.z + height - propBottom(*q, models_.find(q->model)), 1e-6);
            propsOut.push_back(c);
        }
    if (out.empty() && propsOut.empty()) return;
    tool_->cancel();
    Command c = out.empty() ? doc_.makeModifyProps(propsOut, "Snap to ground") : doc_.makeModifyPrims(out, "Snap to ground");
    if (!out.empty() && !propsOut.empty()) c.propChanges = doc_.makeModifyProps(propsOut, "").propChanges;
    doc_.apply(std::move(c));
}

void Editor::setPrimOp(PrimOp op) {
    std::vector<MathPrim> out;
    for (MathPrimId pid : doc_.primSelection())
        if (const MathPrim* p = doc_.findPrim(pid))
            if (p->combo && p->op != op) {
                MathPrim c = *p;
                c.op = op;
                out.push_back(c);
            }
    if (!out.empty()) doc_.apply(doc_.makeModifyPrims(out, "Combine mode"));
}

void Editor::drawPrimPanel() {
    static const char* kindNames[] = {"sphere", "cube", "cylinder", "cone", "torus", "capsule", "disc"};
    auto texturePicker = [&](std::string& tex, bool& changed) {
        ImGui::Text("texture: %s", tex.empty() ? "(flat colour)" : tex.c_str());
        if (ImGui::SmallButton("use current texture")) { tex = currentTexture_; changed = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("flat colour")) { tex.clear(); changed = true; }
    };
    auto colorEdit = [](Vec3& c) {
        float f[3] = {(float)(c.x / 255), (float)(c.y / 255), (float)(c.z / 255)};
        if (ImGui::ColorEdit3("colour", f, ImGuiColorEditFlags_Uint8)) c = {std::round(f[0] * 255), std::round(f[1] * 255), std::round(f[2] * 255)};
        return ImGui::IsItemDeactivatedAfterEdit();
    };
    auto vec3Edit = [](const char* label, Vec3& v) {
        double a[3] = {v.x, v.y, v.z};
        ImGui::SetNextItemWidth(220);
        ImGui::InputScalarN(label, ImGuiDataType_Double, a, 3, nullptr, nullptr, "%g");
        v = {a[0], a[1], a[2]};
        return ImGui::IsItemDeactivatedAfterEdit();
    };
    const std::set<MathPrimId>& sel = doc_.primSelection();
    if (!sel.empty()) {
        if ((sel != primBufFor_ || doc_.revision() != primBufRev_) && !ImGui::IsAnyItemActive()) {
            if (const MathPrim* p = doc_.findPrim(*sel.begin())) primBuf_ = *p;
            primBufFor_ = sel;
            primBufRev_ = doc_.revision();
        }
        MathPrim& b = primBuf_;
        const bool one = sel.size() == 1;
        // Own ID scope: the "new primitive" fields below use the same labels.
        ImGui::PushID("selected primitives");
        std::set<std::uint32_t> combos;
        for (MathPrimId id : sel) if (const MathPrim* p = doc_.findPrim(id)) combos.insert(p->combo);
        if (one) ImGui::Text("%s", kindNames[(int)b.kind]);
        else ImGui::Text("%zu primitives selected", sel.size());
        // What changed is applied to every selected primitive; place, turn and size only
        // when a single one is selected.
        std::vector<std::function<void(MathPrim&)>> fns;
        const char* what = "Edit primitive";
        ImGui::PushItemWidth(150);
        if (one) {
            if (vec3Edit("position", b.pos)) { const Vec3 v = b.pos; fns.push_back([v](MathPrim& p) { p.pos = v; }); }
            if (vec3Edit("rotation X Y Z", b.rot)) { const Vec3 v = b.rot; fns.push_back([v](MathPrim& p) { p.rot = v; }); }
            if (vec3Edit("size", b.size)) {
                Vec3 v = b.size;
                for (int a = 0; a < 3; ++a) v[a] = std::max(b.kind == PrimKind::Disc && a == 2 ? 0.0 : 1.0, std::fabs(v[a]));
                fns.push_back([v](MathPrim& p) { p.size = v; });
            }
        }
        if (b.kind == PrimKind::Cube) {
            ImGui::InputDouble("rounded corners", &b.round, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit()) { const double v = std::max(0.0, b.round); fns.push_back([v](MathPrim& p) { if (p.kind == PrimKind::Cube) p.round = v; }); }
        }
        if (b.kind == PrimKind::Torus) {
            ImGui::InputDouble("tube thickness (0.05-0.5)", &b.thickness, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit()) { const double v = std::clamp(b.thickness, 0.05, 0.5); fns.push_back([v](MathPrim& p) { if (p.kind == PrimKind::Torus) p.thickness = v; }); }
        }
        bool texChanged = false;
        texturePicker(b.texture, texChanged);
        if (texChanged) { const std::string t = b.texture; fns.push_back([t](MathPrim& p) { p.texture = t; }); }
        if (colorEdit(b.color)) { const Vec3 c = b.color; fns.push_back([c](MathPrim& p) { p.color = c; }); }
        ImGui::InputDouble("texture repeats", &b.texScale, 0, 0, "%g");
        if (ImGui::IsItemDeactivatedAfterEdit()) { const double v = std::clamp(b.texScale, 0.01, 1000.0); fns.push_back([v](MathPrim& p) { p.texScale = v; }); }
        ImGui::PopItemWidth();
        if (!fns.empty()) {
            std::vector<MathPrim> out;
            for (MathPrimId id : sel)
                if (const MathPrim* p = doc_.findPrim(id)) {
                    MathPrim c = *p;
                    for (auto& f : fns) f(c);
                    if (c != *p) out.push_back(c);
                }
            if (!out.empty()) doc_.apply(doc_.makeModifyPrims(out, what));
        }
        ImGui::Separator();
        // Combining.
        const bool inCombo = !combos.count(0);
        if (inCombo && combos.size() == 1) {
            const auto members = comboMembers(doc_.prims(), *combos.begin());
            ImGui::Text("combination of %zu", members.size());
            ImGui::SetNextItemWidth(150);
            ImGui::InputDouble("blend (units, 0 = sharp)", &b.blend, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const double v = std::max(0.0, b.blend);
                std::vector<MathPrim> out;
                for (MathPrimId id : sel)
                    if (const MathPrim* p = doc_.findPrim(id)) {
                        MathPrim c = *p;
                        c.blend = v;
                        if (c != *p) out.push_back(c);
                    }
                if (!out.empty()) doc_.apply(doc_.makeModifyPrims(out, "Blend"));
            }
            ImGui::TextDisabled("how softly the selected pieces melt into the others");
            int op = (int)b.op;
            if (ImGui::RadioButton("add", one && op == 0)) setPrimOp(PrimOp::Add);
            ImGui::SameLine();
            if (ImGui::RadioButton("subtract", one && op == 1)) setPrimOp(PrimOp::Subtract);
            ImGui::SameLine();
            if (ImGui::RadioButton("intersect", one && op == 2)) setPrimOp(PrimOp::Intersect);
            ImGui::TextDisabled("pick one piece:");
            for (const MathPrim* m : members) {
                char label[64];
                std::snprintf(label, sizeof label, "%s (%s)##%u", kindNames[(int)m->kind], primOpName(m->op), m->id);
                if (ImGui::Selectable(label, one && sel.count(m->id))) doc_.setPrimSelection({m->id});
            }
            if (ImGui::SmallButton("whole combination")) doc_.setPrimSelection(doc_.withCombos(sel));
        }
        ImGui::BeginDisabled(sel.size() < 2);
        if (ImGui::Button("Combine")) combinePrims();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!inCombo && combos.size() == 1);
        if (ImGui::Button("Separate")) separatePrims();
        ImGui::EndDisabled();
        if (!worldError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", worldError_.c_str());
        if (!landMessage_.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", landMessage_.c_str());
        if (ImGui::SmallButton("Snap to land (Cmd+J)")) snapToLand();
        ImGui::TextDisabled("Delete removes, Cmd+D duplicates");
        ImGui::PopID();
        if (tool_ != &primTool_) return;
        ImGui::Separator();
    }
    PrimTool& t = primTool_;
    ImGui::PushID("new primitive");
    ImGui::TextUnformatted("New primitive");
    int kind = (int)t.templ.kind;
    for (int i = 0; i < kPrimKinds; ++i) {
        if (i % 4) ImGui::SameLine();
        ImGui::RadioButton(kindNames[i], &kind, i);
    }
    t.templ.kind = (PrimKind)kind;
    ImGui::PushItemWidth(150);
    if (t.templ.kind == PrimKind::Cube) ImGui::InputDouble("rounded corners", &t.templ.round, 0, 0, "%g");
    if (t.templ.kind == PrimKind::Torus) {
        ImGui::InputDouble("tube thickness (0.05-0.5)", &t.templ.thickness, 0, 0, "%g");
        t.templ.thickness = std::clamp(t.templ.thickness, 0.05, 0.5);
    }
    ImGui::Checkbox("flat colour (else the current texture)", &t.flat);
    colorEdit(t.templ.color);
    ImGui::InputDouble("texture repeats", &t.templ.texScale, 0, 0, "%g");
    ImGui::PopItemWidth();
    if (auto p = t.pending()) {
        ImGui::TextDisabled("box (centre, size):");
        double c[3], s[3];
        // the box as drawn (before a disc is turned to face its view)
        if (!ImGui::IsAnyItemActive()) {
            const Vec3 cc = p->pos;
            const Vec3 ss = primBounds(*p).size();
            primPend_[0] = cc.x; primPend_[1] = cc.y; primPend_[2] = cc.z;
            primPend_[3] = ss.x; primPend_[4] = ss.y; primPend_[5] = ss.z;
        }
        for (int i = 0; i < 3; ++i) { c[i] = primPend_[i]; s[i] = primPend_[3 + i]; }
        ImGui::SetNextItemWidth(220);
        ImGui::InputScalarN("centre", ImGuiDataType_Double, c, 3, nullptr, nullptr, "%g");
        const bool e1 = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetNextItemWidth(220);
        ImGui::InputScalarN("box size", ImGuiDataType_Double, s, 3, nullptr, nullptr, "%g");
        const bool e2 = ImGui::IsItemDeactivatedAfterEdit();
        for (int i = 0; i < 3; ++i) { primPend_[i] = c[i]; primPend_[3 + i] = std::fabs(s[i]); }
        if (e1 || e2) {
            int ax = 2;
            if (p->kind == PrimKind::Disc) ax = std::fabs(p->rot.y) > 1 ? 0 : std::fabs(p->rot.x) > 1 ? 1 : 2;
            t.setPending({c[0], c[1], c[2]}, {primPend_[3], primPend_[4], primPend_[5]}, ax);
        }
        if (ImGui::Button("Create (Return)")) t.create(doc_, currentTexture_, &lastBox_);
    } else {
        ImGui::TextDisabled("drag a box in a 2D view");
    }
    ImGui::PopID();
}

void Editor::separatePrims() {
    std::vector<MathPrim> out;
    for (MathPrimId pid : doc_.primSelection())
        if (const MathPrim* p = doc_.findPrim(pid))
            if (p->combo) {
                MathPrim c = *p;
                c.combo = 0;
                c.op = PrimOp::Add;
                out.push_back(c);
            }
    if (out.empty()) return;
    tool_->cancel();
    doc_.apply(doc_.makeModifyPrims(out, "Separate"));
}

} // namespace scal
