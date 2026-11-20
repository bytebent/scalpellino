// Editor (part of the class, split by topic; see editor.hpp): Lights, camera bookmarks, the world dialogs (sky, ambient, fog, figure).
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


namespace {
// The fields shared by "new light" and "selected lights". Returns a bit per edited field
// (only when an edit is finished, so one change = one undo step).
enum LightField : unsigned {
    LF_Color = 1, LF_Bright = 2, LF_Range = 4, LF_Pitch = 8, LF_Yaw = 16, LF_Inner = 32, LF_Outer = 64, LF_Pos = 128,
    LF_Falloff = 256, LF_Exponent = 512, LF_Spread = 1024
};
unsigned lightFields(Light& l, bool withPos) {
    unsigned done = 0;
    float col[3] = {(float)(l.color.x / 255), (float)(l.color.y / 255), (float)(l.color.z / 255)};
    ImGui::PushItemWidth(150);
    if (ImGui::ColorEdit3("colour", col, ImGuiColorEditFlags_Uint8)) l.color = {std::round(col[0] * 255), std::round(col[1] * 255), std::round(col[2] * 255)};
    if (ImGui::IsItemDeactivatedAfterEdit()) done |= LF_Color;
    auto num = [&](const char* label, double& v, unsigned bit, double lo, double hi) {
        ImGui::InputDouble(label, &v, 0, 0, "%g");
        if (ImGui::IsItemDeactivatedAfterEdit()) { v = std::clamp(v, lo, hi); done |= bit; }
    };
    num("brightness", l.brightness, LF_Bright, 0, 100000);
    if (l.kind != LightKind::Sun) {
        num("reach (units)", l.range, LF_Range, 1, 1 << 20);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The editor's rough light preview ends here. In renders it is Hammer's 0%% distance:\n"
                              "it only counts with the 50%% / 0%% falloff.");
        // Baked light: how it fades (as Hammer / VRAD).
        int mode = (int)l.falloff;
        const char* modes[] = {"inverse-square (Hammer)", "50% / 0% distance", "custom"};
        if (ImGui::Combo("falloff", &mode, modes, 3)) {
            l.falloff = (LightFalloff)mode;
            if (l.falloff == LightFalloff::FiftyZero && l.fifty <= 0) l.fifty = std::max(1.0, l.range * 0.25);
            done |= LF_Falloff;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How the baked light fades with distance.\nInverse-square: Hammer's default, the brightness is reached at 254 units (100 Hammer units).");
        if (l.falloff == LightFalloff::FiftyZero) {
            num("50% distance", l.fifty, LF_Falloff, 1, 1 << 20);
            if (ImGui::Checkbox("hard falloff (nothing past the reach)", &l.hardFalloff)) done |= LF_Falloff;
        } else if (l.falloff == LightFalloff::Custom) {
            num("constant", l.constantAttn, LF_Falloff, 0, 1e9);
            num("linear", l.linearAttn, LF_Falloff, 0, 1e9);
            num("quadratic", l.quadraticAttn, LF_Falloff, 0, 1e9);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("1 / (constant + linear d + quadratic d^2), d in Hammer units (inches)");
        }
    } else {
        num("spread (degrees)", l.sunSpread, LF_Spread, 0, 90);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hammer's SunSpreadAngle: 0 = hard shadows, more = softer");
    }
    if (l.hasDirection()) {
        num("pitch (-90 = down)", l.pitch, LF_Pitch, -90, 90);
        num("yaw", l.yaw, LF_Yaw, -360, 360);
    }
    if (l.kind == LightKind::Spot) {
        num("inner angle", l.inner, LF_Inner, 0, 89);
        num("outer angle", l.outer, LF_Outer, 1, 89);
        num("exponent", l.exponent, LF_Exponent, 0, 100);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hammer's _exponent: how the light fades between the inner and outer angle");
    }
    if (withPos) {
        double p[3] = {l.pos.x, l.pos.y, l.pos.z};
        ImGui::SetNextItemWidth(220);
        ImGui::InputScalarN("position", ImGuiDataType_Double, p, 3, nullptr, nullptr, "%g");
        if (ImGui::IsItemDeactivatedAfterEdit()) { l.pos = {p[0], p[1], p[2]}; done |= LF_Pos; }
        else l.pos = {p[0], p[1], p[2]};
    }
    ImGui::PopItemWidth();
    return done;
}
} // namespace

