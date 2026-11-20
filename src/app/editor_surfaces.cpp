// Editor (part of the class, split by topic; see editor.hpp): Textures (faces, browser) and terrain.
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


void Editor::applyTexture(const std::string& tex) {
    if (tool_ == &textureTool_ && !textureTool_.faces(doc_).empty()) {
        editFaces("Apply texture", [&](Face& f, const Polygon&, TexSize, const Vec3&) { f.texture = tex; });
        return;
    }
    if (doc_.selection().empty()) return;
    std::vector<Brush> out;
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id)) {
            Brush c = *b;
            for (Face& f : c.faces) f.texture = tex;
            out.push_back(std::move(c));
        }
    doc_.apply(doc_.makeModify(out, "Apply texture"));
}

void Editor::wrapAroundPicked() {
    const std::vector<FaceRef> picked = textureTool_.faces(doc_);
    if (picked.size() < 2) return;
    FaceRef start = textureTool_.primary(doc_);
    if (std::find(picked.begin(), picked.end(), start) == picked.end()) start = picked[0];
    std::vector<Brush> work; // copies of touched brushes
    auto brushOf = [&](BrushId id) -> Brush& {
        for (Brush& b : work) if (b.id == id) return b;
        work.push_back(*doc_.find(id));
        return work.back();
    };
    auto polyOf = [](const Brush& b, int face) {
        for (const Polygon& p : b.polygons()) if (p.face == face) return p;
        return Polygon{};
    };
    std::vector<FaceRef> done{start};
    bool progress = true;
    while (progress) {
        progress = false;
        for (const FaceRef& f : picked) {
            if (std::find(done.begin(), done.end(), f) != done.end()) continue;
            for (const FaceRef& d : done) {
                Brush& db = brushOf(d.first);
                const Face src = db.faces[d.second];
                const Polygon sp = polyOf(db, d.second);
                Brush& fb = brushOf(f.first);
                if (wrapTexture(src, sp, fb.faces[f.second], polyOf(fb, f.second))) {
                    done.push_back(f);
                    progress = true;
                    break;
                }
            }
        }
    }
    if (done.size() < 2) { errorText_ = "Wrap around: none of the picked faces touch the first one."; return; }
    doc_.apply(doc_.makeModify(work, "Wrap texture"));
}

