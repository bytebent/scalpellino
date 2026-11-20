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
#include <fstream>
#include "app/editor_util.hpp"

namespace scal {

using namespace editor_util;
namespace fs = std::filesystem;

namespace {
constexpr int kPreviewSmall = 256; // while a value is being dragged
constexpr int kPreviewLarge = 1024; // once it is let go
constexpr double kPi = 3.14159265358979323846;

bool sameAngle(double a, double b) { return std::fabs(a - b) < 1e-4; }

// The part of the map's fog a sky depends on ("" = none in the sky).
std::string fogKey(bool on, const Fog& f) {
    if (!on || f.thickness <= 0) return "";
    char b[200];
    std::snprintf(b, sizeof b, "%g %g %g %g %g %g %g", f.color.x, f.color.y, f.color.z, f.start, f.end, f.base, f.thickness);
    return b;
}

SkyExtras fogExtras(bool on, const Fog& f) {
    SkyExtras x;
    x.fogOn = on;
    x.fogColor = f.color;
    x.fogStart = f.start;
    x.fogEnd = f.end;
    x.fogBase = f.base;
    x.fogThickness = f.thickness;
    return x;
}

bool imageFile(const fs::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp";
}

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
bool sunCompass(const char* id, double& azimuth, double& altitude, ImU32 dotColour = IM_COL32(255, 220, 120, 255)) {
    ImGui::PushID(id);
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
    dl->AddCircleFilled(s, 6, altitude >= 0 ? dotColour : IM_COL32(110, 100, 80, 255), 16);
    ImGui::PopID();
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
    const Environment& e = doc_.environment();
    const SkyLab& s = e.skylab;
    if (!s.used) return false;
    // The map's fog changed since the bake (only matters when it reaches into the sky).
    if (fogKey(e.fogOn, e.fog) != s.bakedFog) return true;
    if (!s.lockSun) return false;
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

SkyExtras Editor::skyExtras(const SkyLab& s, bool fogOn, const Fog& fog) {
    SkyExtras x = fogExtras(fogOn, fog);
    if (!s.moonVisible || s.moonTexture == "none") return x;
    // The moon's image, loaded once and kept until another one is chosen.
    if (moonLoaded_ != s.moonTexture) {
        moonLoaded_ = s.moonTexture;
        moonPixels_.clear();
        moonW_ = moonH_ = 0;
        std::string file;
        if (s.moonTexture == "builtin") file = moonBuiltinPath_;
        else if (bottega::usable(bottegaCheck_)) file = bottega::path(bottegaRoot_, s.moonTexture);
        Image img = file.empty() ? Image{} : LoadImage(file.c_str());
        if (img.data) {
            // Bigger than 1024 wide never shows (the moon is small on the sky).
            if (img.width > 1024) ImageResize(&img, 1024, std::max(1, img.height * 1024 / img.width));
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
            moonW_ = img.width;
            moonH_ = img.height;
            const unsigned char* p = (const unsigned char*)img.data;
            moonPixels_.assign(p, p + (size_t)moonW_ * moonH_ * 3);
            UnloadImage(img);
        } else if (!file.empty()) {
            skyLabMsg_ = "Couldn't read the moon image; the moon is drawn plain.";
        }
    }
    if (!moonPixels_.empty()) x.moon = {moonW_, moonH_, moonPixels_.data()};
    return x;
}

std::vector<std::string> Editor::moonImages() const {
    std::vector<std::string> out;
    if (!bottega::usable(bottegaCheck_)) return out;
    std::error_code ec;
    const fs::path dir = fs::path(bottega::path(bottegaRoot_, "skies")) / "moons";
    for (fs::directory_iterator it(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec) && imageFile(it->path())) out.push_back("skies/moons/" + it->path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> Editor::skyPresets() const {
    std::vector<std::string> out;
    if (!bottega::usable(bottegaCheck_)) return out;
    std::error_code ec;
    const fs::path dir = bottega::path(bottegaRoot_, "presets/skies");
    for (fs::directory_iterator it(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == ".skylab") out.push_back(it->path().stem().string());
    std::sort(out.begin(), out.end());
    return out;
}

// A saved sky is a text file in the Bottega's presets/skies/: the settings as saved in maps.
bool Editor::saveSkyPreset(const std::string& raw) {
    std::string name;
    for (char c : raw)
        name += (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == ' ') ? c : '_';
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.empty() || !bottega::usable(bottegaCheck_)) return false;
    std::error_code ec;
    const fs::path dir = bottega::path(bottegaRoot_, "presets/skies");
    fs::create_directories(dir, ec);
    std::ofstream f(dir / (name + ".skylab"));
    if (!f) return false;
    SkyLab s = skyLabBuf_;
    s.bakedFog.clear();
    f << skyLabToString(s) << "\n";
    return (bool)f;
}

bool Editor::loadSkyPreset(const std::string& name) {
    if (!bottega::usable(bottegaCheck_)) return false;
    std::ifstream f(fs::path(bottega::path(bottegaRoot_, "presets/skies")) / (name + ".skylab"));
    std::string text;
    if (!f || !std::getline(f, text)) return false;
    const bool lock = skyLabBuf_.lockSun;
    const int size = skyLabBuf_.bakeSize;
    skyLabBuf_ = skyLabFromString(text);
    // The sun lock and the baked size belong to this map, not to the saved sky.
    skyLabBuf_.lockSun = lock && mapSunLight();
    skyLabBuf_.bakeSize = size;
    if (skyLabBuf_.lockSun) sunFromLightAngles(mapSunLight()->pitch, mapSunLight()->yaw, skyLabBuf_.sunAzimuth, skyLabBuf_.sunAltitude);
    return true;
}

void Editor::randomizeSky() {
    const SkyLab old = skyLabBuf_;
    skyLabBuf_ = randomSkyLab(skyLabRandomSeed_++);
    skyLabBuf_.lockSun = old.lockSun;
    skyLabBuf_.bakeSize = old.bakeSize;
    skyLabBuf_.moonTexture = old.moonTexture;
    if (skyLabBuf_.lockSun)
        if (const Light* sun = mapSunLight()) sunFromLightAngles(sun->pitch, sun->yaw, skyLabBuf_.sunAzimuth, skyLabBuf_.sunAltitude);
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
    skyLabFogOn_ = e.fogOn;
    skyLabFog_ = e.fog;
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
    const SkyExtras extras = skyExtras(skyLabBuf_, skyLabFogOn_, skyLabFog_);
    const std::string key = skyLabToString(skyLabBuf_) + "|" + fogKey(skyLabFogOn_, skyLabFog_) + "|" + moonLoaded_ + "|" + std::to_string(moonW_);
    if (key == skyLabPreviewKey_ && skyLabPreviewW_ >= want) return; // nothing new
    const std::vector<unsigned char> rgb = generateSky(skyLabBuf_, want, extras);
    Image img{(void*)rgb.data(), want, want / 2, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8};
    if (skyLabTex_.id && skyLabTex_.width == want) UpdateTexture(skyLabTex_, rgb.data());
    else {
        if (skyLabTex_.id) UnloadTexture(skyLabTex_);
        skyLabTex_ = LoadTextureFromImage(img);
        SetTextureFilter(skyLabTex_, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(skyLabTex_, TEXTURE_WRAP_CLAMP);
    }
    skyLabPreviewKey_ = key;
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
    // From the window: its fog goes to the map with the sky. Otherwise the map's own.
    b->setFog = skyLabOpen_;
    b->fogOn = skyLabOpen_ ? skyLabFogOn_ : doc_.environment().fogOn;
    b->fog = skyLabOpen_ ? skyLabFog_ : doc_.environment().fog;
    b->settings.bakedFog = fogKey(b->fogOn, b->fog);
    b->gen = std::make_unique<SkyGenerator>(b->settings, b->settings.bakeSize, skyExtras(b->settings, b->fogOn, b->fog));
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
    if (b.setFog) {
        e.ambient = skyLabAmbient_;
        e.fogOn = b.fogOn;
        e.fog = b.fog;
    }
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
    // A missing file comes back under its old name; otherwise under the map's own. The
    // map's fog is used (and the window's edits stay in the window).
    const bool wasOpen = skyLabOpen_;
    skyLabOpen_ = false;
    startSkyBake(s, skyFileMissing() && e.sky.rfind("skies/", 0) == 0 ? e.sky : skyLabBakeName(), false);
    skyLabOpen_ = wasOpen;
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
            // A tab asked for (tests: skylabtab) is opened once; a section too.
            const int askedTab = skyLabTab_;
            skyLabTab_ = -1;
            auto tabFlags = [askedTab](int which) { return askedTab == which ? ImGuiTabItemFlags_SetSelected : 0; };
            // The tabs scroll inside a fixed height so the window fits the screen.
            const float tabHeight = std::min(480.0f, ImGui::GetMainViewport()->WorkSize.y - 420.0f);
            const std::string askedSection = skyLabSection_;
            skyLabSection_.clear();
            auto section = [&](const char* name, bool openFirst) {
                const bool asked = !askedSection.empty() && std::string(name).rfind(askedSection, 0) == 0;
                if (asked) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                else ImGui::SetNextItemOpen(openFirst, ImGuiCond_Once);
                const bool shown = ImGui::CollapsingHeader(name);
                if (asked) ImGui::SetScrollHereY(0.0f); // brought into view
                return shown;
            };
            if (ImGui::BeginTabItem("Sun & Moon", nullptr, tabFlags(0))) {
                ImGui::BeginChild("##sunmoon", ImVec2(560, std::max(200.0f, tabHeight)));
                if (section("Sun", true)) {
                    ImGui::BeginGroup();
                    sunCompass("sun", s.sunAzimuth, s.sunAltitude);
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
                    ImGui::EndDisabled();
                    slider("horizon illusion (% larger, sun and moon)", s.horizonIllusion, 0, 200);
                }
                if (section("Moon", s.moonVisible)) {
                    ImGui::PushID("moon");
                    ImGui::Checkbox("moon visible", &s.moonVisible);
                    ImGui::BeginDisabled(!s.moonVisible);
                    ImGui::BeginGroup();
                    sunCompass("moon", s.moonAzimuth, s.moonAltitude, IM_COL32(225, 230, 240, 255));
                    ImGui::EndGroup();
                    ImGui::SameLine();
                    ImGui::BeginGroup();
                    ImGui::SetNextItemWidth(90);
                    ImGui::InputScalar("azimuth", ImGuiDataType_Double, &s.moonAzimuth, nullptr, nullptr, "%g");
                    ImGui::SetNextItemWidth(90);
                    ImGui::InputScalar("altitude", ImGuiDataType_Double, &s.moonAltitude, nullptr, nullptr, "%g");
                    if (ImGui::Button("Move moon onto sun")) {
                        s.moonAzimuth = s.sunAzimuth;
                        s.moonAltitude = s.sunAltitude;
                    }
                    ImGui::TextDisabled("(a solar eclipse)");
                    ImGui::EndGroup();
                    slider("phase (0 new, 50 full)", s.moonPhase, 0, 100);
                    slider("size (degrees across)", s.moonSize, 0.1, 45, "%.1f");
                    slider("disk intensity", s.moonIntensity, 0, 100);
                    slider("glow intensity", s.moonGlow, 0, 100);
                    slider("earthshine (the dark part)", s.earthshine, 0, 100);
                    slider("softness", s.moonSoftness, 0, 100);
                    colour("moon colour", s.moonColor);
                    // The texture: the built-in NASA map, none, or one from the Bottega.
                    const std::vector<std::string> own = moonImages();
                    std::string label = s.moonTexture == "builtin" ? "Built-in moon" : s.moonTexture == "none" ? "None (plain colour)" : s.moonTexture;
                    ImGui::SetNextItemWidth(260);
                    if (ImGui::BeginCombo("moon texture", label.c_str())) {
                        if (ImGui::Selectable("Built-in moon", s.moonTexture == "builtin")) s.moonTexture = "builtin";
                        if (ImGui::Selectable("None (plain colour)", s.moonTexture == "none")) s.moonTexture = "none";
                        for (const std::string& k : own)
                            if (ImGui::Selectable(k.c_str(), s.moonTexture == k)) s.moonTexture = k;
                        ImGui::EndCombo();
                    }
                    if (s.moonTexture == "builtin") ImGui::TextDisabled("Built-in moon: NASA's Scientific Visualization Studio");
                    else ImGui::TextDisabled("Your own: images in the Bottega's skies/moons (2:1 = a map, square = the face)");
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                if (section("Eclipses", false)) {
                    const double cover = solarEclipseCover(s);
                    if (cover > 0) ImGui::Text("Solar eclipse: the moon covers %.0f%% of the sun.", cover * 100);
                    else ImGui::TextDisabled("Solar eclipse: move the moon over the sun (Moon > Move moon onto sun).");
                    slider("corona intensity", s.coronaIntensity, 0, 100);
                    slider("corona size", s.coronaSize, 0, 100);
                    ImGui::BeginDisabled(!s.moonVisible);
                    slider("lunar eclipse (Earth's shadow)", s.lunarEclipse, 0, 100);
                    colour("eclipse colour", s.eclipseColor);
                    ImGui::EndDisabled();
                }
                if (section("Stars and comets", s.stars)) {
                    ImGui::Checkbox("stars", &s.stars);
                    ImGui::BeginDisabled(!s.stars);
                    int field = s.earthStars ? 1 : 0;
                    ImGui::RadioButton("random field", &field, 0);
                    ImGui::SameLine();
                    ImGui::RadioButton("Earth's sky", &field, 1);
                    s.earthStars = field == 1;
                    slider("intensity", s.starIntensity, 0, 100);
                    slider("amount (brightest first)", s.starAmount, 0, 10000);
                    slider("latitude", s.starLatitude, -90, 90);
                    slider("star time (hours)", s.starTime, 0, 24, "%.1f");
                    ImGui::SetNextItemWidth(90);
                    ImGui::InputInt("star seed", &s.starSeed, 0);
                    ImGui::SameLine();
                    if (ImGui::Button("New stars")) s.starSeed = s.starSeed % 9999 + 1;
                    ImGui::Checkbox("comets", &s.comets);
                    ImGui::BeginDisabled(!s.comets);
                    slider("comet intensity", s.cometIntensity, 0, 100);
                    slider("comet amount", s.cometAmount, 0, 50);
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                }
                if (section("Rings", s.rings)) {
                    ImGui::Checkbox("halo rings round the sun and moon", &s.rings);
                    ImGui::BeginDisabled(!s.rings);
                    slider("ring intensity", s.ringIntensity, 0, 100);
                    slider("ring radius (50 = 22 degrees)", s.ringRadius, 0, 100);
                    ImGui::Checkbox("secondary ring", &s.secondaryRing);
                    ImGui::EndDisabled();
                }
                if (section("Scene lighting (stored; used by renders later)", false)) {
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
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Clouds", nullptr, tabFlags(1))) {
                ImGui::BeginChild("##clouds", ImVec2(560, std::max(200.0f, tabHeight)));
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
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Atmosphere", nullptr, tabFlags(2))) {
                ImGui::BeginChild("##atmosphere", ImVec2(560, std::max(200.0f, tabHeight)));
                if (section("Haze", true)) {
                    ImGui::Checkbox("haze", &s.hazeOn);
                    ImGui::BeginDisabled(!s.hazeOn);
                    colour("haze colour", s.hazeColor);
                    slider("haze density", s.hazeDensity, 0, 100);
                    slider("haze thickness", s.hazeThickness, 0, 100);
                    slider("haze base height", s.hazeBase, -100, 100);
                    ImGui::EndDisabled();
                    colour("ground colour (below the horizon)", s.groundColor);
                }
                if (section("Fog (the map's fog, as in World > Environment)", skyLabFogOn_)) {
                    ImGui::PushID("fog");
                    ImGui::Checkbox("fog", &skyLabFogOn_);
                    ImGui::BeginDisabled(!skyLabFogOn_);
                    colour("fog colour", skyLabFog_.color);
                    ImGui::SetNextItemWidth(100);
                    ImGui::InputScalar("clear up to (cm)", ImGuiDataType_Double, &skyLabFog_.start, nullptr, nullptr, "%g");
                    ImGui::SetNextItemWidth(100);
                    ImGui::InputScalar("fully fogged from (cm)", ImGuiDataType_Double, &skyLabFog_.end, nullptr, nullptr, "%g");
                    ImGui::SetNextItemWidth(100);
                    ImGui::InputScalar("base height (cm)", ImGuiDataType_Double, &skyLabFog_.base, nullptr, nullptr, "%g");
                    ImGui::SetNextItemWidth(100);
                    ImGui::InputScalar("thickness (cm; 0 = not in the sky)", ImGuiDataType_Double, &skyLabFog_.thickness, nullptr, nullptr, "%g");
                    skyLabFog_.thickness = std::max(0.0, skyLabFog_.thickness);
                    skyLabFog_.end = std::max(skyLabFog_.end, skyLabFog_.start + 1);
                    ImGui::TextDisabled("With a thickness, the fog lies in a layer and shows in the sky.");
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                if (section("Rainbow", s.rainbow)) {
                    ImGui::Checkbox("rainbow (opposite the sun, while it is up)", &s.rainbow);
                    ImGui::BeginDisabled(!s.rainbow);
                    slider("width", s.rainbowRadius, 0, 100, "%.2f");
                    slider("opacity", s.rainbowOpacity, 0, 100);
                    ImGui::Checkbox("secondary bow", &s.secondaryBow);
                    ImGui::EndDisabled();
                }
                if (section("Blend with Sun", false)) {
                    ImGui::Checkbox("blend with sun", &s.blendSun);
                    ImGui::BeginDisabled(!s.blendSun);
                    slider("colour", s.blendColor, 0, 100);
                    slider("luminance", s.blendLuminance, 0, 100);
                    ImGui::Checkbox("blend haze", &s.blendHaze);
                    ImGui::SameLine();
                    ImGui::Checkbox("blend fog", &s.blendFog);
                    ImGui::EndDisabled();
                    ImGui::TextDisabled("Soft and Darker skies; Custom and Atmosphere off keep their colours.");
                }
                if (section("Scene effects (stored; used by renders later)", false)) {
                    ImGui::Checkbox("volumetric world (visible sunlight)", &s.volumetric);
                    ImGui::BeginDisabled(!s.volumetric);
                    slider("quality", s.volumetricQuality, 0, 100);
                    slider("density", s.volumetricDensity, 0, 100);
                    ImGui::EndDisabled();
                    ImGui::Checkbox("colour perspective", &s.colorPerspective);
                    ImGui::BeginDisabled(!s.colorPerspective);
                    colour("colour perspective (rates)", s.colorPerspectiveRgb);
                    ImGui::EndDisabled();
                    ImGui::Checkbox("light the scene with this sky (IBL)", &s.ibl);
                    ImGui::BeginDisabled(!s.ibl);
                    slider("IBL intensity", s.iblIntensity, 0, 999);
                    ImGui::EndDisabled();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        clampSkyLab(s);

        ImGui::Separator();
        // Saved skies (the Bottega's presets/skies) and Randomize Sky.
        {
            const std::vector<std::string> presets = skyPresets();
            ImGui::BeginDisabled(!bottega::usable(bottegaCheck_));
            ImGui::SetNextItemWidth(170);
            if (ImGui::BeginCombo("##presets", "Load a saved sky...")) {
                for (const std::string& p : presets)
                    if (ImGui::Selectable(p.c_str())) loadSkyPreset(p);
                if (presets.empty()) ImGui::TextDisabled("(none saved yet)");
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            ImGui::InputTextWithHint("##presetname", "name", skyPresetName_, sizeof skyPresetName_);
            ImGui::SameLine();
            if (ImGui::Button("Save sky") && skyPresetName_[0]) {
                if (!saveSkyPreset(skyPresetName_)) skyLabMsg_ = "Couldn't save the sky in the Bottega's presets/skies.";
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Randomize sky")) randomizeSky();
        }
        int size = s.bakeSize == 8192 ? 2 : s.bakeSize == 4096 ? 1 : 0;
        const char* sizes[] = {"2048 x 1024", "4096 x 2048", "8192 x 4096"};
        ImGui::SetNextItemWidth(130);
        if (ImGui::Combo("baked size", &size, sizes, 3)) s.bakeSize = size == 2 ? 8192 : size == 1 ? 4096 : 2048;
        ImGui::TextDisabled("Baked into the Bottega as %s.png", skyLabBakeName().c_str());
        if (skyOutOfDate()) {
            ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "The sun light or the fog has changed since the sky was baked.");
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
