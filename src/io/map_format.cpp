#include "io/map_format.hpp"
#include "core/terrain.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
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
    // Lightmap scale per face (baked light, v0.12.1), only when one isn't the default.
    bool lm = false;
    for (const Face& f : b.faces) if (f.lightmapScale != kDefaultLightmapScale) lm = true;
    if (lm) {
        o << "//@ lm";
        for (const Face& f : b.faces) o << ' ' << num(f.lightmapScale);
        o << '\n';
    }
    // Decals put on this brush (v0.12.5), the material last (the rest of the line).
    auto v3 = [](const Vec3& v) { return num(v.x) + ',' + num(v.y) + ',' + num(v.z); };
    for (const Decal& d : b.decals) {
        o << "//@ decal pos=" << v3(d.pos) << " n=" << v3(d.normal) << " up=" << v3(d.up) << " size=" << num(d.width) << ','
          << num(d.height) << " reach=" << num(d.reach) << " order=" << d.order;
        if (d.visgroup) o << " vg=" << d.visgroup;
        o << " mat=" << d.material << '\n';
    }
    o << "}\n";
}

void writeKeys(std::ostringstream& o, const KeyValues& kv) {
    for (const auto& [k, v] : kv) o << quoted(k) << ' ' << quoted(v) << '\n';
}

} // namespace

