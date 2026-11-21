#include "core/material.hpp"
#include "core/bottega.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace scal {

namespace fs = std::filesystem;

std::string lowerCase(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// ---------------------------------------------------------------- KeyValues
namespace {
struct Lexer {
    const std::string& s;
    size_t i = 0;
    // Next token: a quoted or bare string, "{" or "}". False at the end.
    bool next(std::string& tok, bool& quoted) {
        for (;;) {
            while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
            if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
                while (i < s.size() && s[i] != '\n') ++i;
                continue;
            }
            break;
        }
        if (i >= s.size()) return false;
        quoted = false;
        tok.clear();
        if (s[i] == '{' || s[i] == '}') {
            tok = s[i++];
            return true;
        }
        if (s[i] == '"') {
            quoted = true;
            ++i;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '"') ++i;
                tok += s[i++];
            }
            if (i < s.size()) ++i;
            return true;
        }
        // bare: up to white space, a quote or a brace; "[1 1 1]" written without quotes
        // is read up to its ']'
        if (s[i] == '[') {
            while (i < s.size() && s[i] != ']' && s[i] != '\n') tok += s[i++];
            if (i < s.size() && s[i] == ']') tok += s[i++];
            return true;
        }
        while (i < s.size() && !std::isspace((unsigned char)s[i]) && s[i] != '"' && s[i] != '{' && s[i] != '}') tok += s[i++];
        return true;
    }
};

bool parseBlockBody(Lexer& lx, VmtBlock& b, std::string* error, int depth) {
    if (depth > 32) {
        if (error) *error = "blocks nested too deep";
        return false;
    }
    std::string tok, val;
    bool q = false, q2 = false;
    while (lx.next(tok, q)) {
        if (!q && tok == "}") return true;
        if (!q && tok == "{") {
            if (error) *error = "a '{' without a name";
            return false;
        }
        const size_t save = lx.i;
        if (!lx.next(val, q2)) {
            if (error) *error = "'" + tok + "' has no value";
            return false;
        }
        if (!q2 && val == "{") {
            VmtBlock child;
            child.name = tok;
            if (!parseBlockBody(lx, child, error, depth + 1)) return false;
            b.blocks.push_back(std::move(child));
            continue;
        }
        if (!q2 && val == "}") {
            lx.i = save;
            if (error) *error = "'" + tok + "' has no value";
            return false;
        }
        // Source's conditional suffix ("[$X360]") after a value: skipped
        {
            const size_t keep = lx.i;
            std::string c;
            bool qc = false;
            if (lx.next(c, qc) && !qc && c.size() > 2 && c[0] == '[' && c[1] == '$') {
            } else
                lx.i = keep;
        }
        b.keys.push_back({lowerCase(tok), val});
    }
    if (error) *error = "a '}' is missing";
    return false;
}

std::string quote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"') o += '\\';
        o += c;
    }
    return o + "\"";
}

void writeBlock(const VmtBlock& b, int indent, std::string& out) {
    const std::string pad(indent, '\t');
    out += pad + (indent == 0 ? b.name : quote(b.name)) + "\n" + pad + "{\n";
    for (const VmtKey& k : b.keys) out += pad + "\t" + quote(k.key) + " " + quote(k.value) + "\n";
    for (const VmtBlock& c : b.blocks) writeBlock(c, indent + 1, out);
    out += pad + "}\n";
}

std::optional<std::string> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

bool parseVmt(const std::string& text, VmtBlock& root, std::string* error) {
    Lexer lx{text};
    std::string tok;
    bool q = false;
    if (!lx.next(tok, q)) {
        if (error) *error = "empty file";
        return false;
    }
    if (!q && (tok == "{" || tok == "}")) {
        if (error) *error = "no shader name";
        return false;
    }
    root = VmtBlock{};
    root.name = tok;
    std::string br;
    if (!lx.next(br, q) || q || br != "{") {
        if (error) *error = "'{' expected after the shader name";
        return false;
    }
    return parseBlockBody(lx, root, error, 0);
}

std::string writeVmt(const VmtBlock& root) {
    std::string out;
    writeBlock(root, 0, out);
    return out;
}

