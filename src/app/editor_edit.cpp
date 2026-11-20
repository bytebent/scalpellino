// Editor (part of the class, split by topic; see editor.hpp): Editing commands: undo, selection, duplicate, transform, precision, carve / hollow, curves.
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

void Editor::undo() { tool_->cancel(); capture_ = -1; doc_.undo(); }

void Editor::redo() { tool_->cancel(); capture_ = -1; doc_.redo(); }

void Editor::selectAll() {
    std::set<BrushId> all;
    for (const Brush& b : doc_.brushes())
        if (doc_.visible(b)) all.insert(b.id); // hidden brushes aren't selectable
    doc_.setSelection(all);
}

void Editor::selectNone() {
    tool_->cancel();
    doc_.setSelection({});
    doc_.setPropSelection({});
    doc_.setCameraSelection({});
}

void Editor::duplicate() {
    if (doc_.selection().empty() && !doc_.primSelection().empty()) {
        tool_->cancel();
        // Copies keep their combinations together, as new combinations.
        std::map<std::uint32_t, std::uint32_t> combos;
        std::uint32_t next = newComboId(doc_.prims());
        std::vector<MathPrim> copies;
        for (MathPrimId id : doc_.primSelection())
            if (const MathPrim* p = doc_.findPrim(id)) {
                MathPrim c = *p;
                c.id = 0;
                c.pos += Vec3{grid_.size, grid_.size, 0};
                if (c.combo) {
                    auto it = combos.find(c.combo);
                    if (it == combos.end()) it = combos.emplace(c.combo, next++).first;
                    c.combo = it->second;
                }
                copies.push_back(std::move(c));
            }
        doc_.apply(doc_.makeAddPrims(std::move(copies), "Duplicate primitive"));
        return;
    }
    if (doc_.selection().empty() && !doc_.propSelection().empty()) {
        tool_->cancel();
        std::vector<Prop> copies;
        for (PropId id : doc_.propSelection())
            if (const Prop* p = doc_.findProp(id)) {
                Prop c = *p;
                c.id = 0;
                c.pos += Vec3{grid_.size, grid_.size, 0};
                copies.push_back(std::move(c));
            }
        doc_.apply(doc_.makeAddProps(std::move(copies), "Duplicate prop"));
        return;
    }
    if (doc_.selection().empty() && !doc_.cameraSelection().empty()) {
        tool_->cancel();
        std::vector<Camera> copies;
        for (CameraId id : doc_.cameraSelection())
            if (const Camera* c = doc_.findCamera(id)) {
                Camera k = *c;
                k.id = 0;
                k.active = false; // a copy never takes over
                k.name = freeCameraName(doc_, copies);
                k.pos += Vec3{grid_.size, grid_.size, 0};
                copies.push_back(std::move(k));
            }
        doc_.apply(doc_.makeAddCameras(std::move(copies), "Duplicate camera"));
        return;
    }
    if (doc_.selection().empty() && !doc_.lightSelection().empty()) {
        tool_->cancel();
        std::vector<Light> copies;
        for (LightId id : doc_.lightSelection())
            if (const Light* l = doc_.findLight(id)) {
                Light c = *l;
                c.id = 0;
                c.pos += Vec3{grid_.size, grid_.size, 0};
                copies.push_back(std::move(c));
            }
        doc_.apply(doc_.makeAddLights(std::move(copies), "Duplicate light"));
        return;
    }
    if (doc_.selection().empty()) return;
    tool_->cancel();
    std::vector<Brush> copies;
    const Vec3 offset{grid_.size, grid_.size, 0};
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id)) {
            Brush c = *b;
            c.id = 0;
            c.translate(offset, true);
            copies.push_back(std::move(c));
        }
    doc_.apply(doc_.makeAdd(std::move(copies), "Duplicate"));
}

