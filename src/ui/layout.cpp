#include "ui/layout.hpp"
#include "imgui_internal.h" // DockBuilder, BeginViewportSideBar
#include <cstdio>
#include <initializer_list>

namespace scal {

MenuActions drawMenuBar(const MenuState& st) {
    MenuActions a;
    if (!ImGui::BeginMainMenuBar()) return a;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", "Cmd+N")) a.newMap = true;
        if (ImGui::MenuItem("Open...", "Cmd+O")) a.open = true;
        if (ImGui::MenuItem("Open by typing a path...")) a.openPath = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Bottega folder...")) a.bottegaFolder = true;
        if (ImGui::MenuItem("Rescan Bottega", "Cmd+R")) a.rescanBottega = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Cmd+S")) a.save = true;
        if (ImGui::MenuItem("Save as...", "Cmd+Shift+S")) a.saveAs = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Cmd+Q")) a.quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Cmd+Z", false, st.canUndo)) a.undo = true;
        if (ImGui::MenuItem("Redo", "Cmd+Shift+Z", false, st.canRedo)) a.redo = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Cmd+D", false, st.hasAnything)) a.duplicate = true;
        if (ImGui::MenuItem("Multi-Replicate...", "Cmd+Shift+D", false, st.hasAnything)) a.replicate = true;
        if (ImGui::MenuItem("Random Replicate...", "Cmd+Shift+R", false, st.hasAnything)) a.randomReplicate = true;
        if (ImGui::MenuItem("Delete", "Delete", false, st.hasAnything)) a.del = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Select all", "Cmd+A")) a.selectAll = true;
        if (ImGui::MenuItem("Select none", "Esc", false, st.hasSelection)) a.selectNone = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Select", "Shift+S", st.selectTool)) a.toolSelect = true;
        if (ImGui::MenuItem("Block", "Shift+B", st.blockTool)) a.toolBlock = true;
        if (ImGui::MenuItem("Vertex", "Shift+V", st.vertexTool)) a.toolVertex = true;
        if (ImGui::MenuItem("Texture", "Shift+A", st.textureTool)) a.toolTexture = true;
        if (ImGui::MenuItem("Clip", "Shift+X", st.clipTool)) a.toolClip = true;
        if (ImGui::MenuItem("Curve (arch, ring, wall, stairs)", "Shift+C", st.curveTool)) a.toolCurve = true;
        if (ImGui::MenuItem("Terrain (sculpt, paint)", "Shift+G", st.terrainTool)) a.toolTerrain = true;
        if (ImGui::MenuItem("Light", "Shift+L", st.lightTool)) a.toolLight = true;
        if (ImGui::MenuItem("Camera", "Shift+K", st.cameraTool)) a.toolCamera = true;
        if (ImGui::MenuItem("Math primitive", "Shift+M", st.primTool)) a.toolPrim = true;
        if (ImGui::MenuItem("Props (models)", "Shift+P", st.propTool)) a.toolProp = true;
        if (ImGui::MenuItem("Decal", "Shift+D", st.decalTool)) a.toolDecal = true;
        ImGui::Separator();
        if (ImGui::BeginMenu("Clip: keep")) {
            if (ImGui::MenuItem("Both pieces", nullptr, st.clipKeep == 0)) a.clipKeep = 0;
            if (ImGui::MenuItem("Front", nullptr, st.clipKeep == 1)) a.clipKeep = 1;
            if (ImGui::MenuItem("Back", nullptr, st.clipKeep == 2)) a.clipKeep = 2;
            ImGui::TextDisabled("Shift+X cycles while clipping");
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Clip points...", nullptr, false, st.hasSelection)) a.clipPoints = true;
        if (ImGui::MenuItem("Apply clip", "Return", false, st.clipTool && st.clipPlane)) a.clipApply = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Edit curve", nullptr, false, st.groupSelected)) a.editCurve = true;
        if (ImGui::MenuItem("Bake curve into plain brushes", nullptr, false, st.groupSelected)) a.bakeCurve = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Carve with selection", "Cmd+Shift+C", false, st.hasSelection)) a.carve = true;
        if (ImGui::MenuItem("Find overlapping brushes", "Cmd+Shift+O")) a.findOverlaps = true;
        if (ImGui::MenuItem("Hollow...", "Cmd+Shift+H", false, st.hasSelection)) a.hollow = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Move selected vertices...", "Cmd+Shift+T", false, st.vertexTool && st.verticesSelected)) a.vertexMove = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Create block", "Return", false, st.blockPreview)) a.createBlock = true;
        if (ImGui::MenuItem("Cancel", "Esc", false, st.blockPreview)) a.cancel = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Transform")) {
        if (ImGui::MenuItem("Move / Rotate / Scale...", "Cmd+T", false, st.hasSelection)) a.transform = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Snap to land", "Cmd+J", false, st.primsSelected + st.propsSelected > 0)) a.snapLand = true;
        if (ImGui::MenuItem("Snap to ground...", nullptr, false, st.primsSelected + st.propsSelected > 0)) a.snapGround = true;
        ImGui::TextDisabled("drops selected primitives and props onto what is below them");
        ImGui::Separator();
        static const char* axes[] = {"X", "Y", "Z"};
        for (int ax = 2; ax >= 0; --ax) {
            char label[64];
            std::snprintf(label, sizeof label, "Rotate 90%s around %s", "\xC2\xB0", axes[ax]);
            if (ImGui::BeginMenu(label, st.hasSelection)) {
                if (ImGui::MenuItem("Counter-clockwise (+90)")) { a.rotate90Axis = ax; a.rotate90Sign = +1; }
                if (ImGui::MenuItem("Clockwise (-90)"))         { a.rotate90Axis = ax; a.rotate90Sign = -1; }
                ImGui::EndMenu();
            }
        }
        ImGui::Separator();
        for (int ax = 0; ax < 3; ++ax) {
            char label[32];
            std::snprintf(label, sizeof label, "Flip %s", axes[ax]);
            if (ImGui::MenuItem(label, nullptr, false, st.hasSelection)) a.flipAxis = ax;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Snap vertices to grid", "Cmd+B", false, st.hasSelection)) a.snapToGrid = true;
        ImGui::Separator();
        if (ImGui::BeginMenu("Pivot")) {
            if (ImGui::MenuItem("Selection centre", nullptr, st.pivotMode == 0)) a.pivotMode = 0;
            if (ImGui::MenuItem("World origin", nullptr, st.pivotMode == 1)) a.pivotMode = 1;
            if (ImGui::MenuItem("Point...", nullptr, st.pivotMode == 2)) a.pivotMode = 2;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Drag rotation snapping")) {
            if (ImGui::MenuItem("15\xC2\xB0 steps", nullptr, !st.slopeAngles)) a.rotSnap = 0;
            if (ImGui::MenuItem("Grid-friendly angles (3:4:5, 5:12:13...)", nullptr, st.slopeAngles)) a.rotSnap = 1;
            ImGui::EndMenu();
        }
        ImGui::TextDisabled("Drag: Option+corner handle rotates");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Texture")) {
        if (ImGui::MenuItem("Apply current texture", "Shift+T", false, st.canApplyTexture)) a.applyTexture = true;
        if (ImGui::MenuItem("Apply nodraw (hidden faces)", nullptr, false, st.canApplyTexture)) a.applyNoDraw = true;
        if (ImGui::MenuItem("Texture tool (pick faces)", "Shift+A", st.textureTool)) a.toolTexture = true;
        if (ImGui::MenuItem("Wrap around picked faces", "Option+Shift+click", false, st.facesPicked)) a.wrapAround = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Texture lock", nullptr, st.textureLock)) a.toggleTextureLock = true;
        if (ImGui::BeginMenu("Default scale for new brushes")) {
            for (double sc : {0.125, 0.25, 0.5, 1.0, 2.0}) {
                char b[16];
                std::snprintf(b, sizeof b, "%g", sc);
                if (ImGui::MenuItem(b, nullptr, st.defaultScale == sc)) a.defaultScale = sc;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Filtering")) {
            if (ImGui::MenuItem("Crisp pixels", nullptr, !st.smoothTextures)) a.smoothTextures = 0;
            if (ImGui::MenuItem("Smooth", nullptr, st.smoothTextures)) a.smoothTextures = 1;
            ImGui::EndMenu();
        }
        ImGui::Separator();
        // Materials (v0.12.4): the browser's selection is what the Material panel edits
        if (ImGui::MenuItem("Select all shown textures")) a.selectShownTextures = true;
        if (ImGui::MenuItem("Select the textures of the picked faces")) a.selectFaceTextures = true;
        if (ImGui::MenuItem("Make a variant of the current material...", nullptr, false, st.canMakeVariant)) a.makeVariant = true;
        if (ImGui::MenuItem("Show helper maps (_normal, _mask...)", nullptr, st.showHelperMaps)) a.toggleHelperMaps = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Add texture folder...")) a.addTextureFolder = true;
        if (ImGui::MenuItem("Rescan Bottega and texture folders", "Cmd+R")) a.rescanBottega = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Terrain")) {
        if (ImGui::MenuItem("Make terrain (pick faces)...", nullptr, st.terrainPicking)) a.makeTerrain = true;
        if (ImGui::MenuItem("Terrain grid size...", nullptr, false, st.terrainSelected)) a.terrainGrid = true;
        if (ImGui::MenuItem("Remove terrain", nullptr, false, st.terrainSelected)) a.removeTerrain = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Terrain tool", "Shift+G", st.terrainTool)) a.toolTerrain = true;
        static const char* modes[] = {"Raise", "Lower", "Smooth", "Flatten", "Noise", "Paint texture", "Erase paint"};
        for (int i = 0; i < 7; ++i)
            if (ImGui::MenuItem(modes[i], nullptr, st.terrainTool && st.terrainMode == i)) a.terrainMode = i;
        ImGui::Separator();
        if (ImGui::MenuItem("Import heightmap...", nullptr, false, st.terrainSelected)) a.heightmap = true;
        if (ImGui::MenuItem("Stitch neighbouring edges")) a.stitchTerrain = true;
        ImGui::TextDisabled("Make terrain: click any faces (3 or 4 corners),");
        ImGui::TextDisabled("then Return. Vertex tool: move terrain points.");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Primitives")) {
        if (ImGui::MenuItem("Primitive tool", "Shift+M", st.primTool)) a.toolPrim = true;
        static const char* kinds[] = {"New sphere", "New cube", "New cylinder", "New cone", "New torus", "New capsule", "New disc"};
        for (int i = 0; i < 7; ++i)
            if (ImGui::MenuItem(kinds[i], nullptr, st.primTool && st.primKind == i)) a.primKind = i;
        if (ImGui::MenuItem("Create primitive", "Return", false, st.primPending)) a.createPrim = true;
        ImGui::TextDisabled("Primitive tool: drag a box in a 2D view, Return creates");
        ImGui::Separator();
        if (ImGui::MenuItem("Combine selected", nullptr, false, st.primsSelected >= 2)) a.combine = true;
        if (ImGui::MenuItem("Separate selected", nullptr, false, st.primsInCombo)) a.separate = true;
        static const char* ops[] = {"Selected: add", "Selected: subtract", "Selected: intersect"};
        for (int i = 0; i < 3; ++i)
            if (ImGui::MenuItem(ops[i], nullptr, false, st.primsInCombo)) a.primOp = i;
        ImGui::TextDisabled("In a combination, subtract pieces cut into the add");
        ImGui::TextDisabled("pieces and intersect pieces keep only the overlap");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Props")) {
        if (ImGui::MenuItem("Props tool", "Shift+P", st.propTool)) a.toolProp = true;
        if (ImGui::MenuItem("Place prop", "Return", false, st.propPending)) a.createProp = true;
        if (ImGui::MenuItem("Cancel", "Esc", false, st.propPending)) a.cancel = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Rescan Bottega (models, textures, skies)", "Cmd+R")) a.rescanBottega = true;
        ImGui::TextDisabled("Pick a model in the Props panel, then click a surface in 3D");
        ImGui::TextDisabled("or a spot in a 2D view; Return places it. Models live in");
        ImGui::TextDisabled("the Bottega's models folder (File > Bottega folder...).");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Decals")) {
        if (ImGui::MenuItem("Decal tool", "Shift+D", st.decalTool)) a.toolDecal = true;
        if (ImGui::MenuItem("Place decal", "Return", false, st.decalPending)) a.createDecal = true;
        if (ImGui::MenuItem("Cancel", "Esc", false, st.decalPending)) a.cancel = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Decal...", nullptr, false, st.decalsSelected == 1 || st.decalPending)) a.decalDialog = true;
        if (ImGui::MenuItem("Bring forward", nullptr, false, st.decalsSelected > 0)) a.decalForward = true;
        if (ImGui::MenuItem("Send back", nullptr, false, st.decalsSelected > 0)) a.decalBack = true;
        if (ImGui::MenuItem("Size from its picture", nullptr, false, st.decalsSelected > 0 || st.decalPending)) a.decalPictureSize = true;
        if (ImGui::MenuItem("Show decals", nullptr, st.decalsShown)) a.toggleDecals = true;
        ImGui::Separator();
        ImGui::TextDisabled("Pick a picture in the Textures panel, then click a surface");
        ImGui::TextDisabled("in the 3D view; Return places it. A selected decal: corners");
        ImGui::TextDisabled("size it, the ring turns it, dragging slides it.");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("World")) {
        if (ImGui::MenuItem("Light tool", "Shift+L", st.lightTool)) a.toolLight = true;
        static const char* kinds[] = {"New point light", "New spot light", "New sun"};
        for (int i = 0; i < 3; ++i)
            if (ImGui::MenuItem(kinds[i], nullptr, st.lightTool && st.lightKind == i)) a.lightKind = i;
        if (ImGui::MenuItem("Create light", "Return", false, st.lightPending)) a.createLight = true;
        ImGui::TextDisabled("Light tool: click to place, Return creates;");
        ImGui::TextDisabled("drag a selected light's rings to aim it");
        ImGui::Separator();
        if (ImGui::MenuItem("Camera tool", "Shift+K", st.cameraTool)) a.toolCamera = true;
        if (ImGui::MenuItem("Create camera", "Return", false, st.cameraPending)) a.createCamera = true;
        if (ImGui::MenuItem("Make selected camera active", nullptr, false, st.camerasSelected == 1 && !st.selectedCameraActive)) a.activateCamera = true;
        if (ImGui::MenuItem("Aim selected cameras at a clicked point", nullptr, st.cameraAiming, st.camerasSelected > 0)) a.aimCamera = true;
        ImGui::TextDisabled("Camera tool: click to place, Return creates; lens,");
        ImGui::TextDisabled("picture size, aim at a typed point in Properties");
        ImGui::Separator();
        if (ImGui::MenuItem("Environment (sky, ambient, fog)...")) a.environment = true;
        if (ImGui::MenuItem("Sky Lab...")) a.skyLab = true;
        if (ImGui::MenuItem("Rebake Sky Lab sky", nullptr, false, st.canRebakeSky)) a.rebakeSky = true;
        if (ImGui::MenuItem("Camera bookmarks...")) a.bookmarks = true;
        if (ImGui::MenuItem("Scale figure position...")) a.figurePos = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Render")) {
        if (ImGui::MenuItem("Render...", "Cmd+Shift+K", false, st.hasCameras)) a.render = true;
        if (ImGui::MenuItem("Update Camera Preview", "Cmd+K", false, st.hasCameras)) a.updatePreview = true;
        if (!st.hasCameras) ImGui::TextDisabled("Place a camera first (World > Camera tool)");
        ImGui::TextDisabled("The active camera's picture, at its size, saved in");
        ImGui::TextDisabled("the Bottega's renders folder with its log");
        ImGui::Separator();
        if (ImGui::BeginMenu("Quality")) {
            static const char* q[] = {"Draft", "Normal", "High"};
            for (int i = 0; i < 3; ++i)
                if (ImGui::MenuItem(q[i], nullptr, st.renderQuality == i)) a.renderQuality = i;
            ImGui::TextDisabled("sets the anti-aliasing (Draft off, Normal 2x, High 3x);");
            ImGui::TextDisabled("later also the baked light and reflections");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Anti-aliasing")) {
            if (ImGui::MenuItem("Off", nullptr, st.renderAA == 1)) a.renderAA = 1;
            for (int i = 2; i <= 4; ++i) {
                char b[48];
                std::snprintf(b, sizeof b, "%dx (drawn %d times larger)", i, i);
                if (ImGui::MenuItem(b, nullptr, st.renderAA == i)) a.renderAA = i;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Texture filtering")) {
            if (ImGui::MenuItem("Smooth", nullptr, st.renderSmooth)) a.renderSmooth = 1;
            if (ImGui::MenuItem("Crisp pixels", nullptr, !st.renderSmooth)) a.renderSmooth = 0;
            ImGui::TextDisabled("for renders and the Camera Preview;");
            ImGui::TextDisabled("the editor's own: Texture > Filtering");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Prop light")) {
            if (ImGui::MenuItem("Full", nullptr, !st.renderPropsQuick)) a.renderPropsQuick = 0;
            if (ImGui::MenuItem("Quick (a quarter of the rays)", nullptr, st.renderPropsQuick)) a.renderPropsQuick = 1;
            ImGui::TextDisabled("props baked per vertex, for the whole render;");
            ImGui::TextDisabled("the Camera Preview is always quick (Draft)");
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        // Shortcut labels are for an Italian keyboard (the two keys right of P).
        if (ImGui::MenuItem("Smaller grid", "\xC3\xA8")) a.gridSmaller = true;
        if (ImGui::MenuItem("Larger grid", "+"))  a.gridLarger = true;
        if (ImGui::MenuItem("Show grid", "Shift+R", st.gridVisible)) a.toggleGrid = true;
        if (ImGui::MenuItem("Snap to grid", "Shift+W", st.snapOn))  a.toggleSnap = true;
        if (ImGui::MenuItem("Set grid size...")) a.setGridSize = true;
        if (ImGui::MenuItem("Microgrid (sizes below 1)", nullptr, st.microgrid)) a.toggleMicrogrid = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Hide selected", "H", false, st.hasSelection)) a.hideSelected = true;
        if (ImGui::MenuItem("Hide unselected", "Shift+H", false, st.hasSelection)) a.hideUnselected = true;
        if (ImGui::MenuItem("Show all", "Cmd+U")) a.showAll = true;
        if (ImGui::MenuItem("Show primitives", nullptr, st.primsShown)) a.togglePrims = true;
        if (ImGui::MenuItem("Show props", nullptr, st.propsShown)) a.toggleProps = true;
        if (ImGui::MenuItem("Show decals", nullptr, st.decalsShown)) a.toggleDecals = true;
        ImGui::TextDisabled("Visgroups: the tab next to Textures");
        ImGui::Separator();
        if (ImGui::MenuItem("Center 2D views on selection", "Cmd+E", false, st.hasSelection)) a.centerViews = true;
        if (ImGui::MenuItem("All views to origin", "Cmd+0")) a.viewsToOrigin = true;
        if (ImGui::MenuItem("Fly mode (3D view)", "Z", st.flying)) a.flyMode = true;
        if (ImGui::MenuItem("3D view: textured", nullptr, st.textured3D)) a.textured3D = 1;
        if (ImGui::MenuItem("3D view: flat colours", nullptr, !st.textured3D)) a.textured3D = 0;
        if (ImGui::MenuItem("3D view: sky", nullptr, st.showSky)) a.toggleSky = true;
        if (ImGui::MenuItem("3D view: rough light preview", nullptr, st.lightPreview)) a.toggleLightPreview = true;
        if (ImGui::MenuItem("3D view: light grid (the last bake)", nullptr, st.showLightGrid, st.haveLightGrid)) a.toggleLightGrid = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Each sample of the baked light grid as a small cube, coloured by the light from its 6 sides.\n"
                              "Made by a render or a Camera Preview Update.");
        if (ImGui::MenuItem("3D view: fog", nullptr, st.showFog)) a.toggleFog = true;
        if (ImGui::MenuItem("Scale figure (1.75 m)", nullptr, st.showFigure)) a.toggleFigure = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Reset layout")) a.resetLayout = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Controls", "F1", st.controlsOpen)) a.controls = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
    return a;
}

