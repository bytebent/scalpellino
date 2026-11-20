#pragma once
// Built-in Sky Lab presets (v0.11.2): ready-made skies shipped in the app, loaded like the
// user's saved skies (the Bottega's presets/skies) but needing no Bottega.
#include <string>
#include <vector>

namespace scal {

struct BuiltinSky {
    const char* name;  // shown in Sky Lab ("Sunset", "Moonlit night"...)
    const char* text;  // the settings, as skyLabToString writes them
};
const std::vector<BuiltinSky>& builtinSkyPresets();
const BuiltinSky* findBuiltinSky(const std::string& name); // nullptr if there is none

} // namespace scal