void Editor::deleteSelection() {
    if (doc_.selection().empty() && doc_.lightSelection().empty() && doc_.primSelection().empty() && doc_.propSelection().empty() &&
        doc_.cameraSelection().empty())
        return;
    tool_->cancel();
    Command c = doc_.makeDelete(doc_.selection(), "Delete");
    doc_.addLightDeletes(c, doc_.lightSelection());
    doc_.addPrimDeletes(c, doc_.primSelection());
    doc_.addPropDeletes(c, doc_.propSelection());
    doc_.addCameraDeletes(c, doc_.cameraSelection());
    doc_.apply(std::move(c));
}

bool Editor::applyTransform(const TransformSpec& t, const char* name) {
    std::vector<Brush> sel;
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id)) sel.push_back(*b);
    if (sel.empty()) return false;
    std::string err;
    std::vector<Brush> out = scal::applyTransform(sel, t, tfTexLock_, err);
    if (out.empty()) {
        tfError_ = err;
        return false;
    }
    tool_->cancel();
    doc_.apply(doc_.makeModify(out, name));
    return true;
}

void Editor::rotate90(int axis, int sign) {
    TransformSpec t;
    t.kind = TransformKind::Rotate;
    t.axis = axis;
    t.degrees = 90.0 * sign;
    t.pivot = doc_.selectionBounds().center();
    if (!applyTransform(t, "Rotate 90")) errorText_ = "Could not rotate: " + tfError_;
}

void Editor::flip(int axis) {
    TransformSpec t;
    t.kind = TransformKind::Scale;
    t.scale[axis] = -1;
    t.pivot = doc_.selectionBounds().center();
    if (!applyTransform(t, "Flip")) errorText_ = "Could not flip: " + tfError_;
}

void Editor::openTransformDialog() {
    if (doc_.selection().empty()) return;
    tfError_.clear();
    tfPivot_ = pivotMode_;
    if (pivotMode_ == 2) { tfCustom_[0] = customPivot_.x; tfCustom_[1] = customPivot_.y; tfCustom_[2] = customPivot_.z; }
    askTransform_ = true;
}

