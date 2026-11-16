#pragma once
// The editor: owns the document, viewports and tools, and runs one UI frame at a time.
// Real input and scripted input (for automated tests/screenshots) go through the same
// onPress/onDrag/onRelease/onKey paths.
#include "core/document.hpp"
#include "core/grid.hpp"
#include "render/scene.hpp"
#include "render/viewport.hpp"
#include "tools/block_tool.hpp"
#include "tools/select_tool.hpp"
#include <deque>
#include <memory>
#include <string>

namespace scal {

struct EditorOptions {
    std::string openPath;    // --open <file.map>
    std::string scriptPath;  // --script <file>: scripted input, see docs/PLAN.md
    std::string screenshot;  // --screenshot <file.png>: render a few frames, save, exit
    bool automated() const { return !scriptPath.empty() || !screenshot.empty(); }
};

class Editor {
public:
    explicit Editor(const EditorOptions& opt);
    ~Editor();

    // Builds and renders one UI frame (between rlImGuiBegin/End).
    // Returns true while something needs continuous frames (flying, script running...).
    bool frame(float dt);

    void requestQuit();           // window close / Cmd+Q
    bool shouldExit() const { return exit_; }
    int exitCode() const { return scriptFailures_ > 0 ? 1 : 0; }
    std::string takeScreenshotRequest(); // non-empty: caller saves a screenshot now

private:
    // ---- input (shared by real and scripted input)
    // `real`: from the physical mouse (tracks capture). Scripted input passes false.
    void onPress(int view, Vector2 local, bool real = true);
    void onDrag(int view, Vector2 local);
    void onRelease(int view, Vector2 local, bool real = true);
    void onKey(int key);
    Pointer pointerFor(int view, Vector2 local) const;
    ToolEnv env();

    // ---- frame parts
    void handleKeyboard();
    void drawViews();
    void drawToolsPanel();
    void drawDialogs();
    Scene buildScene();
    void updateTitle();
    std::string statusText() const;

    // ---- commands
    void setTool(Tool* t);
    void undo();
    void redo();
    void selectAll();
    void newMap();
    void openMap(const std::string& path);
    bool saveMap(const std::string& path);
    void save();          // to current path, or ask
    enum class Pending { None, New, Open, Quit };
    void guardUnsaved(Pending action); // runs action now or asks first
    void runPending();

    // ---- scripting
    void loadScript(const std::string& path);
    void stepScript();

    EditorOptions opt_;
    Document doc_;
    Grid grid_;
    SceneCache cache_;
    AABB lastBox_;
    std::unique_ptr<Viewport> views_[4];
    Vector2 viewOrigin_[4]{};
    bool viewHovered_[4]{};

    SelectTool selectTool_;
    BlockTool blockTool_;
    Tool* tool_ = &selectTool_;

    int capture_ = -1;          // view that owns the current left-button drag
    int panning_ = -1;          // 2D view being panned
    bool flying_ = false;
    bool rmbLook_ = false;
    bool skipLookDelta_ = false;
    int hovered_ = -1;
    double cursorU_ = 0, cursorV_ = 0;
    bool resetLayout_ = false;
    bool cmd_ = false, shift_ = false, alt_ = false;

    // file + dialogs
    std::string path_;           // current file, empty = untitled
    std::string lastTitle_;
    Pending pending_ = Pending::None;
    std::string pendingOpenPath_;
    bool askSaveAs_ = false, askOpen_ = false, askUnsaved_ = false;
    std::string errorText_;
    char pathBuf_[1024] = "";

    bool exit_ = false;
    unsigned long long redraws_ = 0;

    // scripting
    std::deque<std::string> script_;
    int scriptLine_ = 0;
    int scriptFailures_ = 0;
    int warmup_ = 3;
    std::string shotRequest_;
    int screenshotFrames_ = 6;
};

} // namespace scal
