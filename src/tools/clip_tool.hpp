#pragma once
// Clip tool (Shift+X): cut the selected brushes with a plane.
//  - drag a line in any 2D view: the plane goes through that line, straight into the
//    screen (2-point clip);
//  - three point handles appear (1, 2, 3). Drag any of them in any 2D view to move it
//    there; moving point 3 in another view tilts the plane (3-point clip, as in
//    Hammer++). Tools -> Clip points... types all three exactly;
//  - keep: both pieces / front / back (Shift+X again cycles); only kept pieces are
//    shown in the preview;
//  - Return applies, Esc cancels.
#include "tools/tool.hpp"
#include <optional>

namespace scal {

class ClipTool : public Tool {
public:
    enum class Keep { Both, Front, Back };

    const char* name() const override { return "Clip"; }
    void press(ToolEnv& env, const Pointer& p) override;
    void drag(ToolEnv& env, const Pointer& p) override;
    void release(ToolEnv& env, const Pointer& p) override;
    bool key(ToolEnv& env, int key) override;
    void decorate(const Document& doc, Scene& scene) const override;
    const char* hint() const override;
    void cancel() override;

    Keep keep = Keep::Both;
    void cycleKeep() { keep = keep == Keep::Both ? Keep::Front : keep == Keep::Front ? Keep::Back : Keep::Both; }
    bool hasPlane() const { return active_; }
    void setPoints(const Vec3& a, const Vec3& b, const Vec3& c);
    void points(Vec3& a, Vec3& b, Vec3& c) const { a = p_[0]; b = p_[1]; c = p_[2]; }
    // Clips the selection; one undoable command. Returns false + message if nothing to do.
    bool apply(Document& doc, std::string& message);

private:
    struct Piece { Brush brush; bool kept; };
    std::vector<Brush> keptPieces(const Document& doc) const;

    bool active_ = false;        // a plane is defined
    int dragging_ = -1;          // 0..2 = point being dragged, 3 = drawing the line
    const Viewport* view_ = nullptr;
    Vec3 p_[3];
    mutable unsigned long long cacheRev_ = ~0ull;
    mutable Vec3 cacheP_[3];
    mutable Keep cacheKeep_ = Keep::Both;
    mutable std::vector<Brush> cache_;
    mutable char hintBuf_[200] = "";
    std::string message_;
};

} // namespace scal
