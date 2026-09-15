#include "image_editor_panel.h"
#include "../core/vram_manager.h"
#include "../gfx/image_quantizer.h"
#include "../gfx/raster_ops.h"
#include "../gfx/tim_texture_builder.h"
#include "gl_image.h"
#include "IconsFontAwesome6.h"
#include "icon_button.h"
#include "zoom_pan.h"
#include "../core/gl_compat.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace ui {

void ImageEditorPanel::Render(tim::Document& document, int index) {
    if (index < 0 || index >= static_cast<int>(document.Images().size())) {
        ImGui::TextDisabled("No image selected.");
        return;
    }
    TIM_Image& tim = document.Images()[index];
    gfx::TIMTextureBuilder::EnsureMasterImage(tim);

    if (index != current_index) {
        current_index = index;
        editing_clut_row = tim.selected_clut;
        active_tool = Tool::Pencil;
        tool_dragging = false;
        has_selection = false;
        selection_moving = false;
        canvas_zoom = 8.0f;
        resize_w = tim.master_width;
        resize_h = tim.image_header.height;

        SyncDefaultSwatchSelection(tim);
    }

    if (ImGui::Button("Save")) document.Save(tim.filename);
    if (tim.dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "(unsaved changes)");
    }
    ImGui::SameLine(0, 24.0f);
    RenderBppDropdown(document, tim);

    ImGui::SameLine(ImGui::GetWindowWidth() - 140.0f);
    if (ImGui::Button("Import Image...")) import_dialog.Open(index);
    import_dialog.Render(document);
    ImGui::Separator();

    RenderCanvas(document, tim);
    ImGui::SameLine();

    ImGui::BeginChild("ImgEditSidebar", ImVec2(sidebar_width, 0), true);
    RenderToolbar(tim);
    ImGui::Separator();
    RenderResizeControls(document, tim);
    ImGui::Separator();
    RenderPaletteList(document, tim);
    ImGui::EndChild();
}

void ImageEditorPanel::RenderBppDropdown(tim::Document& document, TIM_Image& tim) {
    const char* labels[] = { "4 BPP", "8 BPP", "16 BPP", "24 BPP" };
    const int bpp_values[] = { 4, 8, 16, 24 };
    int current = 2;
    for (int i = 0; i < 4; i++) {
        if (tim.bpp == bpp_values[i]) current = i;
    }
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::Combo("BPP Mode", &current, labels, 4) && bpp_values[current] != tim.bpp) {
        SwitchBpp(document, tim, bpp_values[current]);
    }
}

void ImageEditorPanel::SwitchBpp(tim::Document& document, TIM_Image& tim, int new_bpp) {
    document.PushContentUndoSnapshot(current_index);

    int px_width = gfx::ImageQuantizer::RoundWidthForBpp(tim.master_width, new_bpp);
    int height = tim.image_header.height;

    std::vector<uint8_t> rgba = tim.master_rgba;
    if (px_width != tim.master_width) {
        std::vector<uint8_t> padded(static_cast<size_t>(px_width) * height * 4, 0);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < tim.master_width; x++) {
                for (int c = 0; c < 4; c++) {
                    padded[(static_cast<size_t>(y) * px_width + x) * 4 + c] =
                        rgba[(static_cast<size_t>(y) * tim.master_width + x) * 4 + c];
                }
            }
        }
        rgba = std::move(padded);
    }

    tim::Document::ImageContent content;
    content.type = static_cast<uint8_t>(new_bpp == 4 ? 0 : new_bpp == 8 ? 1 : new_bpp == 16 ? 2 : 3);
    content.bpp = new_bpp;
    content.pixel_width = px_width;
    content.width_words = gfx::ImageQuantizer::PixelWidthToWords(px_width, new_bpp);
    content.height = height;

    gfx::ImageQuantizer::Result quant;
    if (new_bpp == 4 || new_bpp == 8) {
        int color_count = new_bpp == 4 ? 16 : 256;
        quant = gfx::ImageQuantizer::Quantize(rgba, px_width, height, color_count);
        content.image_data = gfx::ImageQuantizer::PackIndexed(quant.indices, px_width, height, new_bpp);
        content.has_clut = true;
        content.colors_per_clut = color_count;
        content.num_cluts = 1;
        content.clut_data = quant.palette;
    } else {
        content.image_data = gfx::ImageQuantizer::PackDirect(rgba, px_width, height, new_bpp);
        content.has_clut = false;
    }
    document.ReplaceImageContent(current_index, std::move(content));

    tim::Document::MasterContent master;
    master.rgba = rgba;
    master.width = px_width;
    if (new_bpp == 4 || new_bpp == 8) master.index_map = quant.indices;
    document.ReplaceMasterImage(current_index, std::move(master));

    editing_clut_row = 0;
    SyncDefaultSwatchSelection(tim);
    gfx::TIMTextureBuilder::RebuildTextures(tim);
}

