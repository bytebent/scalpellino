#include "core/document.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <map>

namespace scal {

const Brush* Document::find(BrushId id) const {
    for (const Brush& b : brushes_)
        if (b.id == id) return &b;
    return nullptr;
}

std::uint32_t Document::newVisgroupId() const {
    std::uint32_t id = 1;
    for (const Visgroup& v : visgroups_) id = std::max(id, v.id + 1);
    return id;
}

bool Document::visible(const Brush& b) const {
    if (b.hidden) return false;
    if (b.visgroup)
        for (const Visgroup& v : visgroups_)
            if (v.id == b.visgroup) return v.visible;
    return true;
}

const Group* Document::group(GroupId id) const {
    auto it = groups_.find(id);
    return it == groups_.end() ? nullptr : &it->second;
}

std::set<BrushId> Document::groupMembers(GroupId id) const {
    std::set<BrushId> out;
    if (id == 0) return out;
    for (const Brush& b : brushes_)
        if (b.group == id) out.insert(b.id);
    return out;
}

std::set<BrushId> Document::expandToGroups(const std::set<BrushId>& ids) const {
    std::set<BrushId> out = ids;
    for (BrushId id : ids)
        if (const Brush* b = find(id); b && b->group)
            for (BrushId m : groupMembers(b->group)) out.insert(m);
    return out;
}

void Document::pruneEmptyGroups(Command& c) const {
    for (const auto& [gid, g] : groups_) {
        bool handled = false;
        for (const GroupChange& gc : c.groupChanges) handled = handled || gc.id == gid;
        if (handled) continue;
        // Members after the command: untouched members + changed brushes ending in the group.
        size_t count = 0;
        for (const Brush& b : brushes_) {
            if (b.group != gid) continue;
            bool touched = false;
            for (const BrushChange& ch : c.changes) touched = touched || ch.id == b.id;
            if (!touched) ++count;
        }
        for (const BrushChange& ch : c.changes)
            if (ch.after && ch.after->group == gid) ++count;
        if (count == 0) c.groupChanges.push_back({gid, g, std::nullopt});
    }
}

void Document::setGroup(GroupId id, const std::optional<Group>& g) {
    if (g) {
        groups_[id] = *g;
        if (id >= nextGroupId_) nextGroupId_ = id + 1;
    } else {
        groups_.erase(id);
    }
}

AABB Document::selectionBounds() const {
    AABB box;
    for (BrushId id : selection_)
        if (const Brush* b = find(id)) box.add(b->bounds());
    return box;
}

const Light* Document::findLight(LightId id) const {
    for (const Light& l : lights_)
        if (l.id == id) return &l;
    return nullptr;
}

void Document::setLight(LightId id, const std::optional<Light>& l) {
    touchLighting();
    auto it = std::find_if(lights_.begin(), lights_.end(), [&](const Light& x) { return x.id == id; });
    if (l) {
        if (it != lights_.end()) *it = *l;
        else
            lights_.insert(std::lower_bound(lights_.begin(), lights_.end(), id,
                                            [](const Light& x, LightId v) { return x.id < v; }),
                           *l);
        if (id >= nextLightId_) nextLightId_ = id + 1;
    } else if (it != lights_.end()) {
        lights_.erase(it);
        lightSelection_.erase(id);
    }
}

Document::DecalRef Document::findDecal(DecalId id) const {
    if (id == 0) return {};
    for (const Brush& b : brushes_)
        for (const Decal& d : b.decals)
            if (d.id == id) return {&b, &d};
    return {};
}

bool Document::visible(const Brush& b, const Decal& d) const {
    if (!decalsShown_ || !visible(b)) return false;
    if (d.visgroup)
        for (const Visgroup& v : visgroups_)
            if (v.id == d.visgroup) return v.visible;
    return true;
}

void Document::setDecalsShown(bool on) {
    if (on == decalsShown_) return;
    decalsShown_ = on;
    if (!on) decalSelection_.clear();
    bump();
}

size_t Document::decalCount() const {
    size_t n = 0;
    for (const Brush& b : brushes_) n += b.decals.size();
    return n;
}

void Document::pruneDecalSelection() {
    if (decalSelection_.empty()) return;
    std::set<DecalId> keep;
    for (const Brush& b : brushes_)
        for (const Decal& d : b.decals)
            if (decalSelection_.count(d.id)) keep.insert(d.id);
    decalSelection_ = std::move(keep);
}

void Document::normalizeDecals(Command& c) {
    std::set<BrushId> touched;
    for (const BrushChange& ch : c.changes) touched.insert(ch.id);
    std::set<DecalId> elsewhere;
    for (const Brush& b : brushes_)
        if (!touched.count(b.id))
            for (const Decal& d : b.decals) elsewhere.insert(d.id);
    std::map<DecalId, std::vector<Brush*>> holders;
    for (BrushChange& ch : c.changes) {
        if (!ch.after) continue;
        for (Decal& d : ch.after->decals) {
            if (d.id == 0 || elsewhere.count(d.id)) d.id = newDecalId();
            holders[d.id].push_back(&*ch.after);
        }
    }
    for (auto& [id, hs] : holders) {
        if (hs.size() < 2) continue;
        // the piece whose box is nearest to the decal's centre keeps it
        Vec3 at;
        for (const Decal& d : hs[0]->decals) if (d.id == id) at = d.pos;
        Brush* best = nullptr;
        double bestD = 1e300;
        for (Brush* b : hs) {
            const AABB bb = b->bounds();
            double dd = 0;
            for (int k = 0; k < 3; ++k) {
                const double e = std::max({bb.min[k] - at[k], 0.0, at[k] - bb.max[k]});
                dd += e * e;
            }
            if (dd < bestD) { bestD = dd; best = b; }
        }
        for (Brush* b : hs)
            if (b != best)
                b->decals.erase(std::remove_if(b->decals.begin(), b->decals.end(), [&](const Decal& d) { return d.id == id; }),
                                b->decals.end());
    }
}

Command Document::makeAddDecal(Decal d, BrushId on, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = selection_;
    c.selectionAfter = {};
    c.lightSelectionBefore = lightSelection_;
    c.lightSelectionAfter = std::set<LightId>{};
    c.primSelectionBefore = primSelection_;
    c.primSelectionAfter = std::set<MathPrimId>{};
    c.propSelectionBefore = propSelection_;
    c.propSelectionAfter = std::set<PropId>{};
    c.cameraSelectionBefore = cameraSelection_;
    c.cameraSelectionAfter = std::set<CameraId>{};
    c.probeSelectionBefore = probeSelection_;
    c.probeSelectionAfter = std::set<ProbeId>{};
    const Brush* b = find(on);
    if (!b) return c;
    d.id = newDecalId();
    Brush nb = *b;
    nb.decals.push_back(d);
    c.changes.push_back({on, *b, std::move(nb)});
    c.decalSelectionAfter = std::set<DecalId>{d.id};
    return c;
}

Command Document::makeModifyDecals(const std::vector<Decal>& modified, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = selection_;
    c.selectionAfter = selection_;
    std::map<BrushId, Brush> changed;
    for (const Decal& d : modified) {
        const DecalRef r = findDecal(d.id);
        if (!r) continue;
        auto it = changed.find(r.brush->id);
        if (it == changed.end()) it = changed.emplace(r.brush->id, *r.brush).first;
        for (Decal& x : it->second.decals)
            if (x.id == d.id) x = d;
    }
    for (auto& [id, b] : changed) c.changes.push_back({id, *find(id), std::move(b)});
    return c;
}

void Document::addDecalDeletes(Command& c, const std::set<DecalId>& ids) const {
    if (ids.empty()) return;
    c.decalSelectionBefore = decalSelection_;
    std::set<DecalId> after = decalSelection_;
    for (DecalId id : ids) {
        after.erase(id);
        const DecalRef r = findDecal(id);
        if (!r) continue;
        BrushChange* ch = nullptr;
        for (BrushChange& x : c.changes)
            if (x.id == r.brush->id) ch = &x;
        if (ch && !ch->after) continue; // the brush goes anyway
        if (!ch) {
            c.changes.push_back({r.brush->id, *r.brush, *r.brush});
            ch = &c.changes.back();
        }
        auto& v = ch->after->decals;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const Decal& d) { return d.id == id; }), v.end());
    }
    c.decalSelectionAfter = after;
}

