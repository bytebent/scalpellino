// Editor (part of the class, split by topic; see editor.hpp): Sky Lab (milestone 10c):
// the window, the live preview in the 3D view, baking to the Bottega, the sun lock.
#include "app/editor.hpp"
#include "core/bottega.hpp"
#include "ui/layout.hpp"

#include "imgui.h"
#include "rlImGui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include "app/editor_util.hpp"

namespace scal {

using namespace editor_util;
namespace fs = std::filesystem;

namespace {
constexpr int kPreviewSmall = 256; // while a value is being dragged
constexpr int kPreviewLarge = 1024; // once it is let go
constexpr double kPi = 3.14159265358979323846;

bool sameAngle(double a, double b) { return std::fabs(a - b) < 1e-4; }

// A slider to drag plus a field to type in, side by side (Bryce has both).
bool slider(const char* label, double& v, double lo, double hi, const char* fmt = "%.0f") {
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(170);
    bool changed = ImGui::SliderScalar("##s", ImGuiDataType_Double, &v, &lo, &hi, fmt, ImGuiSliderFlags_NoRoundToFormat);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(64);
    changed |= ImGui::InputScalar("##t", ImGuiDataType_Double, &v, nullptr, nullptr, "%g");
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    if (changed) v = std::clamp(v, lo, hi);
    return changed;
}

bool colour(const char* label, Vec3& c) {
    float f[3] = {(float)(c.x / 255), (float)(c.y / 255), (float)(c.z / 255)};
    if (!ImGui::ColorEdit3(label, f, ImGuiColorEditFlags_Uint8)) return false;
    c = {std::round(f[0] * 255), std::round(f[1] * 255), std::round(f[2] * 255)};
    return true;
}

// The sun's place as a compass seen from above (+Y up, as in the top view): the middle
// is straight up, the inner ring the horizon, the edge 30 degrees below it.
bool sunCompass(double& azimuth, double& altitude) {
    const float R = 64;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##compass", ImVec2(2 * R + 8, 2 * R + 8));
    const ImVec2 c(p0.x + R + 4, p0.y + R + 4);
    const float horizon = R * 0.75f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(c, R, IM_COL32(28, 30, 38, 255), 48);
    dl->AddCircleFilled(c, horizon, IM_COL32(46, 70, 120, 255), 48);
    dl->AddCircle(c, horizon, IM_COL32(150, 160, 180, 255), 48);
    dl->AddLine(ImVec2(c.x - R, c.y), ImVec2(c.x + R, c.y), IM_COL32(90, 90, 100, 255));
    dl->AddLine(ImVec2(c.x, c.y - R), ImVec2(c.x, c.y + R), IM_COL32(90, 90, 100, 255));
    dl->AddText(ImVec2(c.x + R - 16, c.y + 1), IM_COL32(200, 200, 210, 255), "+X");
    dl->AddText(ImVec2(c.x + 3, c.y - R), IM_COL32(200, 200, 210, 255), "+Y");
    bool changed = false;
    if (ImGui::IsItemActive()) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const double dx = m.x - c.x, dy = -(m.y - c.y);
        const double r = std::min(std::hypot(dx, dy), (double)R);
        altitude = std::round(std::max(-30.0, 90.0 - r / horizon * 90.0));
        if (r > 1) {
            double a = std::atan2(dy, dx) * 180 / kPi;
            if (a < 0) a += 360;
            azimuth = std::fmod(std::round(a), 360.0);
        }
        changed = true;
    }
    const double r = (90.0 - altitude) / 90.0 * horizon;
    const double a = azimuth * kPi / 180;
    const ImVec2 s(c.x + (float)(std::cos(a) * r), c.y - (float)(std::sin(a) * r));
    dl->AddCircleFilled(s, 6, altitude >= 0 ? IM_COL32(255, 220, 120, 255) : IM_COL32(150, 110, 60, 255), 16);
    return changed;
}
} // namespace

const Light* Editor::mapSunLight() const {
    for (const Light& l : doc_.lights())
        if (l.kind == LightKind::Sun) return &l;
    return nullptr;
}

