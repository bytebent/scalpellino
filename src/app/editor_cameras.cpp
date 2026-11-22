// Editor (part of the class, split by topic; see editor.hpp): cameras (milestone 11):
// the Properties panel for the camera tool and the selected camera, making one active,
// aiming at a point.
#include "app/editor.hpp"
#include "core/bottega.hpp"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace scal {

namespace fs = std::filesystem;

namespace {
constexpr const char* kPresetDir = "presets/camera_presets";
// A file name from what was typed: letters, digits, space, - and _ (others become _).
std::string presetFileName(const std::string& raw) {
    std::string name;
    for (char c : raw) name += (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == ' ') ? c : '_';
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    return name;
}

// The fields of one camera (the selected one, or the next one to be made). True when an
// edit is finished (so one change = one undo step). `withPos`: show the position.
bool cameraFields(Camera& c, bool withPos) {
    bool done = false;
    auto finished = [&] { if (ImGui::IsItemDeactivatedAfterEdit()) done = true; };
    ImGui::PushItemWidth(150);

    char name[64];
    std::snprintf(name, sizeof name, "%s", c.name.c_str());
    if (ImGui::InputText("name", name, sizeof name)) c.name = name;
    finished();

    if (withPos) {
        double p[3] = {c.pos.x, c.pos.y, c.pos.z};
        ImGui::SetNextItemWidth(220);
        if (ImGui::InputScalarN("position", ImGuiDataType_Double, p, 3, nullptr, nullptr, "%g")) c.pos = {p[0], p[1], p[2]};
        finished();
    }
    double a[3] = {c.pitch, c.yaw, c.roll};
    ImGui::SetNextItemWidth(220);
    if (ImGui::InputScalarN("pitch yaw roll", ImGuiDataType_Double, a, 3, nullptr, nullptr, "%g")) {
        c.pitch = a[0]; c.yaw = a[1]; c.roll = a[2];
    }
    finished();
    ImGui::TextDisabled("pitch -90 = down; roll tips it to the right");

    ImGui::SeparatorText("Lens");
    ImGui::BeginDisabled(c.ortho);
    ImGui::InputDouble("field of view", &c.fov, 0, 0, "%g");
    finished();
    double mm = fovToLens(c.fov);
    if (ImGui::InputDouble("lens (mm)", &mm, 0, 0, "%g") && mm > 0) c.fov = lensToFov(mm);
    finished();
    if (ImGui::BeginCombo("lens preset", "choose...")) {
        for (const LensPreset& l : lensPresets())
            if (ImGui::Selectable(l.name, std::fabs(fovToLens(c.fov) - l.mm) < 0.01)) { c.fov = lensToFov(l.mm); done = true; }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("degrees across the longer side;");
    ImGui::TextDisabled("mm on a 36 mm sensor");
    if (ImGui::Checkbox("orthographic", &c.ortho)) done = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("no perspective (isometric-style pictures)");
    if (c.ortho) {
        ImGui::InputDouble("width across (units)", &c.orthoSize, 0, 0, "%g");
        finished();
    }
    ImGui::InputDouble("near (units)", &c.nearDist, 0, 0, "%g");
    finished();
    ImGui::InputDouble("far (units)", &c.farDist, 0, 0, "%g");
    finished();
    ImGui::TextDisabled("near cuts away what is closer");
    ImGui::TextDisabled("(look in through a wall)");

    ImGui::SeparatorText("Picture");
    int wh[2] = {c.width, c.height};
    ImGui::SetNextItemWidth(150);
    if (ImGui::InputInt2("width x height", wh)) { c.width = wh[0]; c.height = wh[1]; }
    finished();
    char cur[64];
    std::snprintf(cur, sizeof cur, "%d x %d", c.width, c.height);
    for (const SizePreset& s : sizePresets())
        if (s.width == c.width && s.height == c.height) std::snprintf(cur, sizeof cur, "%s", s.name);
    if (ImGui::BeginCombo("size preset", cur)) {
        for (const SizePreset& s : sizePresets())
            if (ImGui::Selectable(s.name, s.width == c.width && s.height == c.height)) { c.width = s.width; c.height = s.height; done = true; }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Marker and renderer");
    float col[3] = {(float)(c.color.x / 255), (float)(c.color.y / 255), (float)(c.color.z / 255)};
    if (ImGui::ColorEdit3("marker colour", col, ImGuiColorEditFlags_Uint8))
        c.color = {std::round(col[0] * 255), std::round(col[1] * 255), std::round(col[2] * 255)};
    finished();
    ImGui::InputDouble("exposure (stops)", &c.exposure, 0, 0, "%g");
    finished();
    ImGui::TextDisabled("exposure: for the renderer's HDR (later)");
    ImGui::PopItemWidth();
    return done;
}
} // namespace

std::vector<std::string> Editor::cameraPresets() const {
    std::vector<std::string> out;
    if (!bottega::usable(bottegaCheck_)) return out;
    std::error_code ec;
    const fs::path dir = bottega::path(bottegaRoot_, kPresetDir);
    for (fs::directory_iterator it(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == ".camera") out.push_back(it->path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
}

bool Editor::saveCameraPreset(const std::string& raw) {
    const std::string name = presetFileName(raw);
    if (name.empty() || !bottega::usable(bottegaCheck_)) return false;
    const Camera* from = nullptr;
    if (!doc_.cameraSelection().empty()) from = doc_.findCamera(*doc_.cameraSelection().begin());
    const Camera& c = from ? *from : cameraTool_.templ;
    std::error_code ec;
    const fs::path dir = bottega::path(bottegaRoot_, kPresetDir);
    fs::create_directories(dir, ec);
    std::ofstream f(dir / (name + ".camera"));
    if (!f) return false;
    f << cameraPresetToString(c);
    return (bool)f;
}

bool Editor::loadCameraPreset(const std::string& name) {
    if (!bottega::usable(bottegaCheck_)) return false;
    std::ifstream f(fs::path(bottega::path(bottegaRoot_, kPresetDir)) / (name + ".camera"));
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    if (doc_.cameraSelection().empty()) return applyCameraPreset(cameraTool_.templ, text);
    std::vector<Camera> out;
    for (CameraId id : doc_.cameraSelection())
        if (const Camera* c = doc_.findCamera(id)) {
            Camera m = *c;
            if (!applyCameraPreset(m, text)) return false;
            if (m != *c) out.push_back(m);
        }
    if (!out.empty()) doc_.apply(doc_.makeModifyCameras(out, "Load camera preset"));
    camBufRev_ = ~0ull;
    return true;
}

// Load / save presets: into / from the selected camera, or the new-camera settings.
static void presetRow(Editor& ed, const std::vector<std::string>& presets, char* name, size_t nameSize, bool usable,
                      bool (Editor::*load)(const std::string&), bool (Editor::*save)(const std::string&), std::string& msg) {
    ImGui::BeginDisabled(!usable);
    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("##campresets", "Load a preset...")) {
        for (const std::string& p : presets)
            if (ImGui::Selectable(p.c_str()) && !(ed.*load)(p)) msg = "Couldn't read the preset " + p + ".";
        if (presets.empty()) ImGui::TextDisabled("(none saved yet)");
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(150);
    ImGui::InputTextWithHint("##campresetname", "name", name, nameSize);
    ImGui::SameLine();
    if (ImGui::Button("Save preset") && name[0]) {
        if (!(ed.*save)(name)) msg = "Couldn't save the preset in the Bottega's presets/camera_presets.";
        else msg.clear();
    }
    ImGui::EndDisabled();
    if (!usable) ImGui::TextDisabled("presets need the Bottega (File > Bottega folder...)");
    ImGui::TextDisabled("lens, near / far, size, orthographic, exposure");
    if (!msg.empty()) ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "%s", msg.c_str());
}

void Editor::activateCamera(CameraId id) {
    Command c = doc_.makeActivateCamera(id);
    if (!c.cameraChanges.empty()) doc_.apply(std::move(c));
}

void Editor::startCameraAim() {
    if (doc_.cameraSelection().empty()) return;
    if (tool_ != &cameraTool_) {
        const std::set<CameraId> keep = doc_.cameraSelection();
        setTool(&cameraTool_);
        doc_.setCameraSelection(keep);
    }
    cameraTool_.startAim();
}

void Editor::drawCameraPanel() {
    const std::set<CameraId>& sel = doc_.cameraSelection();
    if (sel.size() > 1) {
        ImGui::Text("%zu cameras selected", sel.size());
        ImGui::TextWrapped("Select one camera to see and type its settings. The rings turn all of them; Delete removes, Cmd+D duplicates.");
        if (ImGui::Button("Aim them at a clicked point")) startCameraAim();
        if (tool_ != &cameraTool_) return;
        ImGui::Separator();
    } else if (sel.size() == 1) {
        const CameraId id = *sel.begin();
        const Camera* cam = doc_.findCamera(id);
        if (!cam) return;
        if (id != camBufFor_ || doc_.revision() != camBufRev_) {
            if (!ImGui::IsAnyItemActive()) {
                camBuf_ = *cam;
                camBufFor_ = id;
                camBufRev_ = doc_.revision();
            }
        }
        // Own ID scope: the "new camera" fields below use the same labels.
        ImGui::PushID("selected camera");
        ImGui::Text("Camera: %s", cam->name.c_str());
        bool active = cam->active;
        ImGui::BeginDisabled(cam->active);
        if (ImGui::Checkbox("active", &active) && active) activateCamera(id);
        ImGui::EndDisabled();
        if (cam->active && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("The active camera (the one Render and the Camera Preview use).\nMake another one active to change it.");
        else if (ImGui::IsItemHovered()) ImGui::SetTooltip("Make this the active camera (the one Render and the Camera Preview use)");
        if (cameraFields(camBuf_, true)) {
            Camera v = camBuf_;
            v.active = cam->active;
            clampCamera(v);
            if (v != *cam) doc_.apply(doc_.makeModifyCameras({v}, "Edit camera"));
            camBufRev_ = ~0ull; // show the clamped values
        }
        ImGui::SeparatorText("Aim at a point");
        ImGui::SetNextItemWidth(220);
        ImGui::InputScalarN("point", ImGuiDataType_Double, camAim_, 3, nullptr, nullptr, "%g");
        ImGui::SameLine();
        if (ImGui::Button("Aim")) CameraTool::aimSelected(doc_, {camAim_[0], camAim_[1], camAim_[2]});
        if (ImGui::Button(cameraTool_.aiming() ? "Click the point in a view..." : "Aim at a clicked point")) startCameraAim();
        ImGui::TextDisabled("or drag the rings (yellow: roll)");
        ImGui::SeparatorText("Presets");
        presetRow(*this, cameraPresets(), camPresetName_, sizeof camPresetName_, bottega::usable(bottegaCheck_),
                  &Editor::loadCameraPreset, &Editor::saveCameraPreset, camMsg_);
        ImGui::TextDisabled("Delete removes, Cmd+D duplicates");
        ImGui::PopID();
        if (tool_ != &cameraTool_) return;
        ImGui::Separator();
    }

    // The camera tool: the map's cameras, then the next one.
    if (!doc_.cameras().empty()) {
        ImGui::PushID("camera list");
        ImGui::TextUnformatted("Cameras in the map (the dot: active)");
        for (const Camera& c : doc_.cameras()) {
            ImGui::PushID((int)c.id);
            if (ImGui::RadioButton("##active", c.active) && !c.active) activateCamera(c.id);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("make this camera the active one");
            ImGui::SameLine();
            if (ImGui::Selectable(c.name.c_str(), sel.count(c.id) > 0)) {
                doc_.setCameraSelection({c.id});
                doc_.setSelection({});
                doc_.setLightSelection({});
                doc_.setPrimSelection({});
                doc_.setPropSelection({}); doc_.setDecalSelection({});
            }
            ImGui::PopID();
        }
        ImGui::PopID();
        ImGui::Separator();
    }
    CameraTool& t = cameraTool_;
    ImGui::PushID("new camera");
    ImGui::TextUnformatted(doc_.cameras().empty() ? "New camera (the first one is the active one)" : "New camera");
    if (t.pending) t.templ.pos = *t.pending;
    if (cameraFields(t.templ, t.pending.has_value())) clampCamera(t.templ);
    if (t.pending) t.pending = t.templ.pos;
    ImGui::SeparatorText("Presets");
    presetRow(*this, cameraPresets(), camPresetName_, sizeof camPresetName_, bottega::usable(bottegaCheck_),
              &Editor::loadCameraPreset, &Editor::saveCameraPreset, camMsg_);
    ImGui::BeginDisabled(!t.pending);
    if (ImGui::Button("Create (Return)")) t.create(doc_);
    ImGui::EndDisabled();
    if (!t.pending) ImGui::TextDisabled("click in a view to place it");
    ImGui::PopID();
}

} // namespace scal

namespace scal {

void Editor::drawCameraPreview() {
    if (doc_.cameras().empty()) { // no panel without a camera; a new map starts blank
        previewDone_ = false;
        previewWanted_ = false;
        return;
    }
    if (ImGui::Begin("Camera Preview###camerapreview", nullptr, ImGuiWindowFlags_NoCollapse)) {
        const Camera* cam = doc_.activeCamera();
        if (ImGui::Button("Update")) previewWanted_ = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A draft render of the active camera's view, now (Cmd+K)");
        ImGui::SameLine();
        if (cam) {
            if (cam->ortho) ImGui::Text("%s  %d x %d  orthographic", cam->name.c_str(), cam->width, cam->height);
            else ImGui::Text("%s  %d x %d  %g mm", cam->name.c_str(), cam->width, cam->height, std::round(fovToLens(cam->fov) * 10) / 10);
        }
        const bool stale = previewDone_ && (previewRev_ != doc_.revision() || !cam || previewCam_ != cam->id);
        if (stale) ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "The map has changed: Update (Cmd+K) to see it.");

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (previewWanted_ && cam && avail.x > 8 && avail.y > 8) {
            previewWanted_ = false;
            // The picture's shape, as large as the panel allows.
            const double aspect = (double)cam->width / cam->height;
            double w = avail.x, h = w / aspect;
            if (h > avail.y) { h = avail.y; w = h * aspect; }
            const float fb = ImGui::GetIO().DisplayFramebufferScale.x > 0 ? ImGui::GetIO().DisplayFramebufferScale.x : 1.0f;
            // A draft render: the renderer itself (the map as it is, the View menu ignored,
            // hidden things included), at the panel's size, no anti-aliasing.
            Scene s = preview_.scene(doc_, textures_, models_, skies_);
            preview_.useBake(bakeNow(BakeQuality::Draft), (float)std::pow(2.0, cam->exposure), s);
            s.reflections = probesNow(0, s, renderSettings_.smooth).get(); // Draft probes (v0.12.6), kept in lastProbes_
            previewDone_ = preview_.draw(*cam, s, (int)std::floor(w), (int)std::floor(h), viewScale_ > 0 ? viewScale_ : fb, renderSettings_.smooth);
            previewRev_ = doc_.revision();
            previewCam_ = cam->id;
            ++previewUpdates_;
        }
        if (previewDone_ && preview_.target()) {
            // The last picture, kept at its own shape inside the panel until the next update.
            const double aspect = (double)preview_.width() / std::max(1, preview_.height());
            double w = avail.x, h = w / aspect;
            if (h > avail.y) { h = avail.y; w = h * aspect; }
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(at.x + (float)std::floor((avail.x - w) * 0.5), at.y));
            ImGui::Image(ImTextureID(preview_.target()->texture.id), ImVec2((float)w, (float)h), ImVec2(0, 1), ImVec2(1, 0));
        } else {
            ImGui::TextWrapped("A quick draft render of the active camera's view shows here when you ask for it: Update, or Cmd+K. "
                               "It doesn't follow every change, so the editor stays quick.");
        }
    }
    ImGui::End();
}

} // namespace scal