void Document::setBrush(BrushId id, const std::optional<Brush>& b) {
    if (b)
        for (const Decal& d : b->decals) nextDecalId_ = std::max(nextDecalId_, d.id + 1);
    auto it = std::find_if(brushes_.begin(), brushes_.end(), [&](const Brush& x) { return x.id == id; });
    if (b) {
        if (it != brushes_.end()) *it = *b;
        else brushes_.push_back(*b);
        if (id >= nextId_) nextId_ = id + 1;
        stampBrush(id);
    } else if (it != brushes_.end()) {
        brushes_.erase(it);
        selection_.erase(id);
        brushStamps_.erase(id);
    }
}

void Document::apply(Command cmd) {
    normalizeDecals(cmd);
    pruneEmptyGroups(cmd); // a group whose last brush goes away goes with it (undoable)
    for (const GroupChange& g : cmd.groupChanges) setGroup(g.id, g.after);
    for (const BrushChange& c : cmd.changes) setBrush(c.id, c.after);
    for (const LightChange& c : cmd.lightChanges) setLight(c.id, c.after);
    for (const PrimChange& c : cmd.primChanges) setPrim(c.id, c.after);
    for (const PropChange& c : cmd.propChanges) setProp(c.id, c.after);
    for (const CameraChange& c : cmd.cameraChanges) setCamera(c.id, c.after);
    for (const ProbeChange& c : cmd.probeChanges) setProbe(c.id, c.after);
    if (cmd.envAfter) { env_ = *cmd.envAfter; touchLighting(); }
    selection_ = cmd.selectionAfter;
    if (cmd.lightSelectionAfter) lightSelection_ = *cmd.lightSelectionAfter;
    if (cmd.primSelectionAfter) primSelection_ = *cmd.primSelectionAfter;
    if (cmd.propSelectionAfter) propSelection_ = *cmd.propSelectionAfter;
    if (cmd.cameraSelectionAfter) cameraSelection_ = *cmd.cameraSelectionAfter;
    if (cmd.probeSelectionAfter) probeSelection_ = *cmd.probeSelectionAfter;
    if (cmd.decalSelectionAfter) decalSelection_ = *cmd.decalSelectionAfter;
    pruneDecalSelection();
    if (!cmd.fileChanges.empty()) writeFiles(cmd.fileChanges, true);
    // Material files aren't the map: an edit of files only leaves "saved?" as it was.
    cmd.serial = cmd.filesOnly() ? historyTop() : ++nextSerial_;
    // New primitives or props show them again if they were hidden.
    for (const PrimChange& c : cmd.primChanges) if (c.after && !c.before) primsShown_ = true;
    for (const PropChange& c : cmd.propChanges) if (c.after && !c.before) propsShown_ = true;
    for (const BrushChange& c : cmd.changes)
        if (c.after && c.after->decals.size() > (c.before ? c.before->decals.size() : 0)) decalsShown_ = true;
    for (std::size_t b : redoBytes_) historyBytes_ -= b;
    redo_.clear();
    redoBytes_.clear();
    const std::size_t bytes = commandBytes(cmd);
    undo_.push_back(std::move(cmd));
    undoBytes_.push_back(bytes);
    historyBytes_ += bytes;
    trimHistory();
    bump();
}

