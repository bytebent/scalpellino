#pragma once
// What the viewports draw this frame: brush polygons (cached per document revision,
// with tool previews substituted), selection, and tool overlays.
#include "core/document.hpp"
#include "core/terrain.hpp"
#include "core/world.hpp"
#include "core/mathprim.hpp"
#include "core/prop.hpp"
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

namespace scal {

struct SceneBrush {
    BrushId id = 0;
    std::vector<Polygon> polys;
    std::vector<Face> faces; // texture data for each polygon's face index
    AABB bounds;
    // Terrain: the terrain face's polygon is replaced by one 4-point polygon per grid
    // cell, polys[terrainFirst ...]; per corner of each cell a smooth normal (4 per cell,
    // same order as the cell's verts). Each painted texture (bottom first) has a blend
    // value per cell corner, laid out the same way.
    std::optional<TerrainGrid> terrain;
    size_t terrainFirst = 0;
    struct Layer { std::string texture; std::vector<float> cellAlpha; };
    std::vector<Layer> layers;
    std::vector<Vec3> cellNormals;
};

class TextureLibrary;
class ModelLibrary;
using FaceRef = std::pair<BrushId, int>; // brush id + face index

struct Scene {
    std::vector<SceneBrush> brushes;
    std::set<BrushId> selected;
    std::optional<AABB> handleBox;  // draw resize handles + size labels for this box (2D)
    std::optional<AABB> previewBox; // block tool: brush about to be created
    std::vector<SceneBrush> ghosts; // brushes being dragged (drawn on top, selected style)
    bool hideSelected = false;      // hide originals while their ghosts are dragged (move)
    // Select tool: the rectangle being dragged in one view (logical pixels).
    struct SelectRect { const void* view = nullptr; float ax = 0, ay = 0, bx = 0, by = 0; };
    std::optional<SelectRect> selectRect;
    std::vector<SceneBrush> invalidGhosts; // vertex edits that are not valid solids (magenta)
    bool invalid = false;
    struct VertexHandle { Vec3 pos; bool selected; bool terrain = false; };
    std::vector<VertexHandle> vertexHandles; // vertex tool corners (terrain: grid points,
                                             // drawn smaller, also in the 3D view)
    std::optional<Vec3> pivot;               // custom rotation pivot marker
    TextureLibrary* textures = nullptr;      // set -> textured 3D view
    std::set<FaceRef> selectedFaces;         // texture tool selection (highlighted in 3D)
    std::vector<Vec3> clipPoints;            // clip tool: points 1, 2, 3
    bool clipActive = false;
    std::set<BrushId> hidden;                // brushes not drawn (e.g. a curve being regenerated)
    std::vector<SceneBrush> replaced;        // drawn like normal brushes in place of hidden ones
                                             // (terrain being sculpted)
    struct BrushCursor {
        Vec3 center, normal;
        double radius;
        std::vector<Vec3> ring; // optional: the circle laid onto the surface (3D view)
    };
    std::optional<BrushCursor> brushCursor;  // terrain tool: circle on the surface
    std::vector<std::vector<Vec3>> outlines; // highlighted polygons (terrain tool: picked faces)

    // Lights (milestone 8): markers in every view; selected spot/sun lights get rotation
    // rings (one per world axis; in a 2D view only the one around the view's depth axis).
    struct SceneLight { Light light; bool selected = false; };
    std::vector<SceneLight> lights;
    std::optional<Light> pendingLight;   // light tool: where Return will create a light
    std::vector<Light> previewLights;    // Multi-Replicate preview

    // Cameras (milestone 11): a pyramid toward the picture (a box when orthographic) and a
    // marker in every view; the active one drawn brighter. Selected ones get the three
    // rotation rings plus a roll ring around their view direction (activeRing 3).
    struct SceneCamera { Camera cam; bool selected = false; };
    std::vector<SceneCamera> cameras;
    std::optional<Camera> pendingCamera;  // camera tool: where Return will create one
    static constexpr double kRollRingScale = 1.35; // roll ring radius / the other rings'

    // Math primitives (milestone 9): exact shapes in 3D, outlines in 2D.
    struct ScenePrim { MathPrim prim; bool selected = false; };
    std::vector<ScenePrim> prims;
    std::vector<MathPrim> pendingPrims;  // primitive tool preview (outlines)

    // Props (milestone 10): models drawn in 3D (from the model library), boxes in 2D.
    // `local` is the model's box in its own space (the missing-model box if `missing`).
    struct SceneProp { Prop prop; bool selected = false; AABB local; bool missing = false; };
    std::vector<SceneProp> props;
    std::vector<SceneProp> pendingProps;  // Props tool / Multi-Replicate preview (yellow boxes)
    ModelLibrary* models = nullptr;       // set -> props are drawn as models in 3D
    // Rings and size handles of the selected primitives and props (tools that edit objects set them;
    // views make the rings at least kRingPx across, and show only handles they see side-on).
    struct PrimGizmo {
        Vec3 center;
        double radius = 0;
        std::vector<std::pair<Vec3, Vec3>> handles; // position, the axis it moves along
    };
    std::vector<PrimGizmo> gizmos; // one for the selected primitives, one for the selected props
    bool lightPreview = false;           // rough light preview on (prims light per pixel)
    Vec3 ambient;                        // for the preview
    int activeRing = -1;                 // ring being dragged (axis), drawn brighter
    static constexpr double kRingPx = 48; // ring radius on screen (logical pixels)

    // Environment preview (3D view only, each switched on in the View menu).
    unsigned skyTexture = 0;             // panorama texture id (0 = no sky drawn)
    float skyCover = 1;                  // the part of the panorama's height it holds (from the top)
    Vec3 skyBelow;                       // the plain colour below that (0..255)
    std::optional<Vec3> figure;          // scale figure: feet position (175 units tall)
    // Rough light preview (render/lighting.hpp). Null = normal shading.
    const std::map<BrushId, std::vector<struct LitPoly>>* lighting = nullptr;
    // Fog preview (3D view): colour 0..255, clear up to `fogStart`, full from `fogEnd`.
    bool fog = false;
    Vec3 fogColor;
    double fogStart = 0, fogEnd = 1;
    // Height fog (Sky Lab: lying from base up through thickness). Renders only; the editor's
    // rough fog stays distance-only (thickness 0 = everywhere).
    double fogBase = 0, fogThickness = 0;
};

SceneBrush makeSceneBrush(const Brush& b);

// Keeps polygons of the VISIBLE brushes, rebuilt only when the document changes.
class SceneCache {
public:
    const std::vector<SceneBrush>& get(const Document& doc);

private:
    unsigned long long revision_ = ~0ull;
    std::vector<SceneBrush> brushes_;
};

} // namespace scal
