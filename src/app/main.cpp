// Scalpellino — entry point and main loop.
//
// Idle policy: the app sleeps in glfwWaitEvents (raylib EnableEventWaiting) whenever
// nothing is happening. After any event it renders a few "settle" frames so ImGui can
// finish layout changes, then sleeps again. It runs continuously only while flying.
#include "raylib.h"
#include "imgui.h"
#include "rlImGui.h"

#include "app/settings.hpp"
#include "core/grid.hpp"
#include "render/viewport.hpp"
#include "ui/layout.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

using namespace scal;

namespace {

constexpr int kSettleFrames = 2;

struct Options {
    const char* screenshot = nullptr; // --screenshot <file.png>: render a few frames, save, exit
};

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) o.screenshot = argv[++i];
    }
    return o;
}

// Physical-key shortcuts (layout independent). On an Italian keyboard the two keys
// right of P ("è" and "+") sit where US "[" and "]" are.
constexpr int kKeyGridSmaller = KEY_LEFT_BRACKET;
constexpr int kKeyGridLarger  = KEY_RIGHT_BRACKET;

} // namespace

int main(int argc, char** argv) {
    const Options opt = parseArgs(argc, argv);

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1440, 900, "Scalpellino");
    if (!IsWindowReady()) {
        std::fprintf(stderr, "Scalpellino: could not open an OpenGL window.\n");
        return 2;
    }
    SetExitKey(KEY_NULL); // Esc must never quit the app
    {
        int hz = GetMonitorRefreshRate(GetCurrentMonitor());
        SetTargetFPS(hz > 0 ? hz : 60);
    }

    rlImGuiSetup(true);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    static std::string iniPath;
    if (std::string dir = settingsDir(); !dir.empty() && !opt.screenshot) {
        iniPath = dir + "/layout.ini";
        io.IniFilename = iniPath.c_str();
    } else {
        io.IniFilename = nullptr; // no settings dir (or automated run): don't write anything
    }

    Grid grid;
    // Hammer default arrangement: 3D top-left, top top-right, front bottom-left, side bottom-right.
    std::unique_ptr<Viewport> views[4] = {
        std::make_unique<Viewport>(ViewKind::Persp),
        std::make_unique<Viewport>(ViewKind::Top),
        std::make_unique<Viewport>(ViewKind::Front),
        std::make_unique<Viewport>(ViewKind::Side),
    };
    Viewport& view3d = *views[0];

    bool flying = false;        // Z toggles: mouse captured, WASD moves
    bool rmbLook = false;       // holding right mouse in the 3D view
    bool skipLookDelta = false; // first delta after capturing the cursor is garbage
    int  panningView = -1;      // index of the 2D view being panned, or -1
    bool resetLayout = false;
    int  settle = kSettleFrames;
    unsigned long long redraws = 0;
    int  hoveredView = -1;
    double cursorU = 0, cursorV = 0; // world coords under the cursor in the hovered 2D view
    char status[256] = "";

    auto captureMouse = [&](bool on) {
        if (on) { DisableCursor(); io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange; skipLookDelta = true; }
        else    { EnableCursor();  io.ConfigFlags &= ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange); }
    };

    int frame = 0;
    bool quit = false;
    while (!WindowShouldClose() && !quit) {
        const float dt = GetFrameTime() > 0.05f ? 0.05f : GetFrameTime();
        ++redraws;

        BeginDrawing();
        ClearBackground({30, 30, 34, 255});
        rlImGuiBegin();

        // ---- chrome
        const MenuActions m = drawMenuBar(grid.visible, grid.snap);
        drawStatusBar(status);
        dockspace(resetLayout, views[0]->title(), views[1]->title(), views[2]->title(), views[3]->title());
        resetLayout = false;

        // ---- keyboard (ignored while typing in a text field)
        const bool typing = io.WantTextInput;
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (m.quit) quit = true;
        if (m.resetLayout) resetLayout = true;
        if (m.gridSmaller || (!typing && IsKeyPressed(kKeyGridSmaller))) grid.halve();
        if (m.gridLarger  || (!typing && IsKeyPressed(kKeyGridLarger)))  grid.doubleUp();
        if (m.toggleGrid  || (!typing && shift && IsKeyPressed(KEY_R)))  grid.visible = !grid.visible;
        if (m.toggleSnap  || (!typing && shift && IsKeyPressed(KEY_W)))  grid.snap = !grid.snap;

        // Fly mode: Z over the 3D view toggles; Esc also leaves.
        if (!typing && IsKeyPressed(KEY_Z) && (flying || hoveredView == 0)) {
            flying = !flying;
            captureMouse(flying);
        }
        if (flying && IsKeyPressed(KEY_ESCAPE)) { flying = false; captureMouse(false); }

        // ---- viewports
        hoveredView = -1;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        for (int i = 0; i < 4; ++i) {
            Viewport& v = *views[i];
            const ImGuiWindowFlags wf = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                        ImGuiWindowFlags_NoCollapse;
            if (ImGui::Begin(v.title(), nullptr, wf)) {
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                v.resize((int)avail.x, (int)avail.y);
                v.render(grid);

                const ImVec2 origin = ImGui::GetCursorScreenPos();
                rlImGuiImageRenderTexture(&v.target());
                const bool hovered = ImGui::IsItemHovered();
                if (hovered) hoveredView = i;

                const Vector2 mouse = GetMousePosition();
                const Vector2 local{mouse.x - origin.x, mouse.y - origin.y};

                if (v.is2D()) {
                    if (hovered) {
                        v.zoom2D(GetMouseWheelMove(), local);
                        v.screenToWorld2D(local, cursorU, cursorV);
                    }
                    const bool startPan = IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) ||
                                          (IsKeyDown(KEY_SPACE) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
                    if (hovered && startPan) panningView = i;
                } else if (hovered && !flying && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                    rmbLook = true;
                    captureMouse(true);
                }

                // Overlay label, Hammer style, in the top-left corner.
                ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + 6, origin.y + 4),
                                                    IM_COL32(200, 200, 200, 255), v.label());
            }
            ImGui::End();
        }
        ImGui::PopStyleVar();

        // ---- 2D panning (continues even if the cursor leaves the view)
        if (panningView >= 0) {
            const bool held = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || IsMouseButtonDown(MOUSE_BUTTON_LEFT);
            if (held) views[panningView]->pan2D(GetMouseDelta());
            else panningView = -1;
        }

        // ---- 3D look / fly
        if (rmbLook && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            rmbLook = false;
            if (!flying) captureMouse(false);
        }
        if (flying || rmbLook) {
            if (skipLookDelta) skipLookDelta = false;
            else view3d.look(GetMouseDelta());
            const float f = (float)IsKeyDown(KEY_W) - (float)IsKeyDown(KEY_S);
            const float r = (float)IsKeyDown(KEY_D) - (float)IsKeyDown(KEY_A);
            const float u = (float)IsKeyDown(KEY_E) - (float)IsKeyDown(KEY_Q);
            view3d.fly(f, r, u, dt, shift);
        }

        // ---- status text for next frame
        {
            char where[96] = "";
            if (flying || rmbLook) {
                const Vector3 p = view3d.cameraPos();
                std::snprintf(where, sizeof where, "3d  cam %.0f %.0f %.0f   WASD move, Q/E down/up, Shift fast%s",
                              p.x, p.y, p.z, flying ? ", Z or Esc to stop" : "");
            } else if (hoveredView > 0) {
                const Viewport& v = *views[hoveredView];
                std::snprintf(where, sizeof where, "%s   %s %.0f  %s %.0f   zoom %.2f",
                              v.label(), v.axisU(), grid.snapValue(cursorU),
                              v.axisV(), grid.snapValue(cursorV), v.zoom());
            } else if (hoveredView == 0) {
                std::snprintf(where, sizeof where, "3d   Z: fly mode   hold right mouse: look");
            }
            // The redraw counter shows idle is working: it stops climbing when nothing happens.
            std::snprintf(status, sizeof status, "Grid: %d   Snap: %s   |   %-60s   redraws %llu",
                          grid.size, grid.snap ? "on" : "off", where, redraws);
        }

        rlImGuiEnd();

        // ---- idle policy
        const bool busy = flying || rmbLook || opt.screenshot != nullptr;
        if (busy || settle > 0) {
            DisableEventWaiting();
            if (!busy) --settle;
        } else {
            EnableEventWaiting();
            settle = kSettleFrames;
        }

        EndDrawing();

        if (opt.screenshot && ++frame >= 6) {
            TakeScreenshot(opt.screenshot);
            break;
        }
    }

    rlImGuiShutdown();
    for (auto& v : views) v.reset(); // free GPU resources before the context dies
    CloseWindow();
    return 0;
}