void drawControlsWindow(bool* open) {
    if (!*open) return;
    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Controls", open)) {
        static const char* rows[][2] = {
            {"Bottega (shared folder)", "File > Bottega folder... picks it; Cmd+R (File menu) rescans it: textures, skies. Never automatic"},
            {"Select tool / Block tool", "Shift+S / Shift+B"},
            {"Draw a block", "Block tool: drag in a 2D view; Return creates, Esc cancels"},
            {"Select", "click (2D or 3D); Cmd+click adds/removes; Cmd+A all; Esc none. 3D view: drag a rectangle to select everything wholly inside it; Cmd+drag adds"},
            {"Move / resize", "drag the selection / its handles or edges"},
            {"Clone", "Shift+drag the selection, or Cmd+D"},
            {"Rotate by dragging", "Option+drag a corner handle (15\xC2\xB0 steps, +Shift free)"},
            {"Exact move / rotate / scale", "Cmd+T (Transform menu)"},
            {"Vertex tool", "Shift+V; click a corner, Cmd+click adds, drag moves"},
            {"Move vertices exactly", "Cmd+Shift+T (vertex tool)"},
            {"Snap vertices to grid", "Cmd+B"},
            {"Pivot / rotation snapping", "Transform menu"},
            {"Grid size / microgrid", "View menu"},
            {"Texture tool (pick faces)", "Shift+A; 3D: click, Shift+click add/remove face, Cmd+click whole brush, Option+click picks texture"},
            {"Apply current texture", "Shift+T (selected brushes, or picked faces)"},
            {"Wrap texture around a corner", "Texture tool: click a face, Option+Shift+click the next (repeat)"},
            {"Textures / Face panels", "right side: search by name, click to choose; adjust picked faces"},
            {"Select textures (materials)", "Textures panel: click chooses one; Cmd+click adds / removes one; Shift+click a range; Select all shown (after a search); Texture menu: Select the textures of the picked faces"},
            {"Material panel", "Texture tool, Properties: the selected materials' settings (shader, glow, cheap chrome, cutout, see-through, detail, texlight...) written to .vmt files beside the textures; several selected = all edited at once (mixed values shown); Cmd+Z undoes"},
            {"Bump maps", "Material panel: $bumpmap (or a picture named <texture>_normal / _bump / _ssbump beside it); %flipgreen for maps with green up; props use their glTF normal map, and a .vmt named like the model's material in its folder (Phong, rim light)"},
            {"Variants and parents", "Material panel: Make a variant... (a copy under a new name, same picture: Texture menu too); Parent: share settings through a parent .vmt (New parent from these...)"},
            {"Clip tool", "Shift+X: drag a line in a 2D view; drag points 1-3 (3 tilts the plane); Return clips"},
            {"Clip: keep both / front / back", "Shift+X again cycles (Tools menu)"},
            {"Carve with selection", "Cmd+Shift+C: cuts the selection's shape out of touching brushes"},
            {"Hollow", "Cmd+Shift+H: turn brushes into walls of a typed thickness"},
            {"Curve tool", "Shift+C: pick a shape in Properties, drag a box (wall: start to end); Return creates"},
            {"Edit / bake a curve", "Tools menu (select the generated shape first)"},
            {"Make terrain", "Terrain menu > Make terrain: click faces in 3D (Shift+click adds) or brushes in 2D (face toward you); Return makes them terrain, Esc stops"},
            {"Terrain points", "Vertex tool on a selected terrain: click a point (Cmd+click adds), drag in the 3D view or a side view; Cmd+Shift+T types a height"},
            {"Terrain tool", "Shift+G: drag over terrain (3D, or a 2D view facing it); mode, radius, amount in Properties; Esc cancels a stroke"},
            {"Primitive tool", "Shift+M (Primitives menu): pick sphere, cube, cylinder, cone, torus, capsule or disc in Properties, drag a box in a 2D view, Return creates, Esc cancels"},
            {"Decal tool", "Shift+D (Decals menu): pick a picture in the Textures panel, click a surface in the 3D view, drag to slide it, Return places it, Esc cancels; Option+click puts a new one over a decal"},
            {"Decals", "click one to select (Cmd+click adds); 3D: corners size it (Shift: proportions), the ring turns it (Shift: freely), drag slides it; Properties or Decals > Decal... for typed values; Decals > Bring forward / Send back; Delete; View > Show decals"},
            {"Props tool", "Shift+P (Props menu): pick a model in the Props panel (search by name), click a surface in 3D or a spot in a 2D view, drag to move it, type position / turn / scale in Properties, Return places it, Esc cancels"},
            {"Select / move / turn / scale props", "click a prop (any tool that selects; 3D picks its triangles, a 2D view its turned box); drag it in a 2D view; rings turn it (15 degrees; Shift: freely); size handles scale it evenly (Shift: one axis); Delete removes, Cmd+D duplicates, Cmd+J drops it onto what is below"},
            {"Select / move primitives", "click a primitive (Select, Light or Primitive tool; a combination selects whole); drag it in a 2D view; Delete removes, Cmd+D duplicates"},
            {"One piece of a combination", "Option+click it (or pick it in Properties), then drag, turn or size it alone"},
            {"Turn / size primitives", "drag the rings around the selection (15 degree steps; Shift: freely); drag a size handle (scales evenly; Shift: one axis, opposite face stays); or type in Properties"},
            {"Multi-Replicate", "Cmd+Shift+D (Edit menu): copies, each one step further (offset; primitives, props and lights also turn, primitives and props also scale); the preview stays while you look around the views; Return creates, Esc cancels"},
            {"Show / hide all primitives or props", "View > Show primitives, View > Show props (tick boxes). Hidden ones can't be clicked, selected or landed on; Show all (Cmd+U) and adding a new one show them again"},
            {"Random Replicate", "Cmd+Shift+R (Edit menu): copies scattered around the selection, each with a random offset, turn and size inside typed ranges (brushes only move, by whole grid steps while snapping is on); New seed shakes them up, the same seed gives the same copies; Return creates, Esc cancels. The copies stay selected: Cmd+J drops them onto the land"},
            {"Snap to land / ground", "Cmd+J (Transform menu): drops selected primitives and props onto the brushes, terrain, primitives or props below them (not onto a shelf above or a neighbour beside them); several selected ones land on each other only if they were stacked to begin with; Snap to ground: onto a typed height"},
            {"Blend primitives", "Properties, in a combination: blend (units) melts a piece into the others (0 = sharp)"},
            {"Combine primitives", "Primitives menu or Properties: Combine selected; each piece adds, subtracts or intersects; Separate undoes it"},
            {"Light tool", "Shift+L (World menu): pick point / spot / sun and settings in Properties, click in a view to place, Return creates, Esc cancels"},
            {"Select / move lights", "click a light's marker (any tool that selects: Select, Light); drag it in a 2D view; Delete removes, Cmd+D duplicates"},
            {"Aim a spot or sun", "drag the rotation rings of the selected light (3D: X red, Y green, Z blue; 2D: the ring around the view's axis), 15 degree steps (Shift: freely); or type pitch / yaw"},
            {"Camera tool", "Shift+K (World menu): settings in Properties (lens in degrees or mm, presets, near / far, picture size, orthographic), click in a view to place, Return creates, Esc cancels. The first camera is the active one"},
            {"Select / move / turn cameras", "click a camera's marker (a diamond; any tool that selects); drag it in a 2D view; drag the rings (15 degree steps; Shift: freely), the yellow ring rolls it; or type pitch / yaw / roll in Properties; Delete removes, Cmd+D duplicates"},
            {"Active camera", "the one Render and the Camera Preview use (white dot in its marker): tick 'active' in Properties, or World > Make selected camera active; only one at a time"},
            {"Camera Preview", "panel under Properties while the map has a camera: a draft render of the active camera's view (the map as it is, whatever the View menu shows); drawn only when you ask: Cmd+K, its Update button or Render > Update Camera Preview"},
            {"Render", "the Render button (left of the status bar), Render > Render... or Cmd+Shift+K: the active camera's picture at its size, saved in the Bottega's renders folder with its log; a terminal window shows the steps; Esc stops it"},
            {"Render options", "Render menu: Quality (Draft / Normal / High: also how finely the light is baked), Anti-aliasing, Texture filtering, Prop light (full / quick)"},
            {"Baked light", "part of every render (and the Camera Preview, at Draft): lightmaps with shadows and bounced light, as Source's VRAD bakes them; kept in memory while nothing that lights the map changes. Lights: falloff (inverse-square, 50% / 0%, custom), spot exponent, sun spread in Properties; sky light %, minimum light, bounce strength and smoothing angle in World > Environment"},
            {"Lightmap scale", "Texture tool, Face panel: units per lightmap pixel for the picked faces (default 40, Hammer's 16 units); primitives: in Properties; props: 'casts shadows' and 'lighting' (baked per vertex / like a Source model) in Properties; View > 3D view: light grid shows the last bake's ambient cubes"},
            {"Camera presets", "Properties (camera tool or a selected camera): Save preset / Load a preset (lens, near / far, size, orthographic, exposure), kept in the Bottega's presets/camera_presets"},
            {"Aim a camera at a point", "Properties: type a point and Aim, or World > Aim selected cameras at a clicked point, then click a surface in 3D or a spot in a 2D view"},
            {"Sky, ambient, fog", "World > Environment (sky folders too: 2:1 panoramas or 4:3 cube crosses)"},
            {"Sky Lab", "World > Sky Lab: make a sky (sky mode, sun, moon and eclipses, stars and comets, rings, stratus and cumulus clouds, haze, fog, rainbow, ground); saved skies in the Bottega's presets/skies, Randomize sky; the 3D view shows it while the window is open; Apply bakes it into the Bottega's skies/skylab and sets it as the map's sky (one undo step), OK applies and closes, Cancel (Esc) drops the changes. With a sun light in the map, tick the lock to keep the sky's sun on it; if the light turns later, World > Rebake Sky Lab sky"},
            {"Show sky / light preview / fog / figure", "View menu (3D view: sky, rough light preview, fog; scale figure)"},
            {"Camera bookmarks / figure position", "World menu"},
            {"Paint textures", "Terrain tool, Paint mode: paints the current texture (up to 8 per patch); painting the face's own texture uncovers it"},
            {"Heightmap / stitch / remove", "Terrain menu"},
            {"Delete", "Delete or Backspace"},
            {"Undo / redo", "Cmd+Z / Cmd+Shift+Z"},
            {"New / open / save / save as", "Cmd+N / Cmd+O / Cmd+S / Cmd+Shift+S"},
            {"Fly mode (3D view)", "Z over the 3D view; Esc or Z to stop"},
            {"Move while flying", "W A S D, Q/E down/up, Shift fast"},
            {"Look without fly mode", "hold right mouse in the 3D view"},
            {"Pan 2D view", "middle mouse, or Space + left mouse"},
            {"Zoom 2D view", "mouse wheel"},
            {"Center 2D views on selection", "Cmd+E"},
            {"Hide selected / unselected", "H / Shift+H"},
            {"Show everything again", "Cmd+U (also un-hides all visgroups)"},
            {"Visgroups", "tab next to Textures: named groups you can show / hide"},
            {"Find overlapping brushes", "Cmd+Shift+O; Next / Prev jump the camera to each"},
            {"Back to the origin", "Cmd+0 (all views), or the 'origin' button in a view's corner"},
            {"Smaller / larger grid", "\xC3\xA8 / + (the two keys right of P)"},
            {"Show grid / snap", "Shift+R / Shift+W"},
            {"This window", "F1"},
        };
        if (ImGui::BeginTable("controls", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupColumn("Keys");
            ImGui::TableHeadersRow();
            for (auto& r : rows) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(r[0]);
                ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", r[1]);
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

bool drawStatusBar(const char* text, bool canRender) {
    const float h = ImGui::GetFrameHeight();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_MenuBar;
    bool clicked = false;
    if (ImGui::BeginViewportSideBar("##statusbar", ImGui::GetMainViewport(), ImGuiDir_Down, h, flags)) {
        if (ImGui::BeginMenuBar()) {
            ImGui::BeginDisabled(!canRender);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.32f, 0.12f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.42f, 0.16f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.80f, 0.50f, 0.20f, 1));
            clicked = ImGui::SmallButton("Render");
            ImGui::PopStyleColor(3);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(canRender ? "Render the active camera's picture (Cmd+Shift+K)" : "Place a camera first (World > Camera tool)");
            ImGui::TextUnformatted(text);
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
    return clicked;
}

void dockspace(bool forceReset, const char* tools, const char* topLeft, const char* topRight,
               const char* bottomLeft, const char* bottomRight, const char* rightTop,
               const char* rightBottom, bool withPanels, const char* rightTopTab, const char* rightTopTab2,
               const char* rightPreview) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // Bump the version suffix whenever the default layout changes.
    const ImGuiID id = ImGui::GetID("MainDockspace.v6");

    if (forceReset || ImGui::DockBuilderGetNode(id) == nullptr) {
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(id, vp->WorkPos);
        ImGui::DockBuilderSetNodeSize(id, vp->WorkSize);

        ImGuiID toolsNode = 0, rest0 = 0, rest = 0, panel = 0;
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.07f, &toolsNode, &rest0);
        ImGuiID panelTop = 0, panelBottom = 0, panelPreview = 0;
        if (withPanels) {
            ImGui::DockBuilderSplitNode(rest0, ImGuiDir_Right, 0.22f, &panel, &rest);
            ImGui::DockBuilderSplitNode(panel, ImGuiDir_Up, 0.5f, &panelTop, &panelBottom);
            // The Camera Preview below Properties (v0.11.1; there only while the map has a camera).
            if (rightPreview) ImGui::DockBuilderSplitNode(panelBottom, ImGuiDir_Down, 0.42f, &panelPreview, &panelBottom);
        } else {
            rest = rest0; // "classic" layout (used by older UI test scripts)
        }
        ImGuiID left = 0, right = 0;
        ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, 0.5f, &left, &right);
        ImGuiID tl = 0, bl = 0, tr = 0, br = 0;
        ImGui::DockBuilderSplitNode(left,  ImGuiDir_Up, 0.5f, &tl, &bl);
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.5f, &tr, &br);

        ImGui::DockBuilderDockWindow(tools, toolsNode);
        if (withPanels) {
            ImGui::DockBuilderDockWindow(rightTop, panelTop);
            if (rightTopTab) ImGui::DockBuilderDockWindow(rightTopTab, panelTop); // second tab
            if (rightTopTab2) ImGui::DockBuilderDockWindow(rightTopTab2, panelTop); // third tab
            ImGui::DockBuilderDockWindow(rightBottom, panelBottom);
            if (rightPreview) ImGui::DockBuilderDockWindow(rightPreview, panelPreview);
        }
        ImGui::DockBuilderDockWindow(topLeft, tl);
        ImGui::DockBuilderDockWindow(topRight, tr);
        ImGui::DockBuilderDockWindow(bottomLeft, bl);
        ImGui::DockBuilderDockWindow(bottomRight, br);
        ImGui::DockBuilderFinish(id);
    }

    ImGui::DockSpaceOverViewport(id, vp, ImGuiDockNodeFlags_AutoHideTabBar);
}

} // namespace scal
