#include "io/map_format.hpp"
#include "core/terrain.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace scal {

// ------------------------------------------------------------------ writing

namespace {

// Shortest text that reads back to exactly the same double.
std::string num(double v) {
    if (v == 0) return "0"; // also turns -0 into 0
    char buf[40];
    for (int prec : {6, 10, 15, 17}) {
        std::snprintf(buf, sizeof buf, "%.*g", prec, v);
        if (std::strtod(buf, nullptr) == v) break;
    }
    return buf;
}

std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) out += (c == '"') ? '\'' : c; // .map has no escapes
    return out + "\"";
}

void writeBrush(std::ostringstream& o, const Brush& b) {
    o << "{\n";
    // Our extra per-brush data, as a comment line other tools skip.
    if (b.visgroup || b.hidden) {
        o << "//@";
        if (b.visgroup) o << " visgroup=" << b.visgroup;
        if (b.hidden) o << " hidden=1";
        o << '\n';
    }
    if (b.terrain) {
        // Terrain: a header, then one line of heights per grid row ("th"); then for each
        // painted texture (bottom first) a "tl <texture>" line and one line of blend
        // values per row ("ta").
        const Terrain& t = *b.terrain;
        const int n = t.size();
        o << "//@ terrain face=" << t.face << " power=" << t.power << " start=" << num(t.start.x) << ','
          << num(t.start.y) << ',' << num(t.start.z) << '\n';
        auto rows = [&](const char* tag, const std::vector<double>& v) {
            for (int j = 0; j < n && (int)v.size() == n * n; ++j) {
                o << "//@ " << tag;
                for (int i = 0; i < n; ++i) o << ' ' << num(v[(size_t)j * n + i]);
                o << '\n';
            }
        };
        rows("th", t.heights);
        for (const TerrainLayer& l : t.layers) {
            bool painted = false;
            for (double a : l.alpha) if (a != 0) painted = true;
            if (!painted || l.texture.empty()) continue;
            o << "//@ tl " << l.texture << '\n';
            rows("ta", l.alpha);
        }
    }
    for (const Face& f : b.faces) {
        for (const Vec3& p : f.p) o << "( " << num(p.x) << ' ' << num(p.y) << ' ' << num(p.z) << " ) ";
        o << f.texture
          << " [ " << num(f.u.axis.x) << ' ' << num(f.u.axis.y) << ' ' << num(f.u.axis.z) << ' ' << num(f.u.offset) << " ]"
          << " [ " << num(f.v.axis.x) << ' ' << num(f.v.axis.y) << ' ' << num(f.v.axis.z) << ' ' << num(f.v.offset) << " ] "
          << num(f.rotation) << ' ' << num(f.scaleU) << ' ' << num(f.scaleV) << '\n';
    }
    o << "}\n";
}

void writeKeys(std::ostringstream& o, const KeyValues& kv) {
    for (const auto& [k, v] : kv) o << quoted(k) << ' ' << quoted(v) << '\n';
}

} // namespace

std::string writeMap(const Document& doc) {
    std::ostringstream o;
    o << "// Game: Generic\n// Format: Valve\n// Written by Scalpellino\n";
    o << "// entity 0\n{\n";
    KeyValues world;
    for (const auto& kv : doc.worldKeys)
        if (kv.first != "_scal_visgroups") world.push_back(kv);
    if (!doc.visgroups().empty()) {
        // id|name|visible;...  ('|' and ';' can't appear in names)
        std::string vg;
        for (const Visgroup& v : doc.visgroups()) {
            std::string name = v.name;
            for (char& c : name) if (c == '|' || c == ';' || c == '"') c = '_';
            vg += std::to_string(v.id) + "|" + name + "|" + (v.visible ? "1" : "0") + ";";
        }
        world.emplace_back("_scal_visgroups", vg);
    }
    writeKeys(o, world);
    int i = 0;
    for (const Brush& b : doc.brushes()) {
        if (b.group) continue; // written with its group below
        o << "// brush " << i++ << '\n';
        writeBrush(o, b);
    }
    o << "}\n";
    int e = 1;
    // Generated groups: func_group entities (merged into the world by Quake-family tools)
    // carrying the generator's parameters as "_scal_*" keys that other tools ignore.
    for (const auto& [gid, g] : doc.groups()) {
        o << "// entity " << e++ << "\n{\n";
        KeyValues kv{{"classname", "func_group"}, {"_scal_group", std::to_string(gid)}};
        for (const auto& [k, v] : g.keys) kv.emplace_back("_scal_" + k, v);
        writeKeys(o, kv);
        int j = 0;
        for (const Brush& b : doc.brushes()) {
            if (b.group != gid) continue;
            o << "// brush " << j++ << '\n';
            writeBrush(o, b);
        }
        o << "}\n";
    }
    for (const RawEntity& ent : doc.otherEntities) {
        o << "// entity " << e++ << "\n{\n";
        writeKeys(o, ent.keys);
        int j = 0;
        for (const Brush& b : ent.brushes) {
            o << "// brush " << j++ << '\n';
            writeBrush(o, b);
        }
        o << "}\n";
    }
    return o.str();
}

