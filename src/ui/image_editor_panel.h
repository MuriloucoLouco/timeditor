#pragma once
#include "../core/tim_document.h"
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
    enum class Tool { Pencil, Eraser, Fill, Eyedropper, Line, Rect };

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

    int editing_clut_row = 0;
    int editing_swatch_index = -1;
    float picker_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    int resize_w = 0, resize_h = 0;

    ImportImageDialog import_dialog;

    void RenderBppDropdown(tim::Document& document, TIM_Image& tim);
    void RenderToolbar(TIM_Image& tim);
    void RenderResizeControls(tim::Document& document, TIM_Image& tim);
    void RenderPaletteList(tim::Document& document, TIM_Image& tim);
    void RenderCanvas(tim::Document& document, TIM_Image& tim);

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
    void PaintMasterPixel(TIM_Image& tim, int px, int py, bool erase);
    void DrawLineMaster(TIM_Image& tim, int x0, int y0, int x1, int y1, bool erase);
    void FillRectMaster(TIM_Image& tim, int x0, int y0, int x1, int y1, bool filled, bool erase);
    void FloodFillMaster(TIM_Image& tim, int px, int py);
};

} // namespace ui
