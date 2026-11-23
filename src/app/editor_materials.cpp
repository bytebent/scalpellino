// Editor (part of the class, split by topic; see editor.hpp): materials (v0.12.4). The
// browser's selection, the Material panel (Texture tool, Properties), variants and parents.
// Every change writes .vmt files beside the textures (core/material.hpp) as one undo step
// (Document's file changes); the map itself doesn't change, so it stays "saved".
#include "app/editor.hpp"
#include "core/bottega.hpp"
#include "core/material.hpp"

#include "imgui.h"
#include "imgui_internal.h" // ImGuiItemFlags_MixedValue

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>

namespace scal {

namespace fs = std::filesystem;

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
bool validName(const std::string& n) {
    if (n.empty() || n.size() > 100) return false;
    for (char c : n)
        if (!std::isalnum((unsigned char)c) && c != '_' && c != '-') return false;
    return true;
}
// Keys a helper map fills by name (not written into files that are made from a material).
bool hookedByHelper(const MaterialIndex& ix, const MaterialIndex::Entry& e, const std::string& key) {
    const std::string v = e.mat.str(key);
    if (v.empty() || e.mat.own.count(key)) return false;
    const MaterialIndex::Entry* h = ix.find(v);
    return h && h->kind == MaterialIndex::Kind::Helper;
}
} // namespace

std::vector<std::string> Editor::shownTextures() const {
    std::string needle = lower(texSearch_);
    std::vector<std::string> out;
    for (const std::string& name : textures_.names())
        if (needle.empty() || lower(name).find(needle) != std::string::npos) out.push_back(name);
    return out;
}

std::vector<std::string> Editor::materialTargets() const {
    if (!parentTarget_.empty()) {
        if (const MaterialIndex::Entry* e = textures_.materials().byVmt(parentTarget_)) return {e->name};
        return {};
    }
    std::vector<std::string> out;
    for (const std::string& n : materialSelection_)
        if (textures_.exists(n)) out.push_back(n);
    if (out.empty() && textures_.exists(currentTexture_)) out.push_back(currentTexture_);
    return out;
}

void Editor::selectMaterial(const std::string& name, int mode) {
    parentTarget_.clear();
    if (mode == 1) { // Cmd: add / remove one
        if (materialSelection_.empty() && textures_.exists(currentTexture_)) materialSelection_.push_back(currentTexture_);
        auto it = std::find(materialSelection_.begin(), materialSelection_.end(), name);
        if (it != materialSelection_.end()) materialSelection_.erase(it);
        else {
            materialSelection_.push_back(name);
            currentTexture_ = name;
        }
        materialAnchor_ = name;
        return;
    }
    if (mode == 2) { // Shift: the range from the last clicked one, in the list as shown
        const std::vector<std::string> shown = shownTextures();
        auto a = std::find(shown.begin(), shown.end(), materialAnchor_.empty() ? currentTexture_ : materialAnchor_);
        auto b = std::find(shown.begin(), shown.end(), name);
        if (a != shown.end() && b != shown.end()) {
            if (a > b) std::swap(a, b);
            for (auto it = a; it <= b; ++it)
                if (std::find(materialSelection_.begin(), materialSelection_.end(), *it) == materialSelection_.end()) materialSelection_.push_back(*it);
            currentTexture_ = name;
            return;
        }
    }
    currentTexture_ = name;
    materialSelection_ = {name};
    materialAnchor_ = name;
}

void Editor::selectShownTextures() {
    parentTarget_.clear();
    materialSelection_ = shownTextures();
    if (!materialSelection_.empty() && std::find(materialSelection_.begin(), materialSelection_.end(), currentTexture_) == materialSelection_.end())
        currentTexture_ = materialSelection_.front();
}