void ImageEditorPanel::SyncDefaultSwatchSelection(TIM_Image& tim) {
    if (tim.has_clut && tim.clut_header.colors_per_clut > 0) {
        editing_swatch_index = 0;
        uint16_t color = tim.clut_data[static_cast<size_t>(editing_clut_row) * tim.clut_header.colors_per_clut];
        uint8_t rgba[4];
        gfx::ImageQuantizer::BGR555ToRGBA(color, rgba);
        draw_color[0] = rgba[0] / 255.0f;
        draw_color[1] = rgba[1] / 255.0f;
        draw_color[2] = rgba[2] / 255.0f;
        draw_color[3] = 1.0f;
    } else {
        editing_swatch_index = -1;
    }
}

void ImageEditorPanel::RenderToolbar(TIM_Image& tim) {
    ImGui::SeparatorText("Tools");

    // Same shared ui::IconButton the Model Editor's toolbar uses - same
    // icon font, same 32x32 size, same active-highlight convention.
    const ImVec2 kIconBtn(32.0f, 32.0f);
    auto ToolButton = [&](const char* icon, const char* tooltip, Tool tool) {
        if (ui::IconButton(icon, tooltip, kIconBtn, active_tool == tool)) active_tool = tool;
    };

    ToolButton(ICON_FA_VECTOR_SQUARE, "Select (drag to draw, drag inside to move)", Tool::Select);
    ImGui::SameLine();
    ToolButton(ICON_FA_PENCIL, "Pencil", Tool::Pencil);
    ImGui::SameLine();
    ToolButton(ICON_FA_ERASER, "Eraser", Tool::Eraser);
    ImGui::SameLine();
    ToolButton(ICON_FA_FILL_DRIP, "Fill (fills inside the selection, if any)", Tool::Fill);

    ToolButton(ICON_FA_EYE_DROPPER, "Eyedropper", Tool::Eyedropper);
    ImGui::SameLine();
    ToolButton(ICON_FA_SLASH, "Line", Tool::Line);
    ImGui::SameLine();
    ToolButton(ICON_FA_SQUARE_FULL, "Rect", Tool::Rect);

    if (active_tool == Tool::Rect) {
        ImGui::SameLine(0, 16.0f);
        ImGui::Checkbox("Filled", &rect_filled);
    }

    if (has_selection) {
        ImGui::TextDisabled("Selection: %d x %d px", std::abs(sel_x1 - sel_x0) + 1, std::abs(sel_y1 - sel_y0) + 1);
        ImGui::SameLine();
        if (ImGui::SmallButton("Deselect")) has_selection = false;
    }

    ImGui::Spacing();
    if (!tim.has_clut) {
        // No palette to pick from - the color itself is the tool.
        ImGui::ColorEdit4("Color", draw_color, ImGuiColorEditFlags_NoInputs);
    } else {
        ImGui::TextDisabled("Painting with the selected palette color below.");
    }
}