std::string Editor::skyLabBakeName() const {
    // A saved map's sky is named after the map. An unsaved map gets the first free
    // "untitled_<n>" (and keeps it), so two unsaved maps never share one file.
    const std::string base = "skies/skylab/";
    if (!path_.empty()) {
        std::string stem = fs::path(path_).stem().string();
        for (char& c : stem)
            if (std::isspace((unsigned char)c)) c = '_';
        return base + stem;
    }
    const std::string& now = doc_.environment().sky;
    if (now.rfind(base + "untitled_", 0) == 0) return now;
    for (int n = 1;; ++n) {
        const std::string name = base + "untitled_" + std::to_string(n);
        if (!skies_.exists(name)) return name;
    }
}

bool Editor::skyLabNeedsFrame() const {
    // A small preview made while dragging becomes the large one on the next frame; a bake
    // runs over several frames.
    return skyBake_ != nullptr || (skyLabOpen_ && skyLabPreviewW_ < kPreviewLarge);
}

bool Editor::skyOutOfDate() const {
    const SkyLab& s = doc_.environment().skylab;
    if (!s.used || !s.lockSun) return false;
    const Light* sun = mapSunLight();
    if (!sun) return false;
    double az = 0, alt = 0;
    sunFromLightAngles(sun->pitch, sun->yaw, az, alt);
    return !sameAngle(az, s.sunAzimuth) || !sameAngle(alt, s.sunAltitude);
}

bool Editor::skyFileMissing() const {
    const Environment& e = doc_.environment();
    return e.skylab.used && !e.sky.empty() && !skies_.exists(e.sky);
}

void Editor::openSkyLab() {
    skyLabMsg_.clear();
    const Environment& e = doc_.environment();
    if (e.skylab.used) skyLabBuf_ = e.skylab;
    else {
        skyLabBuf_ = SkyLab();
        skyLabBuf_.used = true;
        // A map with a sun light starts with the sky's sun on it, locked: a sky whose sun
        // disagrees with the shadows looks wrong (untick to move them apart).
        if (const Light* sun = mapSunLight()) {
            skyLabBuf_.lockSun = true;
            sunFromLightAngles(sun->pitch, sun->yaw, skyLabBuf_.sunAzimuth, skyLabBuf_.sunAltitude);
        }
    }
    if (skyLabBuf_.lockSun) {
        if (const Light* sun = mapSunLight()) sunFromLightAngles(sun->pitch, sun->yaw, skyLabBuf_.sunAzimuth, skyLabBuf_.sunAltitude);
    }
    skyLabAmbient_ = e.ambient;
    skyLabOpen_ = true;
    skyLabFocus_ = true;
    skyLabPreviewW_ = 0; // makes a fresh preview
}

void Editor::closeSkyLab() {
    skyLabOpen_ = false;
    if (skyLabTex_.id) { UnloadTexture(skyLabTex_); skyLabTex_ = Texture2D{}; }
    skyLabPreviewW_ = 0;
}

