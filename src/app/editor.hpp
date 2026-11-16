#pragma once
// The editor: owns the document, viewports and tools, and runs one UI frame at a time.
// Real input and scripted input (for automated tests/screenshots) go through the same
// onPress/onDrag/onRelease/onKey paths.
#include "imgui.h"
#include "core/document.hpp"
#include "core/grid.hpp"
#include "core/transform.hpp"
#include "render/scene.hpp"
#include "render/viewport.hpp"
#include "tools/block_tool.hpp"
#include "tools/select_tool.hpp"
#include "tools/vertex_tool.hpp"
#include "tools/texture_tool.hpp"
#include "render/texture_library.hpp"
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
    // Script `expect edges`: after the frame is drawn (before present), the caller asks
    // whether to verify on-screen pixels, then calls checkScreenEdges().
    bool wantsScreenCheck() const { return edgeCheck_; }
    void checkScreenEdges();

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
    void drawSizeLabels(const Viewport& v, const AABB& box, ImVec2 origin);
    void drawDialogs();
    Scene buildScene();
    void updateTitle();
    std::string statusText() const;

    // ---- commands
    void setTool(Tool* t);
    void undo();
    void redo();
    void selectAll();
    void selectNone();
    void duplicate();
    void deleteSelection();
    bool applyTransform(const TransformSpec& t, const char* name); // false + errorText_ on failure
    void rotate90(int axis, int sign);
    void flip(int axis);
    void centerViews();
    void openTransformDialog();
    void drawTransformDialog();
    void snapSelectionToGrid();
    Vec3 pivotPoint() const;          // current pivot for rotate/scale
    void drawPrecisionDialogs();      // grid size, pivot point, vertex move

    // Textures (milestone 3)
    void drawTextureBrowser();
    void drawFacePanel();
    void applyCurrentTexture() { applyTexture(currentTexture_); }
    void applyTexture(const std::string& name);
    // Applies fn to every picked face (texture tool) as one undoable command.
    template <class Fn> void editFaces(const char* name, Fn fn);
    void loadTextureFolders();
    void saveTextureFolders() const;
    void newMap();
    void openMap(const std::string& path);
    bool saveMap(const std::string& path);
    void save();          // to current path, or ask
    void requestSaveAs(); // native Save panel on macOS, typed path elsewhere
    void requestOpen();   // native Open panel on macOS, typed path elsewhere
    void runNativeDialog();
    enum class Pending { None, New, Open, OpenPath, Quit };
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
    float viewScale_ = 0; // >0 overrides the display scale (SCAL_VIEW_SCALE, tests only)
    bool viewHovered_[4]{};

    SelectTool selectTool_;
    BlockTool blockTool_;
    VertexTool vertexTool_;
    TextureTool textureTool_;
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
    bool controlsOpen_ = false;
    bool closeDialogs_ = false; // script: close any open modal this frame

    // Transform dialog (values are remembered between uses)
    bool askTransform_ = false;
    int tfMode_ = 1;               // 0 move, 1 rotate, 2 scale
    double tfMove_[3] = {0, 0, 0};
    int tfAxis_ = 2;
    double tfDegrees_ = 15;
    double tfScale_[3] = {1, 1, 1};
    int tfPivot_ = 0;              // 0 selection centre, 1 world origin, 2 custom
    double tfCustom_[3] = {0, 0, 0};
    bool tfTexLock_ = true;
    std::string tfError_;

    // Precision options (milestone 2)
    int pivotMode_ = 0;              // 0 selection centre, 1 world origin, 2 custom point
    Vec3 customPivot_;
    bool slopeAngles_ = false;       // drag rotation snaps to grid-friendly slopes
    bool askGridSize_ = false, askPivot_ = false, askVertexMove_ = false;
    double gridSizeBuf_ = 16;
    double pivotBuf_[3] = {0, 0, 0};
    double vmove_[3] = {0, 0, 0};
    int vmoveMode_ = 0;              // 0 move by, 1 set position (single corner)
    std::string precisionError_;

    TextureLibrary textures_;
    std::string currentTexture_ = "Bricks_24";
    double defaultScale_ = 0.25;     // Source-style default; good for 512px textures
    bool textured3D_ = true;
    bool showPanels_ = true;         // right-hand Textures/Face panels ("layout classic" hides)
    char texSearch_[128] = "";
    bool askTexFolder_ = false;
    char folderBuf_[1024] = "";
    int fitTimes_[2] = {1, 1};

    // Native file dialogs (macOS). They open only once no modifier key or mouse button is
    // held: the system panel swallows key-up events, which would leave Cmd "stuck".
    enum class Native { None, Open, SaveAs, Folder };
    Native native_ = Native::None;
    bool useNative_ = false;

    bool exit_ = false;
    unsigned long long redraws_ = 0;

    // scripting
    std::deque<std::string> script_;
    int scriptLine_ = 0;
    int scriptFailures_ = 0;
    bool edgeCheck_ = false;
    int warmup_ = 3;
    std::string shotRequest_;
    int screenshotFrames_ = 6;
};

} // namespace scal
