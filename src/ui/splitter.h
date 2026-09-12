#pragma once
#include "imgui.h"

namespace ui {

// A thin draggable vertical bar between two side-by-side panes. Call it
// right after ending the left pane's child (no need to call SameLine()
// first or after - this does both). Returns the horizontal drag delta while
// the splitter is being dragged, 0 otherwise: add it to a pane's width if
// dragging right should grow that pane, subtract it if dragging right
// should shrink it.
inline float VerticalSplitter(const char* id) {
    ImGui::SameLine();
    ImGui::PushID(id);
    ImGui::Button("##splitter", ImVec2(6.0f, -1));
    float delta = ImGui::IsItemActive() ? ImGui::GetIO().MouseDelta.x : 0.0f;
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    ImGui::PopID();
    ImGui::SameLine();
    return delta;
}

} // namespace ui
