// Editor (part of the class, split by topic; see editor.hpp): Render (milestone 12).
// Render (status bar button, Render menu, Cmd+Shift+K) draws the active camera's picture at
// the camera's size and saves it in the Bottega's renders/ with its log. The editor waits
// while it runs: one step per frame, so the terminal window shows each step as it happens;
// Esc stops it between steps. The Camera Preview is the same renderer at Draft settings.
#include "app/editor.hpp"
#include "app/settings.hpp"
#include "core/bottega.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace scal {

namespace fs = std::filesystem;

void Editor::loadRenderSettings() {
    const std::string dir = settingsDir();
    if (dir.empty()) return;
    std::ifstream f(dir + "/render.txt");
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    renderSettings_ = renderSettingsFromString(ss.str());
}

void Editor::saveRenderSettings() const {
    if (opt_.automated()) return; // tests never touch user settings
    const std::string dir = settingsDir();
    if (dir.empty()) return;
    std::ofstream f(dir + "/render.txt");
    f << renderSettingsToString(renderSettings_);
}

void Editor::renderLog(const std::string& line) {
    if (!renderJob_) return;
    char t[32];
    std::snprintf(t, sizeof t, "[%6.2f s] ", GetTime() - renderJob_->start);
    renderJob_->log.push_back(t + line);
    renderJob_->scrollDown = true;
}

void Editor::startRender() {
    if (renderJob_ && renderJob_->running) return;
    const Camera* cam = doc_.activeCamera();
    // Renders live in the Bottega: without one, ask for it (as Sky Lab does).
    if (cam && !bottega::usable(bottegaCheck_)) {
        std::snprintf(bottegaBuf_, sizeof bottegaBuf_, "%s", bottegaRoot_.c_str());
        bottegaMsg_ = "Renders are saved in the Bottega's renders folder: choose the Bottega first, then Render again.";
        askBottega_ = true;
        return;
    }
    renderJob_ = std::make_unique<RenderJob>();
    RenderJob& j = *renderJob_;
    j.start = GetTime();
    j.settings = renderSettings_;
    renderOpen_ = true;
    if (!cam) {
        renderLog("No camera in the map: place one with the camera tool (Shift+K), then Render again.");
        j.failed = true;
        return;
    }
    j.cam = *cam;
    j.running = true;
    std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    const fs::path dir = bottega::path(bottegaRoot_, "renders");
    std::string stem = renderFileStem(path_, cam->name, local);
    // Never over an earlier render (two in the same second).
    std::error_code ec;
    for (int n = 2; fs::exists(dir / (stem + ".png"), ec) || fs::exists(dir / (stem + ".log"), ec); ++n) {
        stem = renderFileStem(path_, cam->name, local) + "_" + std::to_string(n);
    }
    j.png = (dir / (stem + ".png")).string();
    j.logFile = (dir / (stem + ".log")).string();
    j.name = "renders/" + stem + ".png";
}

void Editor::stopRender() {
    if (!renderJob_ || !renderJob_->running) return;
    renderJob_->stopWanted = true;
}

void Editor::finishRender() {
    RenderJob& j = *renderJob_;
    j.running = false;
    j.draw.release(); // the large picture's graphics memory
    j.scene = Scene{};
    if (!j.done || j.logFile.empty()) return; // a stopped or failed render leaves no files
    std::error_code ec;
    fs::create_directories(fs::path(j.logFile).parent_path(), ec);
    std::ofstream f(j.logFile);
    for (const std::string& l : j.log) f << l << '\n';
}

