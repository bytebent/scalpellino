#pragma once
// Bottega: the one folder shared by Mazzuola, Scalpellino and later tools (textures,
// models, maps, skies, shaders, renders, presets...). This is the file-system side only:
// check a folder, make one, name things inside it. No graphics, so it is tested headless.
//
// A Bottega is recognised by a small marker file at its root (`bottega.txt`) holding a
// format version, so tools know it is one and can upgrade the layout safely later.
#include <string>
#include <vector>

namespace scal::bottega {

constexpr int kFormatVersion = 1;
constexpr const char* kMarkerFile = "bottega.txt";

// The sub-folders every Bottega has, in the order they are made (parents first).
const std::vector<std::string>& subfolders();

enum class State {
    NoPath,        // nothing chosen yet
    MissingFolder, // the path doesn't exist (make() creates it)
    Empty,         // an empty folder (make() fills it)
    NotBottega,    // has files but no marker (make() adds the marker, keeps the files)
    Ready,         // a Bottega this version can use
    NewerVersion,  // made by a newer version of a tool: hands off
    Damaged,       // a marker that can't be read
};

struct Check {
    State state = State::NoPath;
    int version = 0;                  // marker version, when there is one
    std::vector<std::string> missing; // sub-folders a Ready Bottega lacks (make() adds them)
};

// Look at `root` without changing anything.
Check inspect(const std::string& root);

// True for a Bottega the app can read from right now.
inline bool usable(const Check& c) { return c.state == State::Ready; }

// Make `root` a Bottega: create the folder if needed, every sub-folder that is missing and
// the marker if there is none. Never deletes or overwrites anything. Refuses a folder from
// a newer version or one whose marker is damaged. On failure returns false and fills `error`.
bool make(const std::string& root, std::string& error);

// One plain-language line about a check, for the settings dialog.
std::string describe(const Check& c);

// `root` + "/" + `sub` (sub may hold slashes, e.g. "presets/camera_presets").
std::string path(const std::string& root, const std::string& sub);

// The name a file inside a Bottega goes by in a map: its path relative to the root, without
// the extension, with '/' separators and spaces turned into '_' (.map files can't hold
// spaces). "" if `file` isn't inside `root`.
std::string keyFor(const std::string& root, const std::string& file);

} // namespace scal::bottega
