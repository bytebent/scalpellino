#pragma once
// Texture mapping maths for Valve 220 faces. No graphics.
// A point p on a face gets texel coordinates
//   s = dot(p, u.axis) / scaleU + u.offset,   t = dot(p, v.axis) / scaleV + v.offset
// and normalized texture coordinates (s / width, t / height).
#include "core/brush.hpp"
#include <string>

namespace scal {

struct TexSize {
    int w = 64, h = 64;
};

struct UV {
    double u = 0, v = 0;
};

// Normalized texture coordinates of `p` on face `f`.
UV texCoord(const Face& f, const Vec3& p, TexSize ts);

// Resets axes to world-aligned (Hammer "align to world"); keeps scale; zero offsets.
void alignTextureToWorld(Face& f, const Vec3& normal);
// Lays the axes in the face's own plane (Hammer "align to face"); keeps scale.
void alignTextureToFace(Face& f, const Vec3& normal);
// Rotates the texture on the face by `degrees` (around the face normal).
void rotateTexture(Face& f, const Vec3& normal, double degrees);

// Scales and shifts so the texture covers the face exactly `timesU` x `timesV` times.
void fitTexture(Face& f, const Polygon& poly, TexSize ts, int timesU = 1, int timesV = 1);

enum class Justify { Left, Right, Top, Bottom, Center };
// Shifts the texture so its edge (or centre) lines up with the face's edge (or centre).
void justifyTexture(Face& f, const Polygon& poly, TexSize ts, Justify j);

// Continues `from`'s texture onto the neighbouring face `to` as if folded around their
// shared edge (Hammer's "wrap"): texture, scale and rotation are copied, the axes are
// rotated about the edge, and offsets are set so texels line up exactly along the edge.
// Returns false if the faces don't share an edge.
bool wrapTexture(const Face& from, const Polygon& fromPoly, Face& to, const Polygon& toPoly);

// Wraps offsets into [0, size) (same look, tidy numbers).
void normalizeTextureOffsets(Face& f, TexSize ts);

// "nodraw": a tool texture for faces that are never rendered or exported (hidden sides).
// The editor shows it (yellow NODRAW); renders and exports skip those faces.
constexpr const char* kNoDrawTexture = "nodraw";
bool isNoDraw(const std::string& textureName);

// Face hit first by a ray (unit dir); returns face index or -1. `tOut` = distance.
int rayHitFace(const Brush& b, const Vec3& origin, const Vec3& dir, double* tOut = nullptr);

} // namespace scal