void Editor::drawTextureBrowser() {
    if (ImGui::Begin("Textures###textures", nullptr, ImGuiWindowFlags_NoCollapse)) {
        // Current texture with a small preview.
        const Texture2D& cur = textures_.get(currentTexture_);
        ImGui::Image(ImTextureID(cur.id), ImVec2(40, 40));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(currentTexture_.c_str());
        ImGui::TextDisabled("%d x %d", cur.width, cur.height);
        ImGui::EndGroup();
        if (ImGui::Button("Apply to selection (Shift+T)")) applyCurrentTexture();

        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##search", "search by name", texSearch_, sizeof texSearch_);
        std::string needle = texSearch_;
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });

        const float footer = ImGui::GetFrameHeightWithSpacing() * 1.4f;
        if (ImGui::BeginChild("##grid", ImVec2(0, -footer), ImGuiChildFlags_None)) {
            const float cell = 76.0f;
            const float avail = ImGui::GetContentRegionAvail().x;
            const int cols = std::max(1, (int)(avail / (cell + ImGui::GetStyle().ItemSpacing.x)));
            int col = 0, shown = 0;
            for (const std::string& name : textures_.names()) {
                std::string low = name;
                std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (!needle.empty() && low.find(needle) == std::string::npos) continue;
                ++shown;
                if (col > 0) ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::PushID(name.c_str());
                const Texture2D& t = textures_.get(name);
                const bool selected = name == currentTexture_;
                if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::ImageButton("##t", ImTextureID(t.id), ImVec2(cell - 8, cell - 8))) currentTexture_ = name;
                if (selected) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                {
                    bool user = false;
                    for (const std::string& d : textures_.userFolders()) user = user || d == textures_.folderOf(name);
                    const bool inBottega = !bottegaRoot_.empty() && name.rfind("textures/", 0) == 0;
                    ImGui::SetTooltip("%s\n%d x %d\n%s", name.c_str(), t.width, t.height, inBottega ? "Bottega" : user ? "your folder" : "built-in");
                }
                std::string label = name.substr(name.find_last_of('/') == std::string::npos ? 0 : name.find_last_of('/') + 1);
                while (label.size() > 3 && ImGui::CalcTextSize(label.c_str()).x > cell) label = label.substr(0, label.size() - 4) + "..";
                ImGui::TextUnformatted(label.c_str());
                ImGui::PopID();
                ImGui::EndGroup();
                if (++col >= cols) col = 0;
            }
            if (shown == 0) ImGui::TextDisabled(textures_.names().empty() ? "No textures. Texture -> Add texture folder..." : "No texture matches.");
        }
        ImGui::EndChild();
        if (ImGui::Button("Add folder...")) { folderBuf_[0] = 0; askTexFolder_ = true; }
        ImGui::SameLine();
        if (ImGui::Button("Rescan")) rescanBottega();
        ImGui::SameLine();
        if (bottegaRoot_.empty()) ImGui::TextDisabled("%zu textures, no Bottega (File menu)", textures_.names().size());
        else ImGui::TextDisabled("%zu textures", textures_.names().size());
    }
    ImGui::End();

    // Add-folder dialog + folder list.
    if (askTexFolder_) { ImGui::OpenPopup("Texture folders"); askTexFolder_ = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Texture folders", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Your texture folders (PNG, TGA, JPG, BMP; subfolders included):");
        int remove = -1;
        for (size_t i = 0; i < textures_.userFolders().size(); ++i) {
            ImGui::PushID((int)i);
            if (ImGui::SmallButton("Remove")) remove = (int)i;
            ImGui::SameLine();
            ImGui::TextUnformatted(textures_.userFolders()[i].c_str());
            ImGui::PopID();
        }
        if (remove >= 0) { textures_.removeUserFolder((size_t)remove); saveTextureFolders(); }
        if (textures_.userFolders().empty()) ImGui::TextDisabled("(none yet - the built-in textures are always there)");
        ImGui::Separator();
        ImGui::SetNextItemWidth(520);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputTextWithHint("##folder", "folder path to add", folderBuf_, sizeof folderBuf_,
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        if (useNative_) {
            if (ImGui::Button("Browse...")) { native_ = Native::Folder; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
        }
        if ((ImGui::Button("Add") || enter) && folderBuf_[0]) {
            textures_.addUserFolder(folderBuf_);
            saveTextureFolders();
            folderBuf_[0] = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

bool Editor::selectionHasTerrain() const {
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id)) if (b->terrain) return true;
    return false;
}

void Editor::applyTerrainEdit(std::vector<Brush> edited, const char* name) {
    // Stitch with every other terrain, then keep only brushes that actually changed.
    std::vector<Brush> all = edited;
    for (const Brush& b : doc_.brushes()) {
        if (!b.terrain) continue;
        bool mine = false;
        for (const Brush& e : edited) if (e.id == b.id) mine = true;
        if (!mine) all.push_back(b);
    }
    stitchTerrain(all);
    std::vector<Brush> out;
    for (const Brush& b : all) {
        const Brush* old = doc_.find(b.id);
        if (!old) continue;
        const bool had = old->terrain.has_value(), has = b.terrain.has_value();
        if (had != has || (has && *old->terrain != *b.terrain))
            out.push_back(b);
    }
    if (!out.empty()) doc_.apply(doc_.makeModify(out, name));
}

void Editor::startMakeTerrain() {
    terrainError_.clear();
    // Faces already picked with the Texture tool: straight to the grid size dialog.
    if (tool_ == &textureTool_ && !textureTool_.faces(doc_).empty()) { askMakeTerrain_ = true; return; }
    setTool(&terrainTool_);
    terrainTool_.startPicking();
}

void Editor::makeTerrainNow(int power) {
    terrainError_.clear();
    std::vector<std::pair<BrushId, int>> targets; // brush, face
    auto addFaces = [&](const std::vector<FaceRef>& faces) {
        for (const FaceRef& f : faces) {
            for (const auto& t : targets)
                if (t.first == f.first) { terrainError_ = "pick only one face per brush (a brush has one terrain face)"; return false; }
            targets.push_back(f);
        }
        return true;
    };
    const bool picking = tool_ == &terrainTool_ && terrainTool_.picking();
    if (picking) {
        if (!addFaces(terrainTool_.pickedFaces(doc_))) return;
    } else if (tool_ == &textureTool_ && !textureTool_.faces(doc_).empty()) {
        if (!addFaces(textureTool_.faces(doc_))) return;
    } else {
        // No faces picked: change the grid size of the selected terrain.
        for (BrushId id : doc_.selection())
            if (const Brush* b = doc_.find(id))
                if (b->terrain && terrainValid(*b)) targets.push_back({id, b->terrain->face});
    }
    if (targets.empty()) {
        terrainError_ = picking ? "click the faces to turn into terrain first"
                                : "no faces picked: use Terrain > Make terrain and click faces (or select terrain to change its grid size)";
        return;
    }
    std::vector<Brush> edited;
    int failed = 0;
    std::string why;
    for (const auto& [id, face] : targets) {
        Brush b = *doc_.find(id);
        const std::string err = makeTerrain(b, face, power);
        if (!err.empty()) { ++failed; why = err; continue; }
        edited.push_back(std::move(b));
    }
    if (edited.empty()) { terrainError_ = why; return; }
    std::set<BrushId> made;
    for (const Brush& b : edited) made.insert(b.id);
    applyTerrainEdit(std::move(edited), "Make terrain");
    doc_.setSelection(made);
    if (picking) terrainTool_.stopPicking();
    if (failed) terrainError_ = std::to_string(failed) + " face(s) skipped: " + why;
}

void Editor::removeTerrain() {
    std::vector<Brush> out;
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id))
            if (b->terrain) { Brush c = *b; c.terrain.reset(); out.push_back(std::move(c)); }
    if (!out.empty()) doc_.apply(doc_.makeModify(out, "Remove terrain"));
}