namespace {
// Milestone 8 keys in worldspawn (other tools ignore "_scal_*").
const char* const kWorldExtras[] = {"_scal_visgroups", "_scal_sky", "_scal_ambient", "_scal_fog", "_scal_cameras", "_scal_figure", "_scal_skylab", "_scal_bake"};
bool isWorldExtra(const std::string& k) {
    for (const char* e : kWorldExtras) if (k == e) return true;
    return false;
}
std::string vec(const Vec3& v) { return num(v.x) + " " + num(v.y) + " " + num(v.z); }
std::string cleanName(std::string s) {
    for (char& c : s) if (c == '|' || c == ';' || c == '"') c = '_';
    return s;
}

// A light as a Hammer-style entity.
KeyValues lightKeys(const Light& l) {
    KeyValues kv;
    kv.emplace_back("classname", l.kind == LightKind::Point ? "light" : l.kind == LightKind::Spot ? "light_spot" : "light_environment");
    kv.emplace_back("origin", vec(l.pos));
    kv.emplace_back("_light", vec(l.color) + " " + num(l.brightness));
    if (l.hasDirection()) {
        kv.emplace_back("angles", num(-l.pitch) + " " + num(l.yaw) + " 0"); // angles: positive pitch = down
        kv.emplace_back("pitch", num(l.pitch));
    }
    if (l.kind == LightKind::Spot) {
        kv.emplace_back("_inner_cone", num(l.inner));
        kv.emplace_back("_cone", num(l.outer));
        kv.emplace_back("_exponent", num(l.exponent));
    }
    if (l.kind != LightKind::Sun) {
        // Falloff as Hammer writes it (baked light, v0.12.1).
        if (l.falloff == LightFalloff::FiftyZero) {
            kv.emplace_back("_fifty_percent_distance", num(l.fifty));
            kv.emplace_back("_hardfalloff", l.hardFalloff ? "1" : "0");
        } else if (l.falloff == LightFalloff::Custom) {
            kv.emplace_back("_constant_attn", num(l.constantAttn));
            kv.emplace_back("_linear_attn", num(l.linearAttn));
            kv.emplace_back("_quadratic_attn", num(l.quadraticAttn));
        } else {
            kv.emplace_back("_constant_attn", "0");
            kv.emplace_back("_linear_attn", "0");
            kv.emplace_back("_quadratic_attn", "1");
        }
        kv.emplace_back("_zero_percent_distance", num(l.range));
    } else {
        kv.emplace_back("SunSpreadAngle", num(l.sunSpread));
    }
    for (const auto& k : l.otherKeys) kv.push_back(k);
    return kv;
}

// A math primitive as our own point entity ("scal_primitive"; other tools ignore it).
KeyValues primKeys(const MathPrim& p) {
    KeyValues kv;
    kv.emplace_back("classname", "scal_primitive");
    kv.emplace_back("kind", primKindName(p.kind));
    kv.emplace_back("origin", vec(p.pos));
    kv.emplace_back("rotation", vec(p.rot));
    kv.emplace_back("size", vec(p.size));
    if (p.kind == PrimKind::Cube) kv.emplace_back("round", num(p.round));
    if (p.kind == PrimKind::Torus) kv.emplace_back("thickness", num(p.thickness));
    if (p.combo) {
        kv.emplace_back("combo", std::to_string(p.combo));
        kv.emplace_back("op", primOpName(p.op));
        if (p.blend > 0) kv.emplace_back("blend", num(p.blend));
    }
    if (!p.texture.empty()) kv.emplace_back("texture", p.texture);
    kv.emplace_back("color", vec(p.color));
    kv.emplace_back("texscale", num(p.texScale));
    if (p.lightmapScale != 40) kv.emplace_back("lightmapscale", num(p.lightmapScale));
    for (const auto& k : p.otherKeys) kv.push_back(k);
    return kv;
}

// A prop as our own point entity ("scal_prop"; other tools ignore it). Only the model's
// name is stored, so the map opens even when the file is gone.
KeyValues propKeys(const Prop& p) {
    KeyValues kv;
    kv.emplace_back("classname", "scal_prop");
    kv.emplace_back("model", p.model);
    kv.emplace_back("origin", vec(p.pos));
    kv.emplace_back("rotation", vec(p.rot));
    kv.emplace_back("scale", vec(p.scale));
    if (!p.castShadows) kv.emplace_back("disableshadows", "1");
    if (p.modelLighting) kv.emplace_back("disablevertexlighting", "1");
    for (const auto& k : p.otherKeys) kv.push_back(k);
    return kv;
}

// A camera as our own point entity ("scal_camera"; other tools ignore it). Angles as
// Hammer writes them: "pitch yaw roll" with pitch positive DOWN.
KeyValues cameraKeys(const Camera& c) {
    KeyValues kv;
    kv.emplace_back("classname", "scal_camera");
    kv.emplace_back("name", c.name);
    kv.emplace_back("origin", vec(c.pos));
    kv.emplace_back("angles", num(-c.pitch) + " " + num(c.yaw) + " " + num(c.roll));
    kv.emplace_back("fov", num(c.fov));
    kv.emplace_back("near", num(c.nearDist));
    kv.emplace_back("far", num(c.farDist));
    kv.emplace_back("size", std::to_string(c.width) + " " + std::to_string(c.height));
    kv.emplace_back("ortho", c.ortho ? "1" : "0");
    kv.emplace_back("ortho_size", num(c.orthoSize));
    kv.emplace_back("exposure", num(c.exposure));
    kv.emplace_back("color", vec(c.color));
    kv.emplace_back("active", c.active ? "1" : "0");
    for (const auto& k : c.otherKeys) kv.push_back(k);
    return kv;
}

// Numbers from a file: NaN and infinity (which strtod accepts) are refused like any
// other bad number (v0.10.2), so a broken file can't put them into the map.
bool finite3(double a, double b, double c) { return std::isfinite(a) && std::isfinite(b) && std::isfinite(c); }

bool readVec(const std::string& s, Vec3& v) {
    Vec3 r;
    if (std::sscanf(s.c_str(), "%lf %lf %lf", &r.x, &r.y, &r.z) != 3 || !finite3(r.x, r.y, r.z)) return false;
    v = r;
    return true;
}

// Strict number reading (as Mazzuola's): the whole text must be the number, so "abc",
// "1.5x" or "" are refused instead of read as 0 or 1.5. Spaces around it are allowed.
bool onlySpaceFrom(const char* p) {
    while (*p == ' ' || *p == '\t') ++p;
    return *p == 0;
}
bool parseNumber(const std::string& s, double& v) {
    const char* b = s.c_str();
    while (*b == ' ' || *b == '\t') ++b;
    if (!*b) return false;
    char* end = nullptr;
    errno = 0;
    const double x = std::strtod(b, &end);
    if (end == b || !onlySpaceFrom(end) || errno == ERANGE || !std::isfinite(x)) return false;
    v = x;
    return true;
}
bool parseInt(const std::string& s, long lo, long hi, long& v) {
    const char* b = s.c_str();
    while (*b == ' ' || *b == '\t') ++b;
    if (!*b) return false;
    char* end = nullptr;
    errno = 0;
    const long x = std::strtol(b, &end, 10);
    if (end == b || !onlySpaceFrom(end) || errno == ERANGE || x < lo || x > hi) return false;
    v = x;
    return true;
}
bool parseId(const std::string& s, std::uint32_t& v) { // 0 .. 2^32-1
    long x = 0;
    if (!parseInt(s, 0, 4294967295L, x)) return false;
    v = (std::uint32_t)x;
    return true;
}

// The number in `s`, or `keep` if it isn't a finite number.
double numOr(const std::string& s, double keep) {
    double v = 0;
    return parseNumber(s, v) ? v : keep;
}

bool primFromKeys(const KeyValues& kv, MathPrim& p) {
    bool ours = false;
    for (const auto& [k, v] : kv) if (k == "classname" && v == "scal_primitive") ours = true;
    if (!ours) return false;
    for (const auto& [k, v] : kv) {
        if (k == "classname") continue;
        if (k == "kind") { if (auto pk = primKindFromName(v)) p.kind = *pk; }
        else if (k == "origin") readVec(v, p.pos);
        else if (k == "rotation") readVec(v, p.rot);
        else if (k == "size") readVec(v, p.size);
        else if (k == "round") p.round = numOr(v, p.round);
        else if (k == "thickness") p.thickness = numOr(v, p.thickness);
        else if (k == "blend") p.blend = numOr(v, p.blend);
        else if (k == "combo") { std::uint32_t c = 0; if (parseId(v, c)) p.combo = c; } // bad: no combination
        else if (k == "op") p.op = v == "subtract" ? PrimOp::Subtract : v == "intersect" ? PrimOp::Intersect : PrimOp::Add;
        else if (k == "texture") p.texture = v;
        else if (k == "color") readVec(v, p.color);
        else if (k == "texscale") p.texScale = numOr(v, p.texScale);
        else if (k == "lightmapscale") p.lightmapScale = std::clamp(numOr(v, p.lightmapScale), kMinLightmapScale, kMaxLightmapScale);
        else p.otherKeys.emplace_back(k, v);
    }
    return true;
}

bool propFromKeys(const KeyValues& kv, Prop& p) {
    bool ours = false;
    for (const auto& [k, v] : kv) if (k == "classname" && v == "scal_prop") ours = true;
    if (!ours) return false;
    for (const auto& [k, v] : kv) {
        if (k == "classname") continue;
        if (k == "model") p.model = v;
        else if (k == "origin") readVec(v, p.pos);
        else if (k == "rotation") readVec(v, p.rot);
        else if (k == "scale") readVec(v, p.scale);
        else if (k == "disableshadows") p.castShadows = numOr(v, 0) == 0;
        else if (k == "disablevertexlighting") p.modelLighting = numOr(v, 0) != 0;
        else p.otherKeys.emplace_back(k, v);
    }
    return true;
}

// A reflection probe (v0.12.6) as Hammer's env_cubemap: origin and cubemapsize (0 =
// default 32 a side, n = 2^(n-1)); other keys (Hammer's "sides"...) kept as they are.
KeyValues probeKeys(const Probe& p) {
    KeyValues kv;
    kv.emplace_back("classname", "env_cubemap");
    kv.emplace_back("origin", vec(p.pos));
    kv.emplace_back("cubemapsize", std::to_string(p.size));
    for (const auto& k : p.otherKeys) kv.push_back(k);
    return kv;
}

bool probeFromKeys(const KeyValues& kv, Probe& p) {
    bool ours = false;
    for (const auto& [k, v] : kv) if (k == "classname" && v == "env_cubemap") ours = true;
    if (!ours) return false;
    for (const auto& [k, v] : kv) {
        if (k == "classname") continue;
        if (k == "origin") readVec(v, p.pos);
        else if (k == "cubemapsize") p.size = std::clamp((int)std::lround(numOr(v, 0)), 0, kProbeMaxSize);
        else p.otherKeys.emplace_back(k, v);
    }
    return true;
}

bool cameraFromKeys(const KeyValues& kv, Camera& c) {
    bool ours = false;
    for (const auto& [k, v] : kv) if (k == "classname" && v == "scal_camera") ours = true;
    if (!ours) return false;
    for (const auto& [k, v] : kv) {
        if (k == "classname") continue;
        if (k == "name") c.name = v;
        else if (k == "origin") readVec(v, c.pos);
        else if (k == "angles") {
            Vec3 a;
            if (readVec(v, a)) { c.pitch = -a.x; c.yaw = a.y; c.roll = a.z; }
        } else if (k == "fov") c.fov = numOr(v, c.fov);
        else if (k == "near") c.nearDist = numOr(v, c.nearDist);
        else if (k == "far") c.farDist = numOr(v, c.farDist);
        else if (k == "size") {
            double w = 0, h = 0;
            if (std::sscanf(v.c_str(), "%lf %lf", &w, &h) == 2 && std::isfinite(w) && std::isfinite(h) && w > 0 && h > 0 && w < 1e6 && h < 1e6) {
                c.width = (int)std::lround(w);
                c.height = (int)std::lround(h);
            }
        } else if (k == "ortho") c.ortho = numOr(v, 0) != 0;
        else if (k == "ortho_size") c.orthoSize = numOr(v, c.orthoSize);
        else if (k == "exposure") c.exposure = numOr(v, c.exposure);
        else if (k == "color") readVec(v, c.color);
        else if (k == "active") c.active = numOr(v, 0) != 0;
        else c.otherKeys.emplace_back(k, v);
    }
    clampCamera(c);
    return true;
}

// Light entity -> Light (false if it isn't one of ours to edit).
bool lightFromKeys(const KeyValues& kv, Light& l) {
    std::string cls;
    for (const auto& [k, v] : kv) if (k == "classname") cls = v;
    if (cls == "light") l.kind = LightKind::Point;
    else if (cls == "light_spot") l.kind = LightKind::Spot;
    else if (cls == "light_environment") l.kind = LightKind::Sun;
    else return false;
    bool havePitch = false;
    // Falloff (baked light): a 50% distance makes it 50% / 0%; else the constant / linear /
    // quadratic terms (0 0 1, Hammer's default, is inverse-square). Lights without any of
    // these keys (maps from before v0.12.1) are Hammer's default too (agreed 2026-11-20).
    double fifty = 0, c = 0, li = 0, q = 1;
    bool haveAttn = false;
    for (const auto& [k, v] : kv) {
        if (k == "origin") readVec(v, l.pos);
        else if (k == "_fifty_percent_distance") fifty = std::max(0.0, numOr(v, 0));
        else if (k == "_hardfalloff") l.hardFalloff = numOr(v, 0) != 0;
        else if (k == "_constant_attn") { c = std::max(0.0, numOr(v, 0)); haveAttn = true; }
        else if (k == "_linear_attn") { li = std::max(0.0, numOr(v, 0)); haveAttn = true; }
        else if (k == "_quadratic_attn") { q = std::max(0.0, numOr(v, 0)); haveAttn = true; }
        else if (k == "_exponent") l.exponent = std::max(0.0, numOr(v, l.exponent));
        else if (k == "SunSpreadAngle") l.sunSpread = std::clamp(numOr(v, 0), 0.0, 90.0);
        else if (k == "_light") {
            double r = 255, g = 255, b = 255, br = 200;
            const int n = std::sscanf(v.c_str(), "%lf %lf %lf %lf", &r, &g, &b, &br);
            if (n >= 3 && finite3(r, g, b)) l.color = {r, g, b};
            if (n >= 4 && std::isfinite(br)) l.brightness = br;
        } else if (k == "pitch") { l.pitch = numOr(v, l.pitch); havePitch = true; }
        else if (k == "angles") {
            Vec3 a;
            if (readVec(v, a)) { if (!havePitch) l.pitch = -a.x; l.yaw = a.y; }
        } else if (k == "_inner_cone") l.inner = numOr(v, l.inner);
        else if (k == "_cone") l.outer = numOr(v, l.outer);
        else if (k == "_zero_percent_distance") l.range = numOr(v, l.range);
        else if (k != "classname") l.otherKeys.emplace_back(k, v);
    }
    if (fifty > 0) {
        l.falloff = LightFalloff::FiftyZero;
        l.fifty = fifty;
    } else if (haveAttn && !(c == 0 && li == 0 && q == 1)) {
        l.falloff = LightFalloff::Custom;
        l.constantAttn = c;
        l.linearAttn = li;
        l.quadraticAttn = q;
    } else {
        l.falloff = LightFalloff::InverseSquare;
    }
    return true;
}

} // namespace