namespace {
std::size_t strBytes(const std::string& s) { return sizeof(std::string) + (s.capacity() > 15 ? s.capacity() : 0); }
std::size_t keysBytes(const KeyValues& kv) {
    std::size_t n = kv.capacity() * sizeof(kv[0]);
    for (const auto& [k, v] : kv) n += strBytes(k) + strBytes(v) - 2 * sizeof(std::string);
    return n;
}
std::size_t brushBytes(const Brush& b) {
    std::size_t n = sizeof(Brush) + b.faces.capacity() * sizeof(Face);
    for (const Face& f : b.faces) n += strBytes(f.texture) - sizeof(std::string);
    n += b.decals.capacity() * sizeof(Decal);
    if (b.terrain) {
        n += b.terrain->heights.capacity() * sizeof(double) + b.terrain->layers.capacity() * sizeof(TerrainLayer);
        for (const TerrainLayer& l : b.terrain->layers)
            n += l.alpha.capacity() * sizeof(double) + strBytes(l.texture) - sizeof(std::string);
    }
    return n;
}
template <class T> std::size_t optBytes(const std::optional<T>& o, std::size_t (*f)(const T&)) { return o ? f(*o) : 0; }
std::size_t lightBytes(const Light& l) { return sizeof(Light) + keysBytes(l.otherKeys); }
std::size_t primBytes(const MathPrim& p) { return sizeof(MathPrim) + strBytes(p.texture) + keysBytes(p.otherKeys); }
std::size_t propBytes(const Prop& p) { return sizeof(Prop) + strBytes(p.model) + keysBytes(p.otherKeys); }
std::size_t cameraBytes(const Camera& c) { return sizeof(Camera) + strBytes(c.name) + keysBytes(c.otherKeys); }
std::size_t probeBytes(const Probe& p) { return sizeof(Probe) + keysBytes(p.otherKeys); }
std::size_t groupBytes(const Group& g) { return sizeof(Group) + keysBytes(g.keys); }
template <class S> std::size_t setBytes(const S& s) { return s.size() * 40; } // a set node, roughly
template <class S> std::size_t optSetBytes(const std::optional<S>& s) { return s ? setBytes(*s) : 0; }
} // namespace

std::size_t Document::commandBytes(const Command& c) {
    std::size_t n = sizeof(Command) + strBytes(c.name);
    for (const BrushChange& x : c.changes) n += sizeof(x) + optBytes(x.before, brushBytes) + optBytes(x.after, brushBytes);
    for (const GroupChange& x : c.groupChanges) n += sizeof(x) + optBytes(x.before, groupBytes) + optBytes(x.after, groupBytes);
    for (const LightChange& x : c.lightChanges) n += sizeof(x) + optBytes(x.before, lightBytes) + optBytes(x.after, lightBytes);
    for (const PrimChange& x : c.primChanges) n += sizeof(x) + optBytes(x.before, primBytes) + optBytes(x.after, primBytes);
    for (const PropChange& x : c.propChanges) n += sizeof(x) + optBytes(x.before, propBytes) + optBytes(x.after, propBytes);
    for (const CameraChange& x : c.cameraChanges) n += sizeof(x) + optBytes(x.before, cameraBytes) + optBytes(x.after, cameraBytes);
    for (const ProbeChange& x : c.probeChanges) n += sizeof(x) + optBytes(x.before, probeBytes) + optBytes(x.after, probeBytes);
    if (c.envBefore) n += sizeof(Environment);
    if (c.envAfter) n += sizeof(Environment);
    for (const FileChange& f : c.fileChanges) n += sizeof(f) + f.path.size() + (f.before ? f.before->size() : 0) + (f.after ? f.after->size() : 0);
    n += setBytes(c.selectionBefore) + setBytes(c.selectionAfter);
    n += optSetBytes(c.lightSelectionBefore) + optSetBytes(c.lightSelectionAfter);
    n += optSetBytes(c.primSelectionBefore) + optSetBytes(c.primSelectionAfter);
    n += optSetBytes(c.propSelectionBefore) + optSetBytes(c.propSelectionAfter);
    n += optSetBytes(c.cameraSelectionBefore) + optSetBytes(c.cameraSelectionAfter);
    n += optSetBytes(c.probeSelectionBefore) + optSetBytes(c.probeSelectionAfter);
    return n;
}

// Drops the oldest undo steps while the history is over its limit (the newest undo step
// stays). Redo steps only exist right after undos and are cleared by the next edit, so
// they are counted but never dropped here. "Unsaved changes" still works: the saved
// serial simply isn't in the history any more, so the map stays marked as changed.
void Document::trimHistory() {
    std::size_t drop = 0;
    while (drop + 1 < undo_.size() && historyBytes_ > historyLimit_) historyBytes_ -= undoBytes_[drop++];
    if (!drop) return;
    historyFloor_ = undo_[drop - 1].serial;
    undo_.erase(undo_.begin(), undo_.begin() + (long)drop);
    undoBytes_.erase(undoBytes_.begin(), undoBytes_.begin() + (long)drop);
}

