#pragma once
#include "imgui.h"
#include <string>

namespace ui {

// Truncates `text` with a trailing ellipsis so it renders no wider than
// `max_width` pixels in the current font. Returns `text` unchanged if it
// already fits. Used to keep a row's label from visually and
// interactively overlapping a fixed-position button drawn after it - see
// TreeNodeBehavior in imgui_widgets.cpp: an unframed/unspanned tree node's
// clickable rect extends to its rendered text width (plus a small click-
// past allowance), and since ImGui resolves overlapping items
// first-submitted-wins, a long label submitted before a same-row button
// would otherwise swallow clicks meant for that button.
inline std::string TruncateToWidth(const std::string& text, float max_width) {
    if (max_width <= 0.0f) return "...";
    if (ImGui::CalcTextSize(text.c_str()).x <= max_width) return text;

    const char* ellipsis = "...";
    float ellipsis_width = ImGui::CalcTextSize(ellipsis).x;
    for (int len = static_cast<int>(text.size()) - 1; len > 0; len--) {
        std::string candidate = text.substr(0, static_cast<size_t>(len));
        if (ImGui::CalcTextSize(candidate.c_str()).x + ellipsis_width <= max_width) {
            return candidate + ellipsis;
        }
    }
    return ellipsis;
}

} // namespace ui
