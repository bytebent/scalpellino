#pragma once
// Sky images (milestone 8). Two layouts are accepted:
//  - panorama: 2:1, equirectangular (left to right = all the way round, top = straight up)
//  - cube cross: 4:3, a horizontal cross (middle row left, front, right, back; up above
//    the front, down below it), as most sky packs ship them.
// A cross is turned into a panorama when it is loaded, so everything else only knows
// panoramas. No graphics dependencies.
#include "core/math.hpp"
#include <vector>

namespace scal {

enum class SkyLayout { Unknown, Panorama, Cross };
SkyLayout skyLayout(int w, int h);

// RGB (3 bytes per pixel, row-major, top row first) cube cross -> RGB panorama of
// outW x outW/2 pixels (bilinear).
std::vector<unsigned char> crossToPanorama(const unsigned char* rgb, int w, int h, int outW);

// Where world direction `dir` (Z up) is in a panorama, u and v in 0..1 (v = 0 at the
// top). The middle of the image faces +X; turning right (clockwise seen from above)
// moves right in the image.
void skyUV(const Vec3& dir, double& u, double& v);

} // namespace scal
