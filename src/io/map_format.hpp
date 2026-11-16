#pragma once
// Valve 220 .map reading and writing. No graphics dependencies.
// Reads both Valve 220 faces ([ axis offset ] form) and classic Quake faces.
#include "core/document.hpp"
#include <string>

namespace scal {

std::string writeMap(const Document& doc);

struct MapReadResult {
    bool ok = false;
    std::string error;   // with line number when !ok
    int skippedBrushes = 0; // invalid brushes dropped while loading
};

MapReadResult readMap(const std::string& text, Document& doc);

bool saveMapFile(const Document& doc, const std::string& path, std::string& error);
MapReadResult loadMapFile(const std::string& path, Document& doc);

} // namespace scal