void Editor::selectFaceTextures() {
    parentTarget_.clear();
    std::set<std::string> names;
    if (tool_ == &textureTool_)
        for (const FaceRef& f : textureTool_.faces(doc_))
            if (const Brush* b = doc_.find(f.first)) names.insert(b->faces[f.second].texture);
    if (names.empty()) // no picked faces: the faces of the selected brushes
        for (BrushId id : doc_.selection())
            if (const Brush* b = doc_.find(id))
                for (const Face& f : b->faces) names.insert(f.texture);
    std::vector<std::string> out;
    for (const std::string& n : names)
        if (textures_.exists(n)) out.push_back(n);
    if (out.empty()) return;
    materialSelection_ = out;
    currentTexture_ = out.front();
}

void Editor::syncMaterialFiles() {
    if (doc_.filesStamp() == filesStamp_) return;
    filesStamp_ = doc_.filesStamp();
    textures_.rescan();
    if (!doc_.fileError().empty()) errorText_ = "Material: " + doc_.fileError();
    if (!parentTarget_.empty() && !textures_.materials().byVmt(parentTarget_)) parentTarget_.clear();
}

bool Editor::editMaterials(const std::string& undoName, const std::function<void(const MaterialIndex::Entry&, MaterialFile&)>& edit) {
    const MaterialIndex& ix = textures_.materials();
    std::vector<std::pair<std::string, std::optional<std::string>>> files;
    size_t readOnly = 0;
    for (const std::string& n : materialTargets()) {
        const MaterialIndex::Entry* e = ix.find(n);
        if (!e) continue;
        const std::string path = e->vmt.empty() ? ix.vmtPathFor(n) : (e->writable ? e->vmt : std::string());
        if (path.empty()) { ++readOnly; continue; }
        MaterialFile f = e->file;
        edit(*e, f);
        files.push_back({path, f.text()});
    }
    if (readOnly)
        errorText_ = std::to_string(readOnly) + (readOnly == 1 ? " material is" : " materials are") +
                     " built into the app and can't be changed: make a variant (saved in the Bottega).";
    if (files.empty()) return false;
    Command c = doc_.makeFileEdit(files, undoName);
    if (c.fileChanges.empty()) return true; // nothing changes
    doc_.apply(std::move(c));
    syncMaterialFiles();
    return true;
}

bool Editor::setMaterialValue(const std::string& key, const std::optional<std::string>& value) {
    const std::string k = lower(key);
    return editMaterials(value ? "Material " + k : "Material " + k + " reset", [&](const MaterialIndex::Entry&, MaterialFile& f) {
        if (value) f.setOwn(k, *value);
        else f.removeOwn(k);
    });
}

bool Editor::setMaterialShader(const std::string& shader) {
    return editMaterials("Material shader", [&](const MaterialIndex::Entry&, MaterialFile& f) {
        if (!f.isPatch()) f.shader = shader; // a patch's shader is its parent's (as Source)
    });
}

