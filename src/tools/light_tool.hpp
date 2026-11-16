#pragma once
// Light tool (Shift+L): place point, spot and sun lights.
//  - click in a 2D view to put the new light there (the depth comes from the last
//    click in another view, or the last brush), or on a surface in the 3D view (it
//    goes 32 units out from it). Click again to adjust; the position can be typed in
//    Properties. Return creates it, Esc cancels.
//  - the new light's kind, colour, brightness, reach and direction are set in
//    Properties beforehand (`templ`).
//  - lights are selected, moved and aimed here as in the select tool (rings).
#include "tools/light_edit.hpp"
#include "tools/tool.hpp"
#include <optional>

namespace scal {

class LightTool : public Tool {
public:
    const char* name() const override { return "Light"; }
    void press(ToolEnv& env, const Pointer& p) override;
    void drag(ToolEnv& env, const Pointer& p) override;
    void release(ToolEnv& env, const Pointer& p) override;
    bool key(ToolEnv& env, int key) override;
    void decorate(const Document& doc, Scene& scene) const override;
    const char* hint() const override;
    void cancel() override;

    Light templ;                    // what the next light will be like
    std::optional<Vec3> pending;    // where it will go (Return creates it)
    // Creates the pending light (Return). False if there is none.
    bool create(Document& doc);

private:
    LightEdit lights_;
    std::string message_;
};

} // namespace scal
