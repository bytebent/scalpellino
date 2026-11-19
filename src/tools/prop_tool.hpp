#pragma once
// Props tool (Shift+P): placing static models (core/prop.hpp).
//  - pick the model in the Props panel; click in a 3D view on a surface (the prop's origin
//    goes where the click hits it) or in a 2D view (the other axis keeps its last value);
//    drag to move it; type its position, turn and size in Properties. Return places it,
//    Esc cancels. The model shows at the spot while it is pending.
//  - existing objects are selected, moved, turned and sized here as in the select tool.
//  - Snap to Land (Cmd+J) drops the selected props onto what is below.
#include "tools/object_edit.hpp"
#include "tools/tool.hpp"
#include <optional>

namespace scal {

class PropTool : public Tool {
public:
    const char* name() const override { return "Props"; }
    void press(ToolEnv& env, const Pointer& p) override;
    void drag(ToolEnv& env, const Pointer& p) override;
    void release(ToolEnv& env, const Pointer& p) override;
    bool key(ToolEnv& env, int key) override;
    void decorate(const Document& doc, Scene& scene) const override;
    const char* hint() const override;
    void cancel() override;

    // The next prop: model name ("" = none picked), turn (degrees) and an even scale.
    std::string model;
    Vec3 rot;
    double scale = 1;

    bool hasPending() const { return pos_.has_value(); }
    std::optional<Prop> pending() const;
    // Typed placement (Properties).
    void setPending(const Vec3& pos) { pos_ = pos; }
    // Places the pending prop (Return); false if there is none. Sets `message` if it can't.
    bool create(Document& doc);

private:
    bool placeAt(ToolEnv& env, const Pointer& p);
    ObjectEdit objects_;
    std::optional<Vec3> pos_;
    bool placing_ = false;
    const Viewport* view_ = nullptr;
    double depth_ = 0;       // 2D: the axis the view looks along keeps this value
    int depthAxis_ = 2;
    std::string message_;
};

} // namespace scal