void Editor::updateSkyLabPreview(bool dragging) {
    const int want = dragging ? kPreviewSmall : kPreviewLarge;
    if (skyLabBuf_ == skyLabPreviewOf_ && skyLabPreviewW_ >= want) return; // nothing new
    const std::vector<unsigned char> rgb = generateSky(skyLabBuf_, want);
    Image img{(void*)rgb.data(), want, want / 2, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8};
    if (skyLabTex_.id && skyLabTex_.width == want) UpdateTexture(skyLabTex_, rgb.data());
    else {
        if (skyLabTex_.id) UnloadTexture(skyLabTex_);
        skyLabTex_ = LoadTextureFromImage(img);
        SetTextureFilter(skyLabTex_, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(skyLabTex_, TEXTURE_WRAP_CLAMP);
    }
    skyLabPreviewOf_ = skyLabBuf_;
    skyLabPreviewW_ = want;
    ++skyLabPreviews_;
}

bool Editor::startSkyBake(const SkyLab& settings, const std::string& name, bool closeAfter) {
    if (!bottega::usable(bottegaCheck_)) {
        skyLabMsg_ = "Sky Lab saves its skies in the Bottega: set a Bottega folder first.";
        return false;
    }
    auto b = std::make_unique<SkyBake>();
    b->settings = settings;
    b->settings.used = true;
    b->name = name;
    b->closeAfter = closeAfter;
    b->gen = std::make_unique<SkyGenerator>(b->settings, b->settings.bakeSize);
    b->rgb.resize((size_t)b->gen->width() * b->gen->height() * 3);
    skyBake_ = std::move(b);
    skyLabMsg_.clear();
    return true;
}

void Editor::stepSkyBake() {
    if (!skyBake_) return;
    SkyBake& b = *skyBake_;
    // A slice per frame (about 40 ms) so the window can show progress.
    const auto t0 = std::chrono::steady_clock::now();
    while (b.row < b.gen->height()) {
        const int n = std::min(64, b.gen->height() - b.row);
        b.gen->rowsParallel(b.row, b.row + n, b.rgb.data());
        b.row += n;
        if (std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(40)) break;
    }
    if (b.row < b.gen->height()) return;

    // Done: write the PNG into the Bottega's skies/, find it, and record the change.
    const std::string file = bottega::path(bottegaRoot_, b.name.substr(0, b.name.find('/'))) + b.name.substr(b.name.find('/')) + ".png";
    std::error_code ec;
    fs::create_directories(fs::path(file).parent_path(), ec);
    Image img{b.rgb.data(), b.gen->width(), b.gen->height(), 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8};
    const bool written = !ec && ExportImage(img, file.c_str());
    if (!written) {
        skyLabMsg_ = "Couldn't write the sky into the Bottega's skies folder.";
        skyBake_.reset();
        return;
    }
    skies_.rescan(); // the new file (or a fresh copy of an old one) is found and loaded again
    Environment e = doc_.environment();
    e.skylab = b.settings;
    e.sky = b.name;
    if (skyLabOpen_) e.ambient = skyLabAmbient_;
    Command c = doc_.makeEnvironment(e, "Sky Lab");
    // The sun lock turns the map's sun light with the sky (the same undo step).
    if (b.settings.lockSun) {
        if (const Light* sun = mapSunLight()) {
            Light l = *sun;
            lightAnglesFromSun(b.settings.sunAzimuth, b.settings.sunAltitude, l.pitch, l.yaw);
            if (l != *sun) {
                Command lc = doc_.makeModifyLights({l}, "Sky Lab");
                for (LightChange& ch : lc.lightChanges) c.lightChanges.push_back(std::move(ch));
            }
        }
    }
    if (e != doc_.environment() || !c.lightChanges.empty()) {
        doc_.apply(std::move(c));
        lighting_.clear();
    }
    const bool close = b.closeAfter;
    skyBake_.reset();
    if (close) closeSkyLab();
}

void Editor::rebakeSky() {
    const Environment& e = doc_.environment();
    if (!e.skylab.used || skyBake_) return;
    SkyLab s = e.skylab;
    if (s.lockSun)
        if (const Light* sun = mapSunLight()) sunFromLightAngles(sun->pitch, sun->yaw, s.sunAzimuth, s.sunAltitude);
    // A missing file comes back under its old name; otherwise under the map's own.
    startSkyBake(s, skyFileMissing() && e.sky.rfind("skies/", 0) == 0 ? e.sky : skyLabBakeName(), false);
    if (skyLabOpen_) {
        skyLabBuf_.sunAzimuth = s.sunAzimuth;
        skyLabBuf_.sunAltitude = s.sunAltitude;
    }
}

void Editor::drawSkyLabWindow() {
    if (!skyLabOpen_) return;
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(ImVec2(center.x * 1.45f, center.y * 0.15f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
    if (skyLabFocus_) ImGui::SetNextWindowFocus();
    skyLabFocus_ = false;
    bool open = true;
    bool apply = false, ok = false, cancel = false;
    if (ImGui::Begin("Sky Lab", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        SkyLab& s = skyLabBuf_;
        // The whole sky at once (the 3D view shows it around the map).
        if (skyLabTex_.id) rlImGuiImageSize(&skyLabTex_, 400, 200);
        else ImGui::Dummy(ImVec2(400, 200));
        ImGui::TextDisabled("The 3D view shows this sky while Sky Lab is open.");

        const char* modes[] = {"Soft sky", "Darker sky", "Custom sky", "Atmosphere off"};
        int mode = (int)s.mode;
        ImGui::SetNextItemWidth(170);
        if (ImGui::Combo("sky mode", &mode, modes, 4)) s.mode = (SkyMode)mode;
        if (s.mode == SkyMode::Custom) {
            colour("sky colour", s.customSky);
            colour("sun glow colour", s.customGlow);
            colour("horizon colour", s.customHorizon);
        } else if (s.mode == SkyMode::Off) {
            colour("atmosphere colour", s.offColor);
        }

        if (ImGui::BeginTabBar("##skylabtabs")) {
            // A tab asked for (tests: skylabtab) is opened once.
            const int askedTab = skyLabTab_;
            skyLabTab_ = -1;
            auto tabFlags = [askedTab](int which) { return askedTab == which ? ImGuiTabItemFlags_SetSelected : 0; };
            if (ImGui::BeginTabItem("Sun & Moon", nullptr, tabFlags(0))) {
                ImGui::BeginGroup();
                sunCompass(s.sunAzimuth, s.sunAltitude);
                ImGui::EndGroup();
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::SetNextItemWidth(90);
                ImGui::InputScalar("azimuth (degrees, like a light's yaw)", ImGuiDataType_Double, &s.sunAzimuth, nullptr, nullptr, "%g");
                ImGui::SetNextItemWidth(90);
                ImGui::InputScalar("altitude (degrees above the horizon)", ImGuiDataType_Double, &s.sunAltitude, nullptr, nullptr, "%g");
                s.sunAltitude = std::clamp(s.sunAltitude, -90.0, 90.0);
                s.sunAzimuth = std::fmod(std::fmod(s.sunAzimuth, 360.0) + 360.0, 360.0);
                if (const Light* sun = mapSunLight()) {
                    const bool was = s.lockSun;
                    ImGui::Checkbox("lock to the map's sun light", &s.lockSun);
                    if (s.lockSun && !was) sunFromLightAngles(sun->pitch, sun->yaw, s.sunAzimuth, s.sunAltitude);
                    ImGui::TextDisabled(s.lockSun ? "moving this sun turns the light too (on Apply)" : "the sky's sun and the light are separate");
                } else {
                    s.lockSun = false;
                    ImGui::TextDisabled("(no sun light in the map to lock to)");
                }
                ImGui::EndGroup();
                colour("sun colour", s.sunColor);
                ImGui::Checkbox("sun visible", &s.sunVisible);
                ImGui::BeginDisabled(!s.sunVisible);
                slider("disk intensity", s.diskIntensity, 0, 100);
                slider("glow intensity", s.glowIntensity, 0, 100);
                slider("disk size (degrees across)", s.diskSize, 0.1, 45, "%.1f");
                slider("horizon illusion (% larger)", s.horizonIllusion, 0, 200);
                ImGui::EndDisabled();
                ImGui::TextDisabled("Moon, stars and comets come in v0.10.6.");
                if (ImGui::CollapsingHeader("Scene lighting (stored; used by renders later)")) {
                    slider("shadows", s.shadows, 0, 100);
                    ImGui::Checkbox("soft shadows", &s.softShadows);
                    ImGui::BeginDisabled(!s.softShadows);
                    slider("softness", s.shadowSoftness, 0, 100);
                    ImGui::EndDisabled();
                    slider("sun diffuse", s.sunDiffuse, -999, 999);
                    slider("sun specular", s.sunSpecular, -999, 999);
                    colour("ambient colour", skyLabAmbient_);
                    colour("sky dome colour", s.skyDome);
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Clouds", nullptr, tabFlags(1))) {
                // Stratus above, cumulus below, as Bryce arranges them.
                auto layer = [&](const char* name, SkyLab::CloudLayer& l, const char* what) {
                    ImGui::PushID(name);
                    ImGui::Checkbox(name, &l.on);
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", what);
                    ImGui::BeginDisabled(!l.on);
                    slider("cover", l.cover, 0, 100);
                    slider("frequency (smaller formations)", l.frequency, 2, 150);
                    slider("amplitude (harder edges)", l.amplitude, 50, 500);
                    slider("turbulence", l.turbulence, 0, 100);
                    slider("complexity (detail)", l.complexity, 1, 8);
                    ImGui::EndDisabled();
                    ImGui::PopID();
                };
                layer("Stratus clouds", s.stratus, "high, thin, wispy");
                ImGui::Spacing();
                layer("Cumulus clouds", s.cumulus, "lower, thick, fluffy");
                ImGui::BeginDisabled(!s.cumulus.on);
                colour("cumulus colour", s.cumulusColor);
                ImGui::EndDisabled();
                ImGui::Separator();
                slider("cloud height (also widens the haze)", s.cloudHeight, 0, 100);
                ImGui::Checkbox("spherical clouds (rounder, denser)", &s.sphericalClouds);
                ImGui::SetNextItemWidth(90);
                ImGui::InputInt("pattern seed", &s.cloudSeed, 0);
                ImGui::SameLine();
                if (ImGui::Button("New pattern")) s.cloudSeed = s.cloudSeed % 9999 + 1;
                ImGui::Checkbox("clouds cast shadows (stored; used by renders later)", &s.cloudShadows);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Atmosphere", nullptr, tabFlags(2))) {
                ImGui::Checkbox("haze", &s.hazeOn);
                ImGui::BeginDisabled(!s.hazeOn);
                colour("haze colour", s.hazeColor);
                slider("haze density", s.hazeDensity, 0, 100);
                slider("haze thickness", s.hazeThickness, 0, 100);
                slider("haze base height", s.hazeBase, -100, 100);
                ImGui::EndDisabled();
                colour("ground colour (below the horizon)", s.groundColor);
                ImGui::TextDisabled("Fog, rainbows and Blend with Sun come in v0.10.6.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        clampSkyLab(s);

        ImGui::Separator();
        int size = s.bakeSize == 8192 ? 2 : s.bakeSize == 4096 ? 1 : 0;
        const char* sizes[] = {"2048 x 1024", "4096 x 2048", "8192 x 4096"};
        ImGui::SetNextItemWidth(130);
        if (ImGui::Combo("baked size", &size, sizes, 3)) s.bakeSize = size == 2 ? 8192 : size == 1 ? 4096 : 2048;
        ImGui::TextDisabled("Baked into the Bottega as %s.png", skyLabBakeName().c_str());
        if (skyOutOfDate()) {
            ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "The sun light has turned since the sky was baked.");
            ImGui::SameLine();
            if (ImGui::Button("Rebake")) rebakeSky();
        }
        if (!bottega::usable(bottegaCheck_)) {
            ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "Sky Lab saves its skies in the Bottega: set a Bottega folder first.");
            if (ImGui::Button("Bottega folder...")) {
                std::snprintf(bottegaBuf_, sizeof bottegaBuf_, "%s", bottegaRoot_.c_str());
                bottegaMsg_.clear();
                askBottega_ = true;
            }
        } else if (!skyLabMsg_.empty()) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", skyLabMsg_.c_str());
        }
        if (skyBake_) {
            ImGui::ProgressBar((float)skyBake_->row / (float)skyBake_->gen->height(), ImVec2(260, 0), "baking...");
        } else {
            const bool canBake = bottega::usable(bottegaCheck_);
            ImGui::BeginDisabled(!canBake);
            apply = ImGui::Button("Apply");
            ImGui::SameLine();
            ok = ImGui::Button("OK");
            ImGui::EndDisabled();
            ImGui::SameLine();
            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            cancel = ImGui::Button("Cancel (Esc)") || (focused && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) || closeDialogs_;
        }
        updateSkyLabPreview(ImGui::IsAnyItemActive());
    }
    ImGui::End();
    if (!open) cancel = true;
    if (apply || ok) startSkyBake(skyLabBuf_, skyLabBakeName(), ok);
    else if (cancel && !skyBake_) closeSkyLab();
}

} // namespace scal
