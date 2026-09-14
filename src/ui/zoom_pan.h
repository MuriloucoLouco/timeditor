#pragma once
#include "imgui.h"
#include <algorithm>

namespace ui {

// Standard "zoom toward the mouse cursor" scroll-wheel handler for a
// zoomable/pannable child window (call right after BeginChild, before
// drawing anything). The wheel drives `zoom` (clamped to [min_zoom,
// max_zoom]) instead of scrolling - pair with ImGuiWindowFlags_NoScrollWithMouse
// on the BeginChild so the two don't fight.
//
// `content_units` is the drawn content's unscaled size (e.g. the image's
// pixel width/height); `get_scale(zoom) -> ImVec2` converts a zoom value
// into screen-pixels-per-content-unit on each axis (the same value on both
// for a uniform zoom; separate when, e.g., an indexed VRAM view mode
// stretches X but not Y).
//
// Returns the content origin to draw at this frame, computed by hand rather
// than re-read from ImGui after changing scroll: SetScrollX/Y only take
// effect on the window's *next* layout, so using a stale value here would
// flash the view at the old position/zoom for one frame on every wheel tick.
//
// The target scroll is also clamped here to [0, content_size - avail], the
// same bound ImGui's own Begin() applies to whatever we ask for - if we
// requested something outside that (e.g. panning past the left/top edge
// while zooming out), ImGui would silently re-clamp it on the *next*
// frame's Begin(), one frame after we'd already drawn at the unclamped
// position, producing a one-frame snap/flicker right at that edge (most
// noticeable at the top-left corner, since scroll 0 is hit constantly).
// Clamping to the same bound ourselves means this frame's hand-computed
// position already matches what ImGui will settle on.
template <typename GetScaleFn>
ImVec2 ZoomToCursor(float& zoom, float min_zoom, float max_zoom, ImVec2 content_units, GetScaleFn get_scale) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    if (!ImGui::IsWindowHovered() || io.MouseWheel == 0.0f) return canvas_p0;

    ImVec2 scale_before = get_scale(zoom);
    ImVec2 scroll_before(ImGui::GetScrollX(), ImGui::GetScrollY());
    ImVec2 window_origin(canvas_p0.x + scroll_before.x, canvas_p0.y + scroll_before.y);
    float content_x = (io.MousePos.x - canvas_p0.x) / scale_before.x;
    float content_y = (io.MousePos.y - canvas_p0.y) / scale_before.y;

    zoom = std::clamp(zoom * (1.0f + io.MouseWheel * 0.1f), min_zoom, max_zoom);
    ImVec2 scale_after = get_scale(zoom);

    canvas_p0 = ImVec2(io.MousePos.x - content_x * scale_after.x, io.MousePos.y - content_y * scale_after.y);

    ImVec2 content_size(content_units.x * scale_after.x, content_units.y * scale_after.y);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float scroll_max_x = std::max(0.0f, content_size.x - avail.x);
    float scroll_max_y = std::max(0.0f, content_size.y - avail.y);
    float new_scroll_x = std::clamp(window_origin.x - canvas_p0.x, 0.0f, scroll_max_x);
    float new_scroll_y = std::clamp(window_origin.y - canvas_p0.y, 0.0f, scroll_max_y);

    canvas_p0 = ImVec2(window_origin.x - new_scroll_x, window_origin.y - new_scroll_y);
    ImGui::SetScrollX(new_scroll_x);
    ImGui::SetScrollY(new_scroll_y);
    return canvas_p0;
}

// Right-drag-to-pan for the same kind of zoomable/pannable child window
// ZoomToCursor serves - call right after BeginChild, every frame,
// regardless of whether the mouse is currently over the window (a drag
// already in progress must keep panning even if it drifts outside the
// child's bounds, same reasoning as the Model Editor viewport's own
// hover-vs-active-gesture split). `active` is a bool the caller owns
// (persists across frames for the duration of one drag) so multiple
// independent pannable views in the same panel don't share state.
inline void PanWithMouseDrag(bool& active) {
    ImGuiIO& io = ImGui::GetIO();
    if (!active && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        active = true;
    }
    if (active && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        active = false;
    }
    if (!active) return;

    // Content moves the same direction as the mouse (a "grab and drag"
    // pan), so scroll itself moves the opposite way.
    ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
    ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
}

} // namespace ui
