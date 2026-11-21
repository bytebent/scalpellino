#include "render/texture_library.hpp"
#include "core/bake.hpp"
#include "core/bottega.hpp"
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

void TextureLibrary::setBottegaRoot(const std::string& root) {
    if (root == bottega_) return;
    bottega_ = root;
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

void TextureLibrary::rescan() {
    // Keep already-loaded GPU textures whose file is still found.
    std::map<std::string, Entry> old;
    old.swap(entries_);
    index_.clear();
    if (!bottega_.empty()) index_.scanFolder(bottega::path(bottega_, "textures"), bottega_, true);
    for (const std::string& d : userFolders_) index_.scanFolder(d, "", true); // user folders override built-ins
    index_.scanFolder(builtin_, "", false);
    index_.finish();
    for (const std::string& n : index_.names(true)) {
        const MaterialIndex::Entry* m = index_.find(n);
        if (!m || m->picture.empty()) continue;
        Entry e;
        e.path = m->picture;
        e.folder = m->folder;
        auto it = old.find(n);
        if (it != old.end() && it->second.path == e.path) { e = it->second; it->second.loaded = false; }
        entries_[n] = e;
    }
    for (auto& [name, e] : old)
        if (e.loaded) UnloadTexture(e.tex);
    names_ = index_.names(showHelpers_);
}

void TextureLibrary::setShowHelpers(bool on) {
    if (on == showHelpers_) return;
    showHelpers_ = on;
    names_ = index_.names(showHelpers_);
}

bool TextureLibrary::isVariant(const std::string& name) const {
    const MaterialIndex::Entry* e = index_.find(name);
    return e && e->kind == MaterialIndex::Kind::Variant;
}

bool TextureLibrary::isBuiltin(const std::string& name) const {
    const MaterialIndex::Entry* e = index_.find(name);
    return e && !e->writable;
}

TextureLibrary::Entry* TextureLibrary::picture(const std::string& name) {
    const MaterialIndex::Entry* m = index_.find(name);
    const std::string& pic = m ? m->base : name;
    auto it = entries_.find(pic);
    return it == entries_.end() ? nullptr : &it->second;
}

std::string TextureLibrary::folderOf(const std::string& name) const {
    const MaterialIndex::Entry* e = index_.find(name);
    return e ? e->folder : std::string();
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
    Entry* ep = picture(name);
    if (!ep) return missing();
    Entry& e = *ep;
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

Vec3 TextureLibrary::reflectivity(const std::string& name) {
    Entry* ep = picture(name);
    if (!ep) return {-1, 0, 0};
    Entry& e = *ep;
    if (!e.reflDone) {
        e.reflDone = true;
        Image img = LoadImage(e.path.c_str());
        if (img.data) {
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
            const unsigned char* p = (const unsigned char*)img.data;
            const size_t n = (size_t)img.width * img.height;
            double lut[256];
            for (int i = 0; i < 256; ++i) lut[i] = srgbToLinear(i / 255.0);
            double acc[3] = {0, 0, 0};
            for (size_t i = 0; i < n; ++i)
                for (int c = 0; c < 3; ++c) acc[c] += lut[p[3 * i + c]];
            if (n) e.refl = {acc[0] / n, acc[1] / n, acc[2] / n};
            UnloadImage(img);
        }
    }
    return e.refl;
}

bool TextureLibrary::alpha(const std::string& name, int& w, int& h, std::vector<unsigned char>& out) {
    Entry* e = picture(name);
    if (!e) return false;
    Image img = LoadImage(e->path.c_str());
    if (!img.data) return false;
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    w = img.width;
    h = img.height;
    const unsigned char* p = (const unsigned char*)img.data;
    out.resize((size_t)w * h);
    for (size_t i = 0; i < out.size(); ++i) out[i] = p[4 * i + 3];
    UnloadImage(img);
    return true;
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