// ------------------------------------------------------------------ reading

namespace {

struct Token {
    enum Kind { End, Punct, Str, Word, Meta } kind = End; // Meta = "//@ ..." line
    std::string text;
    int line = 0;
};

class Lexer {
public:
    explicit Lexer(const std::string& s) : s_(s) {}

    Token next() {
        skip();
        Token t;
        t.line = line_;
        if (i_ >= s_.size()) return t;
        const char c = s_[i_];
        if (c == '/' && s_.compare(i_, 3, "//@") == 0) {
            t.kind = Token::Meta;
            i_ += 3;
            while (i_ < s_.size() && s_[i_] != '\n') t.text += s_[i_++];
            return t;
        }
        if (c == '{' || c == '}' || c == '(' || c == ')' || c == '[' || c == ']') {
            t.kind = Token::Punct;
            t.text = c;
            ++i_;
        } else if (c == '"') {
            t.kind = Token::Str;
            ++i_;
            while (i_ < s_.size() && s_[i_] != '"') {
                if (s_[i_] == '\n') ++line_;
                t.text += s_[i_++];
            }
            ++i_; // closing quote
        } else {
            t.kind = Token::Word;
            while (i_ < s_.size() && !std::isspace((unsigned char)s_[i_]) &&
                   std::string("{}()[]\"").find(s_[i_]) == std::string::npos)
                t.text += s_[i_++];
        }
        return t;
    }

    Token peek() {
        const size_t i = i_;
        const int l = line_;
        Token t = next();
        i_ = i;
        line_ = l;
        return t;
    }

private:
    void skip() {
        while (i_ < s_.size()) {
            const char c = s_[i_];
            if (c == '\n') { ++line_; ++i_; }
            else if (std::isspace((unsigned char)c)) ++i_;
            else if (c == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '/' && s_.compare(i_, 3, "//@") != 0) {
                while (i_ < s_.size() && s_[i_] != '\n') ++i_;
            } else break;
        }
    }

    const std::string& s_;
    size_t i_ = 0;
    int line_ = 1;
};

struct ParseError {
    std::string msg;
};

class Parser {
public:
    explicit Parser(const std::string& s) : lex_(s) {}

    void expect(const char* p) {
        Token t = lex_.next();
        if (t.kind != Token::Punct || t.text != p) fail(t, std::string("expected '") + p + "'");
    }

    double number() {
        Token t = lex_.next();
        if (t.kind != Token::Word) fail(t, "expected a number");
        char* end = nullptr;
        const double v = std::strtod(t.text.c_str(), &end);
        if (end == t.text.c_str() || *end != '\0') fail(t, "bad number '" + t.text + "'");
        return v;
    }

    Vec3 point() {
        expect("(");
        Vec3 v;
        v.x = number(); v.y = number(); v.z = number();
        expect(")");
        return v;
    }

