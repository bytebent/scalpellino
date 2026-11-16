#pragma once
// What the viewports draw this frame: brush polygons (cached per document revision,
// with tool previews substituted), selection, and tool overlays.
#include "core/document.hpp"
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
};

class TextureLibrary;
using FaceRef = std::pair<BrushId, int>; // brush id + face index

struct Scene {
    std::vector<SceneBrush> brushes;
    std::set<BrushId> selected;
    std::optional<AABB> handleBox;  // draw resize handles + size labels for this box (2D)
    std::optional<AABB> previewBox; // block tool: brush about to be created
    std::vector<SceneBrush> ghosts; // brushes being dragged (drawn on top, selected style)
    bool hideSelected = false;      // hide originals while their ghosts are dragged (move)
    std::vector<SceneBrush> invalidGhosts; // vertex edits that are not valid solids (magenta)
    bool invalid = false;
    struct VertexHandle { Vec3 pos; bool selected; };
    std::vector<VertexHandle> vertexHandles; // vertex tool corners
    std::optional<Vec3> pivot;               // custom rotation pivot marker
    TextureLibrary* textures = nullptr;      // set -> textured 3D view
    std::set<FaceRef> selectedFaces;         // texture tool selection (highlighted in 3D)
    std::vector<Vec3> clipPoints;            // clip tool: points 1, 2, 3
    bool clipActive = false;
    std::set<BrushId> hidden;                // brushes not drawn (e.g. a curve being regenerated)
};

SceneBrush makeSceneBrush(const Brush& b);

// Keeps brush polygons, rebuilt only when the document changes.
class SceneCache {
public:
    const std::vector<SceneBrush>& get(const Document& doc);

private:
    unsigned long long revision_ = ~0ull;
    std::vector<SceneBrush> brushes_;
};

} // namespace scal
