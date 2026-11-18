#pragma once
// The map being edited: world properties, world brushes, other entities (preserved),
// selection, and undo history. All edits go through Document::apply(Command).
#include "core/brush.hpp"
#include "core/world.hpp"
#include "core/mathprim.hpp"
#include <map>
#include <optional>
#include <set>
#include <string>
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
};

// Everything besides brushes that a loaded map brings (milestone 8).
struct WorldData {
    std::vector<Light> lights;
    Environment env;
    std::vector<CameraBookmark> bookmarks;
    std::optional<Vec3> figure; // scale figure position (editor-only)
    std::vector<MathPrim> prims;
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
    void setVisgroups(std::vector<Visgroup> v) { visgroups_ = std::move(v); bump(); }
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

    // Environment (sky, ambient, fog): changed through commands (makeEnvironment).
    const Environment& environment() const { return env_; }
    // Camera bookmarks and the scale figure: editor-only, saved with the map, not undone
    // (like visgroups).
    const std::vector<CameraBookmark>& bookmarks() const { return bookmarks_; }
    void setBookmarks(std::vector<CameraBookmark> b) { bookmarks_ = std::move(b); bump(); }
    const std::optional<Vec3>& figure() const { return figure_; }
    void setFigure(std::optional<Vec3> f) { figure_ = f; bump(); }
    AABB selectionBounds() const;

    // Applies a command and records it for undo. Clears redo.
    void apply(Command cmd);
    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    const std::string& undoName() const;

    // Changes since last save.
    bool dirty() const { return revision_ != savedRevision_; }
    void markSaved() { savedRevision_ = revision_; }
    // Increments on every change (renderers use it to know when to rebuild).
    unsigned long long revision() const { return revision_; }

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
    // Adds group removals to `c` for groups that would have no brushes left.
    void pruneEmptyGroups(Command& c) const;
    void bump() { ++revision_; }

    std::vector<Brush> brushes_;
    std::set<BrushId> selection_;
    BrushId nextId_ = 1;
    std::map<GroupId, Group> groups_;
    std::vector<Visgroup> visgroups_;
    GroupId nextGroupId_ = 1;
    std::vector<MathPrim> prims_;
    std::set<MathPrimId> primSelection_;
    MathPrimId nextPrimId_ = 1;
    std::vector<Light> lights_;
    std::set<LightId> lightSelection_;
    LightId nextLightId_ = 1;
    Environment env_;
    std::vector<CameraBookmark> bookmarks_;
    std::optional<Vec3> figure_;
    std::vector<Command> undo_, redo_;
    unsigned long long revision_ = 0, savedRevision_ = 0;
};

} // namespace scal