void Document::setPrimsShown(bool on) {
    if (on == primsShown_) return;
    primsShown_ = on;
    if (!on) primSelection_.clear();
    bump();
}

void Document::setPropsShown(bool on) {
    if (on == propsShown_) return;
    propsShown_ = on;
    if (!on) propSelection_.clear();
    bump();
}

bool Document::undo() {
    if (undo_.empty()) return false;
    Command cmd = std::move(undo_.back());
    undo_.pop_back();
    redoBytes_.push_back(undoBytes_.back());
    undoBytes_.pop_back();
    for (auto it = cmd.changes.rbegin(); it != cmd.changes.rend(); ++it) setBrush(it->id, it->before);
    for (auto it = cmd.groupChanges.rbegin(); it != cmd.groupChanges.rend(); ++it) setGroup(it->id, it->before);
    for (auto it = cmd.lightChanges.rbegin(); it != cmd.lightChanges.rend(); ++it) setLight(it->id, it->before);
    for (auto it = cmd.primChanges.rbegin(); it != cmd.primChanges.rend(); ++it) setPrim(it->id, it->before);
    for (auto it = cmd.propChanges.rbegin(); it != cmd.propChanges.rend(); ++it) setProp(it->id, it->before);
    for (auto it = cmd.cameraChanges.rbegin(); it != cmd.cameraChanges.rend(); ++it) setCamera(it->id, it->before);
    for (auto it = cmd.probeChanges.rbegin(); it != cmd.probeChanges.rend(); ++it) setProbe(it->id, it->before);
    if (cmd.envBefore) { env_ = *cmd.envBefore; touchLighting(); }
    selection_ = cmd.selectionBefore;
    if (cmd.lightSelectionBefore) lightSelection_ = *cmd.lightSelectionBefore;
    if (cmd.primSelectionBefore) primSelection_ = *cmd.primSelectionBefore;
    if (cmd.propSelectionBefore) propSelection_ = *cmd.propSelectionBefore;
    if (cmd.cameraSelectionBefore) cameraSelection_ = *cmd.cameraSelectionBefore;
    if (cmd.probeSelectionBefore) probeSelection_ = *cmd.probeSelectionBefore;
    if (cmd.decalSelectionBefore) decalSelection_ = *cmd.decalSelectionBefore;
    pruneDecalSelection();
    if (!cmd.fileChanges.empty()) writeFiles(cmd.fileChanges, false);
    redo_.push_back(std::move(cmd));
    bump();
    return true;
}

bool Document::redo() {
    if (redo_.empty()) return false;
    Command cmd = std::move(redo_.back());
    redo_.pop_back();
    undoBytes_.push_back(redoBytes_.back());
    redoBytes_.pop_back();
    for (const GroupChange& g : cmd.groupChanges) setGroup(g.id, g.after);
    for (const BrushChange& c : cmd.changes) setBrush(c.id, c.after);
    for (const LightChange& c : cmd.lightChanges) setLight(c.id, c.after);
    for (const PrimChange& c : cmd.primChanges) setPrim(c.id, c.after);
    for (const PropChange& c : cmd.propChanges) setProp(c.id, c.after);
    for (const CameraChange& c : cmd.cameraChanges) setCamera(c.id, c.after);
    for (const ProbeChange& c : cmd.probeChanges) setProbe(c.id, c.after);
    if (cmd.envAfter) { env_ = *cmd.envAfter; touchLighting(); }
    selection_ = cmd.selectionAfter;
    if (cmd.lightSelectionAfter) lightSelection_ = *cmd.lightSelectionAfter;
    if (cmd.primSelectionAfter) primSelection_ = *cmd.primSelectionAfter;
    if (cmd.propSelectionAfter) propSelection_ = *cmd.propSelectionAfter;
    if (cmd.cameraSelectionAfter) cameraSelection_ = *cmd.cameraSelectionAfter;
    if (cmd.probeSelectionAfter) probeSelection_ = *cmd.probeSelectionAfter;
    if (cmd.decalSelectionAfter) decalSelection_ = *cmd.decalSelectionAfter;
    pruneDecalSelection();
    if (!cmd.fileChanges.empty()) writeFiles(cmd.fileChanges, true);
    undo_.push_back(std::move(cmd));
    bump();
    return true;
}

void Document::writeFiles(const std::vector<FileChange>& files, bool after) {
    namespace fs = std::filesystem;
    lastFiles_.clear();
    fileError_.clear();
    std::vector<std::string> kept; // changed outside Scalpellino since: left as they are
    auto addError = [this](const std::string& e) { fileError_ += (fileError_.empty() ? "" : "; ") + e; };
    for (const FileChange& f : files) {
        const std::optional<std::string>& text = after ? f.after : f.before;
        // What this command last left on disk (or found there): anything else was written by
        // someone else since, and an undo / redo must not overwrite or delete it.
        const std::optional<std::string>& expected = after ? f.before : f.after;
        std::optional<std::string> now;
        {
            std::error_code ec;
            if (fs::exists(f.path, ec)) {
                std::ifstream in(f.path, std::ios::binary);
                std::ostringstream ss;
                ss << in.rdbuf();
                now = ss.str();
            }
        }
        if (now != expected && now != text) {
            kept.push_back(fs::path(f.path).filename().string());
            continue;
        }
        std::error_code ec;
        lastFiles_.push_back(f.path);
        if (now == text) continue; // already as wanted
        if (!text) {
            fs::remove(f.path, ec);
            if (ec) addError("can't remove " + fs::path(f.path).filename().string());
            continue;
        }
        if (fs::path(f.path).has_parent_path()) fs::create_directories(fs::path(f.path).parent_path(), ec);
        std::ofstream o(f.path, std::ios::binary | std::ios::trunc);
        o << *text;
        if (!o) addError("can't write " + fs::path(f.path).filename().string());
    }
    if (!kept.empty()) {
        std::string names;
        for (const std::string& n : kept) names += (names.empty() ? "" : ", ") + n;
        addError(names + (kept.size() == 1 ? " was" : " were") + " changed outside Scalpellino since; left as " +
                 (kept.size() == 1 ? "it is" : "they are"));
    }
    ++filesStamp_;
}