void ImageEditorPanel::RenderResizeControls(tim::Document& document, TIM_Image& tim) {
    ImGui::SeparatorText("Canvas Size");
    ImGui::Text("Current: %d x %d px", tim.master_width, tim.image_header.height);

    // A proper OK/Cancel dialog (like every real image editor's "Canvas
    // Size" - MS Paint, Photoshop, GIMP all use one) rather than inline
    // fields plus a permanently-visible Apply/Reset pair: typed values
    // only ever take effect on OK, and Cancel just closes without
    // touching the image at all, so there's nothing to "reset".
    if (ImGui::Button("Resize Canvas...", ImVec2(-1, 0))) {
        resize_w = tim.master_width;
        resize_h = tim.image_header.height;
        open_resize_popup = true;
    }

    if (open_resize_popup) {
        ImGui::OpenPopup("Resize Canvas");
        open_resize_popup = false;
    }
    ImGui::SetNextWindowSize(ImVec2(280.0f, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Resize Canvas", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("New size, in pixels (top-left corner stays fixed):");

        // step=0, step_fast=0 suppresses InputInt's usual +/- spin buttons,
        // which at this field width left barely any room for the actual
        // text box - this is meant to read as a plain text field, not a
        // stepper.
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Width##resizew", &resize_w, 0, 0);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Height##resizeh", &resize_h, 0, 0);
        resize_w = std::max(1, resize_w);
        resize_h = std::max(1, resize_h);

        int rounded_w = gfx::ImageQuantizer::RoundWidthForBpp(resize_w, tim.bpp);
        if (rounded_w != resize_w) {
            ImGui::TextDisabled("Width rounds up to %d px for %d BPP.", rounded_w, tim.bpp);
        }

        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(90.0f, 0))) {
            ApplyCanvasResize(document, tim);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void ImageEditorPanel::ApplyCanvasResize(tim::Document& document, TIM_Image& tim) {
    document.PushContentUndoSnapshot(current_index);

    int new_w = std::max(1, resize_w);
    int new_h = std::max(1, resize_h);
    int px_width = gfx::ImageQuantizer::RoundWidthForBpp(new_w, tim.bpp);

    // Always anchored at (0,0): growing only pads to the right/bottom,
    // shrinking only crops from the right/bottom - the existing content's
    // top-left corner never moves.
    std::vector<uint8_t> new_rgba(static_cast<size_t>(px_width) * new_h * 4, 0);
    std::vector<uint8_t> new_index_map;
    if (!tim.master_index_map.empty()) new_index_map.assign(static_cast<size_t>(px_width) * new_h, 0);

    int copy_w = std::min(px_width, tim.master_width);
    int copy_h = std::min(new_h, static_cast<int>(tim.image_header.height));
    for (int y = 0; y < copy_h; y++) {
        for (int x = 0; x < copy_w; x++) {
            size_t src = static_cast<size_t>(y) * tim.master_width + x;
            size_t dst = static_cast<size_t>(y) * px_width + x;
            for (int c = 0; c < 4; c++) new_rgba[dst * 4 + c] = tim.master_rgba[src * 4 + c];
            if (!new_index_map.empty()) new_index_map[dst] = tim.master_index_map[src];
        }
    }

    tim::Document::ImageContent content;
    content.type = tim.type;
    content.bpp = tim.bpp;
    content.pixel_width = px_width;
    content.width_words = gfx::ImageQuantizer::PixelWidthToWords(px_width, tim.bpp);
    content.height = new_h;
    content.has_clut = tim.has_clut;
    if (tim.bpp == 4 || tim.bpp == 8) {
        content.image_data = gfx::ImageQuantizer::PackIndexed(new_index_map, px_width, new_h, tim.bpp);
        content.colors_per_clut = tim.clut_header.colors_per_clut;
        content.num_cluts = tim.clut_header.num_cluts;
        content.clut_data = tim.clut_data;
    } else {
        content.image_data = gfx::ImageQuantizer::PackDirect(new_rgba, px_width, new_h, tim.bpp);
    }
    document.ReplaceImageContent(current_index, std::move(content));

    tim::Document::MasterContent master;
    master.rgba = std::move(new_rgba);
    master.width = px_width;
    master.index_map = std::move(new_index_map);
    document.ReplaceMasterImage(current_index, std::move(master));

    resize_w = px_width;
    resize_h = new_h;
    gfx::TIMTextureBuilder::RebuildTextures(tim);
}

void ImageEditorPanel::RenderPaletteList(tim::Document& document, TIM_Image& tim) {
    if (!tim.has_clut) {
        ImGui::TextDisabled("This BPP mode has no palette.");
        return;
    }

    ImGui::SeparatorText("Palette");
    ImGui::Text("Rows: %d", tim.clut_header.num_cluts);
    ImGui::SameLine();
    bool can_add = tim.clut_header.origin_y + tim.clut_header.num_cluts + 1 <= VRAMManager::kHeight;
    ImGui::BeginDisabled(!can_add);
    if (ImGui::SmallButton("+ Add")) {
        if (document.AddClutSlot(current_index)) gfx::TIMTextureBuilder::RebuildTextures(tim);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(tim.clut_header.num_cluts <= 1);
    if (ImGui::SmallButton("- Remove")) {
        if (document.RemoveClutSlot(current_index, editing_clut_row)) {
            gfx::TIMTextureBuilder::RebuildTextures(tim);
            editing_clut_row = std::min(editing_clut_row, static_cast<int>(tim.clut_header.num_cluts) - 1);
        }
    }
    ImGui::EndDisabled();

    ImGui::BeginChild("PaletteRows", ImVec2(0, 80.0f), true);
    for (int row = 0; row < tim.clut_header.num_cluts; row++) {
        ImGui::PushID(row);
        std::string label = "Row " + std::to_string(row);
        if (ImGui::Selectable(label.c_str(), row == editing_clut_row)) {
            editing_clut_row = row;
            tim.selected_clut = row;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    // Defaults to wherever this CLUT already sits (see
    // Document::ReplaceImageContent - a regenerated CLUT never silently
    // moves), so this always reflects its real, current position.
    int origin_x = tim.clut_header.origin_x;
    int origin_y = tim.clut_header.origin_y;
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::InputInt("X##clutx", &origin_x)) document.SetClutOrigin(current_index, origin_x, tim.clut_header.origin_y);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::InputInt("Y##cluty", &origin_y)) document.SetClutOrigin(current_index, tim.clut_header.origin_x, origin_y);

    ImGui::Separator();
    ImGui::TextDisabled("Left-click: pick color to paint with. Right-click: edit.");
    int colors = tim.clut_header.colors_per_clut;
    int columns = colors >= 256 ? 16 : 8;
    float swatch = 18.0f;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 start = ImGui::GetCursorScreenPos();

    for (int i = 0; i < colors; i++) {
        uint16_t color = tim.clut_data[static_cast<size_t>(editing_clut_row) * colors + i];
        uint8_t rgba[4];
        gfx::ImageQuantizer::BGR555ToRGBA(color, rgba);
        int col = i % columns, row_i = i / columns;
        ImVec2 p0(start.x + col * swatch, start.y + row_i * swatch);
        ImVec2 p1(p0.x + swatch, p0.y + swatch);
        draw_list->AddRectFilled(p0, p1, IM_COL32(rgba[0], rgba[1], rgba[2], 255));
        bool is_editing = (i == editing_swatch_index);
        draw_list->AddRect(p0, p1, is_editing ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 80), 0.0f, 0,
                            is_editing ? 2.0f : 1.0f);

        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(p0);
        // Both mouse buttons: left picks the paint color, right edits it -
        // InvisibleButton only tracks the left button unless told otherwise.
        ImGui::InvisibleButton("swatch", ImVec2(swatch, swatch),
                                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            editing_swatch_index = i;
            draw_color[0] = rgba[0] / 255.0f;
            draw_color[1] = rgba[1] / 255.0f;
            draw_color[2] = rgba[2] / 255.0f;
            draw_color[3] = 1.0f;
        }
        bool right_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        ImGui::PopID();

        // OpenPopup() hashes its id together with the *current* ID stack,
        // same as any widget - calling it while still under PushID(i) above
        // gave every swatch's popup a different id than the single
        // BeginPopup("SwatchColorPicker") opened outside this loop expects,
        // so it could never match and the picker never actually appeared.
        if (right_clicked) {
            editing_swatch_index = i;
            picker_color[0] = rgba[0] / 255.0f;
            picker_color[1] = rgba[1] / 255.0f;
            picker_color[2] = rgba[2] / 255.0f;
            picker_color[3] = 1.0f;
            ImGui::OpenPopup("SwatchColorPicker");
        }
    }
    int swatch_rows = (colors + columns - 1) / columns;
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy(ImVec2(columns * swatch, swatch_rows * swatch));

    if (ImGui::BeginPopup("SwatchColorPicker")) {
        if (ImGui::ColorPicker4("##picker", picker_color, ImGuiColorEditFlags_NoAlpha) && editing_swatch_index >= 0) {
            ApplyPaletteColorEdit(document, tim, editing_clut_row, editing_swatch_index, picker_color);
        }
        ImGui::EndPopup();
    }
}

void ImageEditorPanel::ApplyPaletteColorEdit(tim::Document& document, TIM_Image& tim, int row, int swatch,
                                              const float color[4]) {
    document.PushContentUndoSnapshot(current_index);

    uint8_t r = static_cast<uint8_t>(color[0] * 255.0f);
    uint8_t g = static_cast<uint8_t>(color[1] * 255.0f);
    uint8_t b = static_cast<uint8_t>(color[2] * 255.0f);
    uint16_t new_color = gfx::ImageQuantizer::RGBAToBGR555(r, g, b);
    int colors = tim.clut_header.colors_per_clut;
    tim.clut_data[static_cast<size_t>(row) * colors + swatch] = new_color;

    // "Editing one color should update the highest quality version": repaint
    // every master pixel currently using this index, but only meaningful
    // while this row is the one actually decoded into master_index_map.
    if (row == tim.selected_clut && !tim.master_index_map.empty()) {
        for (size_t p = 0; p < tim.master_index_map.size(); p++) {
            if (tim.master_index_map[p] == swatch) {
                tim.master_rgba[p * 4 + 0] = r;
                tim.master_rgba[p * 4 + 1] = g;
                tim.master_rgba[p * 4 + 2] = b;
                tim.master_rgba[p * 4 + 3] = (new_color == 0) ? 0 : 255;
            }
        }
    }

    // draw_color follows the swatch being edited, since it's also the
    // currently-selected paint color.
    if (swatch == editing_swatch_index) {
        draw_color[0] = color[0];
        draw_color[1] = color[1];
        draw_color[2] = color[2];
    }

    document.MarkContentDirty(current_index);
    gfx::TIMTextureBuilder::RebuildTextures(tim);
}

void ImageEditorPanel::RenderCanvas(tim::Document& document, TIM_Image& tim) {
    ImGui::BeginChild("ImgEditCanvas", ImVec2(-sidebar_width, 0), true,
                       ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGuiIO& io = ImGui::GetIO();
    int w = tim.master_width, h = tim.image_header.height;
    ImVec2 canvas_p0 = ZoomToCursor(canvas_zoom, 1.0f, 32.0f, ImVec2((float)w, (float)h),
                                     [](float z) { return ImVec2(z, z); });

    ImVec2 canvas_size(w * canvas_zoom, h * canvas_zoom);
    ImVec2 canvas_p1(canvas_p0.x + canvas_size.x, canvas_p0.y + canvas_size.y);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(50, 50, 50, 255));

    if (!tim.opengl_texture_ids.empty()) {
        uint32_t tex = tim.opengl_texture_ids[tim.selected_clut];
        ImGui::SetCursorScreenPos(canvas_p0);
        ImagePixelPerfect(tex, canvas_size);
    }

    ImGui::SetCursorScreenPos(canvas_p0);
    ImGui::InvisibleButton("canvas_paint", canvas_size);
    bool active = ImGui::IsItemActive();
    bool just_activated = ImGui::IsItemActivated();
    bool canvas_item_hovered = ImGui::IsItemHovered();

    int mouse_px = static_cast<int>((io.MousePos.x - canvas_p0.x) / canvas_zoom);
    int mouse_py = static_cast<int>((io.MousePos.y - canvas_p0.y) / canvas_zoom);
    bool in_bounds = mouse_px >= 0 && mouse_px < w && mouse_py >= 0 && mouse_py < h;

    if (active && in_bounds) {
        HandleToolInput(document, tim, mouse_px, mouse_py, just_activated);
    }
    if (tool_dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        FinishStroke();
    }

    // No OS cursor over the canvas - the hover square below stands in for
    // it, tracking exactly which pixel a click would affect.
    if (canvas_item_hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    if (in_bounds) {
        ImVec2 hp0(canvas_p0.x + mouse_px * canvas_zoom, canvas_p0.y + mouse_py * canvas_zoom);
        ImVec2 hp1(hp0.x + canvas_zoom, hp0.y + canvas_zoom);
        ImU32 cursor_color;
        switch (active_tool) {
            case Tool::Select: cursor_color = IM_COL32(60, 160, 255, 255); break;
            case Tool::Eraser: cursor_color = IM_COL32(255, 90, 90, 255); break;
            case Tool::Fill: cursor_color = IM_COL32(255, 220, 80, 255); break;
            case Tool::Eyedropper: cursor_color = IM_COL32(80, 200, 255, 255); break;
            case Tool::Line: case Tool::Rect: cursor_color = IM_COL32(120, 255, 140, 255); break;
            default: cursor_color = IM_COL32(255, 255, 255, 255); break;
        }
        draw_list->AddRect(hp0, hp1, cursor_color, 0.0f, 0, 1.5f);
    }

    if (tool_dragging && (active_tool == Tool::Line || active_tool == Tool::Rect)) {
        ImVec2 a(canvas_p0.x + drag_start_px * canvas_zoom, canvas_p0.y + drag_start_py * canvas_zoom);
        ImVec2 b(canvas_p0.x + (mouse_px + 1) * canvas_zoom, canvas_p0.y + (mouse_py + 1) * canvas_zoom);
        draw_list->AddRect(a, b, IM_COL32(255, 255, 255, 220), 0.0f, 0, 2.0f);
    }

    // The active selection's outline - offset live by the in-progress move
    // delta while it's being dragged, so it visibly follows the drag rather
    // than jumping to its new spot only on release.
    if (has_selection) {
        int ox = selection_moving ? move_delta_x : 0;
        int oy = selection_moving ? move_delta_y : 0;
        int lo_x = std::min(sel_x0, sel_x1) + ox, hi_x = std::max(sel_x0, sel_x1) + ox;
        int lo_y = std::min(sel_y0, sel_y1) + oy, hi_y = std::max(sel_y0, sel_y1) + oy;
        ImVec2 a(canvas_p0.x + lo_x * canvas_zoom, canvas_p0.y + lo_y * canvas_zoom);
        ImVec2 b(canvas_p0.x + (hi_x + 1) * canvas_zoom, canvas_p0.y + (hi_y + 1) * canvas_zoom);
        draw_list->AddRect(a, b, IM_COL32(60, 160, 255, 255), 0.0f, 0, 2.0f);
    }

    ImGui::EndChild();
}

void ImageEditorPanel::HandleToolInput(tim::Document& document, TIM_Image& tim, int px, int py, bool just_activated) {
    if (just_activated) {
        tool_dragging = true;
        drag_start_px = px;
        drag_start_py = py;
        last_paint_px = px;
        last_paint_py = py;

        // Select doesn't touch pixel data at all when it's defining a new
        // rectangle - only a move-drag (starting inside the existing
        // selection) actually mutates anything, so only that case needs
        // the undo snapshot/backup other tools always take.
        selection_moving = active_tool == Tool::Select && has_selection && px >= std::min(sel_x0, sel_x1) &&
                            px <= std::max(sel_x0, sel_x1) && py >= std::min(sel_y0, sel_y1) &&
                            py <= std::max(sel_y0, sel_y1);
        bool needs_backup = active_tool != Tool::Eyedropper && (active_tool != Tool::Select || selection_moving);
        if (needs_backup) {
            document.PushContentUndoSnapshot(current_index);
            stroke_master_backup = tim.master_rgba;
            stroke_index_backup = tim.master_index_map;
        }
        move_delta_x = 0;
        move_delta_y = 0;

        switch (active_tool) {
            case Tool::Select:
                if (!selection_moving) {
                    has_selection = true; // starts a fresh rect at the click point; grows as the drag continues
                    sel_x0 = sel_x1 = px;
                    sel_y0 = sel_y1 = py;
                }
                break;
            case Tool::Pencil:
                gfx::PaintMasterPixel(tim, px, py, false, draw_color);
                LiveUpdate(document, tim);
                break;
            case Tool::Eraser:
                gfx::PaintMasterPixel(tim, px, py, true, draw_color);
                LiveUpdate(document, tim);
                break;
            case Tool::Fill:
                gfx::FloodFillMaster(tim, px, py, draw_color, CurrentSelectionRect());
                LiveUpdate(document, tim);
                tool_dragging = false; // One-shot: dragging further does nothing until the next click.
                break;
            case Tool::Eyedropper: {
                size_t p = static_cast<size_t>(py) * tim.master_width + px;
                draw_color[0] = tim.master_rgba[p * 4 + 0] / 255.0f;
                draw_color[1] = tim.master_rgba[p * 4 + 1] / 255.0f;
                draw_color[2] = tim.master_rgba[p * 4 + 2] / 255.0f;
                draw_color[3] = tim.master_rgba[p * 4 + 3] / 255.0f;
                tool_dragging = false;
                break;
            }
            case Tool::Line:
            case Tool::Rect:
                break; // Live-previewed as the drag continues below.
        }
    } else if (tool_dragging) {
        switch (active_tool) {
            case Tool::Select:
                if (selection_moving) {
                    tim.master_rgba = stroke_master_backup;
                    tim.master_index_map = stroke_index_backup;
                    move_delta_x = px - drag_start_px;
                    move_delta_y = py - drag_start_py;
                    gfx::MoveSelectionMaster(tim, move_delta_x, move_delta_y, CurrentSelectionRect(),
                                              stroke_master_backup, stroke_index_backup);
                    LiveUpdate(document, tim);
                } else {
                    sel_x1 = px;
                    sel_y1 = py;
                }
                break;
            case Tool::Pencil:
                gfx::DrawLineMaster(tim, last_paint_px, last_paint_py, px, py, false, draw_color);
                last_paint_px = px;
                last_paint_py = py;
                LiveUpdate(document, tim);
                break;
            case Tool::Eraser:
                gfx::DrawLineMaster(tim, last_paint_px, last_paint_py, px, py, true, draw_color);
                last_paint_px = px;
                last_paint_py = py;
                LiveUpdate(document, tim);
                break;
            case Tool::Line:
                // Undo the previous frame's preview, then redraw fresh from
                // the drag's start to *this* frame's position - so the
                // preview tracks the mouse live instead of leaving a trail.
                tim.master_rgba = stroke_master_backup;
                tim.master_index_map = stroke_index_backup;
                gfx::DrawLineMaster(tim, drag_start_px, drag_start_py, px, py, false, draw_color);
                LiveUpdate(document, tim);
                break;
            case Tool::Rect:
                tim.master_rgba = stroke_master_backup;
                tim.master_index_map = stroke_index_backup;
                gfx::FillRectMaster(tim, drag_start_px, drag_start_py, px, py, rect_filled, false, draw_color);
                LiveUpdate(document, tim);
                break;
            default:
                break; // Fill/Eyedropper are one-shot.
        }
    }
}

void ImageEditorPanel::FinishStroke() {
    if (active_tool == Tool::Select) {
        if (selection_moving) {
            // The rect itself stays wherever it was defined; only its
            // pixel content moved (already committed live, frame by
            // frame, in MoveSelectionMaster) - so drag it along too.
            sel_x0 += move_delta_x;
            sel_x1 += move_delta_x;
            sel_y0 += move_delta_y;
            sel_y1 += move_delta_y;
        } else {
            if (sel_x0 > sel_x1) std::swap(sel_x0, sel_x1);
            if (sel_y0 > sel_y1) std::swap(sel_y0, sel_y1);
            // A plain click with no real drag defines nothing - treat it as
            // "click elsewhere to deselect", not a 1x1 selection.
            if (sel_x0 == sel_x1 && sel_y0 == sel_y1) has_selection = false;
        }
        selection_moving = false;
        move_delta_x = move_delta_y = 0;
    }

    tool_dragging = false;
    stroke_master_backup.clear();
    stroke_index_backup.clear();
}

gfx::SelectionRect ImageEditorPanel::CurrentSelectionRect() const {
    return { has_selection, sel_x0, sel_y0, sel_x1, sel_y1 };
}

void ImageEditorPanel::LiveUpdate(tim::Document& document, TIM_Image& tim) {
    tim::Document::ImageContent content;
    content.type = tim.type;
    content.bpp = tim.bpp;
    content.pixel_width = tim.master_width;
    content.width_words = gfx::ImageQuantizer::PixelWidthToWords(tim.master_width, tim.bpp);
    content.height = tim.image_header.height;
    content.has_clut = tim.has_clut;

    if (tim.bpp == 4 || tim.bpp == 8) {
        content.image_data = gfx::ImageQuantizer::PackIndexed(tim.master_index_map, tim.master_width,
                                                                tim.image_header.height, tim.bpp);
        content.colors_per_clut = tim.clut_header.colors_per_clut;
        content.num_cluts = tim.clut_header.num_cluts;
        content.clut_data = tim.clut_data; // Unchanged - painting nearest-matches, never regenerates the palette.
    } else {
        content.image_data = gfx::ImageQuantizer::PackDirect(tim.master_rgba, tim.master_width,
                                                               tim.image_header.height, tim.bpp);
    }
    document.ReplaceImageContent(current_index, std::move(content));
    document.MarkContentDirty(current_index);
    gfx::TIMTextureBuilder::RebuildTextures(tim);
}

} // namespace ui
