// Scalpellino — entry point and main loop.
//
// Idle policy: the app sleeps in glfwWaitEvents (raylib EnableEventWaiting) whenever
// nothing is happening. After any event it renders a few "settle" frames so ImGui can
// finish layout changes, then sleeps again. It runs continuously only while the editor
// says it is busy (flying, right-mouse look, scripted runs).
#include "raylib.h"
#include "imgui.h"
#include "rlImGui.h"

#include "app/editor.hpp"
#include "app/settings.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace scal;

namespace {
constexpr int kSettleFrames = 2;

EditorOptions parseArgs(int argc, char** argv) {
    EditorOptions o;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0) o.screenshot = argv[++i];
        else if (std::strcmp(argv[i], "--open") == 0) o.openPath = argv[++i];
        else if (std::strcmp(argv[i], "--script") == 0) o.scriptPath = argv[++i];
    }
    return o;
}
} // namespace

int main(int argc, char** argv) {
    const EditorOptions opt = parseArgs(argc, argv);

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1440, 900, "Scalpellino");
    if (!IsWindowReady()) {
        std::fprintf(stderr, "Scalpellino: could not open an OpenGL window.\n");
        return 2;
    }
    SetExitKey(KEY_NULL); // Esc must never quit the app
    {
        const int hz = GetMonitorRefreshRate(GetCurrentMonitor());
        SetTargetFPS(hz > 0 ? hz : 60);
    }

    rlImGuiSetup(true);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    static std::string iniPath;
    if (std::string dir = settingsDir(); !dir.empty() && !opt.automated()) {
        iniPath = dir + "/layout.ini";
        io.IniFilename = iniPath.c_str();
    } else {
        io.IniFilename = nullptr; // automated runs never touch user settings
    }

    int exitCode = 0;
    {
        Editor editor(opt);
        int settle = kSettleFrames;
        while (!editor.shouldExit()) {
            if (WindowShouldClose()) editor.requestQuit(); // asks about unsaved changes

            BeginDrawing();
            ClearBackground({30, 30, 34, 255});
            rlImGuiBegin();
            const bool busy = editor.frame(GetFrameTime());
            rlImGuiEnd();

            if (busy || settle > 0) {
                DisableEventWaiting();
                if (!busy) --settle;
            } else {
                EnableEventWaiting();
                settle = kSettleFrames;
            }
            EndDrawing();

            const std::string shot = editor.takeScreenshotRequest();
            if (!shot.empty()) TakeScreenshot(shot.c_str());
        }
        exitCode = editor.exitCode();
    } // editor (and its GPU textures) destroyed before the context

    rlImGuiShutdown();
    CloseWindow();
    return exitCode;
}
