#include "image_editor_panel.h"
#include "../core/vram_manager.h"
#include "../gfx/image_quantizer.h"
#include "../gfx/tim_texture_builder.h"
#include <GL/gl.h>
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
        editing_swatch_index = -1;
        active_tool = Tool::Pencil;
        tool_dragging = false;
        canvas_zoom = 8.0f;
        resize_w = tim.master_width;
        resize_h = tim.image_header.height;
    }

    if (ImGui::Button("Save")) document.Save(tim.filename);
    if (tim.dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "(unsaved changes)");
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 140.0f);
    if (ImGui::Button("Import Image...")) import_dialog.Open(index);
    import_dialog.Render(document);

    RenderBppDropdown(document, tim);
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
    gfx::TIMTextureBuilder::RebuildTextures(tim);
}

void ImageEditorPanel::RenderToolbar(TIM_Image& tim) {
    ImGui::SeparatorText("Tools");
    struct Entry { const char* label; Tool tool; };
    Entry tools[] = { { "Pencil", Tool::Pencil },   { "Eraser", Tool::Eraser }, { "Fill", Tool::Fill },
                       { "Eyedropper", Tool::Eyedropper }, { "Line", Tool::Line },     { "Rect", Tool::Rect } };
    for (int i = 0; i < 6; i++) {
        bool selected = active_tool == tools[i].tool;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.55f, 0.35f, 1.0f));
        if (ImGui::Button(tools[i].label, ImVec2(76.0f, 0))) active_tool = tools[i].tool;
        if (selected) ImGui::PopStyleColor();
        if (i % 2 == 0) ImGui::SameLine();
    }
    if (active_tool == Tool::Rect) ImGui::Checkbox("Filled", &rect_filled);

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
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputInt("W##resizew", &resize_w);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputInt("H##resizeh", &resize_h);
    ImGui::SameLine();
    if (ImGui::Button("Apply##resize")) ApplyCanvasResize(document, tim);
}

void ImageEditorPanel::ApplyCanvasResize(tim::Document& document, TIM_Image& tim) {
    document.PushContentUndoSnapshot(current_index);

    int new_w = std::max(1, resize_w);
    int new_h = std::max(1, resize_h);
    int px_width = gfx::ImageQuantizer::RoundWidthForBpp(new_w, tim.bpp);

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
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    bool hovered = ImGui::IsWindowHovered();
    if (hovered && io.MouseWheel != 0.0f) {
        ImVec2 scroll_before(ImGui::GetScrollX(), ImGui::GetScrollY());
        ImVec2 window_origin(canvas_p0.x + scroll_before.x, canvas_p0.y + scroll_before.y);
        float content_x = (io.MousePos.x - canvas_p0.x) / canvas_zoom;
        float content_y = (io.MousePos.y - canvas_p0.y) / canvas_zoom;

        canvas_zoom = std::clamp(canvas_zoom * (1.0f + io.MouseWheel * 0.1f), 1.0f, 32.0f);

        canvas_p0 = ImVec2(io.MousePos.x - content_x * canvas_zoom, io.MousePos.y - content_y * canvas_zoom);
        ImGui::SetScrollX(window_origin.x - canvas_p0.x);
        ImGui::SetScrollY(window_origin.y - canvas_p0.y);
    }

    int w = tim.master_width, h = tim.image_header.height;
    ImVec2 canvas_size(w * canvas_zoom, h * canvas_zoom);
    ImVec2 canvas_p1(canvas_p0.x + canvas_size.x, canvas_p0.y + canvas_size.y);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(50, 50, 50, 255));

    if (!tim.opengl_texture_ids.empty()) {
        uint32_t tex = tim.opengl_texture_ids[tim.selected_clut];
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        ImGui::SetCursorScreenPos(canvas_p0);
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        if (platform_io.DrawCallback_SetSamplerNearest) draw_list->AddCallback(platform_io.DrawCallback_SetSamplerNearest);
        ImGui::Image((void*)(intptr_t)tex, canvas_size);
        if (platform_io.DrawCallback_SetSamplerLinear) draw_list->AddCallback(platform_io.DrawCallback_SetSamplerLinear);
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

    ImGui::EndChild();
}

void ImageEditorPanel::HandleToolInput(tim::Document& document, TIM_Image& tim, int px, int py, bool just_activated) {
    if (just_activated) {
        tool_dragging = true;
        drag_start_px = px;
        drag_start_py = py;
        last_paint_px = px;
        last_paint_py = py;

        if (active_tool != Tool::Eyedropper) {
            document.PushContentUndoSnapshot(current_index);
            stroke_master_backup = tim.master_rgba;
            stroke_index_backup = tim.master_index_map;
        }

        switch (active_tool) {
            case Tool::Pencil:
                PaintMasterPixel(tim, px, py, false);
                LiveUpdate(document, tim);
                break;
            case Tool::Eraser:
                PaintMasterPixel(tim, px, py, true);
                LiveUpdate(document, tim);
                break;
            case Tool::Fill:
                FloodFillMaster(tim, px, py);
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
            case Tool::Pencil:
                DrawLineMaster(tim, last_paint_px, last_paint_py, px, py, false);
                last_paint_px = px;
                last_paint_py = py;
                LiveUpdate(document, tim);
                break;
            case Tool::Eraser:
                DrawLineMaster(tim, last_paint_px, last_paint_py, px, py, true);
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
                DrawLineMaster(tim, drag_start_px, drag_start_py, px, py, false);
                LiveUpdate(document, tim);
                break;
            case Tool::Rect:
                tim.master_rgba = stroke_master_backup;
                tim.master_index_map = stroke_index_backup;
                FillRectMaster(tim, drag_start_px, drag_start_py, px, py, rect_filled, false);
                LiveUpdate(document, tim);
                break;
            default:
                break; // Fill/Eyedropper are one-shot.
        }
    }
}

