// Editor (part of the class, split by topic; see editor.hpp): the prop benchmark, a tool
// for development (script command "bench props", see docs/DEV.md "Benchmark"). It fills
// the map with props and times what grows with their number: building the scene, drawing
// the 3D and a 2D view, picking with the mouse, Snap to Land. Prints one line per run.
#include "app/editor.hpp"
#include "app/editor_util.hpp"
#include "core/landing.hpp"
#include "tools/object_edit.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

extern "C" void glFinish(void); // so a draw is timed until the GPU has really done it

namespace scal {

namespace {
double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
// Median of `runs` timings of fn (ms).
template <class Fn> double timeMs(int runs, Fn fn) {
    std::vector<double> t;
    for (int i = 0; i < runs; ++i) {
        const double a = nowMs();
        fn();
        t.push_back(nowMs() - a);
    }
    std::sort(t.begin(), t.end());
    return t[t.size() / 2];
}
} // namespace

void Editor::benchProps(int count, const std::string& model, double spacing) {
    // A square of props on the ground, centred on the origin, each turned a little.
    tool_->cancel();
    const int side = std::max(1, (int)std::ceil(std::sqrt((double)count)));
    std::vector<Prop> props;
    for (int i = 0; i < count; ++i) {
        Prop p;
        p.model = model;
        p.pos = {(i % side - side * 0.5) * spacing, (i / side - side * 0.5) * spacing, 0};
        p.rot = {0, 0, (double)(i * 37 % 360)};
        props.push_back(p);
    }
    const double add = timeMs(1, [&] { doc_.apply(doc_.makeAddProps(props, "Bench")); });
    doc_.setPropSelection({});
    const Model* m = models_.find(model);
    const size_t tris = m ? m->triangles() : 0;

    // The 3D camera above one corner, looking across the whole square.
    const double half = side * spacing * 0.5;
    views_[0]->setCamera({-half - 400, -half - 400, half * 0.6 + 300}, 45, -25);

    Scene scene;
    const double build = timeMs(5, [&] { scene = buildScene(); });
    auto drawView = [&](Viewport& v) {
        v.render(grid_, scene);
        glFinish();
    };
    drawView(*views_[0]); // first use uploads the model to the GPU
    const double draw3D = timeMs(5, [&] { drawView(*views_[0]); });
    const size_t culledFar = models_.culled(), callsFar = models_.drawCalls();
    // Close up, looking along one edge: most props are behind or beside the camera.
    views_[0]->setCamera({-half + 200, -half - 200, 250}, 90, -10);
    const double draw3DClose = timeMs(5, [&] { drawView(*views_[0]); });
    const size_t culledClose = models_.culled();
    views_[0]->setCamera({-half - 400, -half - 400, half * 0.6 + 300}, 45, -25);
    const double draw2D = timeMs(5, [&] { drawView(*views_[1]); });

    // Picking: clicks spread over the 3D view.
    const ModelFinder* finder = &modelFinder_;
    const Viewport& v3 = *views_[0];
    int hits = 0;
    const int clicks = 40;
    const double pickTotal = timeMs(1, [&] {
        for (int k = 0; k < clicks; ++k) {
            const Vector2 at{(float)(v3.width() * (0.1 + 0.8 * ((k * 7) % clicks) / clicks)),
                             (float)(v3.height() * (0.3 + 0.6 * ((k * 13) % clicks) / clicks))};
            hits += pickProp(doc_, v3, at, finder) != 0;
        }
    });

    // Snap to Land: up to 50 props lifted, then landed among all the others.
    std::vector<Prop> lifted;
    std::set<PropId> sel;
    for (const Prop& p : doc_.props()) {
        if (p.model != model || lifted.size() >= 50) continue;
        Prop q = p;
        q.pos.z += 300;
        lifted.push_back(q);
        sel.insert(q.id);
    }
    doc_.apply(doc_.makeModifyProps(lifted, "Bench lift"));
    doc_.setPropSelection(sel);
    LandingResult landed;
    const double land = timeMs(1, [&] { landed = landSelection(doc_, modelFinder_); });
    doc_.setPropSelection({});

    std::printf("bench props: %d x %s (%zu triangles each): add %.1f ms | scene %.2f ms (built per view: x4 per redraw)"
                " | 3D view %.1f ms (%zu skipped, %zu draw calls), close up %.1f ms (%zu skipped) | 2D view %.1f ms | pick %.2f ms per click (%d/%d hit) | land %zu: %.0f ms\n",
                count, model.c_str(), tris, add, build, draw3D, culledFar, callsFar, draw3DClose, culledClose, draw2D, pickTotal / clicks, hits, clicks, lifted.size(), land);
    std::fflush(stdout);
}

} // namespace scal