bool Editor::makeVariant(const std::string& newName) {
    const std::vector<std::string> t = materialTargets();
    if (t.size() != 1) { errorText_ = "Make a variant: select one material."; return false; }
    if (!validName(newName)) { errorText_ = "Make a variant: use letters, digits, - and _ only."; return false; }
    const MaterialIndex& ix = textures_.materials();
    const MaterialIndex::Entry* e = ix.find(t[0]);
    if (!e) return false;
    fs::path dir;
    std::string name;
    if (e->writable) {
        dir = fs::path(!e->vmt.empty() ? e->vmt : e->picture).parent_path();
    } else { // a built-in texture: the variant goes in the Bottega's textures folder
        if (!bottega::usable(bottegaCheck_)) {
            errorText_ = "A variant of a built-in texture is saved in the Bottega: choose one first (File > Bottega folder...).";
            return false;
        }
        dir = fs::path(bottega::path(bottegaRoot_, "textures"));
    }
    const fs::path path = (dir / (newName + ".vmt")).lexically_normal();
    std::error_code ec;
    if (fs::exists(path, ec)) { errorText_ = "Make a variant: " + path.filename().string() + " is there already."; return false; }
    for (const char* ext : {".png", ".tga", ".jpg", ".jpeg", ".bmp"})
        if (fs::exists(fs::path(dir / newName).replace_extension(ext), ec)) {
            errorText_ = "Make a variant: a texture is called " + newName + " already.";
            return false;
        }
    MaterialFile f = e->file;
    f.setOwn("$basetexture", e->base); // the same picture, whatever the copy's name
    // where a variant's name comes from (as the library names files)
    const std::string root = e->writable ? e->keyRoot : bottegaRoot_;
    name = !root.empty() ? bottega::keyFor(root, path.string()) : newName;
    if (textures_.exists(name)) { errorText_ = "Make a variant: " + name + " exists already."; return false; }
    Command c = doc_.makeFileEdit({{path.string(), f.text()}}, "Make a variant");
    if (c.fileChanges.empty()) return false;
    doc_.apply(std::move(c));
    syncMaterialFiles();
    if (textures_.exists(name)) {
        currentTexture_ = name;
        materialSelection_ = {name};
        materialAnchor_ = name;
        parentTarget_.clear();
    }
    return true;
}

bool Editor::setMaterialParent(const std::string& parentName) {
    const MaterialIndex& ix = textures_.materials();
    if (parentName.empty()) { // no parent: the material keeps its look in a file of its own
        return editMaterials("Material parent", [&](const MaterialIndex::Entry& e, MaterialFile& f) {
            if (!f.isPatch()) return;
            MaterialFile flat;
            flat.shader = e.mat.shader;
            for (const auto& [k, v] : e.mat.params)
                if (!hookedByHelper(ix, e, k)) flat.params.push_back({k, v});
            flat.extra = f.extra;
            f = flat;
        });
    }
    const MaterialIndex::Entry* p = ix.find(parentName);
    if (!p || p->vmt.empty()) { errorText_ = "Parent: " + parentName + " has no .vmt file."; return false; }
    const std::string parentPath = p->vmt;
    const Material parentMat = p->mat;
    for (const std::string& t : materialTargets())
        if (t == parentName) { errorText_ = "Parent: a material can't be its own parent."; return false; }
    if (ix.parentWouldLoop(parentName, materialTargets())) {
        errorText_ = "Parent: " + parentName + " already gets its settings from the selected material(s); that would make a loop.";
        return false;
    }
    return editMaterials("Material parent", [&](const MaterialIndex::Entry& e, MaterialFile& f) {
        MaterialFile child;
        child.shader = "patch";
        child.include = ix.includeName(e, parentPath);
        // its own settings that differ from the parent's stay its own; the rest comes from the parent
        for (const std::string& k : f.ownKeys()) {
            const std::string v = *f.own(k);
            if (k == "$basetexture" || parentMat.str(k) != v) child.params.push_back({k, v});
        }
        if (!child.own("$basetexture")) child.setOwn("$basetexture", e.base);
        child.extra = f.extra;
        f = child;
    });
}

