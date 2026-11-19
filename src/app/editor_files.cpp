// Editor (part of the class, split by topic; see editor.hpp): Files and settings: new / open / save, native dialogs, folders, Bottega.
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

void Editor::loadTextureFolders() {
    const std::string dir = settingsDir();
    if (dir.empty()) return;
    std::ifstream f(dir + "/texture_folders.txt");
    std::string line;
    while (std::getline(f, line))
        if (!line.empty()) textures_.addUserFolder(line);
}

void Editor::loadSkyFolders() {
    const std::string dir = settingsDir();
    if (dir.empty()) return;
    std::ifstream f(dir + "/sky_folders.txt");
    std::string line;
    while (std::getline(f, line))
        if (!line.empty()) skies_.addUserFolder(line);
}

void Editor::saveSkyFolders() const {
    if (opt_.automated()) return; // tests never touch user settings
    const std::string dir = settingsDir();
    if (dir.empty()) return;
    std::ofstream f(dir + "/sky_folders.txt");
    for (const std::string& d : skies_.userFolders()) f << d << '\n';
}

void Editor::saveTextureFolders() const {
    if (opt_.automated()) return; // tests never touch user settings
    const std::string dir = settingsDir();
    if (dir.empty()) return;
    std::ofstream f(dir + "/texture_folders.txt");
    for (const std::string& d : textures_.userFolders()) f << d << '\n';
}

void Editor::loadBottegaSetting() {
    const std::string dir = settingsDir();
    if (dir.empty()) return;
    std::ifstream f(dir + "/bottega.txt");
    std::string line;
    if (std::getline(f, line) && !line.empty()) setBottegaRoot(line);
}

void Editor::saveBottegaSetting() const {
    if (opt_.automated()) return; // tests never touch user settings
    const std::string dir = settingsDir();
    if (dir.empty()) return;
    std::ofstream f(dir + "/bottega.txt");
    f << bottegaRoot_ << '\n';
}

void Editor::setBottegaRoot(const std::string& root) {
    bottegaRoot_ = root;
    bottegaCheck_ = bottega::inspect(root);
    const std::string use = bottega::usable(bottegaCheck_) ? root : std::string();
    textures_.setBottegaRoot(use);
    skies_.setBottegaRoot(use);
    models_.setBottegaRoot(use);
    if (!textures_.exists(currentTexture_) && !textures_.names().empty()) currentTexture_ = textures_.names()[0];
}

void Editor::rescanBottega() {
    // Never automatic (idle must stay free): the user asks, by menu, button or Cmd+R.
    bottegaCheck_ = bottega::inspect(bottegaRoot_);
    const std::string use = bottega::usable(bottegaCheck_) ? bottegaRoot_ : std::string();
    textures_.setBottegaRoot(use);
    textures_.rescan();
    skies_.setBottegaRoot(use);
    skies_.rescan();
    models_.setBottegaRoot(use); // forgets every loaded model; they load again when next drawn
}

