#pragma once
// Math primitives in the 3D view (milestone 9): drawn exactly, per pixel, by a shader
// that does the same maths as core/mathprim (combinations included). Called inside the
// 3D view's camera mode; writes depth so primitives and brushes cut into each other.
#include "core/mathprim.hpp"
#include "core/world.hpp"
#include "render/texture_library.hpp"
#include <vector>

namespace scal {

struct PrimDrawContext {
    Vec3 camPos;
    TextureLibrary* textures = nullptr; // null = flat colours
    bool lightPreview = false;          // rough light preview (else the editor's shading)
    Vec3 ambient;                       // 0..255
    std::vector<Light> lights;
    bool fog = false;
    Vec3 fogColor;
    double fogStart = 0, fogEnd = 1;
};

void drawMathPrims3D(const std::vector<MathPrim>& prims, const PrimDrawContext& ctx);
bool mathPrimShaderOk(); // false if the GPU refused the shader (then only outlines show)

} // namespace scal