// ---------------------------------------------------------------- MaterialFile
bool MaterialFile::parse(const std::string& text, MaterialFile& out, std::string* error) {
    VmtBlock root;
    if (!parseVmt(text, root, error)) return false;
    out = MaterialFile{};
    out.shader = root.name;
    if (out.isPatch()) {
        for (const VmtKey& k : root.keys)
            if (k.key == "include") out.include = k.value;
        for (const VmtBlock& b : root.blocks) {
            const std::string n = lowerCase(b.name);
            if (n == "insert") out.params.insert(out.params.end(), b.keys.begin(), b.keys.end());
            else if (n == "replace") out.replace.insert(out.replace.end(), b.keys.begin(), b.keys.end());
            else out.extra.push_back(b);
        }
    } else {
        out.params = root.keys;
        out.extra = root.blocks;
    }
    return true;
}

std::string MaterialFile::text() const {
    VmtBlock root;
    root.name = shader;
    if (isPatch()) {
        root.keys.push_back({"include", include});
        if (!params.empty()) root.blocks.push_back({"insert", params, {}});
        if (!replace.empty()) root.blocks.push_back({"replace", replace, {}});
    } else
        root.keys = params;
    for (const VmtBlock& b : extra) root.blocks.push_back(b);
    return writeVmt(root);
}

const std::string* MaterialFile::own(const std::string& key) const {
    const std::string k = lowerCase(key);
    for (const VmtKey& p : params)
        if (p.key == k) return &p.value;
    for (const VmtKey& p : replace)
        if (p.key == k) return &p.value;
    return nullptr;
}

std::set<std::string> MaterialFile::ownKeys() const {
    std::set<std::string> s;
    for (const VmtKey& p : params) s.insert(p.key);
    for (const VmtKey& p : replace) s.insert(p.key);
    return s;
}

void MaterialFile::setOwn(const std::string& key, const std::string& value) {
    const std::string k = lowerCase(key);
    replace.erase(std::remove_if(replace.begin(), replace.end(), [&](const VmtKey& p) { return p.key == k; }), replace.end());
    for (VmtKey& p : params)
        if (p.key == k) {
            p.value = value;
            return;
        }
    params.push_back({k, value});
}

void MaterialFile::removeOwn(const std::string& key) {
    const std::string k = lowerCase(key);
    auto drop = [&](std::vector<VmtKey>& v) { v.erase(std::remove_if(v.begin(), v.end(), [&](const VmtKey& p) { return p.key == k; }), v.end()); };
    drop(params);
    drop(replace);
}

// ---------------------------------------------------------------- values
bool parseVmtVector(const std::string& v, Vec3& out) {
    std::string s = v;
    double scale = 1;
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return false;
    if (s[a] == '[' || s[a] == '{') {
        const char close = s[a] == '[' ? ']' : '}';
        if (s[a] == '{') scale = 1.0 / 255.0;
        const size_t b = s.find(close, a + 1);
        s = s.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
    }
    std::istringstream ss(s);
    double x[3];
    int n = 0;
    while (n < 3 && (ss >> x[n])) ++n;
    if (n == 1) {
        out = Vec3{x[0], x[0], x[0]} * scale;
        return true;
    }
    if (n != 3) return false;
    out = Vec3{x[0], x[1], x[2]} * scale;
    return true;
}

std::string formatVmtVector(const Vec3& v) {
    char b[96];
    std::snprintf(b, sizeof b, "[%g %g %g]", v.x, v.y, v.z);
    return b;
}

bool parseTexLight(const std::string& v, Vec3& c, double& brightness) {
    std::istringstream ss(v);
    double x[4];
    int n = 0;
    while (n < 4 && (ss >> x[n])) ++n;
    if (n < 3) return false;
    c = {x[0], x[1], x[2]};
    brightness = n == 4 ? x[3] : 200; // lights.rad without a brightness: Hammer's default 200
    return true;
}

std::string Material::str(const std::string& key, const std::string& def) const {
    auto it = params.find(lowerCase(key));
    return it == params.end() ? def : it->second;
}

double Material::num(const std::string& key, double def) const {
    auto it = params.find(lowerCase(key));
    if (it == params.end() || it->second.empty()) return def;
    const char* s = it->second.c_str();
    while (*s == ' ' || *s == '[' || *s == '{') ++s;
    char* end = nullptr;
    const double d = std::strtod(s, &end);
    return end == s ? def : d;
}

Vec3 Material::vec(const std::string& key, const Vec3& def) const {
    auto it = params.find(lowerCase(key));
    Vec3 v;
    if (it == params.end() || !parseVmtVector(it->second, v)) return def;
    return v;
}