    Face face() {
        Face f;
        for (Vec3& p : f.p) p = point();
        Token tex = lex_.next();
        if (tex.kind != Token::Word && tex.kind != Token::Str) fail(tex, "expected texture name");
        f.texture = tex.text;
        Token nx = lex_.peek();
        if (nx.kind == Token::Punct && nx.text == "[") {
            // Valve 220
            expect("[");
            f.u.axis.x = number(); f.u.axis.y = number(); f.u.axis.z = number(); f.u.offset = number();
            expect("]");
            expect("[");
            f.v.axis.x = number(); f.v.axis.y = number(); f.v.axis.z = number(); f.v.offset = number();
            expect("]");
            f.rotation = number();
            f.scaleU = number();
            f.scaleV = number();
        } else {
            // Classic Quake: xoff yoff rot xscale yscale. Convert to world-aligned axes.
            const double xo = number(), yo = number(), rot = number(), sx = number(), sy = number();
            defaultTexAxes(f.plane().n, f.u.axis, f.v.axis);
            f.u.offset = xo; f.v.offset = yo; f.rotation = rot;
            f.scaleU = sx != 0 ? sx : 1; f.scaleV = sy != 0 ? sy : 1;
        }
        // Some formats append extra numbers (e.g. Quake 2 surface flags); skip them.
        while (lex_.peek().kind == Token::Word) lex_.next();
        if (f.scaleU == 0) f.scaleU = 1;
        if (f.scaleV == 0) f.scaleV = 1;
        return f;
    }

    Brush brush() {
        Brush b;
        // '{' already consumed
        while (true) {
            Token t = lex_.peek();
            if (t.kind == Token::Punct && t.text == "}") { lex_.next(); break; }
            if (t.kind == Token::Punct && t.text == "(") { b.faces.push_back(face()); continue; }
            if (t.kind == Token::Meta) { meta(lex_.next().text, b); continue; }
            fail(t, "unexpected '" + t.text + "' in brush (patches are not supported)");
        }
        if (b.terrain) {
            Terrain& tr = *b.terrain;
            const size_t nn = (size_t)tr.size() * tr.size();
            if (tr.power < kMinTerrainPower || tr.power > kMaxTerrainPower || tr.heights.size() != nn)
                fail(lex_.peek(), "terrain data is incomplete");
            // Files from v0.7.0 had "tex2=" and blend rows with no "tl" line; a blend
            // without a texture name can't be shown, so it is dropped.
            tr.layers.erase(std::remove_if(tr.layers.begin(), tr.layers.end(),
                                           [](const TerrainLayer& l) { return l.texture.empty(); }),
                            tr.layers.end());
            for (const TerrainLayer& l : tr.layers)
                if (l.alpha.size() != nn) fail(lex_.peek(), "terrain blend data is incomplete");
            if ((int)tr.layers.size() > kMaxTerrainLayers) tr.layers.resize(kMaxTerrainLayers);
        }
        return b;
    }

    // "//@ visgroup=3 hidden=1" -> brush fields. "//@ terrain ...", "//@ th ...",
    // "//@ tl <texture>", "//@ ta ..." -> terrain (header, height row, painted texture,
    // its blend row).
    static void meta(const std::string& text, Brush& b) {
        std::istringstream in(text);
        std::string first;
        in >> first;
        if (first == "tl") {
            if (!b.terrain) return;
            std::string name;
            in >> name;
            b.terrain->layers.push_back({name, {}});
            return;
        }
        if (first == "th" || first == "ta") {
            if (!b.terrain) return;
            if (first == "ta" && b.terrain->layers.empty()) b.terrain->layers.push_back({}); // old file, no tex2
            std::vector<double>& v = first == "th" ? b.terrain->heights : b.terrain->layers.back().alpha;
            std::string w;
            while (in >> w) v.push_back(std::strtod(w.c_str(), nullptr));
            return;
        }
        if (first == "terrain") {
            Terrain t;
            std::string kv;
            while (in >> kv) {
                const size_t eq = kv.find('=');
                if (eq == std::string::npos) continue;
                const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
                if (k == "face") t.face = std::atoi(v.c_str());
                else if (k == "power") t.power = std::atoi(v.c_str());
                else if (k == "tex2") t.layers.push_back({v, {}}); // v0.7.0 files
                else if (k == "start") {
                    double x = 0, y = 0, z = 0;
                    if (std::sscanf(v.c_str(), "%lf,%lf,%lf", &x, &y, &z) == 3) t.start = {x, y, z};
                }
            }
            b.terrain = std::move(t);
            return;
        }
        in.clear();
        in.str(text);
        std::string kv;
        while (in >> kv) {
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
            if (k == "visgroup") b.visgroup = (std::uint32_t)std::strtoul(v.c_str(), nullptr, 10);
            else if (k == "hidden") b.hidden = v == "1";
        }
    }

