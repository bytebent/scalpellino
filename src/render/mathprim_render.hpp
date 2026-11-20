#pragma once
// Math primitives in the 3D view (milestone 9): drawn exactly, per pixel, by a shader
// that does the same maths as core/mathprim (combinations included). Called inside the
// 3D view's camera mode; writes depth so primitives and brushes cut into each other.
#include <optional>
#include "core/mathprim.hpp"
#include "core/world.hpp"
#include "render/texture_library.hpp"
#include <vector>

namespace scal {

class BakedLight;

struct PrimDrawContext {
    Vec3 camPos;
    std::optional<Vec3> orthoDir;       // an orthographic camera's view direction (parallel rays)
    TextureLibrary* textures = nullptr; // null = flat colours
    bool lightPreview = false;          // rough light preview (else the editor's shading)
    Vec3 ambient;                       // 0..255
    std::vector<Light> lights;
    bool fog = false;
    Vec3 fogColor;
    double fogStart = 0, fogEnd = 1;
    double fogBase = 0, fogThickness = 0; // height fog (renders); thickness 0 = everywhere
    const BakedLight* baked = nullptr;    // renders: each object reads its 3D lightmap (v0.12.1)
    float exposure = 1;                   // with baked light: multiplier (2^stops)
};

void drawMathPrims3D(const std::vector<MathPrim>& prims, const PrimDrawContext& ctx);
bool mathPrimShaderOk(); // false if the GPU refused the shader (then only outlines show)

} // namespace scal
