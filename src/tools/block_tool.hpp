#pragma once
#include "render/scene.hpp"
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
    // Last built preview (round shapes with many sides are costly; decorate runs per view).
    struct PreviewKey {
        AABB box; Primitive shape; int sides, axis, slope; bool round; std::string tex; double scale;
        bool operator==(const PreviewKey& o) const {
            return box.min == o.box.min && box.max == o.box.max && shape == o.shape && sides == o.sides && axis == o.axis &&
                   slope == o.slope && round == o.round && tex == o.tex && scale == o.scale;
        }
    };
    mutable std::optional<PreviewKey> cacheKey_;
    mutable std::optional<Brush> cacheBrush_;
    mutable std::optional<SceneBrush> cacheGhost_; // its drawable polygons
};

} // namespace scal