Command Document::makeFileEdit(const std::vector<std::pair<std::string, std::optional<std::string>>>& files, const std::string& name) const {
    Command c;
    c.name = name;
    c.selectionBefore = c.selectionAfter = selection_;
    for (const auto& [path, text] : files) {
        FileChange fc;
        fc.path = path;
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            fc.before = ss.str();
        }
        fc.after = text;
        if (fc.before == fc.after) continue;
        c.fileChanges.push_back(std::move(fc));
    }
    return c;
}

const std::string& Document::undoName() const {
    static const std::string empty;
    return undo_.empty() ? empty : undo_.back().name;
}

void Document::reset(KeyValues world, std::vector<Brush> brushes, std::vector<RawEntity> others,
                     std::map<GroupId, Group> groups, std::vector<Visgroup> visgroups, WorldData extra) {
    worldKeys = std::move(world);
    lights_.clear();
    lightSelection_.clear();
    nextLightId_ = 1;
    for (Light& l : extra.lights) {
        l.id = nextLightId_++;
        lights_.push_back(std::move(l));
    }
    env_ = std::move(extra.env);
    prims_.clear();
    primSelection_.clear();
    nextPrimId_ = 1;
    for (MathPrim& p : extra.prims) {
        p.id = nextPrimId_++;
        prims_.push_back(std::move(p));
    }
    props_.clear();
    propSelection_.clear();
    nextPropId_ = 1;
    for (Prop& p : extra.props) {
        p.id = nextPropId_++;
        props_.push_back(std::move(p));
    }
    cameras_.clear();
    cameraSelection_.clear();
    nextCameraId_ = 1;
    for (Camera& c : extra.cameras) {
        c.id = nextCameraId_++;
        cameras_.push_back(std::move(c));
    }
    fixActiveCamera(cameras_); // a file with none or several active
    probes_.clear();
    probeSelection_.clear();
    nextProbeId_ = 1;
    for (Probe& p : extra.probes) {
        p.id = nextProbeId_++;
        probes_.push_back(std::move(p));
    }
    bookmarks_ = std::move(extra.bookmarks);
    figure_ = extra.figure;
    visgroups_ = std::move(visgroups);
    otherEntities = std::move(others);
    groups_ = std::move(groups);
    nextGroupId_ = 1;
    for (const auto& [id, g] : groups_) nextGroupId_ = std::max(nextGroupId_, id + 1);
    brushes_.clear();
    brushStamps_.clear();
    nextId_ = 1;
    decalSelection_.clear();
    // Decal ids follow the order the decals were placed in (equal orders are stacked by id).
    // Decals that come with an id (a file's seq, or another document's ids) keep their
    // relative order; those without one follow, in brush order.
    nextDecalId_ = 1;
    {
        std::vector<Decal*> all;
        for (Brush& b : brushes)
            for (Decal& d : b.decals) all.push_back(&d);
        std::stable_sort(all.begin(), all.end(), [](const Decal* a, const Decal* b) {
            const std::uint64_t ka = a->id ? a->id : ~std::uint64_t(0), kb = b->id ? b->id : ~std::uint64_t(0);
            return ka < kb;
        });
        for (Decal* d : all) d->id = nextDecalId_++;
    }
    for (Brush& b : brushes) {
        b.id = nextId_++;
        stampBrush(b.id);
        brushes_.push_back(std::move(b));
    }
    touchLighting();
    selection_.clear();
    undo_.clear();
    redo_.clear();
    undoBytes_.clear();
    redoBytes_.clear();
    historyBytes_ = 0;
    historyFloor_ = 0;
    bump();
    savedSerial_ = historyTop();
    sideEdits_ = false;
}

Command Document::makeAdd(std::vector<Brush> brushes, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = selection_;
    for (Brush& b : brushes) {
        if (b.id == 0) b.id = newId();
        c.selectionAfter.insert(b.id);
        c.changes.push_back({b.id, std::nullopt, std::move(b)});
    }
    return c;
}

Command Document::makeDelete(const std::set<BrushId>& ids, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = selection_;
    c.selectionAfter = selection_;
    for (BrushId id : ids) {
        if (const Brush* b = find(id)) {
            c.changes.push_back({id, *b, std::nullopt});
            c.selectionAfter.erase(id);
        }
    }
    return c;
}

Command Document::makeModify(const std::vector<Brush>& modified, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = selection_;
    c.selectionAfter = selection_;
    for (const Brush& b : modified)
        if (const Brush* old = find(b.id)) c.changes.push_back({b.id, *old, b});
    return c;
}

