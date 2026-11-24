#pragma once
// v0.12.7 — mirrors (Source's LightmappedReflective, agreed with the user in batch 8): before
// a picture is drawn, each flat mirror plane in view (core/mirror.hpp, at most 8, the ones
// covering most of the picture first) gets the scene drawn again through the renderer:
//  - its reflection, from the mirrored camera, keeping only what is in front of the glass;
//  - what is behind it (if its material sees through), from the camera itself, keeping only
//    what is behind the glass.
// Both into 16-bit float targets, in linear light before exposure (the mirror face's own
// shader applies the exposure and fog once), the render's own size (halved at Draft). Mirror
// faces inside these pictures reflect the probes (no mirror in a mirror).
#include "core/mirror.hpp"
#include "render/scene.hpp"
#include "render/viewport.hpp"
#include "raylib.h"
#include <memory>
#include <vector>

namespace scal {

class TextureLibrary;

struct MirrorSet {
    struct Plane {
        MirrorPlane p;
        RenderTexture2D refl{}, refr{}; // id 0: not drawn
        double seconds = 0;
    };
    std::vector<Plane> planes;
    size_t dropped = 0; // mirror planes in view beyond the limit (they reflect the probes)
    int w = 0, h = 0;
    MirrorSet() = default;
    MirrorSet(const MirrorSet&) = delete;
    MirrorSet& operator=(const MirrorSet&) = delete;
    ~MirrorSet();
};

// The mirror faces among `brushes` (flat faces whose material is LightmappedReflective; not
// terrain, not hidden).
std::vector<MirrorFace> mirrorFaces(const Scene& scene);

class MirrorBuilder {
public:
    // The pictures for camera `cam` (w x h pixels each). Null when there are no mirror planes
    // in view. `scene`: lit, as the picture will be drawn (its probes included).
    std::shared_ptr<MirrorSet> build(const Camera& cam, const Scene& scene, int w, int h, bool smooth);
    void release() { view_.reset(); }

private:
    std::unique_ptr<Viewport> view_;
};

} // namespace scal
