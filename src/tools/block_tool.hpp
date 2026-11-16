#pragma once
// Block tool: drag a rectangle in a 2D view to get a box preview (depth taken from the
// last brush), adjust it with handles in any 2D view, Return creates it, Esc cancels.
#include "core/primitives.hpp"
#include "tools/tool.hpp"
#include <optional>

namespace scal {

class BlockTool : public Tool {
public:
    const char* name() const override { return "Block"; }
    void press(ToolEnv& env, const Pointer& p) override;
    void drag(ToolEnv& env, const Pointer& p) override;
    void release(ToolEnv& env, const Pointer& p) override;
    bool key(ToolEnv& env, int key) override;
    void decorate(const Document& doc, Scene& scene) const override;
    const char* hint() const override;
    void cancel() override;

    bool hasPreview() const { return box_.has_value(); }
    // Shape options (Properties panel).
    Primitive shape = Primitive::Box;
    int sides = 16;
    bool roundToWhole = true;
    // The brush Return would create (nullopt if none / impossible).
    std::optional<Brush> previewBrush() const;
    std::string texture = "default";
    double textureScale = 1.0;

private:
    enum class Mode { Idle, Drawing, Editing };
    Mode mode_ = Mode::Idle;
    const Viewport* view_ = nullptr;
    std::optional<AABB> box_;
    double au_ = 0, av_ = 0; // drawing anchor (snapped)
    double u0_ = 0, v0_ = 0; // editing start
    AABB startBox_;
    BoxGrab grab_;
    int axU_ = 0, axV_ = 1, axD_ = 2; // axes of the view the block was drawn in
};

} // namespace scal
