#include "io/map_format.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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
    writeKeys(o, doc.worldKeys);
    int i = 0;
    for (const Brush& b : doc.brushes()) {
        o << "// brush " << i++ << '\n';
        writeBrush(o, b);
    }
    o << "}\n";
    int e = 1;
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
    enum Kind { End, Punct, Str, Word } kind = End;
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
            else if (c == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '/') {
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
            fail(t, "unexpected '" + t.text + "' in brush (patches are not supported)");
        }
        return b;
    }

    // Returns false at end of input.
    bool entity(KeyValues& keys, std::vector<Brush>& brushes) {
        Token t = lex_.next();
        if (t.kind == Token::End) return false;
        if (t.kind != Token::Punct || t.text != "{") fail(t, "expected '{' to start an entity");
        while (true) {
            Token k = lex_.next();
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
            } else {
                others.push_back({std::move(keys), std::move(brushes)});
            }
        }
    } catch (const ParseError& e) {
        r.error = e.msg;
        return r;
    }
    if (!haveWorld) world = {{"classname", "worldspawn"}, {"mapversion", "220"}};
    doc.reset(std::move(world), std::move(worldBrushes), std::move(others));
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
