#pragma once
// The map being edited: world properties, world brushes, other entities (preserved),
// selection, and undo history. All edits go through Document::apply(Command).
#include "core/brush.hpp"
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

// One brush before/after an edit. Missing `before` = created; missing `after` = deleted.
struct BrushChange {
    BrushId id = 0;
    std::optional<Brush> before;
    std::optional<Brush> after;
};

struct Command {
    std::string name;
    std::vector<BrushChange> changes;
    std::set<BrushId> selectionBefore;
    std::set<BrushId> selectionAfter;
    std::vector<GroupChange> groupChanges;
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

    const std::set<BrushId>& selection() const { return selection_; }
    void setSelection(std::set<BrushId> s) { selection_ = std::move(s); }
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
               std::map<GroupId, Group> groups = {});

    // Helpers that build commands.
    Command makeAdd(std::vector<Brush> brushes, const std::string& name);
    Command makeDelete(const std::set<BrushId>& ids, const std::string& name);
    Command makeModify(const std::vector<Brush>& modified, const std::string& name);
    // Mixed edit as one undo step (clip, carve, hollow). New brushes get ids. The
    // selection afterwards is `selectAfter` if given, else the old selection minus
    // deleted brushes plus the added ones.
    Command makeChange(const std::vector<Brush>& modified, std::vector<Brush> added,
                       const std::set<BrushId>& deleted, const std::string& name,
                       const std::set<BrushId>* selectAfter = nullptr);

private:
    void setBrush(BrushId id, const std::optional<Brush>& b);
    void setGroup(GroupId id, const std::optional<Group>& g);
    // Adds group removals to `c` for groups that would have no brushes left.
    void pruneEmptyGroups(Command& c) const;
    void bump() { ++revision_; }

    std::vector<Brush> brushes_;
    std::set<BrushId> selection_;
    BrushId nextId_ = 1;
    std::map<GroupId, Group> groups_;
    GroupId nextGroupId_ = 1;
    std::vector<Command> undo_, redo_;
    unsigned long long revision_ = 0, savedRevision_ = 0;
};

} // namespace scal