    // Returns false at end of input.
    bool entity(KeyValues& keys, std::vector<Brush>& brushes) {
        Token t = lex_.next();
        while (t.kind == Token::Meta) t = lex_.next();
        if (t.kind == Token::End) return false;
        if (t.kind != Token::Punct || t.text != "{") fail(t, "expected '{' to start an entity");
        while (true) {
            Token k = lex_.next();
            if (k.kind == Token::Meta) continue; // entity-level metadata: none yet
            if (k.kind == Token::Punct && k.text == "}") break;
            if (k.kind == Token::Str) {
                Token v = lex_.next();
                if (v.kind != Token::Str) fail(v, "expected a quoted value");
                keys.emplace_back(k.text, v.text);
            } else if (k.kind == Token::Punct && k.text == "{") {
                brushes.push_back(brush());
            } else {
                fail(k, k.kind == Token::End ? "unexpected end of file" : "unexpected '" + k.text + "'");
            }
        }
        return true;
    }

    [[noreturn]] void fail(const Token& t, const std::string& msg) {
        throw ParseError{"line " + std::to_string(t.line) + ": " + msg};
    }

private:
    Lexer lex_;
};

std::string classnameOf(const KeyValues& kv) {
    for (const auto& [k, v] : kv)
        if (k == "classname") return v;
    return {};
}

} // namespace

MapReadResult readMap(const std::string& text, Document& doc) {
    MapReadResult r;
    KeyValues world;
    std::vector<Brush> worldBrushes;
    std::vector<RawEntity> others;
    std::map<GroupId, Group> groups;
    GroupId nextGroup = 1;
    bool haveWorld = false;
    try {
        Parser p(text);
        while (true) {
            KeyValues keys;
            std::vector<Brush> brushes;
            if (!p.entity(keys, brushes)) break;
            if (!haveWorld && classnameOf(keys) == "worldspawn") {
                haveWorld = true;
                world = std::move(keys);
                for (Brush& b : brushes) {
                    if (b.valid()) worldBrushes.push_back(std::move(b));
                    else ++r.skippedBrushes;
                }
            } else if (classnameOf(keys) == "func_group" && [&] {
                           for (const auto& [k, v] : keys) if (k.rfind("_scal_", 0) == 0 && k != "_scal_group") return true;
                           return false;
                       }()) {
                // One of our generated groups: restore its parameters, brushes join the world.
                Group g;
                g.id = nextGroup++;
                for (const auto& [k, v] : keys)
                    if (k.rfind("_scal_", 0) == 0 && k != "_scal_group") g.keys.emplace_back(k.substr(6), v);
                for (Brush& b : brushes) {
                    if (!b.valid()) { ++r.skippedBrushes; continue; }
                    b.group = g.id;
                    worldBrushes.push_back(std::move(b));
                }
                groups[g.id] = std::move(g);
            } else {
                others.push_back({std::move(keys), std::move(brushes)});
            }
        }
    } catch (const ParseError& e) {
        r.error = e.msg;
        return r;
    }
    if (!haveWorld) world = {{"classname", "worldspawn"}, {"mapversion", "220"}};
    std::vector<Visgroup> visgroups;
    for (auto it = world.begin(); it != world.end(); ++it)
        if (it->first == "_scal_visgroups") {
            std::istringstream in(it->second);
            std::string entry;
            while (std::getline(in, entry, ';')) {
                const size_t a = entry.find('|'), b = entry.rfind('|');
                if (a == std::string::npos || b == a) continue;
                Visgroup v;
                v.id = (std::uint32_t)std::strtoul(entry.substr(0, a).c_str(), nullptr, 10);
                v.name = entry.substr(a + 1, b - a - 1);
                v.visible = entry.substr(b + 1) != "0";
                if (v.id) visgroups.push_back(v);
            }
            world.erase(it);
            break;
        }
    doc.reset(std::move(world), std::move(worldBrushes), std::move(others), std::move(groups), std::move(visgroups));
    r.ok = true;
    return r;
}

bool saveMapFile(const Document& doc, const std::string& path, std::string& error) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) { error = "cannot write file"; return false; }
        f << writeMap(doc);
        if (!f) { error = "write failed"; return false; }
    }
    // Write-then-rename so a crash never leaves a half-written map.
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        error = "cannot replace file";
        return false;
    }
    return true;
}

MapReadResult loadMapFile(const std::string& path, Document& doc) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { MapReadResult r; r.error = "cannot open file"; return r; }
    std::ostringstream ss;
    ss << f.rdbuf();
    return readMap(ss.str(), doc);
}

} // namespace scal
