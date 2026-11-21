#pragma once
// The editor: owns the document, viewports and tools, and runs one UI frame at a time.
// Real input and scripted input (for automated tests/screenshots) go through the same
// onPress/onDrag/onRelease/onKey paths.
// The class is defined over several files by topic (v0.10.3): editor.cpp (main loop,
// input, scene, views), editor_edit.cpp (commands, transform, carve, curves),
// editor_visibility.cpp, editor_surfaces.cpp (textures, terrain), editor_objects.cpp
// (primitives, props, landing), editor_replicate.cpp, editor_world.cpp (lights, sky,
// bookmarks), editor_cameras.cpp (cameras), editor_skylab.cpp (Sky Lab), editor_files.cpp (files, settings, Bottega), editor_script.cpp (test
// scripts). Shared helpers: editor_util.hpp; member templates: editor_templates.hpp.
#include <functional>
#include <map>
#include <sstream>
#include "core/bottega.hpp"
#include "imgui.h"
#include "core/document.hpp"
#include "core/grid.hpp"
#include "core/transform.hpp"
#include "core/csg.hpp"
#include "render/scene.hpp"
#include "render/viewport.hpp"
#include "tools/block_tool.hpp"
#include "tools/select_tool.hpp"
#include "tools/vertex_tool.hpp"
#include "tools/texture_tool.hpp"
#include "tools/clip_tool.hpp"
#include "tools/curve_tool.hpp"
#include "tools/terrain_tool.hpp"
#include "tools/light_tool.hpp"
#include "tools/camera_tool.hpp"
#include "tools/prim_tool.hpp"
#include "tools/prop_tool.hpp"
#include "render/sky_library.hpp"
#include "render/lighting.hpp"
#include "core/replicate.hpp"
#include "core/landing.hpp"
#include "render/texture_library.hpp"
#include "render/model_library.hpp"
#include "render/renderer.hpp"
#include "core/render_job.hpp"
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
    // Wraps from the start face through every connected picked face (one undo step).
    void wrapAroundPicked();
    // Applies fn to every picked face (texture tool) as one undoable command.
    template <class Fn> void editFaces(const char* name, Fn fn);
    void loadTextureFolders();
    // Bottega: the shared folder (textures, skies... later models, shaders). See core/bottega.hpp.
    void loadBottegaSetting();
    void saveBottegaSetting() const;
    void setBottegaRoot(const std::string& root); // inspects it and points the libraries at it when usable
    void rescanBottega();                         // re-reads every library; never automatic
    void drawBottegaDialog();
    void saveTextureFolders() const;
    void newMap();
    void openMap(const std::string& path);
    bool saveMap(const std::string& path);
    void save();          // to current path, or ask
    void requestSaveAs(); // native Save panel on macOS, typed path elsewhere
    void requestOpen();   // native Open panel on macOS, typed path elsewhere
    void runNativeDialog();

    // Shaping (milestone 4)
    void carveWithSelection();
    void hollowSelection(double thickness);
    void drawShapingDialogs();

    // Curves (milestone 5)
    void commitCurve();
    void editCurve();          // load the selected generated group into the curve tool
    void bakeCurves();         // selected generated groups -> plain brushes
    GroupId selectedGroup() const; // the single generated group the selection is, or 0
    void drawCurvePanel();
    void drawBlockPanel();

    // Visibility (milestone 6)
    void hideSelected();
    void hideUnselected();
    void showAll();
    void setVisgroupVisible(std::uint32_t id, bool visible);
    void dropInvisibleFromSelection();
    void drawVisgroupsPanel();

    // Terrain (milestone 7)
    void startMakeTerrain();          // terrain tool picks faces (or dialog for Texture tool picks)
    void makeTerrainNow(int power);   // picked faces, or the selected terrain's grid size
    void removeTerrain();
    void stitchAllTerrain();
    bool importHeightmap(const std::string& path, double low, double high); // false + terrainError_
    // Applies changed copies of terrain brushes plus stitching with every other terrain.
    void applyTerrainEdit(std::vector<Brush> edited, const char* name);
    bool selectionHasTerrain() const;
    void drawTerrainPanel();
    void drawTerrainDialogs();
    bool askMakeTerrain_ = false, askHeightmap_ = false;
    int terrainPower_ = 3;
    char heightmapPath_[1024] = "";
    double heightLow_ = 0, heightHigh_ = 256;
    std::string terrainError_;

    // World (milestone 8)
    void drawLightPanel();
    // Math primitives (milestone 9)
    void combinePrims();   // selected primitives become one combination
    void separatePrims();  // selected primitives leave their combinations
    void drawPrimPanel();
    void setPrimOp(PrimOp op);
    // v0.9.2: Snap to Land / Ground (primitives) and Multi-Replicate
    void snapToLand();
    void snapToGround(double height);
    void openReplicate();
    void drawReplicateDialog();
    bool applyReplicate(const ReplicateSpec& spec);
    // The selection as Multi- and Random Replicate copy it (groups and combinations whole).
    void replicateSource(std::vector<Brush>& bs, std::vector<MathPrim>& ps, std::vector<Light>& ls, std::vector<Prop>& qs) const;
    bool commitCopies(const std::vector<ReplicateCopy>& copies, const char* name);
    // v0.10.1: Random Replicate
    void openRandomReplicate();
    void drawRandomReplicateWindow();
    bool applyRandomReplicate(const RandomReplicateSpec& spec);
    RandomReplicateSpec randSpec_;
    RandomReplicateSpec randSpecNow() const; // randSpec_ with the current grid for brushes
    bool randomizing_ = false, randomFocus_ = false;
    ReplicateSpec repSpec_;
    bool replicating_ = false, askGround_ = false;
    bool scriptPreview_ = false; // scripts show the preview without the dialog
    bool replicateFocus_ = false; // put the keyboard in its window when it opens
    int repPivot_ = 0;
    double groundHeight_ = 0;
    std::string landMessage_;
    MathPrim primBuf_;               // Properties fields for the selected primitives
    std::set<MathPrimId> primBufFor_;
    unsigned long long primBufRev_ = ~0ull;
    double primPend_[6] = {0, 0, 0, 64, 64, 64}; // typed centre + size of the pending box
    // Props (milestone 10)
    void drawPropsPanel();   // the Props panel: models of the Bottega, search by name
    void drawPropPanel();    // Properties for the Props tool / selected props
    Prop propBuf_;           // Properties fields for the selected prop
    std::set<PropId> propBufFor_;
    unsigned long long propBufRev_ = ~0ull;
    char propSearch_[128] = "";
    bool propPerAxis_ = false;       // show a scale per axis (else one even scale)
    double propPend_[3] = {0, 0, 0}; // typed position of the pending prop
    std::string propMessage_;
    void drawWorldDialogs();
    // Applies `fn` to every selected light as one undo step (if anything changed).
    template <class Fn> void editLights(const char* name, Fn fn);
    Light lightBuf_;                 // Properties fields for the selected lights
    std::set<LightId> lightBufFor_;
    unsigned long long lightBufRev_ = ~0ull;
    bool showSky_ = true, lightPreview_ = false, showFog_ = false, showFigure_ = false;
    bool showLightGrid_ = false;                        // View: the last bake's light grid
    std::shared_ptr<const BakeResult> lastBake_;        // the last bake made or reused
    bool askEnvironment_ = false, askBookmarks_ = false, askFigure_ = false;
    Environment envBuf_;
    double figureBuf_[3] = {0, 0, 0};
    char bookmarkName_[64] = "";
    std::string worldError_;

    // Cameras (milestone 11): editor_cameras.cpp
    void drawCameraPanel();
    void activateCamera(CameraId id);   // one undo step; nothing if it already is
    void startCameraAim();              // the next click aims the selected cameras
    Camera camBuf_;                     // Properties fields for the selected camera
    CameraId camBufFor_ = 0;
    unsigned long long camBufRev_ = ~0ull;
    double camAim_[3] = {0, 0, 0};      // typed point to aim at
    // Saved presets (v0.11.1): the Bottega's presets/camera_presets/<name>.camera
    std::vector<std::string> cameraPresets() const;
    bool saveCameraPreset(const std::string& name);  // from the selected camera, else the new-camera settings
    bool loadCameraPreset(const std::string& name);  // into the selected cameras (one undo step), else the new-camera settings
    char camPresetName_[64] = "";
    std::string camMsg_;
    // Camera Preview (v0.11.1): a docked panel under Properties, only while the map has a
    // camera. Drawn only when asked (Cmd+K, its Update button, Render menu): since v0.12.0 a
    // draft render (the renderer at Draft settings, the map as it is, the View menu
    // ignored), at the picture's shape, as large as the panel.
    void drawCameraPreview();
    void requestCameraPreview() { previewWanted_ = true; }
    Renderer preview_;
    bool previewWanted_ = false;       // draw it at the next panel draw (which knows the size)
    bool previewDone_ = false;         // there is a picture to show
    unsigned long long previewRev_ = 0; // the document when it was drawn
    CameraId previewCam_ = 0;
    int previewUpdates_ = 0;           // times drawn (scripts check it)

    // Render (milestone 12): editor_render.cpp
    void startRender();               // Render button / menu / Cmd+Shift+K: opens the terminal window
    void stopRender();                // Esc while it runs
    void stepRender();                // one step per frame while running
    void finishRender();              // writes the log file, frees the picture
    void drawRenderWindow();
    bool renderNeedsFrame() const;
    void renderLog(const std::string& line);
    void loadRenderSettings();
    void saveRenderSettings() const;
    RenderSettings renderSettings_;   // Render menu options (the user's settings, not the map)
    struct RenderJob {
        double start = 0;             // GetTime() when it began
        RenderSettings settings;
        Camera cam;                   // the active camera when Render was clicked
        int step = 0, aa = 1;
        bool running = false, done = false, stopped = false, failed = false, stopWanted = false;
        bool scrollDown = false;
        std::vector<std::string> log;
        std::string png, logFile, name; // full paths; name: "renders/<stem>.png" for the log
        Renderer draw;
        Scene scene;
        std::vector<unsigned char> rgb;
        int w = 0, h = 0;
        std::unique_ptr<BakeTask> bake;          // the light bake, while it runs
        std::shared_ptr<const BakeResult> baked; // its result (or the kept one)
    };
    // Baked light kept in memory, one per quality, while the map's light doesn't change
    // (agreed 2026-11-20: memory only). The key is core/bake.hpp bakeKey.
    struct KeptBake { std::uint64_t key = 0; std::shared_ptr<const BakeResult> result; };
    std::map<int, KeptBake> bakes_;
    // The bake for `q` now, on this thread (the Camera Preview's draft), kept or made.
    std::shared_ptr<const BakeResult> bakeNow(BakeQuality q);
    std::unique_ptr<RenderJob> renderJob_; // the last render (kept for its log)
    bool renderOpen_ = false;              // the terminal window is shown
    int rendersDone_ = 0;                  // renders saved (scripts check it)
    std::vector<std::string> renderFiles_; // the saved PNGs, oldest first (scripts compare them)
    bool scriptWaitRender_ = false;

    // Sky Lab (milestone 10c): editor_skylab.cpp
    void openSkyLab();
    void closeSkyLab();
    void drawSkyLabWindow();
    void updateSkyLabPreview(bool dragging); // makes the live preview when the settings changed
    // Bakes `settings` to the Bottega as `name` (over several frames), then records it as
    // one undo step (with the sun light, if locked). False (and a message) without a Bottega.
    bool startSkyBake(const SkyLab& settings, const std::string& name, bool closeAfter);
    void stepSkyBake();
    void rebakeSky();               // the map's settings again (sun from the light if locked)
    bool skyOutOfDate() const;      // locked, and the sun light has turned since the bake
    bool skyFileMissing() const;    // the map has Sky Lab settings but its PNG is gone
    bool skyLabNeedsFrame() const;
    const Light* mapSunLight() const;
    std::string skyLabBakeName() const; // "skies/skylab/<map name>"
    bool skyLabOpen_ = false, skyLabFocus_ = false;
    SkyLab skyLabBuf_;               // the settings being edited
    Vec3 skyLabAmbient_;             // the environment's ambient, edited in Sky Lab
    Texture2D skyLabTex_{};          // the live preview (also the 3D view's sky while open)
    int skyLabPreviewW_ = 0;
    unsigned long long skyLabPreviews_ = 0; // how many previews were made (tests)
    struct SkyBake {
        std::unique_ptr<SkyGenerator> gen;
        std::vector<unsigned char> rgb;
        int row = 0;
        SkyLab settings;
        std::string name;
        bool closeAfter = false;
        bool fogOn = false;   // the fog it was made with (set on the map with the sky)
        Fog fog;
        bool setFog = false;  // made from the Sky Lab window: its fog goes to the map
    };
    std::unique_ptr<SkyBake> skyBake_;
    std::string skyLabMsg_;
    // v0.10.6: the fog (shared with World > Environment), the moon image, saved skies.
    bool skyLabFogOn_ = false;
    Fog skyLabFog_;
    std::string skyLabPreviewKey_;    // what the preview was made from
    std::string moonBuiltinPath_;     // the built-in moon (NASA's map) inside the app
    std::vector<unsigned char> moonPixels_;
    int moonW_ = 0, moonH_ = 0;
    std::string moonLoaded_;          // which moon image moonPixels_ holds
    unsigned skyLabRandomSeed_ = 1;   // Randomize Sky
    char skyPresetName_[64] = "";
    // The fog and moon a sky needs (loads the moon image if it changed).
    SkyExtras skyExtras(const SkyLab& s, bool fogOn, const Fog& fog);
    std::vector<std::string> moonImages() const;   // Bottega skies/moons/ (keys)
    std::vector<std::string> skyPresets() const;   // Bottega presets/skies/ (names)
    bool saveSkyPreset(const std::string& name);
    bool loadSkyPreset(const std::string& name, bool builtin = false); // builtin: core/sky_presets
    bool applySkyPresetText(const std::string& text);
    void randomizeSky();
    int skyLabTab_ = -1; // a tab to open (0 Sun & Moon, 1 Clouds, 2 Atmosphere), for tests
    std::string skyLabSection_; // a section to open (its title's start), for tests

    // Overlap finder + navigation
    void findOverlapsNow();
    void focusOverlap(int index);
    void focusRegion(const AABB& region);
    void allViewsToOrigin();
    void drawOverlapsWindow();
    std::vector<Overlap> overlaps_;
    int overlapIndex_ = -1;
    bool overlapsOpen_ = false;
    char newVisgroupName_[64] = "";
    enum class Pending { None, New, Open, OpenPath, Quit };
    void guardUnsaved(Pending action); // runs action now or asks first
    void runPending();

    // ---- scripting
    void loadScript(const std::string& path);
    // Test scripts: one table entry per command (editor_script_commands.cpp) and per
    // "expect <what>" check (editor_script_expects.cpp), filled on first use.
    using ScriptFn = std::function<void(const std::string& cmd, std::istringstream& in)>;
    void registerScriptCommands();
    // Development: fill the map with props and time scene, drawing, picking, landing.
    void benchProps(int count, const std::string& model, double spacing);
    // Development: fill the map with boxes and terrain and time scene, redraw, edits.
    void benchBrushes(int count, int terrains, int power);
    void registerScriptExpects();
    std::map<std::string, ScriptFn> scriptCommands_, scriptExpects_;
    int scriptDragView_ = -1; // the view a scripted press / drag happens in
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
    ClipTool clipTool_;
    CurveTool curveTool_;
    TerrainTool terrainTool_;
    LightTool lightTool_;
    CameraTool cameraTool_;
    PrimTool primTool_;
    PropTool propTool_;
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
    SkyLibrary skies_;
    ModelLibrary models_;      // props' models (the Bottega's models folder), loaded on first use
    ModelFinder modelFinder_;  // models_.finder(), handed to the tools
    LightingCache lighting_;   // only used (and built) while the light preview is on
    void loadSkyFolders();
    void saveSkyFolders() const;
    char skyFolderBuf_[1024] = "";
    // Where the scale figure stands when it is first shown: on whatever is below the
    // top view's centre (or z = 0).
    Vec3 groundBelow(double x, double y) const;
    void addBookmark(const std::string& name);
    void goToBookmark(size_t i);
    std::string currentTexture_ = "Bricks_24";
    double defaultScale_ = 0.25;     // Source-style default; good for 512px textures
    bool textured3D_ = true;
    bool showPanels_ = true;         // right-hand Textures/Face panels ("layout classic" hides)
    char texSearch_[128] = "";
    bool askTexFolder_ = false;
    bool askBottega_ = false;
    char bottegaBuf_[1024] = "";
    std::string bottegaRoot_;        // the chosen folder ("" = none), kept in the user's settings
    bottega::Check bottegaCheck_;
    std::string bottegaMsg_;         // result of the last make
    char folderBuf_[1024] = "";
    int fitTimes_[2] = {1, 1};

    // Native file dialogs (macOS). They open only once no modifier key or mouse button is
    // held: the system panel swallows key-up events, which would leave Cmd "stuck".
    enum class Native { None, Open, SaveAs, Folder, Heightmap, SkyFolder, Bottega };
    Native native_ = Native::None;
    bool useNative_ = false;

    bool askClipPoints_ = false, askHollow_ = false;
    double clipBuf_[3][3] = {};
    double hollowThickness_ = 16;
    std::string shapingError_;

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