void Editor::drawTransformDialog() {
    if (askTransform_) { ImGui::OpenPopup("Transform"); askTransform_ = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Transform", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::RadioButton("Move", &tfMode_, 0); ImGui::SameLine();
    ImGui::RadioButton("Rotate", &tfMode_, 1); ImGui::SameLine();
    ImGui::RadioButton("Scale", &tfMode_, 2);
    ImGui::Separator();
    ImGui::PushItemWidth(260);
    if (tfMode_ == 0) {
        ImGui::InputScalarN("Offset X Y Z", ImGuiDataType_Double, tfMove_, 3, nullptr, nullptr, "%g");
    } else if (tfMode_ == 1) {
        static const char* axes[] = {"X", "Y", "Z"};
        ImGui::Combo("Axis", &tfAxis_, axes, 3);
        ImGui::InputDouble("Degrees", &tfDegrees_, 0, 0, "%.6g");
        ImGui::TextDisabled("positive = counter-clockwise, looking down the axis");
        for (double d : {15.0, 22.5, 30.0, 45.0, 90.0, -90.0}) {
            char b[16];
            std::snprintf(b, sizeof b, "%g", d);
            if (ImGui::SmallButton(b)) tfDegrees_ = d;
            ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::TextDisabled("grid-friendly angles: corners stay on whole numbers when sizes");
        ImGui::TextDisabled("(from the pivot) are multiples of 5, 13, 17 or 25:");
        const struct { const char* label; double rise, run; } slopes[] = {
            {"3:4:5", 3, 4}, {"4:3:5", 4, 3}, {"5:12:13", 5, 12}, {"12:5:13", 12, 5},
            {"8:15:17", 8, 15}, {"15:8:17", 15, 8}, {"7:24:25", 7, 24}, {"24:7:25", 24, 7}};
        for (const auto& sl : slopes) {
            if (ImGui::SmallButton(sl.label)) tfDegrees_ = std::atan2(sl.rise, sl.run) * 180.0 / 3.14159265358979323846;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%.4f\xC2\xB0", std::atan2(sl.rise, sl.run) * 180.0 / 3.14159265358979323846);
            ImGui::SameLine();
        }
        ImGui::NewLine();
    } else {
        ImGui::InputScalarN("Factor X Y Z", ImGuiDataType_Double, tfScale_, 3, nullptr, nullptr, "%g");
        ImGui::TextDisabled("1 = unchanged, 2 = double, -1 = mirror");
    }
    if (tfMode_ != 0) {
        ImGui::Separator();
        ImGui::TextUnformatted("Pivot");
        ImGui::RadioButton("Selection centre", &tfPivot_, 0); ImGui::SameLine();
        ImGui::RadioButton("World origin", &tfPivot_, 1); ImGui::SameLine();
        ImGui::RadioButton("Point", &tfPivot_, 2);
        if (tfPivot_ == 2) ImGui::InputScalarN("Point X Y Z", ImGuiDataType_Double, tfCustom_, 3, nullptr, nullptr, "%g");
    }
    ImGui::PopItemWidth();
    ImGui::Separator();
    ImGui::Checkbox("Texture lock (also Texture menu)", &tfTexLock_);
    if (!tfError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", tfError_.c_str());

    const bool apply = ImGui::Button("Apply (Return)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                       ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_;
    if (apply) {
        TransformSpec t;
        t.kind = tfMode_ == 0 ? TransformKind::Move : tfMode_ == 1 ? TransformKind::Rotate : TransformKind::Scale;
        t.move = {tfMove_[0], tfMove_[1], tfMove_[2]};
        t.axis = tfAxis_;
        t.degrees = tfDegrees_;
        t.scale = {tfScale_[0], tfScale_[1], tfScale_[2]};
        t.pivot = tfPivot_ == 0 ? doc_.selectionBounds().center()
                : tfPivot_ == 1 ? Vec3{} : Vec3{tfCustom_[0], tfCustom_[1], tfCustom_[2]};
        static const char* names[] = {"Move", "Rotate", "Scale"};
        if (applyTransform(t, names[tfMode_])) ImGui::CloseCurrentPopup();
    } else if (cancel) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

Vec3 Editor::pivotPoint() const {
    if (pivotMode_ == 1) return {};
    if (pivotMode_ == 2) return customPivot_;
    const AABB b = doc_.selectionBounds();
    return b.valid() ? b.center() : Vec3{};
}

void Editor::snapSelectionToGrid() {
    if (doc_.selection().empty()) return;
    tool_->cancel();
    std::vector<Brush> out;
    for (BrushId id : doc_.selection()) {
        const Brush* b = doc_.find(id);
        if (!b) continue;
        if (brushOnGrid(*b, grid_.size)) continue;
        HullResult r = snapBrushToGrid(*b, grid_.size);
        if (r.ok && b->terrain) {
            const double g = grid_.size;
            carryTerrain(*b, r.brush, [g](const Vec3& v) {
                return Vec3{std::round(v.x / g) * g, std::round(v.y / g) * g, std::round(v.z / g) * g};
            });
        }
        if (!r.ok) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%g", grid_.size);
            errorText_ = "Snap to grid (" + std::string(buf) + "): " + r.error;
            return;
        }
        out.push_back(std::move(r.brush));
    }
    if (!out.empty()) doc_.apply(doc_.makeModify(out, "Snap to grid"));
}

void Editor::drawPrecisionDialogs() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    if (askGridSize_)   { ImGui::OpenPopup("Grid size"); askGridSize_ = false; }
    if (askPivot_)      { ImGui::OpenPopup("Pivot point"); askPivot_ = false; }
    if (askVertexMove_) { ImGui::OpenPopup("Move vertices"); askVertexMove_ = false; }
    auto buttons = [&](bool& apply, bool& cancel) {
        apply = ImGui::Button("Apply (Return)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        ImGui::SameLine();
        cancel = ImGui::Button("Cancel (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_;
    };
    bool apply = false, cancel = false;

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Grid size", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(160);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputDouble("units", &gridSizeBuf_, 0, 0, "%g");
        ImGui::TextDisabled("any size, e.g. 80; below 1 turns on the microgrid");
        for (double g : {0.125, 0.25, 0.5, 1.0, 8.0, 16.0, 32.0, 64.0}) {
            char b[16]; std::snprintf(b, sizeof b, "%g", g);
            if (ImGui::SmallButton(b)) gridSizeBuf_ = g;
            ImGui::SameLine();
        }
        ImGui::NewLine();
        buttons(apply, cancel);
        if (apply && gridSizeBuf_ > 0) { grid_.setSize(gridSizeBuf_); ImGui::CloseCurrentPopup(); }
        else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Pivot point", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(260);
        ImGui::InputScalarN("X Y Z", ImGuiDataType_Double, pivotBuf_, 3, nullptr, nullptr, "%g");
        ImGui::TextDisabled("rotations and scaling (drag and Transform dialog) use this point");
        buttons(apply, cancel);
        if (apply) {
            customPivot_ = {pivotBuf_[0], pivotBuf_[1], pivotBuf_[2]};
            pivotMode_ = 2;
            ImGui::CloseCurrentPopup();
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Move vertices", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Vec3 single;
        const bool one = vertexTool_.singlePosition(single);
        if (ImGui::IsWindowAppearing()) {
            vmoveMode_ = 0;
            vmove_[0] = vmove_[1] = vmove_[2] = 0;
        }
        if (vertexTool_.terrainPointCount() > 0) {
            // Terrain points move along the terrain's normal only.
            double h = 0;
            const bool sameH = vertexTool_.singleHeight(doc_, h);
            ImGui::Text("%zu terrain point(s) selected", vertexTool_.terrainPointCount());
            ImGui::RadioButton("Raise by", &vmoveMode_, 0);
            ImGui::SameLine();
            if (ImGui::RadioButton("Set height", &vmoveMode_, 1)) vmove_[0] = sameH ? h : 0;
            ImGui::SetNextItemWidth(160);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::InputDouble("units", &vmove_[0], 0, 0, "%g");
            ImGui::TextDisabled("heights are measured from the face, along its normal");
            if (!precisionError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", precisionError_.c_str());
            buttons(apply, cancel);
            if (apply) {
                std::string err;
                if (vertexTool_.moveTerrainSelected(doc_, vmove_[0], vmoveMode_ == 1, err)) ImGui::CloseCurrentPopup();
                else precisionError_ = "Not applied: " + err;
            } else if (cancel) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::Text("%zu corner(s) selected", vertexTool_.selectedCount());
        ImGui::RadioButton("Move by", &vmoveMode_, 0);
        if (one) {
            ImGui::SameLine();
            if (ImGui::RadioButton("Set position", &vmoveMode_, 1)) { vmove_[0] = single.x; vmove_[1] = single.y; vmove_[2] = single.z; }
        } else if (vmoveMode_ == 1) vmoveMode_ = 0;
        ImGui::SetNextItemWidth(260);
        ImGui::InputScalarN("X Y Z", ImGuiDataType_Double, vmove_, 3, nullptr, nullptr, "%g");
        if (!precisionError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", precisionError_.c_str());
        buttons(apply, cancel);
        if (apply) {
            Vec3 d{vmove_[0], vmove_[1], vmove_[2]};
            if (vmoveMode_ == 1 && one) d = d - single;
            std::string err;
            if (vertexTool_.moveSelected(doc_, d, err)) ImGui::CloseCurrentPopup();
            else precisionError_ = "Not applied: " + err;
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Editor::carveWithSelection() {
    if (doc_.selection().empty()) return;
    tool_->cancel();
    const std::set<BrushId> cutters = doc_.selection();
    std::vector<Brush> modified, added;
    std::set<BrushId> deleted;
    int touched = 0;
    for (const Brush& b : doc_.brushes()) {
        if (cutters.count(b.id)) continue;
        std::vector<Brush> pieces{b};
        bool changed = false;
        for (BrushId cid : cutters) {
            const Brush* cutter = doc_.find(cid);
            std::vector<Brush> next;
            for (const Brush& p : pieces) {
                std::vector<Brush> r = carveBrush(p, *cutter);
                if (r.size() != 1 || r[0].faces.size() != p.faces.size()) changed = true;
                for (Brush& q : r) next.push_back(std::move(q));
            }
            pieces = std::move(next);
        }
        if (!changed) continue;
        ++touched;
        if (pieces.empty()) { deleted.insert(b.id); continue; }
        pieces[0].id = b.id;
        modified.push_back(pieces[0]);
        for (size_t i = 1; i < pieces.size(); ++i) added.push_back(pieces[i]);
    }
    if (touched == 0) { errorText_ = "Carve: the selection doesn't touch any other brush."; return; }
    doc_.apply(doc_.makeChange(modified, std::move(added), deleted, "Carve", &cutters));
}

void Editor::hollowSelection(double t) {
    if (doc_.selection().empty()) return;
    tool_->cancel();
    std::vector<Brush> modified, added;
    for (BrushId id : doc_.selection()) {
        const Brush* b = doc_.find(id);
        if (!b) continue;
        std::string err;
        std::vector<Brush> walls = hollowBrush(*b, t, err);
        if (walls.empty()) { shapingError_ = err; return; }
        walls[0].id = id;
        modified.push_back(walls[0]);
        for (size_t i = 1; i < walls.size(); ++i) added.push_back(walls[i]);
    }
    Command c = doc_.makeChange(modified, std::move(added), {}, "Hollow");
    doc_.apply(std::move(c));
}

void Editor::drawShapingDialogs() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    if (askClipPoints_) { ImGui::OpenPopup("Clip points"); askClipPoints_ = false; }
    if (askHollow_) { ImGui::OpenPopup("Hollow"); askHollow_ = false; }
    auto buttons = [&](bool& apply, bool& cancel) {
        apply = ImGui::Button("Apply (Return)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        ImGui::SameLine();
        cancel = ImGui::Button("Cancel (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_;
    };
    bool apply = false, cancel = false;

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Clip points", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The clip plane goes through these three points:");
        ImGui::PushItemWidth(260);
        ImGui::InputScalarN("Point 1", ImGuiDataType_Double, clipBuf_[0], 3, nullptr, nullptr, "%g");
        ImGui::InputScalarN("Point 2", ImGuiDataType_Double, clipBuf_[1], 3, nullptr, nullptr, "%g");
        ImGui::InputScalarN("Point 3", ImGuiDataType_Double, clipBuf_[2], 3, nullptr, nullptr, "%g");
        ImGui::PopItemWidth();
        ImGui::TextDisabled("Then Return in the views clips (Tools -> Clip: keep chooses the side).");
        if (!shapingError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", shapingError_.c_str());
        buttons(apply, cancel);
        if (apply) {
            const Vec3 a{clipBuf_[0][0], clipBuf_[0][1], clipBuf_[0][2]};
            const Vec3 b{clipBuf_[1][0], clipBuf_[1][1], clipBuf_[1][2]};
            const Vec3 c{clipBuf_[2][0], clipBuf_[2][1], clipBuf_[2][2]};
            if (length(cross(c - a, b - a)) < 1e-9) shapingError_ = "The three points are on one line: no plane.";
            else { setTool(&clipTool_); clipTool_.setPoints(a, b, c); ImGui::CloseCurrentPopup(); }
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Hollow", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Turn the selected brushes into walls around an empty inside.");
        ImGui::SetNextItemWidth(120);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputDouble("wall thickness", &hollowThickness_, 0, 0, "%g");
        for (double t : {4.0, 8.0, 16.0, 32.0, 64.0}) {
            char b[16]; std::snprintf(b, sizeof b, "%g", t);
            if (ImGui::SmallButton(b)) hollowThickness_ = t;
            ImGui::SameLine();
        }
        ImGui::NewLine();
        if (!shapingError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", shapingError_.c_str());
        buttons(apply, cancel);
        if (apply) {
            shapingError_.clear();
            hollowSelection(hollowThickness_);
            if (shapingError_.empty()) ImGui::CloseCurrentPopup();
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

GroupId Editor::selectedGroup() const {
    GroupId g = 0;
    for (BrushId id : doc_.selection()) {
        const Brush* b = doc_.find(id);
        if (!b || b->group == 0) return 0;
        if (g && b->group != g) return 0;
        g = b->group;
    }
    return g && doc_.group(g) ? g : 0;
}

void Editor::commitCurve() {
    const CurveParams p = curveTool_.params;
    CurveResult r = generateCurve(p);
    if (!r.ok()) { curveTool_.setMessage("can't build: " + r.error); return; }

    if (p.shape == CurveShape::CarveArch) {
        // Cut the opening out of the selected brushes (or everything it touches).
        std::set<BrushId> targets = doc_.selection();
        if (targets.empty()) for (const Brush& b : doc_.brushes()) targets.insert(b.id);
        std::vector<Brush> modified, added;
        std::set<BrushId> deleted;
        int touched = 0;
        for (BrushId id : targets) {
            const Brush* b = doc_.find(id);
            if (!b) continue;
            std::vector<Brush> pieces{*b};
            bool changed = false;
            for (const Brush& cut : r.brushes) {
                std::vector<Brush> next;
                for (const Brush& q : pieces) {
                    std::vector<Brush> c = carveBrush(q, cut);
                    if (c.size() != 1 || c[0].faces.size() != q.faces.size()) changed = true;
                    for (Brush& x : c) next.push_back(std::move(x));
                }
                pieces = std::move(next);
            }
            if (!changed) continue;
            ++touched;
            for (Brush& q : pieces) q.group = 0;
            if (pieces.empty()) { deleted.insert(id); continue; }
            pieces[0].id = id;
            modified.push_back(pieces[0]);
            for (size_t i = 1; i < pieces.size(); ++i) added.push_back(pieces[i]);
        }
        if (!touched) { curveTool_.setMessage("the opening doesn't cut through any selected brush"); return; }
        // Refill above the curve with arch slices, in the wall's texture.
        std::string wallTex = p.texture;
        if (const Brush* w = doc_.find(*targets.begin()); w && !w->faces.empty()) wallTex = w->faces[0].texture;
        for (Brush f : r.fill) {
            for (Face& face : f.faces) face.texture = wallTex;
            added.push_back(std::move(f));
        }
        Command c = doc_.makeChange(modified, std::move(added), deleted, "Carve arch");
        for (const BrushChange& ch : c.changes) if (ch.after) c.selectionAfter.insert(ch.id);
        doc_.apply(std::move(c));
        curveTool_.finished();
        return;
    }

    // A (re)generated group.
    const GroupId old = curveTool_.editingGroup();
    const GroupId gid = old ? old : doc_.newGroupId();
    for (Brush& b : r.brushes) b.group = gid;
    const std::set<BrushId> oldMembers = old ? doc_.groupMembers(old) : std::set<BrushId>{};
    Command c = doc_.makeChange({}, std::move(r.brushes), oldMembers, old ? "Edit curve" : "Create curve");
    std::set<BrushId> sel;
    for (const BrushChange& ch : c.changes) if (ch.after) sel.insert(ch.id);
    c.selectionAfter = sel;
    std::optional<Group> before;
    if (old) before = *doc_.group(old);
    c.groupChanges.push_back({gid, before, Group{gid, curveToKeys(p)}});
    doc_.apply(std::move(c));
    curveTool_.finished();
}

void Editor::editCurve() {
    const GroupId g = selectedGroup();
    if (!g) return;
    CurveParams p;
    if (!curveFromKeys(doc_.group(g)->keys, p)) { errorText_ = "This group's curve settings can't be read."; return; }
    // If the group was moved since it was made, follow it.
    const CurveResult fresh = generateCurve(p);
    AABB now, then;
    for (BrushId id : doc_.groupMembers(g)) now.add(doc_.find(id)->bounds());
    for (const Brush& b : fresh.brushes) then.add(b.bounds());
    if (now.valid() && then.valid() && length(now.size() - then.size()) < 1e-6) shiftCurve(p, now.min - then.min);
    setTool(&curveTool_);
    curveTool_.startEditing(p, g);
}

void Editor::bakeCurves() {
    std::set<GroupId> groups;
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id); b && b->group) groups.insert(b->group);
    if (groups.empty()) return;
    tool_->cancel();
    std::vector<Brush> modified;
    for (GroupId g : groups)
        for (BrushId id : doc_.groupMembers(g)) {
            Brush b = *doc_.find(id);
            b.group = 0;
            modified.push_back(std::move(b));
        }
    Command c = doc_.makeModify(modified, "Bake curve");
    for (GroupId g : groups) c.groupChanges.push_back({g, *doc_.group(g), std::nullopt});
    doc_.apply(std::move(c));
}

void Editor::drawBlockPanel() {
    ImGui::TextUnformatted("Block tool");
    static const char* shapes[] = {"Box", "Wedge", "Cylinder", "Cone", "Pyramid", "Sphere"};
    int shape = (int)blockTool_.shape;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##blockshape", &shape, shapes, 6)) blockTool_.shape = (Primitive)shape;
    const Primitive k = blockTool_.shape;
    if (k == Primitive::Cylinder || k == Primitive::Cone || k == Primitive::Sphere) {
        ImGui::SetNextItemWidth(120);
        if (ImGui::InputInt("sides", &blockTool_.sides)) blockTool_.sides = std::clamp(blockTool_.sides, 3, 64);
        ImGui::Checkbox("round corners to whole units", &blockTool_.roundToWhole);
    }
    ImGui::TextDisabled(k == Primitive::Wedge ? "slopes down to the right in the view you draw it in"
                        : k == Primitive::Box ? "drag in a 2D view, Return creates"
                        : "stands along the view's depth (top view = upright)");
    ImGui::Text("texture: %s (scale %g)", currentTexture_.c_str(), defaultScale_);
    ImGui::Separator();
    ImGui::BeginDisabled(!blockTool_.hasPreview());
    if (ImGui::Button("Create (Return)")) onKey(KEY_ENTER);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel (Esc)")) onKey(KEY_ESCAPE);
}

void Editor::drawCurvePanel() {
    CurveParams& p = curveTool_.params;
    const bool editing = curveTool_.editingGroup() != 0;
    ImGui::TextUnformatted(editing ? "Editing a generated curve" : "Curve tool");
    static const char* shapes[] = {"Arch (doorway)", "Ring / pipe / cylinder", "Curved wall", "Spiral stairs",
                                   "Arched opening (cut)", "Dome", "Helix ramp", "Straight stairs"};
    int shape = (int)p.shape;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##shape", &shape, shapes, 8) && !editing) {
        p.shape = (CurveShape)shape;
        if (p.shape == CurveShape::SpiralStairs || p.shape == CurveShape::HelixRamp) { p.thickness = 8; }
    }
    ImGui::Separator();
    ImGui::PushItemWidth(120);
    auto intField = [&](const char* label, int& v, int lo, int hi) {
        if (ImGui::InputInt(label, &v)) v = std::clamp(v, lo, hi);
    };
    auto dblField = [&](const char* label, double& v) { ImGui::InputDouble(label, &v, 0, 0, "%g"); };
    switch (p.shape) {
        case CurveShape::Arch:
            intField("segments", p.segments, 2, 64);
            dblField("thickness", p.thickness);
            ImGui::Checkbox("square top", &p.flatTop);
            ImGui::SameLine();
            ImGui::Checkbox("legs", &p.legs);
            break;
        case CurveShape::Ring:
            intField("segments", p.segments, 1, 128);
            dblField("thickness", p.thickness);
            ImGui::TextDisabled("thickness = radius: solid cylinder");
            dblField("start degrees", p.startDeg);
            dblField("end degrees", p.endDeg);
            break;
        case CurveShape::CurvedWall:
            intField("segments", p.segments, 1, 128);
            dblField("thickness", p.thickness);
            ImGui::TextDisabled("drag points 1 (start), 2 (end), 3 (bend)");
            dblField("bend u", p.cu);
            dblField("bend v", p.cv);
            break;
        case CurveShape::SpiralStairs:
            intField("steps", p.steps, 1, 256);
            dblField("rise per step", p.rise);
            dblField("step thickness", p.thickness);
            dblField("total turn (deg)", p.turnDeg);
            dblField("inner radius", p.innerRadius);
            dblField("start degrees", p.startDeg);
            ImGui::Checkbox("clockwise", &p.clockwise);
            ImGui::SameLine();
            ImGui::Checkbox("centre column", &p.column);
            ImGui::TextDisabled("draw in the top view");
            break;
        case CurveShape::HelixRamp:
            intField("segments", p.steps, 1, 512);
            dblField("rise per segment", p.rise);
            dblField("thickness", p.thickness);
            dblField("total turn (deg)", p.turnDeg);
            dblField("inner radius", p.innerRadius);
            dblField("start degrees", p.startDeg);
            ImGui::Checkbox("clockwise", &p.clockwise);
            ImGui::TextDisabled("draw in the top view");
            break;
        case CurveShape::Dome:
            intField("segments around", p.segments, 3, 64);
            intField("rings", p.rings, 1, 32);
            dblField("shell thickness", p.thickness);
            ImGui::TextDisabled("draw in the top view");
            break;
        case CurveShape::StraightStairs:
            intField("steps", p.steps, 1, 256);
            ImGui::Checkbox("solid to the floor", &p.solid);
            if (!p.solid) dblField("step thickness", p.thickness);
            ImGui::Checkbox("rise to the left", &p.flip);
            ImGui::TextDisabled("the box = the staircase's side view");
            break;
        case CurveShape::CarveArch:
            intField("segments", p.segments, 2, 64);
            ImGui::TextDisabled("select the wall(s), draw the opening's box,");
            ImGui::TextDisabled("Return cuts it out.");
            break;
    }
    ImGui::Separator();
    static const char* ax = "XYZ";
    ImGui::Text("Box (%c %c) and depth (%c)", ax[p.axU], ax[p.axV], ax[p.axD]);
    double box[4] = {p.u0, p.v0, p.u1, p.v1};
    ImGui::SetNextItemWidth(240);
    if (ImGui::InputScalarN("##box", ImGuiDataType_Double, box, 4, nullptr, nullptr, "%g")) { p.u0 = box[0]; p.v0 = box[1]; p.u1 = box[2]; p.v1 = box[3]; }
    double depth[2] = {p.d0, p.d1};
    ImGui::SetNextItemWidth(240);
    const bool upright = p.shape == CurveShape::SpiralStairs || p.shape == CurveShape::HelixRamp || p.shape == CurveShape::Dome;
    if (ImGui::InputScalarN("##depth", ImGuiDataType_Double, depth, upright ? 1 : 2, nullptr, nullptr, "%g")) { p.d0 = depth[0]; p.d1 = depth[1]; }
    ImGui::PopItemWidth();
    ImGui::Checkbox("round corners to whole units", &p.roundToWhole);
    if (p.shape != CurveShape::CarveArch) {
        ImGui::Text("texture: %s", p.texture.c_str());
        if (editing) { ImGui::SameLine(); if (ImGui::SmallButton("use current")) { p.texture = currentTexture_; p.texScale = defaultScale_; } }
    }
    ImGui::Separator();
    const CurveResult& r = curveTool_.preview();
    if (!curveTool_.hasShape()) ImGui::TextDisabled("drag in a 2D view to place the shape");
    else if (!r.error.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "%s", r.error.c_str());
    else ImGui::Text("%zu brushes", r.brushes.size());
    ImGui::BeginDisabled(!curveTool_.hasShape() || !r.ok());
    if (ImGui::Button(p.shape == CurveShape::CarveArch ? "Cut opening (Return)" : editing ? "Update (Return)" : "Create (Return)")) commitCurve();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel (Esc)")) curveTool_.cancel();
}

} // namespace scal