bool Editor::newMaterialParent(const std::string& name) {
    if (!validName(name)) { errorText_ = "New parent: use letters, digits, - and _ only."; return false; }
    const MaterialIndex& ix = textures_.materials();
    std::vector<const MaterialIndex::Entry*> ts;
    for (const std::string& n : materialTargets())
        if (const MaterialIndex::Entry* e = ix.find(n); e && e->writable) ts.push_back(e);
    if (ts.empty()) { errorText_ = "New parent: the selected materials are built in (read only)."; return false; }
    // beside them if they share a folder, else at the root of their library folder
    fs::path dir = fs::path(!ts[0]->vmt.empty() ? ts[0]->vmt : ts[0]->picture).parent_path();
    for (const MaterialIndex::Entry* e : ts)
        if (fs::path(!e->vmt.empty() ? e->vmt : e->picture).parent_path() != dir) {
            dir = ts[0]->keyRoot.empty() ? fs::path(ts[0]->folder) : fs::path(bottega::path(ts[0]->keyRoot, "textures"));
            break;
        }
    const fs::path path = (dir / (name + ".vmt")).lexically_normal();
    std::error_code ec;
    if (fs::exists(path, ec)) { errorText_ = "New parent: " + path.filename().string() + " is there already (pick it as the parent instead)."; return false; }
    // the parent holds the first material's settings (not its picture or helper maps)
    const MaterialIndex::Entry& first = *ts[0];
    MaterialFile parent;
    parent.shader = first.mat.shader;
    for (const auto& [k, v] : first.mat.params)
        if (k != "$basetexture" && !hookedByHelper(ix, first, k)) parent.params.push_back({k, v});
    std::vector<std::pair<std::string, std::optional<std::string>>> files{{path.string(), parent.text()}};
    Material parentMat;
    for (const VmtKey& k : parent.params) parentMat.params[k.key] = k.value;
    for (const MaterialIndex::Entry* e : ts) {
        MaterialFile child;
        child.shader = "patch";
        child.include = ix.includeName(*e, path.string());
        for (const auto& [k, v] : e->mat.params) {
            if (hookedByHelper(ix, *e, k)) continue;
            if (k == "$basetexture" || parentMat.str(k) != v) child.params.push_back({k, v});
        }
        for (const auto& [k, v] : parentMat.params) // the parent has it, this one doesn't: removed here
            if (!e->mat.has(k)) child.params.push_back({k, ""});
        if (!child.own("$basetexture")) child.setOwn("$basetexture", e->base);
        child.extra = e->file.extra;
        files.push_back({e->vmt.empty() ? ix.vmtPathFor(e->name) : e->vmt, child.text()});
    }
    Command c = doc_.makeFileEdit(files, "New material parent");
    if (c.fileChanges.empty()) return false;
    doc_.apply(std::move(c));
    syncMaterialFiles();
    return true;
}

void Editor::openVariantDialog() {
    const std::vector<std::string> t = materialTargets();
    if (t.size() != 1) { errorText_ = "Make a variant: select one material."; return; }
    std::string leaf = t[0].substr(t[0].find_last_of('/') == std::string::npos ? 0 : t[0].find_last_of('/') + 1);
    std::snprintf(matNameBuf_, sizeof matNameBuf_, "%s_variant", leaf.c_str());
    askVariant_ = true;
}

