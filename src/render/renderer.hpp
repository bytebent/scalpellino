#pragma once
// The renderer (milestone 12): the active camera's picture of the map, as the map has it.
// Renders (Render menu, Cmd+Shift+K) and the Camera Preview (a draft render) both come from
// here, so the preview can't disagree with the final picture.
//  - The View menu is ignored: sky, fog, primitives, props, textures are what the map has.
//  - Hidden brushes and visgroups are drawn (hiding is an editing aid only).
//  - nodraw faces are left out.
// v0.12.0: the lighting is the rough light preview's (lights and ambient, no shadows) until
// the baked light arrives (v0.12.1).
#include "core/document.hpp"
#include "render/lighting.hpp"
#include "render/scene.hpp"
#include "render/viewport.hpp"
#include <memory>
#include <string>
#include <vector>

namespace scal {

class TextureLibrary;
class ModelLibrary;
class SkyLibrary;

class Renderer {
public:
    // What went into the picture (for the render log).
    struct Stats {
        size_t brushes = 0, hiddenBrushes = 0, nodrawFaces = 0, terrains = 0;
        size_t prims = 0, props = 0, missingProps = 0, lights = 0;
        std::vector<std::string> missingTextures; // names the library doesn't have (drawn as the checker)
        std::string sky, skyProblem;              // the map's sky ("" none) and why it can't be shown
        bool fog = false;
    };

    // The scene a picture holds (see the top of this file). Brushes are kept per document
    // revision, and rebuilt only for brushes an edit touched. `lit`: also the lighting (the slow part; the render log shows it as a step).
    Scene scene(const Document& doc, TextureLibrary& textures, ModelLibrary& models, SkyLibrary& skies, Stats* stats = nullptr);
    void light(const Document& doc, Scene& s);

    // Draws the picture through `cam` into the renderer's own target, w x h logical pixels
    // at `scale` pixels each (the preview: the screen's scale; a render: 1). Texture
    // filtering for this picture: smooth or crisp (the editor's own switch is put back
    // after). False if the graphics card couldn't make a target that large.
    bool draw(const Camera& cam, const Scene& s, int w, int h, float scale, bool smooth);
    const RenderTexture2D* target() const { return view_ ? &view_->target() : nullptr; }
    int width() const { return view_ ? view_->width() : 0; }
    int height() const { return view_ ? view_->height() : 0; }
    // The last drawing as RGBA, rows top first (w, h: its pixel size).
    std::vector<unsigned char> read(int& w, int& h) const;
    // Frees the graphics memory (a render's large target).
    void release() { view_.reset(); }

private:
    std::unique_ptr<Viewport> view_;
    SceneCache brushes_{SceneCache::Options{false, true}}; // every brush, nodraw faces left out
    LightingCache lighting_;
};

} // namespace scal
