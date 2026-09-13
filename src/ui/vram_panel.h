#pragma once
#include "../core/tim_document.h"
#include "../core/vram_manager.h"
#include "imgui.h"
#include <vector>
#include <utility>
#include <algorithm>
#include <array>

namespace ui {

// "VRAM Viewer" tab: shows emulated VRAM content, overlays each loaded TIM's
// image/CLUT bounds, and lets the user select (click, Ctrl/Shift) and drag
// them to change their VRAM position.
class VRAMPanel {
public:
    void Render(tim::Document& document, VRAMManager& vram_manager);

    // Selects the given image (is_clut=false) or CLUT (is_clut=true) - same
    // effect as clicking it here, including the image/CLUT mutual exclusion
    // - and requests that the next Render() zoom in and scroll so it's
    // centered. Used by the Inspector's "Go to VRAM" buttons. Does not
    // itself switch to the VRAM Viewer tab; the caller (EditorApp) handles that.
    void FocusOn(tim::Document& document, int index, bool is_clut);

private:
    static constexpr float kMinZoom = 0.1f;
    static constexpr float kMaxZoom = 16.0f;
    static constexpr float kSidebarMinWidth = 150.0f;
    static constexpr float kSidebarMaxWidth = 500.0f;
    // Constant blank space kept above and to the left of VRAM row/word 0
    // inside the scroll area, so there's room to pan/zoom around the very
    // top-left edge instead of content being pinned flush against the
    // window's border.
    static constexpr float kCanvasMargin = 24.0f;

    float sidebar_width = 240.0f; // User-adjustable via the splitter next to it.

    bool pending_focus = false;
    int focus_x = 0, focus_y = 0, focus_w = 0, focus_h = 0;

    int bpp_mode_index = 2; // Combo index: 0 = 4 BPP, 1 = 8 BPP, 2 = 16 BPP
    float zoom = 1.0f;
    bool snap_enabled = true;
    int last_vram_version = -1;
    int last_structure_version = -1;

    // Common PS1 display buffer footprints, drawn as a highlighted region at
    // VRAM origin - index 0 is "None". Mirrors what a game's frame/display
    // buffer(s) would actually reserve, so placing a texture there is
    // visibly a mistake instead of a silent VRAM collision.
    struct FramebufferPreset { const char* label; int width; int height; };
    static const FramebufferPreset kFramebufferPresets[];
    static constexpr int kFramebufferPresetCount = 21;
    int framebuffer_preset_index = 0;
    bool framebuffer_double_buffered = false;
    bool framebuffer_stack_horizontal = false;

    // Image selection lives on TIM_Image::selected (shared with InspectorPanel);
    // CLUT selection has no inspector equivalent, so it stays local here.
    // The two are mutually exclusive: selecting an image clears any CLUT
    // selection and vice versa, so exactly one "kind" is ever draggable.
    std::vector<int> selected_cluts;
    int image_selection_anchor = -1;
    int clut_selection_anchor = -1;

    enum class DragTarget { None, Images, Cluts };
    struct DragState {
        DragTarget target = DragTarget::None;
        ImVec2 mouse_start{};
        std::vector<std::pair<int, ImVec2>> original_origins; // index -> origin at drag start

        // A plain click (no Ctrl/Shift) on an already-selected item must not
        // change selection right away - it might be the start of a group
        // drag. The actual click outcome (collapse to one item, or - if it
        // was already the sole selection - deselect it entirely) only
        // happens on release if the mouse never actually moved.
        bool pending_click_reset = false;
        int click_reset_index = -1;
        bool click_deselect_if_sole = false;
    } drag;

    static VRAMViewMode IndexToViewMode(int index);
    static int TPageWidthPixelsForMode(VRAMViewMode mode);

    void DrawTPageGrid(ImDrawList* draw_list, ImVec2 origin, float tpage_w, float tpage_h) const;
    void DrawFramebufferOverlay(ImDrawList* draw_list, ImVec2 canvas_origin, float words_scale) const;
    void DrawRegions(tim::Document& document, ImVec2 canvas_origin, float words_scale);
    void UpdateDrag(tim::Document& document, float words_scale);

    // Persistent side panel (a normal sibling of the canvas, not an overlay
    // drawn on top of it): VRAM coordinates under the mouse, plus a
    // scrollable list of the selected images with small previews and a
    // per-image palette switcher.
    void RenderSidebar(tim::Document& document, int mouse_word_x, int mouse_line_y, bool mouse_in_vram);

    // Arrow keys nudge every selected image/CLUT by one VRAM unit (Shift for
    // a bigger 10-unit step), for pixel-precise positioning the mouse can't
    // reliably do. Only acts while the VRAM canvas has focus and no drag is
    // in progress.
    void HandleKeyboardNudge(tim::Document& document);

    // Snaps `candidate` to the nearest value in `lines` if within
    // `max_screen_px` (converted using `scale`), otherwise returns it unchanged.
    static int SnapToNearest(int candidate, const std::vector<int>& lines, float scale, float max_screen_px);
};

} // namespace ui