void Editor::drawMaterialDialogs() {
    if (askVariant_) { ImGui::OpenPopup("Make a variant"); askVariant_ = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Make a variant", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::vector<std::string> t = materialTargets();
        ImGui::TextUnformatted(("A copy of " + (t.empty() ? std::string("?") : t[0]) + "'s settings under a new name,").c_str());
        ImGui::TextUnformatted("the same picture; it starts identical. Name (letters, digits, - and _):");
        ImGui::SetNextItemWidth(360);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##variant", matNameBuf_, sizeof matNameBuf_, ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Make") || enter) {
            if (makeVariant(matNameBuf_)) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (askParent_) { ImGui::OpenPopup("New material parent"); askParent_ = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("New material parent", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("A parent .vmt with the first selected material's settings; every selected");
        ImGui::TextUnformatted("material then points to it and keeps only what differs (and its picture).");
        ImGui::SetNextItemWidth(360);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("##parent", matNameBuf_, sizeof matNameBuf_, ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Make") || enter) {
            if (newMaterialParent(matNameBuf_)) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Editor::drawMaterialPanel() {
    // The panel follows the picked faces: picking faces selects their materials.
    {
        std::set<std::string> picked;
        for (const FaceRef& f : textureTool_.faces(doc_))
            if (const Brush* b = doc_.find(f.first)) picked.insert(b->faces[f.second].texture);
        std::string sig;
        for (const std::string& n : picked) sig += n + "\n";
        if (sig != lastPickedMaterials_) {
            lastPickedMaterials_ = sig;
            if (!picked.empty()) {
                parentTarget_.clear();
                materialSelection_.clear();
                for (const std::string& n : picked)
                    if (textures_.exists(n)) materialSelection_.push_back(n);
            }
        }
    }
    if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("material");
    const MaterialIndex& ix = textures_.materials();
    const std::vector<std::string> targets = materialTargets();
    std::vector<const MaterialIndex::Entry*> es;
    size_t readOnly = 0, patches = 0;
    for (const std::string& n : targets)
        if (const MaterialIndex::Entry* e = ix.find(n)) {
            es.push_back(e);
            if (!e->writable) ++readOnly;
            if (e->file.isPatch()) ++patches;
        }
    if (es.empty()) {
        ImGui::TextDisabled("No material: choose a texture in the Textures panel.");
        ImGui::PopID();
        return;
    }
    // ---- what is being edited
    if (!parentTarget_.empty()) {
        ImGui::TextWrapped("Editing the parent %s (the materials that include it change too)", es[0]->name.c_str());
        if (ImGui::SmallButton("Back to the selection")) parentTarget_.clear();
    } else if (es.size() == 1) {
        const MaterialIndex::Entry& e = *es[0];
        ImGui::TextWrapped("%s", e.name.c_str());
        if (e.kind == MaterialIndex::Kind::Variant) ImGui::TextDisabled("variant of %s", e.base.c_str());
        else if (e.kind == MaterialIndex::Kind::Helper) ImGui::TextDisabled("helper map of %s", e.helperOf.c_str());
        if (!e.writable) ImGui::TextDisabled("built-in: read only (Make a variant to change it)");
        else if (e.vmt.empty()) ImGui::TextDisabled("no .vmt yet (made beside the texture on the first change)");
        else ImGui::TextDisabled("%s", fs::path(e.vmt).filename().string().c_str());
    } else {
        ImGui::Text("%zu materials selected", es.size());
        if (readOnly) ImGui::TextDisabled("%zu built in: read only", readOnly);
        ImGui::TextDisabled("a change sets only that setting on all of them");
    }
    for (const MaterialIndex::Entry* e : es)
        if (!e->mat.problem.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "%s: %s", e->name.c_str(), e->mat.problem.c_str());
    const bool canWrite = readOnly < es.size();
    ImGui::BeginDisabled(!canWrite);

    // ---- shader
    {
        std::set<std::string> shaders;
        for (const MaterialIndex::Entry* e : es) shaders.insert(e->mat.shader);
        const std::string cur = shaders.size() == 1 ? *shaders.begin() : std::string("mixed");
        ImGui::TextUnformatted("Shader");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(-1);
        ImGui::BeginDisabled(patches == es.size());
        if (ImGui::BeginCombo("##shader", cur.c_str())) {
            for (const std::string& n : shaders_.names())
                if (ImGui::Selectable(n.c_str(), lower(n) == lower(cur))) setMaterialShader(n);
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(patches == es.size() ? "A material with a parent uses its parent's shader (as Source's patch)"
                                                   : "Source's shaders and the Bottega's shaders folder");
        if (const ShaderFile* sf = shaders_.find(cur); sf && !sf->about.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sf->about.c_str());
    }
    // ---- parent
    if (parentTarget_.empty()) {
        std::set<std::string> parents;
        for (const MaterialIndex::Entry* e : es) {
            const MaterialIndex::Entry* p = e->mat.parentFile.empty() ? nullptr : ix.byVmt(e->mat.parentFile);
            parents.insert(p ? p->name : e->mat.parentFile.empty() ? std::string("(none)") : fs::path(e->mat.parentFile).filename().string());
        }
        const std::string cur = parents.size() == 1 ? *parents.begin() : std::string("mixed");
        ImGui::TextUnformatted("Parent");
        ImGui::SameLine(80);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##parent", cur.c_str())) {
            if (ImGui::Selectable("(none)", cur == "(none)")) setMaterialParent("");
            for (const MaterialIndex::Entry* p : ix.vmtEntries()) {
                if (std::find(targets.begin(), targets.end(), p->name) != targets.end()) continue;
                if (ix.parentWouldLoop(p->name, targets)) continue; // its own parent chain has one of these
                if (ImGui::Selectable(p->name.c_str(), p->name == cur)) setMaterialParent(p->name);
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Share settings: the materials include a parent .vmt (Source's patch) and keep only what differs");
        if (ImGui::SmallButton("New parent from these...")) {
            std::snprintf(matNameBuf_, sizeof matNameBuf_, "%s", "base");
            askParent_ = true;
        }
        if (es.size() == 1 && !es[0]->mat.parentFile.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit the parent")) parentTarget_ = es[0]->mat.parentFile;
        }
        if (es.size() == 1 && ImGui::SmallButton("Make a variant...")) openVariantDialog();
    }
    ImGui::Separator();

    // ---- the shader's settings
    std::set<std::string> shaderNames;
    for (const MaterialIndex::Entry* e : es) shaderNames.insert(e->mat.shader);
    const ShaderFile* sf = shaders_.find(*shaderNames.begin());
    if (!sf) sf = shaders_.find("LightmappedGeneric");
    if (shaderNames.size() > 1) ImGui::TextDisabled("Different shaders: the settings of %s", sf ? sf->name.c_str() : "?");
    if (!sf) {
        ImGui::TextDisabled("The shader files aren't there (the app's shaders folder).");
    } else {
        // the keys' column: as wide as the longest key, at most half the panel (longer keys
        // are cut; the tooltip has the whole key)
        float keysW = ImGui::CalcTextSize("$alphatestreference").x;
        for (const ShaderSetting& st : sf->settings) keysW = std::max(keysW, ImGui::CalcTextSize(st.key.c_str()).x);
        const float labelW = std::min(keysW + ImGui::GetStyle().ItemSpacing.x * 2, 0.5f * ImGui::GetContentRegionAvail().x);
        for (const ShaderSetting& st : sf->settings) {
            if (st.key == "$basetexture") continue;
            ImGui::PushID(st.key.c_str());
            std::set<std::string> values;
            bool own = false;
            for (const MaterialIndex::Entry* e : es) {
                values.insert(e->mat.has(st.key) ? e->mat.str(st.key) : st.def);
                own = own || e->mat.own.count(st.key);
            }
            const bool mixed = values.size() > 1;
            const std::string v = mixed ? std::string() : *values.begin();
            // the key: bright when a selected material sets it, dim when it comes from the parent or the default
            {
                const ImVec2 at = ImGui::GetCursorScreenPos();
                const float clipW = labelW - ImGui::GetStyle().ItemSpacing.x;
                ImGui::PushClipRect(at, ImVec2(at.x + clipW, at.y + ImGui::GetFrameHeight()), true);
                if (own) ImGui::TextUnformatted(st.key.c_str());
                else ImGui::TextDisabled("%s", st.key.c_str());
                ImGui::PopClipRect();
                const bool cut = ImGui::CalcTextSize(st.key.c_str()).x > clipW;
                if (ImGui::IsItemHovered() && (cut || !st.help.empty()))
                    ImGui::SetTooltip("%s%s%s", cut ? st.key.c_str() : "", cut && !st.help.empty() ? ": " : "", st.help.c_str());
            }
            ImGui::SameLine(labelW);
            const float resetW = own ? ImGui::CalcTextSize("x").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x : 0;
            ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - resetW));
            if (st.type == "bool") {
                bool b = !mixed && std::atof(v.c_str()) != 0;
                if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
                if (ImGui::Checkbox("##v", &b)) setMaterialValue(st.key, std::string(b ? "1" : "0"));
                if (mixed) ImGui::PopItemFlag();
            } else if (st.type == "color") {
                Vec3 c{1, 1, 1};
                if (!mixed) parseVmtVector(v, c);
                // a swatch (a picker on click) and the value typed as Source writes it, "[r g b]" (may pass 1)
                float f[3] = {(float)c.x, (float)c.y, (float)c.z};
                ImGui::ColorEdit3("##c", f, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoInputs);
                if (ImGui::IsItemDeactivatedAfterEdit()) setMaterialValue(st.key, formatVmtVector({f[0], f[1], f[2]}));
                ImGui::SameLine();
                char buf[96];
                std::snprintf(buf, sizeof buf, "%s", mixed ? "" : (v.empty() ? formatVmtVector(c).c_str() : v.c_str()));
                ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - resetW));
                ImGui::InputTextWithHint("##v", mixed ? "mixed" : "", buf, sizeof buf);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    Vec3 nc;
                    if (std::string(buf).empty()) setMaterialValue(st.key, std::nullopt);
                    else if (parseVmtVector(buf, nc)) setMaterialValue(st.key, std::string(buf));
                    else errorText_ = st.key + ": a colour as [r g b], please.";
                }
            } else if (st.type == "light") {
                Vec3 c{255, 255, 255};
                double br = 0;
                bool on = !mixed && !v.empty() && parseTexLight(v, c, br) && br > 0;
                if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
                if (ImGui::Checkbox("glows##on", &on)) setMaterialValue(st.key, on ? std::optional<std::string>("255 255 255 200") : std::optional<std::string>(""));
                if (mixed) ImGui::PopItemFlag();
                if (on) {
                    float f[3] = {(float)(c.x / 255), (float)(c.y / 255), (float)(c.z / 255)};
                    float b = (float)br;
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(90);
                    ImGui::ColorEdit3("##c", f, ImGuiColorEditFlags_NoInputs);
                    const bool doneC = ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70);
                    ImGui::InputFloat("##b", &b, 0, 0, "%g");
                    const bool doneB = ImGui::IsItemDeactivatedAfterEdit();
                    if (doneC || doneB) {
                        char buf[96];
                        std::snprintf(buf, sizeof buf, "%d %d %d %g", (int)std::lround(f[0] * 255), (int)std::lround(f[1] * 255), (int)std::lround(f[2] * 255),
                                      std::max(0.0f, b));
                        setMaterialValue(st.key, std::string(buf));
                    }
                }
            } else { // float, int, text, texture: typed
                char buf[256];
                std::snprintf(buf, sizeof buf, "%s", v.c_str());
                const char* hint = mixed ? "mixed" : st.type == "texture" ? "(none) texture name" : st.type == "text" ? "(none)" : "";
                ImGui::InputTextWithHint("##v", hint, buf, sizeof buf);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    std::string nv = buf;
                    bool ok = true;
                    if (st.type == "float" || st.type == "int") {
                        char* end = nullptr;
                        std::strtod(nv.c_str(), &end);
                        ok = nv.empty() || (end && *end == 0);
                    }
                    if (ok) {
                        if (nv.empty()) setMaterialValue(st.key, own ? std::nullopt : std::optional<std::string>(""));
                        else setMaterialValue(st.key, nv);
                    } else
                        errorText_ = st.key + ": a number, please.";
                }
                if (st.type == "texture" && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s\nA texture's name as the Textures panel shows it (or a file beside the .vmt).", st.help.c_str());
            }
            if (own) {
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) setMaterialValue(st.key, std::nullopt);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back to the parent's or the shader's value");
            }
            ImGui::PopID();
        }
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Renders and the Camera Preview show materials; the editor shows the base texture.");
    ImGui::PopID();
}

} // namespace scal