void Editor::drawLightPanel() {
    const std::set<LightId>& sel = doc_.lightSelection();
    if (!sel.empty()) {
        // Fields show the first selected light; an edit goes to every selected light.
        if (sel != lightBufFor_ || doc_.revision() != lightBufRev_) {
            if (!ImGui::IsAnyItemActive()) {
                if (const Light* l = doc_.findLight(*sel.begin())) lightBuf_ = *l;
                lightBufFor_ = sel;
                lightBufRev_ = doc_.revision();
            }
        }
        // Own ID scope: the "new light" fields below use the same labels.
        ImGui::PushID("selected lights");
        ImGui::Text("%zu %s light(s) selected", sel.size(), lightKindName(lightBuf_.kind));
        const unsigned done = lightFields(lightBuf_, sel.size() == 1);
        if (done) {
            const Light v = lightBuf_;
            editLights("Edit light", [&](Light& l) {
                if (done & LF_Color) l.color = v.color;
                if (done & LF_Bright) l.brightness = v.brightness;
                if (done & LF_Range) l.range = v.range;
                if ((done & LF_Pitch) && l.hasDirection()) l.pitch = v.pitch;
                if ((done & LF_Yaw) && l.hasDirection()) l.yaw = v.yaw;
                if (done & LF_Inner) l.inner = std::min(v.inner, l.outer);
                if (done & LF_Outer) { l.outer = v.outer; l.inner = std::min(l.inner, l.outer); }
                if (done & LF_Pos) l.pos = v.pos;
                if ((done & LF_Falloff) && l.kind != LightKind::Sun) {
                    l.falloff = v.falloff;
                    l.fifty = v.fifty;
                    l.hardFalloff = v.hardFalloff;
                    l.constantAttn = v.constantAttn;
                    l.linearAttn = v.linearAttn;
                    l.quadraticAttn = v.quadraticAttn;
                }
                if (done & LF_Exponent) l.exponent = v.exponent;
                if ((done & LF_Spread) && l.kind == LightKind::Sun) l.sunSpread = v.sunSpread;
            });
        }
        if (lightBuf_.hasDirection()) {
            if (ImGui::SmallButton("point down")) editLights("Aim light", [](Light& l) { if (l.hasDirection()) l.pitch = -90; });
            ImGui::SameLine();
            ImGui::TextDisabled("or drag the rings");
        }
        ImGui::TextDisabled("Delete removes, Cmd+D duplicates");
        ImGui::PopID();
        if (tool_ != &lightTool_) return;
        ImGui::Separator();
    }
    LightTool& t = lightTool_;
    ImGui::PushID("new light");
    ImGui::TextUnformatted("New light");
    int kind = (int)t.templ.kind;
    ImGui::RadioButton("point", &kind, 0); ImGui::SameLine();
    ImGui::RadioButton("spot", &kind, 1); ImGui::SameLine();
    ImGui::RadioButton("sun", &kind, 2);
    t.templ.kind = (LightKind)kind;
    if (t.pending) t.templ.pos = *t.pending;
    lightFields(t.templ, t.pending.has_value());
    if (t.pending) t.pending = t.templ.pos;
    ImGui::BeginDisabled(!t.pending);
    if (ImGui::Button("Create (Return)")) t.create(doc_);
    ImGui::EndDisabled();
    if (!t.pending) ImGui::TextDisabled("click in a view to place it");
    ImGui::PopID();
}

Vec3 Editor::groundBelow(double x, double y) const {
    double best = -1;
    for (const Brush& b : doc_.brushes()) {
        if (!doc_.visible(b)) continue;
        for (double t : {rayHit(b, {x, y, 1e5}, {0, 0, -1}), rayHitTerrain(b, {x, y, 1e5}, {0, 0, -1})})
            if (t >= 0 && (best < 0 || t < best)) best = t;
    }
    return {x, y, best >= 0 ? 1e5 - best : 0};
}

