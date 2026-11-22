#pragma once
// v0.12.6 — reflection probes on the graphics card: "building cubemaps" (as Source's
// buildcubemaps) and the texture the material shaders read them from.
//  - Each probe is drawn 6 times (the cube's faces, OpenGL's order) through the renderer at
//    its own spot, in linear light before exposure (Source's HDR cubemaps are float too),
//    fog and everything else as a render has it. Drawn 2x larger and averaged down (Draft: as
//    is), with a 1-pixel margin so neighbouring faces join without a seam.
//  - All probes go into ONE float texture (the atlas), so every pixel can blend its 4 nearest
//    probes (core/probe.hpp, probeWeights; the shader's probeEnv does the same). Each probe
//    is a tile of 3 x 2 face slots; a slot holds its face of `side` pixels plus a border of
//    `side / 4` (at least 2) taken from the neighbouring faces, so bilinear reads never mix
//    in another face: "seamless" filtering (agreed: a difference from Source, whose DirectX 9
//    filtering can show the cube's edges). Smaller copies (mipmaps) are made per slot, so a
//    reflection seen from far away or on a bumpy surface is soft, as Source's mipmapped
//    cubemaps (the shader picks the level from how fast the reflection turns).
//  - A second data texture holds each probe's position and tile (texelFetch).
#include "core/probe.hpp"
#include "render/scene.hpp"
#include "render/viewport.hpp"
#include <memory>
#include <string>
#include <vector>

namespace scal {

struct ProbeAtlas {
    unsigned tex = 0;      // RGB 16-bit float, with mipmaps
    unsigned data = 0;     // RGBA 32-bit float, 2 x count: (pos, side), (tile x, tile y, side, slot)
    int w = 0, h = 0, levels = 1;
    struct Slot {
        Vec3 pos;
        int x = 0, y = 0;  // the tile's corner in the atlas (pixels)
        int side = 0;      // the face's pixels a side
        int slot = 0;      // a face slot's pixels a side (side + 2 x border)
        double seconds = 0; // drawing it (this pass)
    };
    std::vector<Slot> slots;
    ProbeAtlas() = default;
    ProbeAtlas(const ProbeAtlas&) = delete;
    ProbeAtlas& operator=(const ProbeAtlas&) = delete;
    ~ProbeAtlas();
};

// The slot size for a face of `side` pixels: side + 2 x max(2, side / 4).
int probeSlotSize(int side);

// GLSL: `vec3 probeLook(sampler2D atlas, vec2 atlasSize, vec4 tile, vec3 d, float rate)`, a
// probe's colour along direction d (linear). `tile`: x, y of its tile, its side, its slot size
// (as the data texture holds them); `rate`: how fast d turns per screen pixel (radians), which
// picks the mipmap level (0: the sharpest). Shared by the material shaders and the editor's
// mirror balls.
const char* probeLookGlsl();

class ProbeBuilder {
public:
    // One pass over all `probes`: draws each from `scene` (its `reflections`: the previous
    // pass's probes, or null for the sky) at `quality` (0 Draft, 1 Normal, 2 High) and returns
    // the new atlas. Null if the graphics card can't make the pictures.
    std::shared_ptr<ProbeAtlas> pass(const std::vector<Probe>& probes, int quality, const Scene& scene, bool smooth);
    void release() { view_.reset(); }

private:
    std::unique_ptr<Viewport> view_;
};

} // namespace scal
