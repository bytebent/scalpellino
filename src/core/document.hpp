#pragma once
// The map being edited: world properties, world brushes, other entities (preserved),
// selection, and undo history. All edits go through Document::apply(Command).
#include "core/brush.hpp"
#include "core/world.hpp"
#include "core/mathprim.hpp"
#include "core/prop.hpp"
#include "core/camera.hpp"
#include "core/probe.hpp"
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace scal {

using KeyValues = std::vector<std::pair<std::string, std::string>>;

// An entity we don't edit yet (kept so saving never loses data).
struct RawEntity {
    KeyValues keys;
    std::vector<Brush> brushes;
};

// A group of brushes made by a generator (curve tool). `keys` hold the generator's
// parameters so the group can be regenerated; baking removes the group.
using GroupId = std::uint32_t;
struct Group {
    GroupId id = 0;
    KeyValues keys;
};

struct GroupChange {
    GroupId id = 0;
    std::optional<Group> before;
    std::optional<Group> after;
};

// Named visibility group (Hammer "visgroup"). Toggling `visible` hides/shows its
// brushes in every view. Not part of undo (like Hammer), but saved with the map.
struct Visgroup {
    std::uint32_t id = 0;
    std::string name;
    bool visible = true;
};

// One brush before/after an edit. Missing `before` = created; missing `after` = deleted.
struct BrushChange {
    BrushId id = 0;
    std::optional<Brush> before;
    std::optional<Brush> after;
};

// One light before/after an edit (same rules as BrushChange).
struct LightChange {
    LightId id = 0;
    std::optional<Light> before;
    std::optional<Light> after;
};

// One math primitive before/after an edit.
struct PrimChange {
    MathPrimId id = 0;
    std::optional<MathPrim> before;
    std::optional<MathPrim> after;
};

// One prop before/after an edit.
struct PropChange {
    PropId id = 0;
    std::optional<Prop> before;
    std::optional<Prop> after;
};

// One camera before/after an edit.
struct CameraChange {
    CameraId id = 0;
    std::optional<Camera> before;
    std::optional<Camera> after;
};

// One reflection probe before/after an edit (v0.12.6).
struct ProbeChange {
    ProbeId id = 0;
    std::optional<Probe> before;
    std::optional<Probe> after;
};

// A file the command writes (materials, v0.12.4): its text before and after (nullopt = no
// file). Material files aren't part of the map: commands holding only file changes don't
// mark the map as changed.
struct FileChange {
    std::string path;
    std::optional<std::string> before, after;
};

struct Command {
    std::string name;
    std::vector<BrushChange> changes;
    std::set<BrushId> selectionBefore;
    std::set<BrushId> selectionAfter;
    std::vector<GroupChange> groupChanges;
    std::vector<LightChange> lightChanges;
    // Light selection around the command (unset = left as it is).
    std::optional<std::set<LightId>> lightSelectionBefore, lightSelectionAfter;
    std::optional<Environment> envBefore, envAfter; // set = the command changes the environment
    std::vector<PrimChange> primChanges;
    std::optional<std::set<MathPrimId>> primSelectionBefore, primSelectionAfter;
    std::vector<PropChange> propChanges;
    std::optional<std::set<PropId>> propSelectionBefore, propSelectionAfter;
    std::vector<CameraChange> cameraChanges;
    std::optional<std::set<CameraId>> cameraSelectionBefore, cameraSelectionAfter;
    std::vector<ProbeChange> probeChanges;
    std::optional<std::set<ProbeId>> probeSelectionBefore, probeSelectionAfter;
    // Decals live in brushes (their edits are brush changes); their selection (v0.12.5).
    std::optional<std::set<DecalId>> decalSelectionBefore, decalSelectionAfter;
    std::vector<FileChange> fileChanges;
    std::uint64_t serial = 0; // set by Document::apply: which edit this is (for "saved?")
    // Only files change (nothing of the map itself).
    bool filesOnly() const {
        return !fileChanges.empty() && changes.empty() && groupChanges.empty() && lightChanges.empty() && !envAfter &&
               primChanges.empty() && propChanges.empty() && cameraChanges.empty() && probeChanges.empty();
    }
};

// Everything besides brushes that a loaded map brings (milestone 8).
struct WorldData {
    std::vector<Light> lights;
    Environment env;
    std::vector<CameraBookmark> bookmarks;
    std::optional<Vec3> figure; // scale figure position (editor-only)
    std::vector<MathPrim> prims;
    std::vector<Prop> props;
    std::vector<Camera> cameras;
    std::vector<Probe> probes;
};

class Document {
public:
    KeyValues worldKeys{{"classname", "worldspawn"}, {"mapversion", "220"}};
    std::vector<RawEntity> otherEntities;

    const std::vector<Brush>& brushes() const { return brushes_; }
    const Brush* find(BrushId id) const;
    BrushId newId() { return nextId_++; }

    // Generated groups.
    const std::map<GroupId, Group>& groups() const { return groups_; }
    const Group* group(GroupId id) const;
    GroupId newGroupId() { return nextGroupId_++; }
    std::set<BrushId> groupMembers(GroupId id) const;
    // Adds every member of any group touched by `ids` (groups select as a whole).
    std::set<BrushId> expandToGroups(const std::set<BrushId>& ids) const;

    // Visibility.
    const std::vector<Visgroup>& visgroups() const { return visgroups_; }
    void setVisgroups(std::vector<Visgroup> v) { visgroups_ = std::move(v); sideEdit(); }
    std::uint32_t newVisgroupId() const;
    bool visible(const Brush& b) const;
    bool visible(BrushId id) const { const Brush* b = find(id); return b && visible(*b); }

    const std::set<BrushId>& selection() const { return selection_; }
    void setSelection(std::set<BrushId> s) { selection_ = std::move(s); }

    // Lights (milestone 8). Selected separately from brushes; tools clear one when a
    // plain click picks the other.
    const std::vector<Light>& lights() const { return lights_; }
    const Light* findLight(LightId id) const;
    LightId newLightId() { return nextLightId_++; }
    const std::set<LightId>& lightSelection() const { return lightSelection_; }
    void setLightSelection(std::set<LightId> s) { lightSelection_ = std::move(s); }

    // Math primitives (milestone 9). Selected on their own like lights.
    const std::vector<MathPrim>& prims() const { return prims_; }
    const MathPrim* findPrim(MathPrimId id) const;
    MathPrimId newPrimId() { return nextPrimId_++; }
    const std::set<MathPrimId>& primSelection() const { return primSelection_; }
    void setPrimSelection(std::set<MathPrimId> s) { primSelection_ = std::move(s); }
    // All members of the combinations the given primitives are in.
    std::set<MathPrimId> withCombos(const std::set<MathPrimId>& ids) const;

    // Props (milestone 10): placed models. Selected on their own like primitives.
    const std::vector<Prop>& props() const { return props_; }
    const Prop* findProp(PropId id) const;
    PropId newPropId() { return nextPropId_++; }
    const std::set<PropId>& propSelection() const { return propSelection_; }
    void setPropSelection(std::set<PropId> s) { propSelection_ = std::move(s); }

    // Cameras (milestone 11). Selected on their own like lights. Exactly one is active
    // whenever there are any: the camera commands below keep it so.
    const std::vector<Camera>& cameras() const { return cameras_; }
    const Camera* findCamera(CameraId id) const;
    const Camera* activeCamera() const;
    CameraId newCameraId() { return nextCameraId_++; }
    const std::set<CameraId>& cameraSelection() const { return cameraSelection_; }
    void setCameraSelection(std::set<CameraId> s) { cameraSelection_ = std::move(s); }

    // Reflection probes (v0.12.6): selected on their own like lights.
    const std::vector<Probe>& probes() const { return probes_; }
    const Probe* findProbe(ProbeId id) const;
    ProbeId newProbeId() { return nextProbeId_++; }
    const std::set<ProbeId>& probeSelection() const { return probeSelection_; }
    void setProbeSelection(std::set<ProbeId> s) { probeSelection_ = std::move(s); }

    // Decals (v0.12.5): kept in the brush they were put on (Brush::decals), selected on their
    // own like props. Edits are brush changes, so undo, copies and deletes follow the brush.
    struct DecalRef {
        const Brush* brush = nullptr;
        const Decal* decal = nullptr;
        explicit operator bool() const { return decal != nullptr; }
    };
    DecalRef findDecal(DecalId id) const;
    DecalId newDecalId() { return nextDecalId_++; }
    const std::set<DecalId>& decalSelection() const { return decalSelection_; }
    void setDecalSelection(std::set<DecalId> s) { decalSelection_ = std::move(s); }
    // Shown: its brush shown, decals shown (View), its own visgroup shown.
    bool visible(const Brush& b, const Decal& d) const;
    bool decalsShown() const { return decalsShown_; }
    void setDecalsShown(bool on);
    size_t decalCount() const;

    // View options (v0.10.2): all primitives / all props shown or hidden. Hidden ones are
    // not drawn, picked, selected or landed on; hiding drops them from the selection.
    // Not saved and not undone; adding new ones shows them again.
    bool primsShown() const { return primsShown_; }
    bool propsShown() const { return propsShown_; }
    void setPrimsShown(bool on);
    void setPropsShown(bool on);

    // Environment (sky, ambient, fog): changed through commands (makeEnvironment).
    const Environment& environment() const { return env_; }
    // Camera bookmarks and the scale figure: editor-only, saved with the map, not undone
    // (like visgroups).
    const std::vector<CameraBookmark>& bookmarks() const { return bookmarks_; }
    void setBookmarks(std::vector<CameraBookmark> b) { bookmarks_ = std::move(b); sideEdit(); }
    const std::optional<Vec3>& figure() const { return figure_; }
    void setFigure(std::optional<Vec3> f) { figure_ = f; sideEdit(); }
    AABB selectionBounds() const;

    // Applies a command and records it for undo. Clears redo.
    void apply(Command cmd);
    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    size_t undoCount() const { return undo_.size(); }
    bool canRedo() const { return !redo_.empty(); }
    const std::string& undoName() const;
    // Files written by commands (materials): changes with every apply / undo / redo that
    // writes one; the paths of the last ones; the last write error ("" none).
    std::uint64_t filesStamp() const { return filesStamp_; }
    const std::vector<std::string>& lastFiles() const { return lastFiles_; }
    const std::string& fileError() const { return fileError_; }
    // A material edit: the files' new texts (nullopt = delete); the old ones are read now.
    Command makeFileEdit(const std::vector<std::pair<std::string, std::optional<std::string>>>& files, const std::string& name) const;

    // Changes since last save: undoing back to the saved state counts as no change
    // (v0.10.2). Visgroups, bookmarks and the figure aren't undoable: changing them stays
    // a change until the next save.
    bool dirty() const { return sideEdits_ || historyTop() != savedSerial_; }
    void markSaved() { savedSerial_ = historyTop(); sideEdits_ = false; }
    // Increments on every change (renderers use it to know when to rebuild).
    unsigned long long revision() const { return revision_; }
    // Which edit last touched this brush (0 = no such brush). Caches of drawing data keep
    // it per brush and rebuild only the brushes whose stamp changed, so an edit, undo or
    // redo costs what it touched, not the whole map (ARCHITECTURE §4, edit flow).
    std::uint64_t brushStamp(BrushId id) const {
        auto it = brushStamps_.find(id);
        return it == brushStamps_.end() ? 0 : it->second;
    }
    // Changes whenever a light or the environment changes (the light preview's input
    // besides the brushes).
    std::uint64_t lightingStamp() const { return lightingStamp_; }

    // Memory the undo and redo history may use (the brushes, terrain and objects each
    // step keeps before and after); the oldest steps are dropped first. The newest step
    // always stays, however big. Taken from Mazzuola (same 64 MB).
    static constexpr std::size_t kHistoryBytes = 64u * 1024 * 1024;
    std::size_t historyBytes() const { return historyBytes_; }
    void setHistoryLimit(std::size_t bytes) { historyLimit_ = bytes; trimHistory(); } // tests
    // Rough size of a command in memory.
    static std::size_t commandBytes(const Command& c);

    // Replaces all content (used when loading). Clears history.
    // Brush `group` fields must refer to entries of `groups` (ids are kept).
    void reset(KeyValues world, std::vector<Brush> brushes, std::vector<RawEntity> others,
               std::map<GroupId, Group> groups = {}, std::vector<Visgroup> visgroups = {},
               WorldData extra = {});

    // Helpers that build commands.
    Command makeAdd(std::vector<Brush> brushes, const std::string& name);
    Command makeDelete(const std::set<BrushId>& ids, const std::string& name);
    Command makeModify(const std::vector<Brush>& modified, const std::string& name);
    // Lights: new ones get ids and become the light selection (brushes deselected).
    Command makeAddLights(std::vector<Light> lights, const std::string& name);
    Command makeModifyLights(const std::vector<Light>& modified, const std::string& name);
    // Adds deleting these lights to any command (e.g. Delete removes brushes and lights).
    void addLightDeletes(Command& c, const std::set<LightId>& ids) const;
    Command makeEnvironment(const Environment& env, const std::string& name);
    // Math primitives: new ones get ids and become the primitive selection (brushes and
    // lights deselected).
    Command makeAddPrims(std::vector<MathPrim> prims, const std::string& name);
    Command makeModifyPrims(const std::vector<MathPrim>& modified, const std::string& name);
    void addPrimDeletes(Command& c, const std::set<MathPrimId>& ids) const;
    // Props: new ones get ids and become the prop selection (everything else deselected).
    Command makeAddProps(std::vector<Prop> props, const std::string& name);
    // Decals: a new one gets an id, goes into brush `on` and becomes the decal selection
    // (everything else deselected). Modified ones replace theirs in their brushes.
    Command makeAddDecal(Decal d, BrushId on, const std::string& name);
    Command makeModifyDecals(const std::vector<Decal>& modified, const std::string& name);
    // Adds deleting these decals to any command (brushes it already deletes are left).
    void addDecalDeletes(Command& c, const std::set<DecalId>& ids) const;
    Command makeModifyProps(const std::vector<Prop>& modified, const std::string& name);
    void addPropDeletes(Command& c, const std::set<PropId>& ids) const;
    // Cameras: new ones get ids and become the camera selection (everything else
    // deselected); the first camera of a map becomes the active one. Making one active
    // (in a modify, or with makeActivateCamera) deactivates the others; deleting the active
    // one makes the first that is left active.
    Command makeAddCameras(std::vector<Camera> cams, const std::string& name);
    Command makeModifyCameras(const std::vector<Camera>& modified, const std::string& name);
    Command makeActivateCamera(CameraId id);
    void addCameraDeletes(Command& c, const std::set<CameraId>& ids) const;
    // Reflection probes: new ones get ids and become the probe selection (everything else
    // deselected).
    Command makeAddProbes(std::vector<Probe> probes, const std::string& name);
    Command makeModifyProbes(const std::vector<Probe>& modified, const std::string& name);
    void addProbeDeletes(Command& c, const std::set<ProbeId>& ids) const;
    // Mixed edit as one undo step (clip, carve, hollow). New brushes get ids. The
    // selection afterwards is `selectAfter` if given, else the old selection minus
    // deleted brushes plus the added ones.
    Command makeChange(const std::vector<Brush>& modified, std::vector<Brush> added,
                       const std::set<BrushId>& deleted, const std::string& name,
                       const std::set<BrushId>* selectAfter = nullptr);

private:
    void setBrush(BrushId id, const std::optional<Brush>& b);
    void setGroup(GroupId id, const std::optional<Group>& g);
    void setLight(LightId id, const std::optional<Light>& l);
    void setPrim(MathPrimId id, const std::optional<MathPrim>& p);
    void setProp(PropId id, const std::optional<Prop>& p);
    void setCamera(CameraId id, const std::optional<Camera>& c);
    void setProbe(ProbeId id, const std::optional<Probe>& p);
    // Adds the changes that leave exactly one active camera after `c` (`prefer`: the one
    // to keep active when several would be).
    void keepOneActive(Command& c, CameraId prefer = 0) const;
    // Adds group removals to `c` for groups that would have no brushes left.
    void pruneEmptyGroups(Command& c) const;
    // Decals after a new command: copies of decals that stay elsewhere get new ids; a decal
    // in several new pieces of a brush (clip, carve) stays only in the one nearest to it.
    void normalizeDecals(Command& c);
    void pruneDecalSelection();
    void bump() { ++revision_; }
    void stampBrush(BrushId id) { brushStamps_[id] = ++stampCounter_; }
    void touchLighting() { lightingStamp_ = ++stampCounter_; }
    void trimHistory();
    void sideEdit() { bump(); sideEdits_ = true; }
    // Serial of the state the history's top undoes to; with every step dropped by the memory
    // limit, the newest dropped one (so undoing all the way doesn't look like the saved state).
    void writeFiles(const std::vector<FileChange>& files, bool after);
    std::uint64_t filesStamp_ = 0;
    std::vector<std::string> lastFiles_;
    std::string fileError_;
    std::uint64_t historyTop() const { return undo_.empty() ? historyFloor_ : undo_.back().serial; }

    std::vector<Brush> brushes_;
    std::set<BrushId> selection_;
    BrushId nextId_ = 1;
    std::map<GroupId, Group> groups_;
    std::vector<Visgroup> visgroups_;
    GroupId nextGroupId_ = 1;
    std::vector<MathPrim> prims_;
    std::set<MathPrimId> primSelection_;
    MathPrimId nextPrimId_ = 1;
    std::vector<Prop> props_;
    std::set<PropId> propSelection_;
    PropId nextPropId_ = 1;
    std::set<DecalId> decalSelection_;
    DecalId nextDecalId_ = 1;
    bool decalsShown_ = true;
    std::vector<Camera> cameras_;
    std::set<CameraId> cameraSelection_;
    CameraId nextCameraId_ = 1;
    std::vector<Probe> probes_;
    std::set<ProbeId> probeSelection_;
    ProbeId nextProbeId_ = 1;
    std::vector<Light> lights_;
    std::set<LightId> lightSelection_;
    LightId nextLightId_ = 1;
    Environment env_;
    std::vector<CameraBookmark> bookmarks_;
    std::optional<Vec3> figure_;
    std::vector<Command> undo_, redo_;
    std::vector<std::size_t> undoBytes_, redoBytes_; // commandBytes of each step
    std::size_t historyBytes_ = 0, historyLimit_ = kHistoryBytes;
    std::unordered_map<BrushId, std::uint64_t> brushStamps_;
    std::uint64_t stampCounter_ = 0, lightingStamp_ = 0;
    unsigned long long revision_ = 0;
    std::uint64_t nextSerial_ = 0, savedSerial_ = 0, historyFloor_ = 0;
    bool sideEdits_ = false;
    bool primsShown_ = true, propsShown_ = true;
};

} // namespace scal