Command Document::makeAddLights(std::vector<Light> lights, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = selection_;
    c.lightSelectionBefore = lightSelection_;
    c.lightSelectionAfter = std::set<LightId>{};
    c.primSelectionBefore = primSelection_;
    c.primSelectionAfter = std::set<MathPrimId>{};
    c.propSelectionBefore = propSelection_;
    c.propSelectionAfter = std::set<PropId>{};
    c.cameraSelectionBefore = cameraSelection_;
    c.cameraSelectionAfter = std::set<CameraId>{};
    c.probeSelectionBefore = probeSelection_;
    c.probeSelectionAfter = std::set<ProbeId>{};
    c.decalSelectionBefore = decalSelection_;
    c.decalSelectionAfter = std::set<DecalId>{};
    for (Light& l : lights) {
        if (l.id == 0) l.id = newLightId();
        c.lightSelectionAfter->insert(l.id);
        c.lightChanges.push_back({l.id, std::nullopt, std::move(l)});
    }
    return c; // brushes deselected (selectionAfter empty)
}

Command Document::makeModifyLights(const std::vector<Light>& modified, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = c.selectionAfter = selection_;
    for (const Light& l : modified)
        if (const Light* old = findLight(l.id)) c.lightChanges.push_back({l.id, *old, l});
    return c;
}

void Document::addLightDeletes(Command& c, const std::set<LightId>& ids) const {
    std::set<LightId> after = c.lightSelectionAfter ? *c.lightSelectionAfter : lightSelection_;
    if (!c.lightSelectionBefore) c.lightSelectionBefore = lightSelection_;
    for (LightId id : ids)
        if (const Light* l = findLight(id)) {
            c.lightChanges.push_back({id, *l, std::nullopt});
            after.erase(id);
        }
    c.lightSelectionAfter = after;
}

const MathPrim* Document::findPrim(MathPrimId id) const {
    for (const MathPrim& p : prims_)
        if (p.id == id) return &p;
    return nullptr;
}

void Document::setPrim(MathPrimId id, const std::optional<MathPrim>& p) {
    auto it = std::find_if(prims_.begin(), prims_.end(), [&](const MathPrim& x) { return x.id == id; });
    if (p) {
        if (it != prims_.end()) *it = *p;
        else // kept in creation order, so undoing a delete puts it back where it was
            prims_.insert(std::lower_bound(prims_.begin(), prims_.end(), id,
                                           [](const MathPrim& x, MathPrimId v) { return x.id < v; }),
                          *p);
        if (id >= nextPrimId_) nextPrimId_ = id + 1;
    } else if (it != prims_.end()) {
        prims_.erase(it);
        primSelection_.erase(id);
    }
}

std::set<MathPrimId> Document::withCombos(const std::set<MathPrimId>& ids) const {
    std::set<MathPrimId> out = ids;
    std::set<std::uint32_t> combos;
    for (MathPrimId id : ids)
        if (const MathPrim* p = findPrim(id)) if (p->combo) combos.insert(p->combo);
    for (const MathPrim& p : prims_)
        if (p.combo && combos.count(p.combo)) out.insert(p.id);
    return out;
}

Command Document::makeAddPrims(std::vector<MathPrim> prims, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = selection_;
    c.lightSelectionBefore = lightSelection_;
    c.lightSelectionAfter = std::set<LightId>{};
    c.primSelectionBefore = primSelection_;
    c.primSelectionAfter = std::set<MathPrimId>{};
    c.propSelectionBefore = propSelection_;
    c.propSelectionAfter = std::set<PropId>{};
    c.cameraSelectionBefore = cameraSelection_;
    c.cameraSelectionAfter = std::set<CameraId>{};
    c.probeSelectionBefore = probeSelection_;
    c.probeSelectionAfter = std::set<ProbeId>{};
    c.decalSelectionBefore = decalSelection_;
    c.decalSelectionAfter = std::set<DecalId>{};
    for (MathPrim& p : prims) {
        if (p.id == 0) p.id = newPrimId();
        c.primSelectionAfter->insert(p.id);
        c.primChanges.push_back({p.id, std::nullopt, std::move(p)});
    }
    return c;
}

Command Document::makeModifyPrims(const std::vector<MathPrim>& modified, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = c.selectionAfter = selection_;
    for (const MathPrim& p : modified)
        if (const MathPrim* old = findPrim(p.id)) c.primChanges.push_back({p.id, *old, p});
    return c;
}

void Document::addPrimDeletes(Command& c, const std::set<MathPrimId>& ids) const {
    std::set<MathPrimId> after = c.primSelectionAfter ? *c.primSelectionAfter : primSelection_;
    if (!c.primSelectionBefore) c.primSelectionBefore = primSelection_;
    for (MathPrimId id : ids)
        if (const MathPrim* p = findPrim(id)) {
            c.primChanges.push_back({id, *p, std::nullopt});
            after.erase(id);
        }
    c.primSelectionAfter = after;
}

const Prop* Document::findProp(PropId id) const {
    for (const Prop& p : props_)
        if (p.id == id) return &p;
    return nullptr;
}

void Document::setProp(PropId id, const std::optional<Prop>& p) {
    auto it = std::find_if(props_.begin(), props_.end(), [&](const Prop& x) { return x.id == id; });
    if (p) {
        if (it != props_.end()) *it = *p;
        else // kept in creation order, so undoing a delete puts it back where it was
            props_.insert(std::lower_bound(props_.begin(), props_.end(), id,
                                           [](const Prop& x, PropId v) { return x.id < v; }),
                          *p);
        if (id >= nextPropId_) nextPropId_ = id + 1;
    } else if (it != props_.end()) {
        props_.erase(it);
        propSelection_.erase(id);
    }
}