Material resolveMaterial(const MaterialFile& f, const ParentLoader& parent, int depth) {
    Material m;
    if (!f.isPatch()) {
        m.shader = f.shader;
        for (const VmtKey& k : f.params)
            if (!k.value.empty()) m.params[k.key] = k.value;
        m.own = f.ownKeys();
        return m;
    }
    std::optional<std::pair<MaterialFile, std::string>> p;
    if (depth >= 8) m.problem = "parents nested too deep (a loop?)";
    else if (parent) p = parent(f.include);
    if (p) {
        m = resolveMaterial(p->first, parent, depth + 1);
        m.own.clear();
        m.parentFile = p->second;
        if (m.problem.empty() && lowerCase(m.shader) == "patch") m.shader = "LightmappedGeneric";
    } else if (m.problem.empty())
        m.problem = "parent not found: " + f.include;
    if (lowerCase(m.shader) == "patch") m.shader = "LightmappedGeneric";
    for (const VmtKey& k : f.params) {
        if (k.value.empty()) m.params.erase(k.key);
        else m.params[k.key] = k.value;
    }
    for (const VmtKey& k : f.replace) {
        if (!m.params.count(k.key)) continue; // replace only changes what the parent has
        if (k.value.empty()) m.params.erase(k.key);
        else m.params[k.key] = k.value;
    }
    m.own = f.ownKeys();
    return m;
}

const std::vector<HelperSuffix>& helperSuffixes() {
    static const std::vector<HelperSuffix> s = {
        {"_ssbump", "$bumpmap"},     {"_normalmap", "$bumpmap"},       {"_normal", "$bumpmap"},
        {"_bump", "$bumpmap"},       {"_envmapmask", "$envmapmask"},   {"_mask", "$envmapmask"},
        {"_spec", "$envmapmask"},    {"_selfillum", "$selfillummask"}, {"_illum", "$selfillummask"},
    };
    return s;
}

// ---------------------------------------------------------------- MaterialIndex
void MaterialIndex::clear() {
    pictures_.clear();
    vmts_.clear();
    entries_.clear();
    byVmt_.clear();
}

void MaterialIndex::scanFolder(const std::string& dir, const std::string& keyRoot, bool writable) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return;
    std::vector<Found> pics, vmts;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = lowerCase(it->path().extension().string());
        const Found f{it->path().lexically_normal().string(), dir, keyRoot, writable};
        if (ext == ".png" || ext == ".tga" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") pics.push_back(f);
        else if (ext == ".vmt") vmts.push_back(f);
    }
    // the same order on every machine (directory listings aren't sorted)
    auto byPath = [](const Found& a, const Found& b) { return a.path < b.path; };
    std::sort(pics.begin(), pics.end(), byPath);
    std::sort(vmts.begin(), vmts.end(), byPath);
    pictures_.insert(pictures_.end(), pics.begin(), pics.end());
    vmts_.insert(vmts_.end(), vmts.begin(), vmts.end());
}

std::string MaterialIndex::nameOf(const Found& f) const {
    if (!f.keyRoot.empty()) return bottega::keyFor(f.keyRoot, f.path);
    std::string name = fs::path(f.path).stem().string();
    for (char& c : name)
        if (std::isspace((unsigned char)c)) c = '_';
    return name;
}

