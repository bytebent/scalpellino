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

// Memory saving (v0.11.2): a panorama (RGB rows, top to bottom) whose bottom is one plain
// colour all the way across (Sky Lab's ground) needs only the rows above it in memory;
// what lies below is drawn in that colour. Returns how many rows to keep from the top:
// down to where the colour stops changing (the horizon's fade, haze, fog), plus
// `marginDeg` degrees more to be sure (the image is 180 degrees tall), and never above
// the horizon. `below` gets the plain colour. Returns `h` (keep everything) when there is
// no plain bottom or dropping it would save less than 5%.
int skyRowsToKeep(const unsigned char* rgb, int w, int h, double marginDeg, unsigned char below[3], int tolerance = 2);

// Where world direction `dir` (Z up) is in a panorama, u and v in 0..1 (v = 0 at the
// top). The middle of the image faces +X; turning right (clockwise seen from above)
// moves right in the image.
void skyUV(const Vec3& dir, double& u, double& v);

} // namespace scal
