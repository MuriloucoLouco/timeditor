#pragma once
#include "../core/tim_document.h"
#include "../gfx/raster_ops.h"
#include "import_image_dialog.h"
#include "imgui.h"

namespace ui {

// The Inspector's "Image Editor" tab for one image: a BPP dropdown (re-
// quantizing from the master image, never losing fidelity below it),
// palette list/editor, paint tools, a zoomable canvas, and canvas resize.
class ImageEditorPanel {
public:
    void Render(tim::Document& document, int index);

private:
    enum class Tool { Select, Pencil, Eraser, Fill, Eyedropper, Line, Rect };

    int current_index = -1;

    float canvas_zoom = 8.0f;
    float sidebar_width = 260.0f;

    Tool active_tool = Tool::Pencil;
    bool rect_filled = false;
    float draw_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool tool_dragging = false;
    int drag_start_px = 0, drag_start_py = 0;
    int last_paint_px = 0, last_paint_py = 0;
    // Master buffer as it was right before the current stroke started, so
    // Line/Rect previews can be redrawn fresh each frame (undo the interim
    // preview, then repaint from drag_start to the current mouse position)
    // instead of permanently committing every intermediate frame.
    std::vector<uint8_t> stroke_master_backup;
    std::vector<uint8_t> stroke_index_backup;

    // Rectangular selection (Select tool): a pixel rect, always normalized
    // (x0<=x1, y0<=y1) except transiently while it's being dragged out
    // (see HandleToolInput). Persists across tool switches - e.g. select a
    // region, then switch to Fill, and the bucket only fills inside it
    // (FloodFillMaster) - it isn't specific to the Select tool itself.
    bool has_selection = false;
    int sel_x0 = 0, sel_y0 = 0, sel_x1 = 0, sel_y1 = 0;
    // True for a drag that starts *inside* an existing selection - moves
    // (cuts and re-stamps) its pixel content instead of redefining the
    // rectangle. move_delta_* is the in-progress drag offset, redrawn fresh
    // from stroke_master_backup each frame (same pattern as Line/Rect's
    // preview) and committed into sel_x0.. on release.
    bool selection_moving = false;
    int move_delta_x = 0, move_delta_y = 0;

    int editing_clut_row = 0;
    int editing_swatch_index = -1;
    float picker_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    int resize_w = 0, resize_h = 0; // manually-typed target canvas size, always anchored at (0,0)
    bool open_resize_popup = false; // one-shot flag: RenderResizeControls calls OpenPopup on the frame this is set

    ImportImageDialog import_dialog;

    void RenderBppDropdown(tim::Document& document, TIM_Image& tim);
    void RenderToolbar(TIM_Image& tim);
    void RenderResizeControls(tim::Document& document, TIM_Image& tim);
    void RenderPaletteList(tim::Document& document, TIM_Image& tim);
    void RenderCanvas(tim::Document& document, TIM_Image& tim);

    // Picks swatch 0 of the current row as the highlighted/paint color
    // whenever the image has a palette (or clears the highlight when it
    // doesn't) - called after anything that can leave editing_swatch_index
    // stale: switching images, and switching BPP mode into/out of an
    // indexed one. Keeps the palette grid's highlight and draw_color (what
    // Pencil/Fill actually use) in agreement instead of the grid showing no
    // selection at all while a tool silently paints with a leftover color.
    void SyncDefaultSwatchSelection(TIM_Image& tim);

    void SwitchBpp(tim::Document& document, TIM_Image& tim, int new_bpp);
    void ApplyCanvasResize(tim::Document& document, TIM_Image& tim);
    void ApplyPaletteColorEdit(tim::Document& document, TIM_Image& tim, int row, int swatch, const float color[4]);

    // Repacks image_data (and, for indexed modes, leaves clut_data as-is)
    // from the current master_rgba/master_index_map, marks the file dirty,
    // and rebuilds the GL texture - called after every live paint update so
    // changes are visible immediately instead of only once the mouse is released.
    void LiveUpdate(tim::Document& document, TIM_Image& tim);

    void HandleToolInput(tim::Document& document, TIM_Image& tim, int px, int py, bool just_activated);
    void FinishStroke();

    // Small helpers binding this panel's own member state (draw_color, the
    // Select tool's SelectionRect) to the plain, ImGui-free pixel ops in
    // gfx/raster_ops.h.
    gfx::SelectionRect CurrentSelectionRect() const;
};

} // namespace ui