void Editor::drawBottegaDialog() {
    if (askBottega_) { ImGui::OpenPopup("Bottega folder"); askBottega_ = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Bottega folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextWrapped("The Bottega is one folder shared by Scalpellino, Mazzuola and other tools: textures, skies, models, "
                       "shaders, maps and renders live in its sub-folders. Choose it once.");
    ImGui::Separator();
    ImGui::SetNextItemWidth(520);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputTextWithHint("##bottega", "path of the Bottega folder", bottegaBuf_, sizeof bottegaBuf_,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    if (useNative_) {
        if (ImGui::Button("Browse...")) { native_ = Native::Bottega; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
    }
    const bottega::Check typed = bottega::inspect(bottegaBuf_);
    ImGui::TextWrapped("%s", bottega::describe(typed).c_str());
    if (!bottegaMsg_.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", bottegaMsg_.c_str());

    const bool canMake = typed.state == bottega::State::MissingFolder || typed.state == bottega::State::Empty ||
                         typed.state == bottega::State::NotBottega || (typed.state == bottega::State::Ready && !typed.missing.empty());
    const bool canUse = typed.state == bottega::State::Ready && typed.missing.empty();
    if (canMake) {
        const char* label = typed.state == bottega::State::Ready ? "Add missing folders" : "Make this a Bottega and use it";
        if (ImGui::Button(label)) {
            if (bottega::make(bottegaBuf_, bottegaMsg_)) {
                bottegaMsg_.clear();
                setBottegaRoot(bottegaBuf_);
                saveBottegaSetting();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(!canUse);
    if (ImGui::Button("Use this folder") || (enter && canUse)) {
        bottegaMsg_.clear();
        setBottegaRoot(bottegaBuf_);
        saveBottegaSetting();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (!bottegaRoot_.empty() && ImGui::Button("Stop using the Bottega")) {
        setBottegaRoot("");
        saveBottegaSetting();
        ImGui::CloseCurrentPopup();
    }
    if (!bottegaRoot_.empty()) { ImGui::SameLine(); }
    if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || closeDialogs_) ImGui::CloseCurrentPopup();
    ImGui::TextDisabled("Current: %s", bottegaRoot_.empty() ? "(none)" : bottegaRoot_.c_str());
    ImGui::EndPopup();
}

void Editor::newMap() {
    tool_->cancel();
    doc_.reset({{"classname", "worldspawn"}, {"mapversion", "220"}}, {}, {});
    path_.clear();
}

void Editor::openMap(const std::string& path) {
    Document loaded;
    const MapReadResult r = loadMapFile(path, loaded);
    if (!r.ok) {
        errorText_ = "Could not open " + fileName(path) + ":\n" + r.error;
        return;
    }
    tool_->cancel();
    doc_.reset(loaded.worldKeys, loaded.brushes(), loaded.otherEntities, loaded.groups(), loaded.visgroups(),
               WorldData{loaded.lights(), loaded.environment(), loaded.bookmarks(), loaded.figure(), loaded.prims(), loaded.props()});
    path_ = path;
    if (r.skippedBrushes > 0)
        errorText_ = "Opened, but skipped " + std::to_string(r.skippedBrushes) + " invalid brush(es).";
}

bool Editor::saveMap(const std::string& path) {
    std::string err;
    if (!saveMapFile(doc_, path, err)) {
        errorText_ = "Could not save " + fileName(path) + ":\n" + err;
        return false;
    }
    doc_.markSaved();
    path_ = path;
    return true;
}

void Editor::requestSaveAs() {
    if (useNative_) { native_ = Native::SaveAs; return; }
    std::snprintf(pathBuf_, sizeof pathBuf_, "%s", path_.empty() ? defaultMapPath().c_str() : path_.c_str());
    askSaveAs_ = true;
}

void Editor::requestOpen() {
    if (useNative_) { native_ = Native::Open; return; }
    std::snprintf(pathBuf_, sizeof pathBuf_, "%s", path_.empty() ? defaultMapPath().c_str() : path_.c_str());
    askOpen_ = true;
}

void Editor::runNativeDialog() {
    if (native_ == Native::None) return;
    // Wait until no modifier key or mouse button is held (see editor.hpp).
    for (int k : {KEY_LEFT_SUPER, KEY_RIGHT_SUPER, KEY_LEFT_SHIFT, KEY_RIGHT_SHIFT, KEY_LEFT_ALT, KEY_RIGHT_ALT,
                  KEY_LEFT_CONTROL, KEY_RIGHT_CONTROL})
        if (IsKeyDown(k)) return;
    for (int b : {MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE})
        if (IsMouseButtonDown(b)) return;
    const Native what = native_;
    native_ = Native::None;
    namespace fs = std::filesystem;
    const std::string docs = fs::path(defaultMapPath()).parent_path().string();
    const std::string startDir = path_.empty() ? docs : fs::path(path_).parent_path().string();
    std::string picked;
    switch (what) {
        case Native::Open:
            if (openFileDialog(startDir, {"map"}, picked)) openMap(picked);
            break;
        case Native::SaveAs:
            if (saveFileDialog(startDir, path_.empty() ? "untitled.map" : fileName(path_), {"map"}, picked)) {
                if (fs::path(picked).extension() != ".map") picked += ".map";
                if (saveMap(picked)) runPending();
                else pending_ = Pending::None;
            } else {
                pending_ = Pending::None; // cancelled: e.g. don't quit
            }
            break;
        case Native::Folder:
            if (chooseFolderDialog(docs, picked)) { textures_.addUserFolder(picked); saveTextureFolders(); }
            break;
        case Native::Bottega:
            if (chooseFolderDialog(docs, picked)) std::snprintf(bottegaBuf_, sizeof bottegaBuf_, "%s", picked.c_str());
            askBottega_ = true; // back to the Bottega dialog
            break;
        case Native::SkyFolder:
            if (chooseFolderDialog(docs, picked)) { skies_.addUserFolder(picked); saveSkyFolders(); }
            askEnvironment_ = true; // back to the environment dialog
            break;
        case Native::Heightmap:
            if (openFileDialog(docs, {"png", "jpg", "jpeg", "bmp", "tga"}, picked))
                std::snprintf(heightmapPath_, sizeof heightmapPath_, "%s", picked.c_str());
            askHeightmap_ = true; // back to the heightmap dialog
            break;
        case Native::None: break;
    }
}

void Editor::save() {
    if (path_.empty()) {
        requestSaveAs();
    } else {
        saveMap(path_);
    }
}

void Editor::requestQuit() { guardUnsaved(Pending::Quit); }

void Editor::guardUnsaved(Pending action) {
    pending_ = action;
    if (doc_.dirty() && !opt_.automated()) askUnsaved_ = true;
    else runPending();
}

void Editor::runPending() {
    const Pending p = pending_;
    pending_ = Pending::None;
    switch (p) {
        case Pending::New:  newMap(); break;
        case Pending::Open: requestOpen(); break;
        case Pending::OpenPath:
            std::snprintf(pathBuf_, sizeof pathBuf_, "%s", path_.empty() ? defaultMapPath().c_str() : path_.c_str());
            askOpen_ = true;
            break;
        case Pending::Quit: exit_ = true; break;
        case Pending::None: break;
    }
}

} // namespace scal