void MaterialIndex::finish() {
    entries_.clear();
    byVmt_.clear();
    // pictures: the first of a name wins (folders were scanned in search order)
    for (const Found& f : pictures_) {
        const std::string n = nameOf(f);
        if (n.empty() || entries_.count(n)) continue;
        Entry e;
        e.name = n;
        e.kind = Kind::Texture;
        e.picture = f.path;
        e.folder = f.folder;
        e.keyRoot = f.keyRoot;
        e.writable = f.writable;
        e.base = n;
        entries_[n] = std::move(e);
    }
    // helpers: "<name><suffix>" next to "<name>" (same folder)
    for (auto& [n, e] : entries_) {
        const std::string low = lowerCase(n);
        for (const HelperSuffix& hs : helperSuffixes()) {
            const std::string suf = hs.suffix;
            if (low.size() <= suf.size() || low.compare(low.size() - suf.size(), suf.size(), suf) != 0) continue;
            const std::string owner = n.substr(0, n.size() - suf.size());
            auto it = entries_.find(owner);
            if (it == entries_.end() || it->second.kind != Kind::Texture) continue;
            if (fs::path(it->second.picture).parent_path() != fs::path(e.picture).parent_path()) continue;
            e.kind = Kind::Helper;
            e.helperOf = owner;
            break;
        }
    }
    // .vmt files: settings of a texture of the same name, a variant, or only a parent
    for (const Found& f : vmts_) {
        const std::string n = nameOf(f);
        if (n.empty()) continue;
        auto it = entries_.find(n);
        if (it != entries_.end()) {
            if (!it->second.vmt.empty()) continue; // an earlier folder's file wins
            if (it->second.kind == Kind::Variant || it->second.kind == Kind::Parent) continue;
            it->second.vmt = f.path;
            byVmt_[f.path] = n;
            continue;
        }
        Entry e;
        e.name = n;
        e.kind = Kind::Parent; // until its $basetexture is found
        e.vmt = f.path;
        e.folder = f.folder;
        e.keyRoot = f.keyRoot;
        e.writable = f.writable;
        byVmt_[f.path] = n;
        entries_[n] = std::move(e);
    }
    // read every .vmt
    for (auto& [n, e] : entries_) {
        if (e.vmt.empty()) {
            e.file = MaterialFile{};
            e.file.params.push_back({"$basetexture", n});
            continue;
        }
        std::string err;
        const std::optional<std::string> text = readFile(e.vmt);
        if (!text || !MaterialFile::parse(*text, e.file, &err)) {
            e.file = MaterialFile{};
            if (e.kind == Kind::Texture) e.file.params.push_back({"$basetexture", n});
            e.mat.problem = text ? "can't read " + fs::path(e.vmt).filename().string() + ": " + err : "can't open " + e.vmt;
        }
    }
    // resolve, and find what each material's picture is
    for (auto& [n, e] : entries_) {
        const std::string problem = e.mat.problem;
        e.mat = resolveEntry(e);
        if (!problem.empty()) e.mat.problem = problem;
        const std::string bt = e.mat.str("$basetexture");
        std::string base = bt.empty() ? "" : resolveTexture(e, bt);
        if (e.kind == Kind::Texture || e.kind == Kind::Helper) {
            e.base = !base.empty() && base != n ? base : n;
        } else if (!base.empty()) {
            e.kind = Kind::Variant;
            e.base = base;
        }
    }
    // helper maps fill their key when the material doesn't set it (variants too, by their base)
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> hooks; // texture -> (key, helper)
    for (const auto& [n, e] : entries_) {
        if (e.kind != Kind::Helper) continue;
        const std::string low = lowerCase(n);
        for (const HelperSuffix& hs : helperSuffixes()) {
            const std::string suf = hs.suffix;
            if (low.size() > suf.size() && low.compare(low.size() - suf.size(), suf.size(), suf) == 0) {
                hooks[e.helperOf].push_back({hs.key, n});
                if (suf == "_ssbump") hooks[e.helperOf].push_back({"$ssbump", "1"});
                break;
            }
        }
    }
    for (auto& [n, e] : entries_) {
        if (e.kind != Kind::Texture && e.kind != Kind::Variant) continue;
        auto it = hooks.find(e.base);
        if (it == hooks.end()) continue;
        for (const auto& [k, v] : it->second)
            if (!e.mat.params.count(k)) e.mat.params[k] = v;
    }
}

Material MaterialIndex::resolveEntry(const Entry& e) const {
    ParentLoader loader = [&](const std::string& inc) -> std::optional<std::pair<MaterialFile, std::string>> {
        const std::string path = resolveInclude(e, inc);
        if (path.empty()) return std::nullopt;
        auto it = byVmt_.find(path);
        if (it != byVmt_.end()) {
            auto en = entries_.find(it->second);
            if (en != entries_.end()) return std::make_pair(en->second.file, path);
        }
        const std::optional<std::string> text = readFile(path);
        MaterialFile f;
        if (!text || !MaterialFile::parse(*text, f)) return std::nullopt;
        return std::make_pair(f, path);
    };
    // A parent's chain is resolved from its own folder: entries are looked up by include
    // name relative to `e`, which is fine for chains inside one library folder.
    return resolveMaterial(e.file, loader);
}

const MaterialIndex::Entry* MaterialIndex::find(const std::string& name) const {
    auto it = entries_.find(name);
    return it == entries_.end() ? nullptr : &it->second;
}

std::vector<std::string> MaterialIndex::names(bool helpers) const {
    std::vector<std::string> out;
    for (const auto& [n, e] : entries_)
        if (e.kind == Kind::Texture || e.kind == Kind::Variant || (helpers && e.kind == Kind::Helper)) out.push_back(n);
    return out;
}