void Editor::addBookmark(const std::string& name) {
    std::vector<CameraBookmark> b = doc_.bookmarks();
    const Vector3 c = views_[0]->cameraPos();
    std::string n = name.empty() ? "view " + std::to_string(b.size() + 1) : name;
    b.push_back({n, {c.x, c.y, c.z}, views_[0]->yaw(), views_[0]->pitch()});
    doc_.setBookmarks(std::move(b));
}

void Editor::goToBookmark(size_t i) {
    if (i >= doc_.bookmarks().size()) return;
    const CameraBookmark& b = doc_.bookmarks()[i];
    views_[0]->setCamera(b.pos, (float)b.yaw, (float)b.pitch);
}

void Editor::drawWorldDialogs() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    if (askEnvironment_) { ImGui::OpenPopup("Environment"); askEnvironment_ = false; }
    if (askBookmarks_) { ImGui::OpenPopup("Camera bookmarks"); askBookmarks_ = false; }
    if (askFigure_) { ImGui::OpenPopup("Scale figure"); askFigure_ = false; }
    auto buttons = [&](bool& apply, bool& cancel, const char* ok = "Apply (Return)") {
        apply = ImGui::Button(ok) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        ImGui::SameLine();
        cancel = ImGui::Button("Cancel (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_;
    };
    bool apply = false, cancel = false;
    auto color3 = [](const char* label, Vec3& c) {
        float f[3] = {(float)(c.x / 255), (float)(c.y / 255), (float)(c.z / 255)};
        if (ImGui::ColorEdit3(label, f, ImGuiColorEditFlags_Uint8))
            c = {std::round(f[0] * 255), std::round(f[1] * 255), std::round(f[2] * 255)};
    };

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Environment", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        Environment& e = envBuf_;
        ImGui::TextUnformatted("Sky");
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo("##sky", e.sky.empty() ? "(none)" : e.sky.c_str())) {
            if (ImGui::Selectable("(none)", e.sky.empty())) e.sky.clear();
            for (const std::string& n : skies_.names())
                if (ImGui::Selectable(n.c_str(), n == e.sky)) e.sky = n;
            ImGui::EndCombo();
        }
        if (!e.sky.empty() && !skies_.exists(e.sky)) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "not found in the sky folders");
        else if (!e.sky.empty() && !skies_.get(e.sky)) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", skies_.problem(e.sky).c_str());
        ImGui::TextDisabled("2:1 panoramas or 4:3 cube crosses; shown in the 3D view (View menu)");
        if (ImGui::TreeNode("Sky folders")) {
            int remove = -1;
            for (size_t i = 0; i < skies_.userFolders().size(); ++i) {
                ImGui::PushID((int)i);
                if (ImGui::SmallButton("remove")) remove = (int)i;
                ImGui::SameLine();
                ImGui::TextUnformatted(skies_.userFolders()[i].c_str());
                ImGui::PopID();
            }
            if (remove >= 0) { skies_.removeUserFolder((size_t)remove); saveSkyFolders(); }
            if (skies_.userFolders().empty()) ImGui::TextDisabled("(none yet - the built-in skies are always there)");
            ImGui::SetNextItemWidth(260);
            ImGui::InputTextWithHint("##skydir", "folder path", skyFolderBuf_, sizeof skyFolderBuf_);
            ImGui::SameLine();
            if (ImGui::Button("Add") && skyFolderBuf_[0]) { skies_.addUserFolder(skyFolderBuf_); saveSkyFolders(); skyFolderBuf_[0] = 0; }
            if (useNative_) {
                ImGui::SameLine();
                if (ImGui::Button("Browse...")) { native_ = Native::SkyFolder; ImGui::CloseCurrentPopup(); }
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
        color3("ambient light", e.ambient);
        ImGui::TextDisabled("the editor's rough preview: lights everything a little");
        ImGui::TextUnformatted("Baked light (renders)");
        ImGui::PushItemWidth(120);
        ImGui::InputDouble("sky light (%)", &e.skyLight, 0, 0, "%g");
        e.skyLight = std::clamp(e.skyLight, 0.0, 100000.0);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Light from the sky, where it can be seen (as Source's light_environment ambient).\n"
                              "Its colours come from the sky picture; with no sky, the ambient colour.");
        ImGui::Checkbox("minimum light (the ambient colour everywhere)", &e.minLight);
        ImGui::InputDouble("smoothing angle", &e.smoothAngle, 0, 0, "%g");
        e.smoothAngle = std::clamp(e.smoothAngle, 0.0, 180.0);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Faces closer than this many degrees are lit smoothly (VRAD's default 45)");
        ImGui::PopItemWidth();
        ImGui::Separator();
        ImGui::Checkbox("fog", &e.fogOn);
        ImGui::BeginDisabled(!e.fogOn);
        color3("fog colour", e.fog.color);
        ImGui::PushItemWidth(120);
        ImGui::InputDouble("clear up to (units)", &e.fog.start, 0, 0, "%g");
        ImGui::InputDouble("fully fogged from", &e.fog.end, 0, 0, "%g");
        ImGui::InputDouble("base height", &e.fog.base, 0, 0, "%g");
        ImGui::InputDouble("thickness (0 = no height)", &e.fog.thickness, 0, 0, "%g");
        e.fog.thickness = std::max(0.0, e.fog.thickness);
        ImGui::TextDisabled("base height and thickness: where Sky Lab skies show the fog");
        ImGui::PopItemWidth();
        ImGui::EndDisabled();
        ImGui::TextDisabled("the 3D view shows fog only when View > 3D view: fog is on");
        if (!worldError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", worldError_.c_str());
        buttons(apply, cancel);
        if (apply) {
            e.fog.start = std::max(0.0, e.fog.start);
            if (e.fog.end <= e.fog.start) worldError_ = "fog: 'fully fogged from' must be farther than 'clear up to'";
            else {
                worldError_.clear();
                if (e != doc_.environment()) doc_.apply(doc_.makeEnvironment(e, "Environment"));
                ImGui::CloseCurrentPopup();
            }
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Camera bookmarks", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Saved 3D viewpoints (kept in the map, editor only)");
        int go = -1, del = -1;
        for (size_t i = 0; i < doc_.bookmarks().size(); ++i) {
            ImGui::PushID((int)i);
            if (ImGui::SmallButton("go")) go = (int)i;
            ImGui::SameLine();
            if (ImGui::SmallButton("delete")) del = (int)i;
            ImGui::SameLine();
            ImGui::TextUnformatted(doc_.bookmarks()[i].name.c_str());
            ImGui::PopID();
        }
        if (doc_.bookmarks().empty()) ImGui::TextDisabled("(none yet)");
        if (go >= 0) goToBookmark((size_t)go);
        if (del >= 0) { auto b = doc_.bookmarks(); b.erase(b.begin() + del); doc_.setBookmarks(std::move(b)); }
        ImGui::Separator();
        ImGui::SetNextItemWidth(200);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##bm", "name", bookmarkName_, sizeof bookmarkName_);
        ImGui::SameLine();
        if (ImGui::Button("Save current 3D view (Return)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            addBookmark(bookmarkName_);
            bookmarkName_[0] = 0;
        }
        if (ImGui::Button("Close (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Scale figure", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Where the 175-unit (1.75 m) figure stands (its feet)");
        ImGui::SetNextItemWidth(260);
        ImGui::InputScalarN("X Y Z", ImGuiDataType_Double, figureBuf_, 3, nullptr, nullptr, "%g");
        if (ImGui::Button("Under the 3D view's centre")) {
            Vec3 o, d;
            views_[0]->pickRay({views_[0]->width() * 0.5f, views_[0]->height() * 0.5f}, o, d);
            double best = -1;
            for (const Brush& b : doc_.brushes()) {
                if (!doc_.visible(b)) continue;
                for (double t : {rayHit(b, o, d), rayHitTerrain(b, o, d)})
                    if (t >= 0 && (best < 0 || t < best)) best = t;
            }
            const Vec3 h = o + d * (best >= 0 ? best : 256.0);
            figureBuf_[0] = grid_.snapValue(h.x); figureBuf_[1] = grid_.snapValue(h.y); figureBuf_[2] = h.z;
        }
        ImGui::TextDisabled("View > Scale figure shows or hides it");
        buttons(apply, cancel);
        if (apply) {
            doc_.setFigure(Vec3{figureBuf_[0], figureBuf_[1], figureBuf_[2]});
            showFigure_ = true;
            ImGui::CloseCurrentPopup();
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace scal