Command Document::makeAddProps(std::vector<Prop> props, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = selection_;
    c.lightSelectionBefore = lightSelection_;
    c.lightSelectionAfter = std::set<LightId>{};
    c.primSelectionBefore = primSelection_;
    c.primSelectionAfter = std::set<MathPrimId>{};
    c.propSelectionBefore = propSelection_;
    c.propSelectionAfter = std::set<PropId>{};
    c.cameraSelectionBefore = cameraSelection_;
    c.cameraSelectionAfter = std::set<CameraId>{};
    c.probeSelectionBefore = probeSelection_;
    c.probeSelectionAfter = std::set<ProbeId>{};
    c.decalSelectionBefore = decalSelection_;
    c.decalSelectionAfter = std::set<DecalId>{};
    for (Prop& p : props) {
        if (p.id == 0) p.id = newPropId();
        c.propSelectionAfter->insert(p.id);
        c.propChanges.push_back({p.id, std::nullopt, std::move(p)});
    }
    return c;
}

Command Document::makeModifyProps(const std::vector<Prop>& modified, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = c.selectionAfter = selection_;
    for (const Prop& p : modified)
        if (const Prop* old = findProp(p.id)) c.propChanges.push_back({p.id, *old, p});
    return c;
}

void Document::addPropDeletes(Command& c, const std::set<PropId>& ids) const {
    std::set<PropId> after = c.propSelectionAfter ? *c.propSelectionAfter : propSelection_;
    if (!c.propSelectionBefore) c.propSelectionBefore = propSelection_;
    for (PropId id : ids)
        if (const Prop* p = findProp(id)) {
            c.propChanges.push_back({id, *p, std::nullopt});
            after.erase(id);
        }
    c.propSelectionAfter = after;
}

Command Document::makeEnvironment(const Environment& env, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = c.selectionAfter = selection_;
    c.envBefore = env_;
    c.envAfter = env;
    return c;
}

Command Document::makeChange(const std::vector<Brush>& modified, std::vector<Brush> added,
                             const std::set<BrushId>& deleted, const std::string& name,
                             const std::set<BrushId>* selectAfter) {
    Command c;
    c.name = name;
    c.selectionBefore = selection_;
    c.selectionAfter = selection_;
    for (const Brush& b : modified)
        if (const Brush* old = find(b.id)) c.changes.push_back({b.id, *old, b});
    for (BrushId id : deleted)
        if (const Brush* old = find(id)) {
            c.changes.push_back({id, *old, std::nullopt});
            c.selectionAfter.erase(id);
        }
    for (Brush& b : added) {
        b.id = newId();
        c.selectionAfter.insert(b.id);
        c.changes.push_back({b.id, std::nullopt, std::move(b)});
    }
    if (selectAfter) c.selectionAfter = *selectAfter;
    return c;
}

const Camera* Document::findCamera(CameraId id) const {
    for (const Camera& c : cameras_)
        if (c.id == id) return &c;
    return nullptr;
}

const Camera* Document::activeCamera() const {
    for (const Camera& c : cameras_)
        if (c.active) return &c;
    return nullptr;
}

void Document::setCamera(CameraId id, const std::optional<Camera>& c) {
    auto it = std::find_if(cameras_.begin(), cameras_.end(), [&](const Camera& x) { return x.id == id; });
    if (c) {
        if (it != cameras_.end()) *it = *c;
        else // kept in creation order, so undoing a delete puts it back where it was
            cameras_.insert(std::lower_bound(cameras_.begin(), cameras_.end(), id,
                                             [](const Camera& x, CameraId v) { return x.id < v; }),
                            *c);
        if (id >= nextCameraId_) nextCameraId_ = id + 1;
    } else if (it != cameras_.end()) {
        cameras_.erase(it);
        cameraSelection_.erase(id);
    }
}

void Document::keepOneActive(Command& c, CameraId prefer) const {
    // The cameras as they will be after the command, in id order.
    std::map<CameraId, Camera> after;
    for (const Camera& x : cameras_) after[x.id] = x;
    for (const CameraChange& ch : c.cameraChanges) {
        if (ch.after) after[ch.id] = *ch.after;
        else after.erase(ch.id);
    }
    std::vector<Camera> list;
    if (prefer && after.count(prefer) && after[prefer].active) list.push_back(after[prefer]);
    for (const auto& [id, x] : after) if (id != prefer || list.empty()) list.push_back(x);
    for (CameraId id : fixActiveCamera(list)) {
        const Camera* fixed = nullptr;
        for (const Camera& x : list) if (x.id == id) fixed = &x;
        bool merged = false;
        for (CameraChange& ch : c.cameraChanges)
            if (ch.id == id && ch.after) { ch.after->active = fixed->active; merged = true; }
        if (!merged) c.cameraChanges.push_back({id, *findCamera(id), *fixed});
    }
}

