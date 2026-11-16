#include "render/texture_library.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace scal {

namespace fs = std::filesystem;

TextureLibrary::~TextureLibrary() { unloadAll(); }

void TextureLibrary::setBuiltinFolder(const std::string& dir) {
    builtin_ = dir;
    rescan();
}

void TextureLibrary::addUserFolder(const std::string& dir) {
    if (dir.empty()) return;
    for (const std::string& d : userFolders_)
        if (d == dir) return;
    userFolders_.push_back(dir);
    rescan();
}

void TextureLibrary::removeUserFolder(size_t i) {
    if (i >= userFolders_.size()) return;
    userFolders_.erase(userFolders_.begin() + (long)i);
    rescan();
}

void TextureLibrary::scanFolder(const std::string& dir) {
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
        e.folder = dir;
        entries_[name] = e;
    }
}

void TextureLibrary::rescan() {
    // Keep already-loaded GPU textures whose file is still found.
    std::map<std::string, Entry> old;
    old.swap(entries_);
    for (const std::string& d : userFolders_) scanFolder(d); // user folders override built-ins
    scanFolder(builtin_);
    for (auto& [name, e] : entries_) {
        auto it = old.find(name);
        if (it != old.end() && it->second.path == e.path) { e = it->second; it->second.loaded = false; }
    }
    for (auto& [name, e] : old)
        if (e.loaded) UnloadTexture(e.tex);
    names_.clear();
    for (const auto& [name, e] : entries_) names_.push_back(name);
}

std::string TextureLibrary::folderOf(const std::string& name) const {
    auto it = entries_.find(name);
    return it == entries_.end() ? std::string() : it->second.folder;
}

void TextureLibrary::applyFilter(Texture2D& t) const {
    SetTextureFilter(t, smooth_ ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_POINT);
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
}

const Texture2D& TextureLibrary::missing() {
    if (!missing_.id) {
        Image img = GenImageChecked(64, 64, 16, 16, {255, 0, 255, 255}, {20, 20, 20, 255});
        missing_ = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(missing_, TEXTURE_FILTER_POINT);
        SetTextureWrap(missing_, TEXTURE_WRAP_REPEAT);
    }
    return missing_;
}

const Texture2D& TextureLibrary::get(const std::string& name) {
    auto it = entries_.find(name);
    if (it == entries_.end()) return missing();
    Entry& e = it->second;
    if (!e.loaded && !e.failed) {
        Image img = LoadImage(e.path.c_str());
        if (!img.data) {
            e.failed = true;
            return missing();
        }
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        e.tex = LoadTextureFromImage(img);
        UnloadImage(img);
        if (!e.tex.id) { e.failed = true; return missing(); }
        GenTextureMipmaps(&e.tex); // smooth, shimmer-free textures in the distance
        applyFilter(e.tex);
        e.loaded = true;
    }
    return e.failed ? missing() : e.tex;
}

TexSize TextureLibrary::size(const std::string& name) {
    const Texture2D& t = get(name);
    return {std::max(1, t.width), std::max(1, t.height)};
}

void TextureLibrary::setSmooth(bool smooth) {
    smooth_ = smooth;
    for (auto& [name, e] : entries_)
        if (e.loaded) applyFilter(e.tex);
}

void TextureLibrary::unloadAll() {
    for (auto& [name, e] : entries_)
        if (e.loaded) { UnloadTexture(e.tex); e.loaded = false; }
    if (missing_.id) { UnloadTexture(missing_); missing_ = {}; }
}

} // namespace scal
