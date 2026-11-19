#pragma once
// Reads a `.glb` / `.gltf` file into a Model (core/model.hpp) with cgltf. No graphics.
// glTF is Y-up, in metres, right-handed; the model comes out Z-up in centimetres (the
// editor's own space): (x, y, z) -> (x, -z, y) * 100. Node transforms are applied, so the
// scene is flattened; only triangle meshes are read (animations, skins, cameras and lights
// are ignored). Materials: base colour factor and texture, alpha cutout; everything else
// (metal, roughness, emissive, blending) is left out for now.
#include "core/model.hpp"
#include <string>

namespace scal {

// Fills `out` (name and file are left as they are). False with a plain message in `error`
// if the file can't be read or holds nothing to show.
bool loadGlbFile(const std::string& path, Model& out, std::string& error);

} // namespace scal
