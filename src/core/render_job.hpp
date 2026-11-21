#pragma once
// Render (milestone 12), the parts that need no graphics: the render options, the size a
// picture is drawn at (anti-aliasing draws it larger), scaling it back down, and the
// names of the files a render writes. The drawing itself is in render/renderer.*.
#include <ctime>
#include <string>
#include <vector>

namespace scal {

enum class RenderQuality { Draft, Normal, High };
const char* renderQualityName(RenderQuality q); // "Draft", "Normal", "High"

// The render options (Render menu). Kept in the user's settings, not in the map.
struct RenderSettings {
    RenderQuality quality = RenderQuality::Normal;
    int aa = 2;          // anti-aliasing: the picture is drawn aa x aa times larger, then scaled down (1 = off)
    bool smooth = true;  // texture filtering: smooth (mipmaps) or crisp pixels
    bool propsQuick = false; // Prop light: quick = props baked per vertex with a quarter of the rays
    bool operator==(const RenderSettings& o) const { return quality == o.quality && aa == o.aa && smooth == o.smooth && propsQuick == o.propsQuick; }
};
constexpr int kMaxRenderAA = 4;
// Choosing a quality sets what it decides (for now the anti-aliasing: Draft off, Normal
// 2x, High 3x; later also the baked light and the reflection probes).
void applyQuality(RenderSettings& s, RenderQuality q);
std::string renderSettingsToString(const RenderSettings& s); // "key value" lines
RenderSettings renderSettingsFromString(const std::string& text); // unknown / bad lines ignored

// The anti-aliasing a picture of w x h can be drawn with: at most `aa`, lowered until the
// drawn picture fits `maxSide` pixels per side and `maxPixels` in all (graphics memory).
// 0 if even 1 doesn't fit.
int fitAA(int w, int h, int aa, int maxSide = 16384, long long maxPixels = 50'000'000);

// Scales an RGBA picture (rows top first) down by `f` in both directions, averaging each
// f x f block in linear light (as the eye adds light up), into RGB. w and h must be
// multiples of f. f = 1 just drops the alpha.
std::vector<unsigned char> downscaleRGB(const std::vector<unsigned char>& rgba, int w, int h, int f);

// File names: "<map>_<camera>_<YYYY-MM-DD_HH-MM-SS>" (no extension). The map is its file
// name without folder and extension ("untitled" for a new map); names are cleaned for
// files (letters, digits, - and _ kept, everything else becomes _).
std::string renderFileStem(const std::string& mapPath, const std::string& cameraName, const std::tm& when);
std::string cleanFileName(const std::string& raw);

} // namespace scal
