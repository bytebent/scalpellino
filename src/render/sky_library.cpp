#include "render/sky_library.hpp"
#include "core/sky.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace scal {

namespace fs = std::filesystem;

SkyLibrary::~SkyLibrary() { unloadAll(); }

void SkyLibrary::setBuiltinFolder(const std::string& dir) {
    builtin_ = dir;
    rescan();
}

void SkyLibrary::addUserFolder(const std::string& dir) {
    if (dir.empty()) return;
    for (const std::string& d : userFolders_)
        if (d == dir) return;
    userFolders_.push_back(dir);
    rescan();
}

void SkyLibrary::removeUserFolder(size_t i) {
    if (i >= userFolders_.size()) return;
    userFolders_.erase(userFolders_.begin() + (long)i);
    rescan();
}

void SkyLibrary::scanFolder(const std::string& dir) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (ext != ".png" && ext != ".tga" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp") continue;
        std::string name = it->path().stem().string();
        for (char& c : name) if (std::isspace((unsigned char)c)) c = '_';
        if (entries_.count(name)) continue; // first folder wins
        Entry e;
        e.path = it->path().string();
        entries_[name] = e;
    }
}

void SkyLibrary::rescan() {
    unloadAll();
    entries_.clear();
    for (const std::string& d : userFolders_) scanFolder(d);
    scanFolder(builtin_);
    names_.clear();
    for (const auto& [name, e] : entries_) names_.push_back(name);
}

const Texture2D* SkyLibrary::get(const std::string& name) {
    auto it = entries_.find(name);
    if (it == entries_.end()) return nullptr;
    Entry& e = it->second;
    if (e.loaded) return &e.tex;
    if (e.failed) return nullptr;
    Image img = LoadImage(e.path.c_str());
    if (!img.data) { e.failed = true; e.problem = "can't read the file"; return nullptr; }
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
    const SkyLayout layout = skyLayout(img.width, img.height);
    if (layout == SkyLayout::Unknown) {
        UnloadImage(img);
        e.failed = true;
        e.problem = "not a sky: needs a 2:1 panorama or a 4:3 cube cross";
        return nullptr;
    }
    if (layout == SkyLayout::Cross) {
        const int outW = std::min(4096, std::max(256, img.width));
        std::vector<unsigned char> pano = crossToPanorama((const unsigned char*)img.data, img.width, img.height, outW);
        UnloadImage(img);
        img = GenImageColor(outW, outW / 2, BLACK);
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
        std::copy(pano.begin(), pano.end(), (unsigned char*)img.data);
    }
    e.tex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(e.tex, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(e.tex, TEXTURE_WRAP_CLAMP);
    e.loaded = true;
    return &e.tex;
}

std::string SkyLibrary::problem(const std::string& name) const {
    auto it = entries_.find(name);
    return it == entries_.end() ? "not found" : it->second.problem;
}

void SkyLibrary::unloadAll() {
    for (auto& [name, e] : entries_)
        if (e.loaded) { UnloadTexture(e.tex); e.loaded = false; }
}

} // namespace scal