void Editor::stepRender() {
    if (!renderJob_ || !renderJob_->running) return;
    RenderJob& j = *renderJob_;
    if (j.stopWanted) {
        renderLog("Stopped (Esc). Nothing was saved.");
        j.stopped = true;
        finishRender();
        return;
    }
    switch (j.step++) {
    case 0: {
        std::string map = path_.empty() ? std::string("untitled (not saved yet)") : fs::path(path_).filename().string();
        renderLog("Render: " + map + ", camera \"" + j.cam.name + "\"");
        char b[256];
        j.aa = fitAA(j.cam.width, j.cam.height, j.settings.aa);
        if (j.aa == 0) {
            std::snprintf(b, sizeof b, "The picture is too large to draw (%d x %d). Make it smaller on the camera.", j.cam.width, j.cam.height);
            renderLog(b);
            j.failed = true;
            finishRender();
            return;
        }
        char aaText[32];
        if (j.settings.aa > 1) std::snprintf(aaText, sizeof aaText, "anti-aliasing %dx", j.aa);
        else std::snprintf(aaText, sizeof aaText, "no anti-aliasing");
        std::snprintf(b, sizeof b, "Picture %d x %d (the camera's size), quality %s, %s, textures %s", j.cam.width, j.cam.height,
                      renderQualityName(j.settings.quality), aaText, j.settings.smooth ? "smooth" : "crisp");
        renderLog(b);
        if (j.settings.aa > 1) {
            std::snprintf(b, sizeof b, "  anti-aliasing %dx: drawn at %d x %d, then scaled down", j.aa, j.cam.width * j.aa, j.cam.height * j.aa);
            renderLog(b);
            if (j.aa < j.settings.aa) {
                std::snprintf(b, sizeof b, "  (%dx would be too large for the graphics card at this size)", j.settings.aa);
                renderLog(b);
            }
        }
        if (j.cam.ortho) std::snprintf(b, sizeof b, "  orthographic, %g units across", j.cam.orthoSize);
        else std::snprintf(b, sizeof b, "  lens %g mm (%g degrees), near %g, far %g", std::round(fovToLens(j.cam.fov) * 10) / 10,
                           std::round(j.cam.fov * 10) / 10, j.cam.nearDist, j.cam.farDist);
        renderLog(b);
        renderLog("Gathering the map...");
        break;
    }
    case 1: {
        Renderer::Stats st;
        j.scene = j.draw.scene(doc_, textures_, models_, skies_, &st);
        char b[256];
        std::snprintf(b, sizeof b, "  %zu brushes%s, %zu primitives, %zu props, %zu lights", st.brushes,
                      st.hiddenBrushes ? (" (" + std::to_string(st.hiddenBrushes) + " hidden in the editor, drawn anyway)").c_str() : "",
                      st.prims, st.props, st.lights);
        renderLog(b);
        if (!st.lights) renderLog("  no lights: only the ambient light (World > Light tool)");
        if (st.terrains) renderLog("  " + std::to_string(st.terrains) + " terrain patches");
        if (st.nodrawFaces) renderLog("  " + std::to_string(st.nodrawFaces) + " nodraw faces left out");
        if (st.missingProps) renderLog("  warning: " + std::to_string(st.missingProps) + " props have no model file (left out; Rescan the Bottega?)");
        for (const std::string& t : st.missingTextures) renderLog("  warning: texture \"" + t + "\" not found (drawn as a checker)");
        if (st.sky.empty()) renderLog("  sky: none");
        else if (!st.skyProblem.empty()) renderLog("  warning: sky \"" + st.sky + "\": " + st.skyProblem + " (left out)");
        else renderLog("  sky: " + st.sky);
        if (!st.fog) renderLog("  fog: off");
        else {
            const Fog& f = doc_.environment().fog;
            if (f.thickness > 0) std::snprintf(b, sizeof b, "  fog: %g to %g, lying from %g up through %g", f.start, f.end, f.base, f.thickness);
            else std::snprintf(b, sizeof b, "  fog: %g to %g", f.start, f.end);
            renderLog(b);
        }
        renderLog("Lighting (rough for now: lights and ambient, no shadows; baked light comes next)...");
        break;
    }
    case 2:
        j.draw.light(doc_, j.scene);
        renderLog("  done");
        renderLog("Drawing...");
        break;
    case 3: {
        if (!j.draw.draw(j.cam, j.scene, j.cam.width * j.aa, j.cam.height * j.aa, 1.0f, j.settings.smooth)) {
            renderLog("The graphics card couldn't make a picture this large. Try less anti-aliasing (Render menu).");
            j.failed = true;
            finishRender();
            return;
        }
        renderLog(j.aa > 1 ? "Scaling down..." : "Reading the picture...");
        break;
    }
    case 4: {
        int w = 0, h = 0;
        const std::vector<unsigned char> rgba = j.draw.read(w, h);
        j.rgb = downscaleRGB(rgba, w, h, j.aa);
        j.w = w / std::max(1, j.aa);
        j.h = h / std::max(1, j.aa);
        j.draw.release();
        if (j.rgb.empty()) {
            renderLog("Couldn't read the picture back from the graphics card.");
            j.failed = true;
            finishRender();
            return;
        }
        renderLog("Saving " + j.name + "...");
        break;
    }
    default: {
        std::error_code ec;
        fs::create_directories(fs::path(j.png).parent_path(), ec);
        Image img{};
        img.data = j.rgb.data();
        img.width = j.w;
        img.height = j.h;
        img.mipmaps = 1;
        img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
        if (!ExportImage(img, j.png.c_str())) {
            renderLog("Couldn't save " + j.name + " (is the Bottega folder writable?).");
            j.failed = true;
        } else {
            char b[64];
            std::snprintf(b, sizeof b, "Done in %.1f s.", GetTime() - j.start);
            renderLog("  saved, with this log beside it");
            renderLog(b);
            j.done = true;
            ++rendersDone_;
            renderFiles_.push_back(j.png);
        }
        j.rgb.clear();
        j.rgb.shrink_to_fit();
        finishRender();
        break;
    }
    }
}

bool Editor::renderNeedsFrame() const { return renderJob_ && renderJob_->running; }

void Editor::drawRenderWindow() {
    if (!renderOpen_) return;
    if (!ImGui::IsPopupOpen("Render###render")) ImGui::OpenPopup("Render###render");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(680, 380), ImGuiCond_Appearing);
    const bool running = renderJob_ && renderJob_->running;
    if (ImGui::BeginPopupModal("Render###render", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        // The terminal: the steps as they run.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.06f, 0.07f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.86f, 0.74f, 1));
        const float footer = ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginChild("##renderlog", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
            if (renderJob_)
                for (const std::string& l : renderJob_->log) {
                    const bool warn = l.find("warning") != std::string::npos || l.find("Couldn't") != std::string::npos ||
                                      l.find("Stopped") != std::string::npos || l.find("too large") != std::string::npos;
                    if (warn) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.75f, 0.3f, 1));
                    ImGui::TextUnformatted(l.c_str());
                    if (warn) ImGui::PopStyleColor();
                }
            if (running) ImGui::TextUnformatted("_");
            if (renderJob_ && renderJob_->scrollDown) {
                ImGui::SetScrollHereY(1.0f);
                renderJob_->scrollDown = false;
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        if (running) {
            ImGui::TextDisabled("Rendering... Esc stops it.");
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) stopRender();
        } else {
            if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                closeDialogs_) {
                renderOpen_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

} // namespace scal
