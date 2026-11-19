#include "io/model_store.hpp"
#include "core/bottega.hpp"
#include "io/glb_loader.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace scal {

namespace fs = std::filesystem;

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
bool isModelFile(const fs::path& p) {
    const std::string e = lower(p.extension().string());
    return e == ".glb" || e == ".gltf";
}
std::string cleanName(std::string s) {
    for (char& c : s)
        if (std::isspace((unsigned char)c)) c = '_';
    return s;
}

void scanFolder(const std::string& root, const fs::path& dir, bool isRoot, std::map<std::string, std::string>& out, int depth,
                std::vector<std::string>* clashes) {
    auto add = [&](const std::string& k, const fs::path& f) {
        if (k.empty()) return;
        auto [it, fresh] = out.emplace(k, f.string());
        if (!fresh && clashes) {
            const fs::path base(root);
            clashes->push_back(f.lexically_relative(base).generic_string() + " is left out: \"" + k + "\" is already " +
                               fs::path(it->second).lexically_relative(base).generic_string());
        }
    };
    std::error_code ec;
    std::vector<fs::path> files, dirs;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        if (e.is_directory(ec)) dirs.push_back(e.path());
        else if (e.is_regular_file(ec) && isModelFile(e.path())) files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    std::sort(dirs.begin(), dirs.end());
    if (!files.empty()) {
        // The folder's own model: its only file, or the one named like the folder. Else every file is a model.
        const std::string folderName = lower(dir.filename().string());
        const fs::path* own = nullptr;
        if (!isRoot)
            for (const fs::path& f : files)
                if (lower(f.stem().string()) == folderName) { own = &f; break; }
        if (!own && !isRoot && files.size() == 1) own = &files[0]; // the folder's only model, whatever the file is called
        if (own) {
            // The folder's name as the key (not through keyFor, which would cut a dot-extension
            // off a folder called "tree.v2").
            add(cleanName(dir.lexically_relative(fs::path(root)).generic_string()), *own);
        } else {
            for (const fs::path& f : files) add(bottega::keyFor(root, f.string()), f);
        }
    }
    if (depth >= 8) return;
    for (const fs::path& d : dirs) scanFolder(root, d, false, out, depth + 1, clashes);
}
} // namespace

std::map<std::string, std::string> scanModelFiles(const std::string& root, std::vector<std::string>* clashes) {
    std::map<std::string, std::string> out;
    if (root.empty()) return out;
    std::error_code ec;
    const fs::path models = fs::path(root) / "models";
    if (!fs::is_directory(models, ec)) return out;
    scanFolder(root, models, true, out, 0, clashes);
    return out;
}

void ModelStore::setBottegaRoot(const std::string& root) {
    root_ = root;
    rescan();
}

void ModelStore::rescan() {
    clashes_.clear();
    files_ = scanModelFiles(root_, &clashes_);
    names_.clear();
    for (const auto& [n, f] : files_) names_.push_back(n);
    loaded_.clear();
    problems_.clear();
    ++generation_;
}

std::string ModelStore::fileOf(const std::string& name) const {
    auto it = files_.find(name);
    return it == files_.end() ? "" : it->second;
}

const Model* ModelStore::find(const std::string& name) {
    if (name.empty()) return nullptr;
    auto it = loaded_.find(name);
    if (it != loaded_.end()) return it->second.get();
    if (problems_.count(name)) return nullptr;
    auto f = files_.find(name);
    if (f == files_.end()) {
        problems_[name] = root_.empty() ? "no Bottega folder is set" : "not found in the Bottega's models folder";
        return nullptr;
    }
    auto m = std::make_unique<Model>();
    std::string err;
    if (!loadGlbFile(f->second, *m, err)) {
        problems_[name] = err;
        return nullptr;
    }
    m->name = name;
    m->file = f->second;
    const Model* p = m.get();
    loaded_[name] = std::move(m);
    return p;
}

std::string ModelStore::problem(const std::string& name) const {
    auto it = problems_.find(name);
    return it == problems_.end() ? "" : it->second;
}

} // namespace scal
