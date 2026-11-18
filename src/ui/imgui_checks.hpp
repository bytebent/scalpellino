#pragma once
// UI test check: two widgets with the same ID in one frame (Dear ImGui then mixes them
// up and shows its "conflicting ID" error to the user, but only while hovering them).
// On in scripted runs only.
#include <string>
#include <vector>

namespace scal {

void enableImGuiIdCheck();
// Labels of IDs seen twice in a frame since the last call (empty = none).
std::vector<std::string> takeImGuiIdConflicts();

} // namespace scal
