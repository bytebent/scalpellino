#pragma once
// Small helpers shared by the editor_*.cpp files (the Editor class is split over
// several files by topic; see editor.hpp).
#include "core/document.hpp"
#include "core/mathprim.hpp"
#include "raylib.h"
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace scal::editor_util {

// Physical-key shortcuts (layout independent). On an Italian keyboard the two keys
// right of P ("è" and "+") sit where US "[" and "]" are.
constexpr int kKeyGridSmaller = KEY_LEFT_BRACKET;
constexpr int kKeyGridLarger  = KEY_RIGHT_BRACKET;

inline bool cmdDown() {
#if defined(__APPLE__)
    return IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
#else
    return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
#endif
}

inline std::string defaultMapPath() {
    const char* home = std::getenv("HOME");
    if (!home) return "untitled.map";
    return (std::filesystem::path(home) / "Documents" / "untitled.map").string();
}

inline std::string fileName(const std::string& p) {
    return std::filesystem::path(p).filename().string();
}

inline int keyFromName(const std::string& n) {
    if (n == "Return" || n == "Enter") return KEY_ENTER;
    if (n == "Esc" || n == "Escape") return KEY_ESCAPE;
    if (n == "Delete") return KEY_DELETE;
    if (n == "Backspace") return KEY_BACKSPACE;
    return 0;
}

// The selected primitives as objects: a combination is one object.
inline std::vector<std::vector<const MathPrim*>> selectedObjects(const Document& doc) {
    std::vector<std::vector<const MathPrim*>> out;
    std::set<std::uint32_t> combos;
    for (MathPrimId id : doc.primSelection()) {
        const MathPrim* p = doc.findPrim(id);
        if (!p) continue;
        if (!p->combo) { out.push_back({p}); continue; }
        if (combos.insert(p->combo).second) out.push_back(comboMembers(doc.prims(), p->combo));
    }
    return out;
}

} // namespace scal::editor_util