Command Document::makeAddCameras(std::vector<Camera> cams, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = selection_;
    c.lightSelectionBefore = lightSelection_;
    c.lightSelectionAfter = std::set<LightId>{};
    c.primSelectionBefore = primSelection_;
    c.primSelectionAfter = std::set<MathPrimId>{};
    c.propSelectionBefore = propSelection_;
    c.propSelectionAfter = std::set<PropId>{};
    c.cameraSelectionBefore = cameraSelection_;
    c.cameraSelectionAfter = std::set<CameraId>{};
    c.probeSelectionBefore = probeSelection_;
    c.probeSelectionAfter = std::set<ProbeId>{};
    c.decalSelectionBefore = decalSelection_;
    c.decalSelectionAfter = std::set<DecalId>{};
    const bool hadActive = activeCamera() != nullptr;
    for (Camera& x : cams) {
        if (x.id == 0) x.id = newCameraId();
        if (hadActive) x.active = false; // new cameras never take over (duplicates included)
        c.cameraSelectionAfter->insert(x.id);
        c.cameraChanges.push_back({x.id, std::nullopt, std::move(x)});
    }
    keepOneActive(c);
    return c;
}

Command Document::makeModifyCameras(const std::vector<Camera>& modified, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = c.selectionAfter = selection_;
    CameraId newlyActive = 0;
    for (const Camera& x : modified)
        if (const Camera* old = findCamera(x.id)) {
            c.cameraChanges.push_back({x.id, *old, x});
            if (x.active && !old->active) newlyActive = x.id;
        }
    if (newlyActive) // it wins: every other camera is switched off
        for (const Camera& x : cameras_) {
            if (x.id == newlyActive || !x.active) continue;
            bool merged = false;
            for (CameraChange& ch : c.cameraChanges)
                if (ch.id == x.id && ch.after) { ch.after->active = false; merged = true; }
            if (!merged) { Camera off = x; off.active = false; c.cameraChanges.push_back({x.id, x, off}); }
        }
    keepOneActive(c, newlyActive);
    return c;
}

Command Document::makeActivateCamera(CameraId id) {
    std::vector<Camera> mod;
    if (const Camera* x = findCamera(id)) { Camera a = *x; a.active = true; mod.push_back(a); }
    Command c = makeModifyCameras(mod, "Make camera active");
    // Nothing to do if it already was: keep the command empty.
    if (!c.cameraChanges.empty() && c.cameraChanges[0].before == c.cameraChanges[0].after && c.cameraChanges.size() == 1)
        c.cameraChanges.clear();
    return c;
}

void Document::addCameraDeletes(Command& c, const std::set<CameraId>& ids) const {
    std::set<CameraId> after = c.cameraSelectionAfter ? *c.cameraSelectionAfter : cameraSelection_;
    if (!c.cameraSelectionBefore) c.cameraSelectionBefore = cameraSelection_;
    for (CameraId id : ids)
        if (const Camera* x = findCamera(id)) {
            c.cameraChanges.push_back({id, *x, std::nullopt});
            after.erase(id);
        }
    c.cameraSelectionAfter = after;
    keepOneActive(c);
}

} // namespace scal

namespace scal {

const Probe* Document::findProbe(ProbeId id) const {
    for (const Probe& p : probes_)
        if (p.id == id) return &p;
    return nullptr;
}

void Document::setProbe(ProbeId id, const std::optional<Probe>& p) {
    auto it = std::find_if(probes_.begin(), probes_.end(), [&](const Probe& x) { return x.id == id; });
    if (p) {
        if (it != probes_.end()) *it = *p;
        else {
            probes_.insert(std::lower_bound(probes_.begin(), probes_.end(), id, [](const Probe& x, ProbeId v) { return x.id < v; }), *p);
        }
        if (id >= nextProbeId_) nextProbeId_ = id + 1;
    } else if (it != probes_.end()) {
        probes_.erase(it);
        probeSelection_.erase(id);
    }
}

Command Document::makeAddProbes(std::vector<Probe> probes, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = selection_;
    c.selectionAfter = {};
    c.lightSelectionBefore = lightSelection_;
    c.lightSelectionAfter = std::set<LightId>{};
    c.primSelectionBefore = primSelection_;
    c.primSelectionAfter = std::set<MathPrimId>{};
    c.propSelectionBefore = propSelection_;
    c.propSelectionAfter = std::set<PropId>{};
    c.cameraSelectionBefore = cameraSelection_;
    c.cameraSelectionAfter = std::set<CameraId>{};
    c.decalSelectionBefore = decalSelection_;
    c.decalSelectionAfter = std::set<DecalId>{};
    c.probeSelectionBefore = probeSelection_;
    c.probeSelectionAfter = std::set<ProbeId>{};
    for (Probe& p : probes) {
        if (p.id == 0) p.id = newProbeId();
        c.probeSelectionAfter->insert(p.id);
        c.probeChanges.push_back({p.id, std::nullopt, std::move(p)});
    }
    return c;
}

Command Document::makeModifyProbes(const std::vector<Probe>& modified, const std::string& name) {
    Command c;
    c.name = name;
    c.selectionBefore = c.selectionAfter = selection_;
    for (const Probe& p : modified)
        if (const Probe* old = findProbe(p.id))
            if (*old != p) c.probeChanges.push_back({p.id, *old, p});
    return c;
}

void Document::addProbeDeletes(Command& c, const std::set<ProbeId>& ids) const {
    std::set<ProbeId> after = c.probeSelectionAfter ? *c.probeSelectionAfter : probeSelection_;
    if (!c.probeSelectionBefore) c.probeSelectionBefore = probeSelection_;
    for (ProbeId id : ids)
        if (const Probe* p = findProbe(id)) {
            c.probeChanges.push_back({id, *p, std::nullopt});
            after.erase(id);
        }
    c.probeSelectionAfter = after;
}

} // namespace scal
