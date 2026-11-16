#pragma once
// Exact transforms of a set of brushes (used by the Transform dialog, rotate-90 and
// flip commands). Pure core: no graphics.
#include "core/brush.hpp"
#include <string>
#include <vector>

namespace scal {

enum class TransformKind { Move, Rotate, Scale };

struct TransformSpec {
    TransformKind kind = TransformKind::Move;
    Vec3 move;                // Move: offset
    int axis = 2;             // Rotate: 0 = X, 1 = Y, 2 = Z (right-hand rule)
    double degrees = 0;       // Rotate: angle
    Vec3 scale{1, 1, 1};      // Scale: per-axis factor; negative mirrors, 0 not allowed
    Vec3 pivot;               // Rotate/Scale: fixed point
};

// Returns transformed copies (same ids). On failure returns an empty vector and sets
// `error` (e.g. zero scale, or a result that is not a valid solid).
std::vector<Brush> applyTransform(const std::vector<Brush>& brushes, const TransformSpec& t,
                                  bool textureLock, std::string& error);

} // namespace scal
