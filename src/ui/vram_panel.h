#pragma once
#include "../core/tim_document.h"
#include "../core/vram_manager.h"
#include "imgui.h"
#include <vector>
#include <utility>
#include <algorithm>

namespace ui {

// "VRAM Viewer" tab: shows emulated VRAM content, overlays each loaded TIM's
// image/CLUT bounds, and lets the user select (click, Ctrl/Shift) and drag
// them to change their VRAM position.
class VRAMPanel {
public:
    void Render(tim::Document& document, VRAMManager& vram_manager);

private:
    int bpp_mode_index = 2; // Combo index: 0 = 4 BPP, 1 = 8 BPP, 2 = 16 BPP
    float zoom = 1.0f;
    bool snap_enabled = true;
    int last_vram_version = -1;
    int last_structure_version = -1;

    // Image selection lives on TIM_Image::selected (shared with InspectorPanel);
    // CLUT selection has no inspector equivalent, so it stays local here.
    std::vector<int> selected_cluts;
    int image_selection_anchor = -1;
    int clut_selection_anchor = -1;

    enum class DragTarget { None, Images, Cluts };
    struct DragState {
        DragTarget target = DragTarget::None;
        ImVec2 mouse_start{};
        std::vector<std::pair<int, ImVec2>> original_origins; // index -> origin at drag start

        // A plain click (no Ctrl/Shift) on an already-selected item must not
        // collapse the multi-selection right away - it might be the start of
        // a group drag. The collapse-to-one-item only happens on release if
        // the mouse never actually moved.
        bool pending_click_reset = false;
        int click_reset_index = -1;
    } drag;

    static VRAMViewMode IndexToViewMode(int index);
    static int TPageWidthPixelsForMode(VRAMViewMode mode);

    void DrawTPageGrid(ImDrawList* draw_list, ImVec2 origin, float tpage_w, float tpage_h) const;
    void DrawRegions(tim::Document& document, ImVec2 canvas_origin, float words_scale);
    void UpdateDrag(tim::Document& document, float words_scale);

    // Snaps `candidate` to the nearest value in `lines` if within
    // `max_screen_px` (converted using `scale`), otherwise returns it unchanged.
    static int SnapToNearest(int candidate, const std::vector<int>& lines, float scale, float max_screen_px);
};

} // namespace ui
