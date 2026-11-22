#pragma once
// Probe tool (Shift+E, v0.12.6): place reflection probes (Hammer's env_cubemap), select and
// move them.
//  - click in a 2D view to put the new probe there (the depth comes from the last box, as for
//    lights and cameras), or on a surface in the 3D view: 170 cm above a floor, 81 cm (32
//    Hammer units) out from a wall or a ceiling. Click again to adjust; Return creates it,
//    Esc cancels. Its size (Hammer's cubemapsize) is chosen in Properties beforehand (`templ`).
//  - probes are selected and moved as in the select tool (click the marker; drag in a 2D view).
#include "tools/object_edit.hpp"
#include "tools/tool.hpp"
#include <optional>
#include <string>

namespace scal {

class ProbeTool : public Tool {
public:
    const char* name() const override { return "Probe"; }
    void press(ToolEnv& env, const Pointer& p) override;
    void drag(ToolEnv& env, const Pointer& p) override;
    void release(ToolEnv& env, const Pointer& p) override;
    bool key(ToolEnv& env, int key) override;
    void decorate(const Document& doc, Scene& scene) const override;
    const char* hint() const override;
    void cancel() override;

    Probe templ;                    // what the next probe will be like (its size)
    std::optional<Vec3> pending;    // where it will go (Return creates it)
    // Creates the pending probe (Return). False if there is none.
    bool create(Document& doc);

private:
    ObjectEdit objects_;
    std::string message_;
};

} // namespace scal
