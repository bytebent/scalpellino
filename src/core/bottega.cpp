#include "core/bottega.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace scal::bottega {

namespace fs = std::filesystem;

const std::vector<std::string>& subfolders() {
    static const std::vector<std::string> v = {
        "textures", "models", "maps", "palettes", "skies", "shaders", "renders", "presets", "presets/camera_presets",
    };
    return v;
}

std::string path(const std::string& root, const std::string& sub) {
    if (sub.empty()) return root;
    if (root.empty()) return sub;
    return root.back() == '/' ? root + sub : root + "/" + sub;
}

namespace {

// Marker file: a line "bottega <version>" (other lines are notes). -1 if unreadable.
int readMarker(const fs::path& file) {
    std::ifstream f(file);
    if (!f) return -1;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream in(line);
        std::string word;
        int v = 0;
        if (in >> word >> v && word == "bottega" && v > 0) return v;
    }
    return -1;
}

bool isEmptyDir(const fs::path& dir) {
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    return !ec && it == fs::directory_iterator();
}

} // namespace

// Starter materials for a new Bottega (v0.12.7, agreed with the user; they may be removed
// in a later version): textures/starter/. Only written where no file of that name is.
// v0.13.0: water, murky_water, ground and clouds for the endless planes (milestone 13).
namespace {
struct Starter { const char* name; const char* text; };
const Starter kStarters[] = {
        {"water.vmt",
         "\"Water\"\n{\n"
         "\t\"%tooltexture\" \"tool_water\"\n"
         "\t\"$normalmap\" \"water_ripple_normal\"\n"
         "\t\"$bumptransform\" \"center .5 .5 scale 0.25 0.25 rotate 0 translate 0 0\"\n"
         "\t\"$reflecttint\" \"[0.85 0.95 0.95]\"\n"
         "\t\"$reflectamount\" \"0.04\"\n"
         "\t\"$refracttint\" \"[0.7 0.9 0.85]\"\n"
         "\t\"$refractamount\" \"0.03\"\n"
         "\t\"$reflectentities\" \"1\"\n"
         "\t\"$fogcolor\" \"{30 82 88}\"\n"
         "\t\"$fogstart\" \"0\"\n"
         "\t\"$fogend\" \"600\"\n"
         "}\n"},
        {"murky_water.vmt",
         "\"Water\"\n{\n"
         "\t\"%tooltexture\" \"tool_water\"\n"
         "\t\"$normalmap\" \"water_ripple_normal\"\n"
         "\t\"$bumptransform\" \"center .5 .5 scale 0.25 0.25 rotate 0 translate 0 0\"\n"
         "\t\"$reflecttint\" \"[0.6 0.6 0.55]\"\n"
         "\t\"$reflectamount\" \"0.03\"\n"
         "\t\"$refractamount\" \"0.02\"\n"
         "\t\"$fogcolor\" \"{70 56 34}\"\n"
         "\t\"$fogstart\" \"0\"\n"
         "\t\"$fogend\" \"60\"\n"
         "}\n"},
        {"ground.vmt",
         "\"LightmappedGeneric\"\n{\n"
         "\t\"$basetexture\" \"Grass_20\"\n"
         "}\n"},
        {"clouds.vmt",
         "\"Clouds\"\n{\n"
         "\t\"%tooltexture\" \"tool_clouds\"\n"
         "\t\"$cover\" \"45\"\n"
         "\t\"$cloudsize\" \"6000\"\n"
         "\t\"$softness\" \"45\"\n"
         "\t\"$opacity\" \"0.9\"\n"
         "}\n"},
        {"mirror.vmt",
         "\"LightmappedReflective\"\n{\n"
         "\t\"%tooltexture\" \"Tile_13\"\n"
         "\t\"$reflecttint\" \"[0.85 0.87 0.9]\"\n"
         "\t\"$minreflectivity\" \"1\"\n"
         "\t\"$refracttexture\" \"none\"\n"
         "}\n"},
        {"glass_pane.vmt",
         "\"Refract\"\n{\n"
         "\t\"%tooltexture\" \"Tile_13\"\n"
         "\t\"$normalmap\" \"glass_ripple_normal\"\n"
         "\t\"$refractamount\" \"0.02\"\n"
         "\t\"$refracttint\" \"[0.85 0.95 0.9]\"\n"
         "\t\"$bluramount\" \"1\"\n"
         "\t\"$envmap\" \"env_cubemap\"\n"
         "\t\"$envmaptint\" \"[0.12 0.12 0.12]\"\n"
         "}\n"},
        {"polished_stone.vmt",
         "\"LightmappedReflective\"\n{\n"
         "\t\"$basetexture\" \"Tile_19\"\n"
         "\t\"$reflecttint\" \"[0.3 0.3 0.3]\"\n"
         "\t\"$refracttexture\" \"none\"\n"
         "}\n"},
};
} // namespace
static void writeStarterMaterials(const fs::path& dir) {
    std::error_code ec;
    const fs::path folder = dir / "textures" / "starter";
    fs::create_directories(folder, ec);
    if (ec) return;
    for (const Starter& s : kStarters) {
        const fs::path f = folder / s.name;
        if (fs::exists(f, ec)) continue;
        std::ofstream o(f);
        o << s.text;
    }
}