void ImageEditorPanel::FinishStroke() {
    tool_dragging = false;
    stroke_master_backup.clear();
    stroke_index_backup.clear();
}

void ImageEditorPanel::PaintMasterPixel(TIM_Image& tim, int px, int py, bool erase) {
    if (px < 0 || py < 0 || px >= tim.master_width || py >= tim.image_header.height) return;
    size_t p = static_cast<size_t>(py) * tim.master_width + px;

    if (erase) {
        tim.master_rgba[p * 4 + 0] = 0;
        tim.master_rgba[p * 4 + 1] = 0;
        tim.master_rgba[p * 4 + 2] = 0;
        tim.master_rgba[p * 4 + 3] = 0;
        if (!tim.master_index_map.empty()) tim.master_index_map[p] = 0; // index 0: transparent color-key convention
        return;
    }

    uint8_t r = static_cast<uint8_t>(draw_color[0] * 255.0f);
    uint8_t g = static_cast<uint8_t>(draw_color[1] * 255.0f);
    uint8_t b = static_cast<uint8_t>(draw_color[2] * 255.0f);
    uint8_t a = static_cast<uint8_t>(draw_color[3] * 255.0f);
    tim.master_rgba[p * 4 + 0] = r;
    tim.master_rgba[p * 4 + 1] = g;
    tim.master_rgba[p * 4 + 2] = b;
    tim.master_rgba[p * 4 + 3] = a;

    if (!tim.master_index_map.empty()) {
        int colors = tim.clut_header.colors_per_clut;
        auto begin = tim.clut_data.begin() + static_cast<long>(tim.selected_clut) * colors;
        std::vector<uint16_t> row(begin, begin + colors);
        tim.master_index_map[p] = static_cast<uint8_t>(gfx::ImageQuantizer::NearestPaletteIndex(row, r, g, b, a));
    }
}

void ImageEditorPanel::DrawLineMaster(TIM_Image& tim, int x0, int y0, int x1, int y1, bool erase) {
    int dx = std::abs(x1 - x0), sx = x1 >= x0 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y1 >= y0 ? 1 : -1;
    int err = dx + dy;
    int x = x0, y = y0;
    while (true) {
        PaintMasterPixel(tim, x, y, erase);
        if (x == x1 && y == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}

void ImageEditorPanel::FillRectMaster(TIM_Image& tim, int x0, int y0, int x1, int y1, bool filled, bool erase) {
    int lo_x = std::min(x0, x1), hi_x = std::max(x0, x1);
    int lo_y = std::min(y0, y1), hi_y = std::max(y0, y1);
    if (filled) {
        for (int y = lo_y; y <= hi_y; y++) {
            for (int x = lo_x; x <= hi_x; x++) PaintMasterPixel(tim, x, y, erase);
        }
    } else {
        for (int x = lo_x; x <= hi_x; x++) {
            PaintMasterPixel(tim, x, lo_y, erase);
            PaintMasterPixel(tim, x, hi_y, erase);
        }
        for (int y = lo_y; y <= hi_y; y++) {
            PaintMasterPixel(tim, lo_x, y, erase);
            PaintMasterPixel(tim, hi_x, y, erase);
        }
    }
}

void ImageEditorPanel::FloodFillMaster(TIM_Image& tim, int px, int py) {
    int w = tim.master_width, h = tim.image_header.height;
    if (px < 0 || py < 0 || px >= w || py >= h) return;

    size_t start = static_cast<size_t>(py) * w + px;
    uint8_t target[4] = { tim.master_rgba[start * 4 + 0], tim.master_rgba[start * 4 + 1],
                           tim.master_rgba[start * 4 + 2], tim.master_rgba[start * 4 + 3] };
    uint8_t fill[4] = { static_cast<uint8_t>(draw_color[0] * 255.0f), static_cast<uint8_t>(draw_color[1] * 255.0f),
                         static_cast<uint8_t>(draw_color[2] * 255.0f), static_cast<uint8_t>(draw_color[3] * 255.0f) };
    if (target[0] == fill[0] && target[1] == fill[1] && target[2] == fill[2] && target[3] == fill[3]) return;

    std::vector<bool> visited(static_cast<size_t>(w) * h, false);
    std::vector<int> stack;
    stack.push_back(py * w + px);
    visited[start] = true;

    while (!stack.empty()) {
        int idx = stack.back();
        stack.pop_back();
        int x = idx % w, y = idx / w;
        PaintMasterPixel(tim, x, y, false);

        int neighbors[4][2] = { { x - 1, y }, { x + 1, y }, { x, y - 1 }, { x, y + 1 } };
        for (auto& n : neighbors) {
            int nx = n[0], ny = n[1];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            size_t ni = static_cast<size_t>(ny) * w + nx;
            if (visited[ni]) continue;
            const uint8_t* c = &tim.master_rgba[ni * 4];
            if (c[0] == target[0] && c[1] == target[1] && c[2] == target[2] && c[3] == target[3]) {
                visited[ni] = true;
                stack.push_back(ny * w + nx);
            }
        }
    }
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
