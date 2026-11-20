#include "core/document.hpp"
#include <algorithm>
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

void Document::setBrush(BrushId id, const std::optional<Brush>& b) {
    auto it = std::find_if(brushes_.begin(), brushes_.end(), [&](const Brush& x) { return x.id == id; });
    if (b) {
        if (it != brushes_.end()) *it = *b;
        else brushes_.push_back(*b);
        if (id >= nextId_) nextId_ = id + 1;
    } else if (it != brushes_.end()) {
        brushes_.erase(it);
        selection_.erase(id);
    }
}

void Document::apply(Command cmd) {
    pruneEmptyGroups(cmd); // a group whose last brush goes away goes with it (undoable)
    for (const GroupChange& g : cmd.groupChanges) setGroup(g.id, g.after);
    for (const BrushChange& c : cmd.changes) setBrush(c.id, c.after);
    for (const LightChange& c : cmd.lightChanges) setLight(c.id, c.after);
    for (const PrimChange& c : cmd.primChanges) setPrim(c.id, c.after);
    for (const PropChange& c : cmd.propChanges) setProp(c.id, c.after);
    for (const CameraChange& c : cmd.cameraChanges) setCamera(c.id, c.after);
    if (cmd.envAfter) env_ = *cmd.envAfter;
    selection_ = cmd.selectionAfter;
    if (cmd.lightSelectionAfter) lightSelection_ = *cmd.lightSelectionAfter;
    if (cmd.primSelectionAfter) primSelection_ = *cmd.primSelectionAfter;
    if (cmd.propSelectionAfter) propSelection_ = *cmd.propSelectionAfter;
    if (cmd.cameraSelectionAfter) cameraSelection_ = *cmd.cameraSelectionAfter;
    cmd.serial = ++nextSerial_;
    // New primitives or props show them again if they were hidden.
    for (const PrimChange& c : cmd.primChanges) if (c.after && !c.before) primsShown_ = true;
    for (const PropChange& c : cmd.propChanges) if (c.after && !c.before) propsShown_ = true;
    undo_.push_back(std::move(cmd));
    redo_.clear();
    bump();
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
    for (auto it = cmd.changes.rbegin(); it != cmd.changes.rend(); ++it) setBrush(it->id, it->before);
    for (auto it = cmd.groupChanges.rbegin(); it != cmd.groupChanges.rend(); ++it) setGroup(it->id, it->before);
    for (auto it = cmd.lightChanges.rbegin(); it != cmd.lightChanges.rend(); ++it) setLight(it->id, it->before);
    for (auto it = cmd.primChanges.rbegin(); it != cmd.primChanges.rend(); ++it) setPrim(it->id, it->before);
    for (auto it = cmd.propChanges.rbegin(); it != cmd.propChanges.rend(); ++it) setProp(it->id, it->before);
    for (auto it = cmd.cameraChanges.rbegin(); it != cmd.cameraChanges.rend(); ++it) setCamera(it->id, it->before);
    if (cmd.envBefore) env_ = *cmd.envBefore;
    selection_ = cmd.selectionBefore;
    if (cmd.lightSelectionBefore) lightSelection_ = *cmd.lightSelectionBefore;
    if (cmd.primSelectionBefore) primSelection_ = *cmd.primSelectionBefore;
    if (cmd.propSelectionBefore) propSelection_ = *cmd.propSelectionBefore;
    if (cmd.cameraSelectionBefore) cameraSelection_ = *cmd.cameraSelectionBefore;
    redo_.push_back(std::move(cmd));
    bump();
    return true;
}

bool Document::redo() {
    if (redo_.empty()) return false;
    Command cmd = std::move(redo_.back());
    redo_.pop_back();
    for (const GroupChange& g : cmd.groupChanges) setGroup(g.id, g.after);
    for (const BrushChange& c : cmd.changes) setBrush(c.id, c.after);
    for (const LightChange& c : cmd.lightChanges) setLight(c.id, c.after);
    for (const PrimChange& c : cmd.primChanges) setPrim(c.id, c.after);
    for (const PropChange& c : cmd.propChanges) setProp(c.id, c.after);
    for (const CameraChange& c : cmd.cameraChanges) setCamera(c.id, c.after);
    if (cmd.envAfter) env_ = *cmd.envAfter;
    selection_ = cmd.selectionAfter;
    if (cmd.lightSelectionAfter) lightSelection_ = *cmd.lightSelectionAfter;
    if (cmd.primSelectionAfter) primSelection_ = *cmd.primSelectionAfter;
    if (cmd.propSelectionAfter) propSelection_ = *cmd.propSelectionAfter;
    if (cmd.cameraSelectionAfter) cameraSelection_ = *cmd.cameraSelectionAfter;
    undo_.push_back(std::move(cmd));
    bump();
    return true;
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
    bookmarks_ = std::move(extra.bookmarks);
    figure_ = extra.figure;
    visgroups_ = std::move(visgroups);
    otherEntities = std::move(others);
    groups_ = std::move(groups);
    nextGroupId_ = 1;
    for (const auto& [id, g] : groups_) nextGroupId_ = std::max(nextGroupId_, id + 1);
    brushes_.clear();
    nextId_ = 1;
    for (Brush& b : brushes) {
        b.id = nextId_++;
        brushes_.push_back(std::move(b));
    }
    selection_.clear();
    undo_.clear();
    redo_.clear();
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
