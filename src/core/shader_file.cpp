#include "core/shader_file.hpp"
#include "core/material.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace scal {

namespace {
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
} // namespace

const ShaderSetting* ShaderFile::setting(const std::string& key) const {
    const std::string k = lowerCase(key);
    for (const ShaderSetting& s : settings)
        if (s.key == k) return &s;
    return nullptr;
}

bool parseShaderFile(const std::string& text, ShaderFile& out, std::string* error) {
    out = ShaderFile{};
    size_t pos = 0, consumed = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string t = trim(text.substr(pos, eol - pos));
        const size_t next = eol < text.size() ? eol + 1 : eol;
        if (t.empty()) { pos = next; continue; }
        if (t.rfind("//", 0) != 0) break;
        pos = consumed = next;
        const std::string c = trim(t.substr(2));
        auto value = [&](const char* key) -> const char* {
            const size_t n = std::char_traits<char>::length(key);
            if (lowerCase(c.substr(0, n)) == key) return c.c_str() + n;
            return nullptr;
        };
        if (lowerCase(c).rfind("scalpellino shader", 0) == 0) {
            out.version = std::atoi(c.c_str() + 18);
        } else if (const char* v = value("name:")) {
            out.name = trim(v);
        } else if (const char* v = value("kind:")) {
            out.kind = lowerCase(trim(v));
        } else if (const char* v = value("blend:")) {
            out.blend = lowerCase(trim(v)) == "modulate" ? "modulate" : "normal";
        } else if (const char* v = value("about:")) {
            if (!out.about.empty()) out.about += ' ';
            out.about += trim(v);
        } else if (const char* v = value("setting:")) {
            std::string s = v, help;
            const size_t bar = s.find('|');
            if (bar != std::string::npos) {
                help = trim(s.substr(bar + 1));
                s = s.substr(0, bar);
            }
            std::istringstream ss(s);
            ShaderSetting st;
            ss >> st.key >> st.type;
            st.key = lowerCase(st.key);
            st.type = lowerCase(st.type);
            std::string rest, w;
            while (ss >> w) rest += (rest.empty() ? "" : " ") + w;
            st.def = rest;
            st.help = help;
            if (!st.key.empty() && !st.type.empty()) out.settings.push_back(st);
        }
    }
    if (out.name.empty()) {
        if (error) *error = "no \"// name:\" line at the top";
        return false;
    }
    out.body = consumed < text.size() ? text.substr(consumed) : std::string();
    out.bodyLine = 1 + (int)std::count(text.begin(), text.begin() + (long)std::min(consumed, text.size()), '\n');
    return true;
}

std::string shaderUniformName(const std::string& key) {
    std::string u = "u_";
    for (char c : lowerCase(key)) {
        if (c == '$' || c == '%') continue;
        u += std::isalnum((unsigned char)c) ? c : '_';
    }
    return u;
}

} // namespace scal