std::vector<const MaterialIndex::Entry*> MaterialIndex::vmtEntries() const {
    std::vector<const Entry*> out;
    for (const auto& [n, e] : entries_)
        if (!e.vmt.empty()) out.push_back(&e);
    return out;
}

const MaterialIndex::Entry* MaterialIndex::byVmt(const std::string& path) const {
    auto it = byVmt_.find(path);
    return it == byVmt_.end() ? nullptr : find(it->second);
}

std::string MaterialIndex::vmtPathFor(const std::string& name) const {
    const Entry* e = find(name);
    if (!e || !e->writable) return "";
    if (!e->vmt.empty()) return e->vmt;
    if (e->picture.empty()) return "";
    fs::path p(e->picture);
    p.replace_extension(".vmt");
    return p.string();
}

std::string MaterialIndex::includeName(const Entry& from, const std::string& parentVmt) const {
    const std::string root = from.keyRoot.empty() ? from.folder : from.keyRoot;
    const fs::path rel = fs::path(parentVmt).lexically_relative(fs::path(root));
    if (!rel.empty() && *rel.begin() != "..") return rel.generic_string();
    return fs::path(parentVmt).generic_string();
}

std::string MaterialIndex::resolveInclude(const Entry& from, const std::string& include) const {
    if (include.empty()) return "";
    std::string inc = include;
    std::replace(inc.begin(), inc.end(), '\\', '/');
    std::error_code ec;
    std::vector<fs::path> tries;
    if (fs::path(inc).is_absolute()) tries.push_back(inc);
    const std::string root = from.keyRoot.empty() ? from.folder : from.keyRoot;
    std::string noMaterials = inc;
    if (lowerCase(noMaterials).rfind("materials/", 0) == 0) noMaterials = noMaterials.substr(10);
    tries.push_back(fs::path(root) / inc);
    tries.push_back(fs::path(root) / noMaterials);
    if (!from.keyRoot.empty()) tries.push_back(fs::path(root) / "textures" / noMaterials);
    const fs::path own = !from.vmt.empty() ? fs::path(from.vmt).parent_path() : fs::path(from.picture).parent_path();
    tries.push_back(own / fs::path(inc).filename());
    for (fs::path p : tries) {
        if (p.extension().empty()) p += ".vmt";
        if (fs::is_regular_file(p, ec)) {
            return p.lexically_normal().string();
        }
    }
    return "";
}

std::string MaterialIndex::resolveTexture(const Entry& from, const std::string& value) const {
    std::string v = value;
    std::replace(v.begin(), v.end(), '\\', '/');
    const size_t dot = v.find_last_of('.');
    if (dot != std::string::npos && v.find('/', dot) == std::string::npos) {
        const std::string ext = lowerCase(v.substr(dot));
        if (ext == ".png" || ext == ".tga" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".vtf") v = v.substr(0, dot);
    }
    for (char& c : v)
        if (std::isspace((unsigned char)c)) c = '_';
    auto isTex = [&](const std::string& n) {
        auto it = entries_.find(n);
        return it != entries_.end() && (it->second.kind == Kind::Texture || it->second.kind == Kind::Helper);
    };
    if (isTex(v)) return v;
    std::string noMaterials = v;
    if (lowerCase(noMaterials).rfind("materials/", 0) == 0) noMaterials = noMaterials.substr(10);
    if (!from.keyRoot.empty() && isTex("textures/" + noMaterials)) return "textures/" + noMaterials;
    // a picture beside the .vmt
    const std::string leaf = fs::path(noMaterials).filename().string();
    const fs::path own = !from.vmt.empty() ? fs::path(from.vmt).parent_path() : fs::path(from.picture).parent_path();
    for (const auto& [n, e] : entries_)
        if ((e.kind == Kind::Texture || e.kind == Kind::Helper) && fs::path(e.picture).parent_path() == own) {
            std::string stem = fs::path(e.picture).stem().string();
            for (char& c : stem)
                if (std::isspace((unsigned char)c)) c = '_';
            if (lowerCase(stem) == lowerCase(leaf)) return n;
        }
    return "";
}

MaterialFile MaterialIndex::fileFor(const std::string& name) const {
    const Entry* e = find(name);
    if (!e) return MaterialFile{};
    return e->file;
}

} // namespace scal