bool writeStarterMaterial(const std::string& root, const std::string& name) {
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return false;
    for (const Starter& s : kStarters) {
        if (name + ".vmt" != s.name) continue;
        const fs::path folder = fs::path(root) / "textures" / "starter";
        fs::create_directories(folder, ec);
        const fs::path f = folder / s.name;
        if (fs::exists(f, ec)) return true;
        std::ofstream o(f);
        o << s.text;
        return (bool)o;
    }
    return false;
}


Check inspect(const std::string& root) {
    Check c;
    if (root.empty()) return c;
    std::error_code ec;
    const fs::path dir(root);
    if (!fs::exists(dir, ec)) { c.state = State::MissingFolder; return c; }
    if (!fs::is_directory(dir, ec)) { c.state = State::Damaged; return c; }
    const fs::path marker = dir / kMarkerFile;
    if (!fs::exists(marker, ec)) {
        c.state = isEmptyDir(dir) ? State::Empty : State::NotBottega;
        return c;
    }
    c.version = readMarker(marker);
    if (c.version < 0) { c.version = 0; c.state = State::Damaged; return c; }
    if (c.version > kFormatVersion) { c.state = State::NewerVersion; return c; }
    c.state = State::Ready;
    for (const std::string& s : subfolders())
        if (!fs::is_directory(dir / s, ec)) c.missing.push_back(s);
    return c;
}

bool make(const std::string& root, std::string& error) {
    error.clear();
    const Check c = inspect(root);
    if (c.state == State::NoPath) { error = "No folder chosen."; return false; }
    if (c.state == State::NewerVersion) { error = "This Bottega was made by a newer version; not touching it."; return false; }
    if (c.state == State::Damaged) { error = "The folder's marker file can't be read (or the path is not a folder)."; return false; }

    std::error_code ec;
    const fs::path dir(root);
    fs::create_directories(dir, ec);
    if (ec) { error = "Can't create the folder: " + ec.message(); return false; }
    for (const std::string& s : subfolders()) {
        fs::create_directories(dir / s, ec);
        if (ec) { error = "Can't create " + s + ": " + ec.message(); return false; }
    }
    const fs::path marker = dir / kMarkerFile;
    if (!fs::exists(marker, ec)) writeStarterMaterials(dir); // a new Bottega only
    if (!fs::exists(marker, ec)) {
        std::ofstream f(marker);
        if (!f) { error = "Can't write " + std::string(kMarkerFile) + "."; return false; }
        f << "bottega " << kFormatVersion << "\n"
          << "# This folder is a Bottega, shared by Mazzuola, Scalpellino and other tools.\n"
          << "# Please keep this file: it tells the tools what layout the folder uses.\n";
        if (!f) { error = "Can't write " + std::string(kMarkerFile) + "."; return false; }
    }
    return true;
}

std::string describe(const Check& c) {
    switch (c.state) {
        case State::NoPath: return "No Bottega folder chosen yet.";
        case State::MissingFolder: return "This folder doesn't exist yet. It can be created as a new Bottega.";
        case State::Empty: return "An empty folder. It can be made into a Bottega (adds the sub-folders).";
        case State::NotBottega:
            return "A folder with files but no Bottega marker. It can still be used: this adds the marker and the missing "
                   "sub-folders and keeps your files.";
        case State::Ready:
            if (c.missing.empty()) return "A Bottega (format " + std::to_string(c.version) + "). All sub-folders are there.";
            return "A Bottega (format " + std::to_string(c.version) + "), but " + std::to_string(c.missing.size()) +
                   " sub-folder(s) are missing. They can be added.";
        case State::NewerVersion:
            return "Made by a newer version (format " + std::to_string(c.version) + "). This app can't use it.";
        case State::Damaged: return "Not usable: the marker file can't be read, or the path isn't a folder.";
    }
    return "";
}

std::string keyFor(const std::string& root, const std::string& file) {
    if (root.empty() || file.empty()) return "";
    const fs::path rel = fs::path(file).lexically_relative(fs::path(root));
    if (rel.empty() || *rel.begin() == "..") return "";
    fs::path noExt = rel;
    noExt.replace_extension();
    std::string key = noExt.generic_string();
    for (char& ch : key)
        if (std::isspace((unsigned char)ch)) ch = '_';
    return key;
}

} // namespace scal::bottega