std::string writeMap(const Document& doc) {
    std::ostringstream o;
    o << "// Game: Generic\n// Format: Valve\n// Written by Scalpellino\n";
    o << "// entity 0\n{\n";
    KeyValues world;
    for (const auto& kv : doc.worldKeys)
        if (!isWorldExtra(kv.first)) world.push_back(kv);
    const Environment& env = doc.environment();
    const Environment defaults;
    if (!env.sky.empty()) world.emplace_back("_scal_sky", env.sky);
    if (env.ambient != defaults.ambient) world.emplace_back("_scal_ambient", vec(env.ambient));
    if (env.fogOn || !(env.fog == defaults.fog))
        world.emplace_back("_scal_fog", std::string(env.fogOn ? "1 " : "0 ") + vec(env.fog.color) + " " + num(env.fog.start) + " " + num(env.fog.end) +
                                            (env.fog.base != 0 || env.fog.thickness != 0 ? " " + num(env.fog.base) + " " + num(env.fog.thickness) : std::string()));
    if (env.skylab.used) world.emplace_back("_scal_skylab", skyLabToString(env.skylab));
    if (env.skyLight != defaults.skyLight || env.minLight != defaults.minLight || env.smoothAngle != defaults.smoothAngle ||
        env.bounceScale != defaults.bounceScale)
        world.emplace_back("_scal_bake", "skylight=" + num(env.skyLight) + ";minlight=" + (env.minLight ? "1" : "0") + ";smooth=" + num(env.smoothAngle) +
                                             ";bounce=" + num(env.bounceScale));
    if (!doc.bookmarks().empty()) {
        // x y z yaw pitch name;...
        std::string cams;
        for (const CameraBookmark& c : doc.bookmarks())
            cams += vec(c.pos) + " " + num(c.yaw) + " " + num(c.pitch) + " " + cleanName(c.name) + ";";
        world.emplace_back("_scal_cameras", cams);
    }
    if (doc.figure()) world.emplace_back("_scal_figure", vec(*doc.figure()));
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
    for (const Light& l : doc.lights()) {
        o << "// entity " << e++ << "\n{\n";
        writeKeys(o, lightKeys(l));
        o << "}\n";
    }
    for (const MathPrim& p : doc.prims()) {
        o << "// entity " << e++ << "\n{\n";
        writeKeys(o, primKeys(p));
        o << "}\n";
    }
    for (const Prop& p : doc.props()) {
        o << "// entity " << e++ << "\n{\n";
        writeKeys(o, propKeys(p));
        o << "}\n";
    }
    for (const Camera& c : doc.cameras()) {
        o << "// entity " << e++ << "\n{\n";
        writeKeys(o, cameraKeys(c));
        o << "}\n";
    }
    for (const Probe& p : doc.probes()) {
        o << "// entity " << e++ << "\n{\n";
        writeKeys(o, probeKeys(p));
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
        if (end == t.text.c_str() || *end != '\0' || !std::isfinite(v)) fail(t, "bad number '" + t.text + "'");
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
            if (t.kind == Token::Meta) { meta(lex_.next(), b); continue; }
            fail(t, "unexpected '" + t.text + "' in brush (patches are not supported)");
        }
        if (b.terrain) {
            Terrain& tr = *b.terrain;
            // The size comes from the power, so check the power before using it.
            if (tr.power < kMinTerrainPower || tr.power > kMaxTerrainPower) fail(lex_.peek(), "terrain size is out of range");
            const size_t nn = (size_t)tr.size() * tr.size();
            if (tr.heights.size() != nn) fail(lex_.peek(), "terrain data is incomplete");
            auto allFinite = [](const std::vector<double>& v) {
                return std::all_of(v.begin(), v.end(), [](double x) { return std::isfinite(x); });
            };
            if (!allFinite(tr.heights) || !finite3(tr.start.x, tr.start.y, tr.start.z)) fail(lex_.peek(), "terrain data has a bad number");
            for (const TerrainLayer& l : tr.layers)
                if (!allFinite(l.alpha)) fail(lex_.peek(), "terrain blend data has a bad number");
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
    void meta(const Token& tok, Brush& b) {
        const std::string& text = tok.text;
        std::istringstream in(text);
        std::string first;
        in >> first;
        if (first == "lm") { // lightmap scale per face, in face order (written after the faces)
            std::string w;
            size_t i = 0;
            while (in >> w) {
                double x = 0;
                if (!parseNumber(w, x)) fail(tok, "lightmap scale '" + w + "' is not a number");
                if (i < b.faces.size()) b.faces[i].lightmapScale = std::clamp(x, kMinLightmapScale, kMaxLightmapScale);
                ++i;
            }
            return;
        }
        if (first == "decal") {
            Decal d;
            std::string kv;
            auto vec = [&](const std::string& v, Vec3& out) {
                double x = 0, y = 0, z = 0;
                if (std::sscanf(v.c_str(), "%lf,%lf,%lf", &x, &y, &z) != 3 || !finite3(x, y, z)) fail(tok, "bad decal value '" + v + "'");
                out = {x, y, z};
            };
            while (in >> kv) {
                const size_t eq = kv.find('=');
                if (eq == std::string::npos) continue;
                const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
                if (k == "mat") {
                    std::string rest;
                    std::getline(in, rest);
                    d.material = v + rest;
                    while (!d.material.empty() && (d.material.back() == ' ' || d.material.back() == '\r')) d.material.pop_back();
                    break;
                }
                if (k == "pos") vec(v, d.pos);
                else if (k == "n") vec(v, d.normal);
                else if (k == "up") vec(v, d.up);
                else if (k == "size") {
                    double w = 0, h = 0;
                    if (std::sscanf(v.c_str(), "%lf,%lf", &w, &h) != 2 || !(w > 0) || !(h > 0) || !std::isfinite(w) || !std::isfinite(h))
                        fail(tok, "bad decal size '" + v + "'");
                    d.width = w;
                    d.height = h;
                } else if (k == "reach") {
                    double r = 0;
                    if (!parseNumber(v, r) || !(r >= 0)) fail(tok, "bad decal reach '" + v + "'");
                    d.reach = r;
                } else if (k == "order") {
                    long x = 0;
                    if (!parseInt(v, -1000000, 1000000, x)) fail(tok, "bad decal order '" + v + "'");
                    d.order = (int)x;
                } else if (k == "vg") {
                    long x = 0;
                    if (!parseInt(v, 0, 1L << 30, x)) fail(tok, "bad decal visgroup '" + v + "'");
                    d.visgroup = (std::uint32_t)x;
                }
            }
            if (length(d.normal) < 1e-9) fail(tok, "a decal has no facing");
            d.normal = normalize(d.normal);
            b.decals.push_back(std::move(d));
            return;
        }
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
            while (in >> w) {
                double x = 0;
                if (!parseNumber(w, x))
                    fail(tok, std::string(first == "th" ? "terrain height" : "terrain blend") + " '" + w + "' is not a number");
                v.push_back(x);
            }
            return;
        }
        if (first == "terrain") {
            Terrain t;
            std::string kv;
            while (in >> kv) {
                const size_t eq = kv.find('=');
                if (eq == std::string::npos) continue;
                const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
                long x = 0;
                if (k == "face") {
                    if (!parseInt(v, 0, 1 << 20, x)) fail(tok, "bad terrain face '" + v + "'");
                    t.face = (int)x;
                } else if (k == "power") {
                    if (!parseInt(v, kMinTerrainPower, kMaxTerrainPower, x)) fail(tok, "terrain size is out of range ('" + v + "')");
                    t.power = (int)x;
                }
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
            if (k == "visgroup") {
                if (!parseId(v, b.visgroup)) fail(tok, "bad visgroup '" + v + "'");
            }
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
    WorldData extra;
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
            } else if (Light l; brushes.empty() && lightFromKeys(keys, l)) {
                extra.lights.push_back(l);
            } else if (MathPrim mp; brushes.empty() && primFromKeys(keys, mp)) {
                extra.prims.push_back(mp);
            } else if (Prop pp; brushes.empty() && propFromKeys(keys, pp)) {
                extra.props.push_back(pp);
            } else if (Camera cam; brushes.empty() && cameraFromKeys(keys, cam)) {
                extra.cameras.push_back(cam);
            } else if (Probe pr; brushes.empty() && probeFromKeys(keys, pr)) {
                extra.probes.push_back(pr);
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
                if (!parseId(entry.substr(0, a), v.id)) continue; // a bad id: the visgroup is dropped
                v.name = entry.substr(a + 1, b - a - 1);
                v.visible = entry.substr(b + 1) != "0";
                if (v.id) visgroups.push_back(v);
            }
            world.erase(it);
            break;
        }
    for (auto it = world.begin(); it != world.end();) {
        const std::string& k = it->first;
        const std::string& v = it->second;
        if (k == "_scal_sky") extra.env.sky = v;
        else if (k == "_scal_skylab") extra.env.skylab = skyLabFromString(v);
        else if (k == "_scal_ambient") readVec(v, extra.env.ambient);
        else if (k == "_scal_bake") { // "skylight=100;minlight=0;smooth=45" (unknown fields ignored)
            std::stringstream ss(v);
            std::string part;
            while (std::getline(ss, part, ';')) {
                const size_t eq = part.find('=');
                if (eq == std::string::npos) continue;
                const std::string f = part.substr(0, eq), val = part.substr(eq + 1);
                if (f == "skylight") extra.env.skyLight = std::clamp(numOr(val, extra.env.skyLight), 0.0, 100000.0);
                else if (f == "minlight") extra.env.minLight = numOr(val, 0) != 0;
                else if (f == "smooth") extra.env.smoothAngle = std::clamp(numOr(val, extra.env.smoothAngle), 0.0, 180.0);
                else if (f == "bounce") extra.env.bounceScale = std::clamp(numOr(val, extra.env.bounceScale), 0.0, 100.0);
            }
        }
        else if (k == "_scal_fog") {
            int on = 0;
            Fog f;
            // 6 numbers, or 8 with the height (base, thickness) from v0.10.6
            const int got = std::sscanf(v.c_str(), "%d %lf %lf %lf %lf %lf %lf %lf", &on, &f.color.x, &f.color.y, &f.color.z, &f.start, &f.end, &f.base, &f.thickness);
            if ((got == 6 || got == 8) && finite3(f.color.x, f.color.y, f.color.z) && std::isfinite(f.start) && std::isfinite(f.end) &&
                std::isfinite(f.base) && std::isfinite(f.thickness)) {
                if (got == 6) f.base = f.thickness = 0;
                f.thickness = std::max(0.0, f.thickness);
                extra.env.fogOn = on != 0;
                extra.env.fog = f;
            }
        } else if (k == "_scal_cameras") {
            std::istringstream in(v);
            std::string entry;
            while (std::getline(in, entry, ';')) {
                CameraBookmark c;
                int used = 0;
                if (std::sscanf(entry.c_str(), "%lf %lf %lf %lf %lf %n", &c.pos.x, &c.pos.y, &c.pos.z, &c.yaw, &c.pitch, &used) >= 5 &&
                    finite3(c.pos.x, c.pos.y, c.pos.z) && std::isfinite(c.yaw) && std::isfinite(c.pitch)) {
                    c.name = entry.substr((size_t)used);
                    extra.bookmarks.push_back(c);
                }
            }
        } else if (k == "_scal_figure") {
            Vec3 f;
            if (readVec(v, f)) extra.figure = f;
        } else {
            ++it;
            continue;
        }
        it = world.erase(it);
    }
    doc.reset(std::move(world), std::move(worldBrushes), std::move(others), std::move(groups), std::move(visgroups),
              std::move(extra));
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