void Editor::stitchAllTerrain() { applyTerrainEdit({}, "Stitch terrain"); }

bool Editor::importHeightmap(const std::string& path, double low, double high) {
    terrainError_.clear();
    std::vector<Brush> sel;
    for (BrushId id : doc_.selection())
        if (const Brush* b = doc_.find(id)) if (b->terrain && terrainValid(*b)) sel.push_back(*b);
    if (sel.empty()) { terrainError_ = "select terrain brushes first"; return false; }
    if (!FileExists(path.c_str())) { terrainError_ = "file not found"; return false; }
    Image img = LoadImage(path.c_str());
    if (!img.data) { terrainError_ = "could not read that image (PNG, JPG, BMP or TGA)"; return false; }
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
    std::vector<float> grey((size_t)img.width * img.height);
    const unsigned char* px = (const unsigned char*)img.data;
    for (size_t i = 0; i < grey.size(); ++i) grey[i] = px[i] / 255.0f;
    const int w = img.width, h = img.height;
    UnloadImage(img);
    if (!applyHeightmap(sel, grey, w, h, low, high)) { terrainError_ = "could not lay the image over the terrain"; return false; }
    applyTerrainEdit(std::move(sel), "Import heightmap");
    return true;
}

void Editor::drawTerrainPanel() {
    TerrainTool& t = terrainTool_;
    if (t.picking()) {
        ImGui::TextUnformatted("Make terrain");
        ImGui::TextWrapped("Click faces in the 3D view (Shift+click adds or removes), or click brushes in a 2D view to pick "
                           "their face toward you. Any face with 3 or 4 corners; one per brush.");
        const size_t n = t.pickedFaces(doc_).size();
        ImGui::Text("%zu face(s) picked", n);
        ImGui::TextUnformatted("Cells per side:");
        for (int pw = kMinTerrainPower; pw <= kMaxTerrainPower; ++pw) {
            char b[8]; std::snprintf(b, sizeof b, "%d", 1 << pw);
            ImGui::SameLine();
            if (ImGui::RadioButton(b, terrainPower_ == pw)) terrainPower_ = pw;
        }
        ImGui::TextDisabled("a face that is already terrain gets the new size");
        ImGui::BeginDisabled(n == 0);
        if (ImGui::Button("Make terrain (Return)")) makeTerrainNow(terrainPower_);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Stop (Esc)")) t.stopPicking();
        if (!terrainError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", terrainError_.c_str());
        return;
    }
    ImGui::TextUnformatted("Terrain tool");
    static const char* modes[] = {"Raise", "Lower", "Smooth", "Flatten", "Noise", "Paint texture", "Erase paint"};
    int mode = (int)t.mode;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##terrainmode", &mode, modes, 7)) t.mode = (SculptMode)mode;
    ImGui::PushItemWidth(120);
    if (ImGui::InputDouble("radius", &t.radius, 0, 0, "%g")) t.radius = std::clamp(t.radius, 1.0, 8192.0);
    const bool usesAmount = t.mode == SculptMode::Raise || t.mode == SculptMode::Lower || t.mode == SculptMode::Noise;
    if (usesAmount) {
        if (ImGui::InputDouble("amount per dab", &t.amount, 0, 0, "%g")) t.amount = std::clamp(t.amount, 0.0, 4096.0);
    } else {
        if (ImGui::InputDouble("strength %", &t.strength, 0, 0, "%g")) t.strength = std::clamp(t.strength, 0.0, 100.0);
    }
    static const char* falloffs[] = {"soft", "linear", "hard"};
    int f = (int)t.falloff;
    if (ImGui::Combo("falloff", &f, falloffs, 3)) t.falloff = (Falloff)f;
    ImGui::PopItemWidth();
    for (double r : {32.0, 64.0, 128.0, 256.0, 512.0}) {
        char b[16]; std::snprintf(b, sizeof b, "%g", r);
        if (ImGui::SmallButton(b)) t.radius = r;
        ImGui::SameLine();
    }
    ImGui::TextDisabled("radius");
    if (t.mode == SculptMode::Paint) ImGui::Text("paints: %s", currentTexture_.c_str());
    ImGui::TextDisabled(t.mode == SculptMode::Flatten ? "levels to the height where a stroke starts" : "one drag = one undo step");
    // Painted textures of the selected terrain (bottom first).
    for (BrushId id : doc_.selection()) {
        const Brush* b = doc_.find(id);
        if (!b || !b->terrain || !terrainValid(*b)) continue;
        std::string list = b->faces[b->terrain->face].texture + " (face)";
        for (const TerrainLayer& l : b->terrain->layers) list += ", " + l.texture;
        ImGui::TextWrapped("Textures on the selected patch: %s", list.c_str());
        ImGui::TextDisabled("up to %d painted ones; paint the face's texture to uncover it", kMaxTerrainLayers);
        break;
    }
    if (!terrainError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", terrainError_.c_str());
    ImGui::Separator();
    if (ImGui::Button("Make terrain...")) startMakeTerrain();
    ImGui::SameLine();
    ImGui::BeginDisabled(!selectionHasTerrain());
    if (ImGui::Button("Grid size...")) {
        terrainError_.clear();
        for (BrushId id : doc_.selection())
            if (const Brush* b = doc_.find(id)) if (b->terrain) terrainPower_ = b->terrain->power;
        askMakeTerrain_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Heightmap...")) { terrainError_.clear(); askHeightmap_ = true; }
    ImGui::EndDisabled();
}

void Editor::drawTerrainDialogs() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    if (askMakeTerrain_) { ImGui::OpenPopup("Terrain grid size"); askMakeTerrain_ = false; }
    if (askHeightmap_) { ImGui::OpenPopup("Import heightmap"); askHeightmap_ = false; }
    auto buttons = [&](bool& apply, bool& cancel) {
        apply = ImGui::Button("Apply (Return)") || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        ImGui::SameLine();
        cancel = ImGui::Button("Cancel (Esc)") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_;
    };
    bool apply = false, cancel = false;

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Terrain grid size", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Grid size of the selected terrain");
        ImGui::TextDisabled("(with the Texture tool: makes the picked faces terrain).");
        ImGui::TextDisabled("Terrain keeps its shape and paint at the new size.");
        ImGui::TextUnformatted("Cells per side:");
        for (int pw = kMinTerrainPower; pw <= kMaxTerrainPower; ++pw) {
            char b[8]; std::snprintf(b, sizeof b, "%d", 1 << pw);
            ImGui::SameLine();
            if (ImGui::RadioButton(b, terrainPower_ == pw)) terrainPower_ = pw;
        }
        if (!terrainError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", terrainError_.c_str());
        buttons(apply, cancel);
        if (apply) {
            makeTerrainNow(terrainPower_);
            if (terrainError_.empty()) { ImGui::CloseCurrentPopup(); setTool(&terrainTool_); }
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Import heightmap", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Set the selected terrain's heights from a grey image");
        ImGui::TextDisabled("(black = low, white = high; the image covers all selected");
        ImGui::TextDisabled("terrain, top of the image = +Y in the top view).");
        ImGui::SetNextItemWidth(360);
        ImGui::InputTextWithHint("##hm", "image path (PNG, JPG, BMP, TGA)", heightmapPath_, sizeof heightmapPath_);
        if (useNative_) {
            ImGui::SameLine();
            if (ImGui::Button("Browse...")) { native_ = Native::Heightmap; ImGui::CloseCurrentPopup(); }
        }
        ImGui::PushItemWidth(120);
        ImGui::InputDouble("height of black", &heightLow_, 0, 0, "%g");
        ImGui::InputDouble("height of white", &heightHigh_, 0, 0, "%g");
        ImGui::PopItemWidth();
        if (!terrainError_.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", terrainError_.c_str());
        buttons(apply, cancel);
        if (apply) {
            if (importHeightmap(heightmapPath_, heightLow_, heightHigh_)) ImGui::CloseCurrentPopup();
        } else if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Editor::drawFacePanel() {
    if (tool_ == &blockTool_) {
        if (ImGui::Begin("Properties###face", nullptr, ImGuiWindowFlags_NoCollapse)) drawBlockPanel();
        ImGui::End();
        return;
    }
    if (tool_ == &curveTool_) {
        if (ImGui::Begin("Properties###face", nullptr, ImGuiWindowFlags_NoCollapse)) drawCurvePanel();
        ImGui::End();
        return;
    }
    if (tool_ == &terrainTool_) {
        if (ImGui::Begin("Properties###face", nullptr, ImGuiWindowFlags_NoCollapse)) drawTerrainPanel();
        ImGui::End();
        return;
    }
    if (tool_ == &propTool_ || ((tool_ == &selectTool_ || tool_ == &lightTool_ || tool_ == &primTool_) && doc_.primSelection().empty() && !doc_.propSelection().empty())) {
        if (ImGui::Begin("Properties###face", nullptr, ImGuiWindowFlags_NoCollapse)) drawPropPanel();
        ImGui::End();
        return;
    }
    if (tool_ == &primTool_ || ((tool_ == &selectTool_ || tool_ == &lightTool_) && !doc_.primSelection().empty())) {
        if (ImGui::Begin("Properties###face", nullptr, ImGuiWindowFlags_NoCollapse)) drawPrimPanel();
        ImGui::End();
        return;
    }
    if (tool_ == &cameraTool_ || (tool_ != &textureTool_ && doc_.selection().empty() && !doc_.cameraSelection().empty())) {
        if (ImGui::Begin("Properties###face", nullptr, ImGuiWindowFlags_NoCollapse)) drawCameraPanel();
        ImGui::End();
        return;
    }
    if (tool_ == &lightTool_ || (tool_ == &selectTool_ && !doc_.lightSelection().empty())) {
        if (ImGui::Begin("Properties###face", nullptr, ImGuiWindowFlags_NoCollapse)) drawLightPanel();
        ImGui::End();
        return;
    }
    if (ImGui::Begin("Properties###face", nullptr, ImGuiWindowFlags_NoCollapse)) {
        const std::vector<FaceRef> faces = textureTool_.faces(doc_);
        if (tool_ != &textureTool_) {
            ImGui::TextWrapped("Pick faces with the Texture tool (Shift+A) to adjust how textures sit on them.");
        } else if (faces.empty()) {
            ImGui::TextWrapped("Click faces in the 3D view. Shift+click adds or removes a face, Cmd+click takes the whole brush, Option+click picks up a face's texture.");
        } else {
            const Face& f = doc_.find(faces[0].first)->faces[faces[0].second];
            ImGui::Text("%zu face(s)   %s", faces.size(), f.texture.c_str());
            ImGui::PushItemWidth(90);
            double su = f.u.offset, sv = f.v.offset;
            ImGui::TextUnformatted("Shift"); ImGui::SameLine(70);
            ImGui::InputDouble("##su", &su, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit()) editFaces("Texture shift", [&](Face& x, const Polygon&, TexSize, const Vec3&) { x.u.offset = su; });
            ImGui::SameLine();
            ImGui::InputDouble("##sv", &sv, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit()) editFaces("Texture shift", [&](Face& x, const Polygon&, TexSize, const Vec3&) { x.v.offset = sv; });
            double cu = f.scaleU, cv = f.scaleV;
            ImGui::TextUnformatted("Scale"); ImGui::SameLine(70);
            ImGui::InputDouble("##cu", &cu, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit() && cu != 0) editFaces("Texture scale", [&](Face& x, const Polygon&, TexSize, const Vec3&) { x.scaleU = cu; });
            ImGui::SameLine();
            ImGui::InputDouble("##cv", &cv, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit() && cv != 0) editFaces("Texture scale", [&](Face& x, const Polygon&, TexSize, const Vec3&) { x.scaleV = cv; });
            double rot = f.rotation;
            ImGui::TextUnformatted("Rotation"); ImGui::SameLine(70);
            ImGui::InputDouble("##rot", &rot, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const double delta = rot - f.rotation;
                editFaces("Texture rotation", [&](Face& x, const Polygon&, TexSize, const Vec3& n) { rotateTexture(x, n, delta); });
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("-90")) editFaces("Texture rotation", [&](Face& x, const Polygon&, TexSize, const Vec3& n) { rotateTexture(x, n, -90); });
            ImGui::SameLine();
            if (ImGui::SmallButton("+90")) editFaces("Texture rotation", [&](Face& x, const Polygon&, TexSize, const Vec3& n) { rotateTexture(x, n, 90); });
            double lm = f.lightmapScale;
            ImGui::TextUnformatted("Lightmap"); ImGui::SameLine(70);
            ImGui::InputDouble("##lmscale", &lm, 0, 0, "%g");
            if (ImGui::IsItemDeactivatedAfterEdit() && lm > 0) {
                lm = std::clamp(lm, kMinLightmapScale, kMaxLightmapScale);
                editFaces("Lightmap scale", [&](Face& x, const Polygon&, TexSize, const Vec3&) { x.lightmapScale = lm; });
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Baked light: units (cm) per lightmap pixel, as Hammer's lightmap scale.\n"
                                  "Smaller = sharper shadows, slower bake. Default %g (Hammer's 16 units).", kDefaultLightmapScale);
            ImGui::PopItemWidth();
            ImGui::Separator();
            if (ImGui::Button("Fit")) editFaces("Fit texture", [&](Face& x, const Polygon& p, TexSize ts, const Vec3&) { fitTexture(x, p, ts, fitTimes_[0], fitTimes_[1]); });
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            ImGui::InputInt2("times", fitTimes_);
            fitTimes_[0] = std::max(1, fitTimes_[0]);
            fitTimes_[1] = std::max(1, fitTimes_[1]);
            ImGui::TextUnformatted("Justify");
            ImGui::SameLine();
            const struct { const char* l; Justify j; } js[] = {{"L", Justify::Left}, {"R", Justify::Right}, {"T", Justify::Top},
                                                              {"B", Justify::Bottom}, {"C", Justify::Center}};
            for (const auto& j : js) {
                if (ImGui::SmallButton(j.l)) editFaces("Justify texture", [&](Face& x, const Polygon& p, TexSize ts, const Vec3&) { justifyTexture(x, p, ts, j.j); });
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::TextUnformatted("Align");
            ImGui::SameLine();
            if (ImGui::SmallButton("World")) editFaces("Align texture", [&](Face& x, const Polygon&, TexSize, const Vec3& n) { alignTextureToWorld(x, n); });
            ImGui::SameLine();
            if (ImGui::SmallButton("Face")) editFaces("Align texture", [&](Face& x, const Polygon&, TexSize, const Vec3& n) { alignTextureToFace(x, n); });
            if (faces.size() > 1) {
                if (ImGui::Button("Wrap around")) wrapAroundPicked();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Continue the first clicked face's texture around all connected picked faces");
            } else {
                ImGui::TextDisabled("Wrap: Option+Shift+click the next face");
            }
            ImGui::Separator();
            if (ImGui::Button("Apply current texture")) applyCurrentTexture();
            ImGui::SameLine();
            if (ImGui::Button("Use this texture")) currentTexture_ = f.texture;
        }
        ImGui::Checkbox("Texture lock", &tfTexLock_);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Textures stay put on surfaces when brushes move, rotate or scale");
    }
    ImGui::End();
}

} // namespace scal
