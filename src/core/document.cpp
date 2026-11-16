#include "core/document.hpp"
#include <algorithm>

namespace scal {

const Brush* Document::find(BrushId id) const {
    for (const Brush& b : brushes_)
        if (b.id == id) return &b;
    return nullptr;
}

AABB Document::selectionBounds() const {
    AABB box;
    for (BrushId id : selection_)
        if (const Brush* b = find(id)) box.add(b->bounds());
    return box;
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
    for (const BrushChange& c : cmd.changes) setBrush(c.id, c.after);
    selection_ = cmd.selectionAfter;
    undo_.push_back(std::move(cmd));
    redo_.clear();
    bump();
}

bool Document::undo() {
    if (undo_.empty()) return false;
    Command cmd = std::move(undo_.back());
    undo_.pop_back();
    for (auto it = cmd.changes.rbegin(); it != cmd.changes.rend(); ++it) setBrush(it->id, it->before);
    selection_ = cmd.selectionBefore;
    redo_.push_back(std::move(cmd));
    bump();
    return true;
}

bool Document::redo() {
    if (redo_.empty()) return false;
    Command cmd = std::move(redo_.back());
    redo_.pop_back();
    for (const BrushChange& c : cmd.changes) setBrush(c.id, c.after);
    selection_ = cmd.selectionAfter;
    undo_.push_back(std::move(cmd));
    bump();
    return true;
}

const std::string& Document::undoName() const {
    static const std::string empty;
    return undo_.empty() ? empty : undo_.back().name;
}

void Document::reset(KeyValues world, std::vector<Brush> brushes, std::vector<RawEntity> others) {
    worldKeys = std::move(world);
    otherEntities = std::move(others);
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
    savedRevision_ = revision_;
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

} // namespace scal
