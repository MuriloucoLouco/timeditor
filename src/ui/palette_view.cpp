#include "palette_view.h"
#include "imgui.h"

namespace ui {

namespace {

ImU32 BGR555ToImU32(uint16_t color) {
    uint8_t r = (color & 0x1F) << 3;
    uint8_t g = ((color >> 5) & 0x1F) << 3;
    uint8_t b = ((color >> 10) & 0x1F) << 3;
    uint8_t a = (color == 0) ? 0 : 255;
    return IM_COL32(r, g, b, a);
}

} // namespace

void PaletteView::Draw(const TIM_Image& tim, int clut_index, float swatch_size) {
    if (!tim.has_clut || clut_index < 0 || clut_index >= tim.clut_header.num_cluts) return;

    int colors = tim.clut_header.colors_per_clut;
    int columns = (colors >= 256) ? 16 : 8;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 start = ImGui::GetCursorScreenPos();

    for (int i = 0; i < colors; i++) {
        uint16_t color = tim.clut_data[clut_index * colors + i];
        int col = i % columns;
        int row = i / columns;

        ImVec2 p0(start.x + col * swatch_size, start.y + row * swatch_size);
        ImVec2 p1(p0.x + swatch_size, p0.y + swatch_size);
        draw_list->AddRectFilled(p0, p1, BGR555ToImU32(color));
        draw_list->AddRect(p0, p1, IM_COL32(0, 0, 0, 60));
    }

    int rows = (colors + columns - 1) / columns;
    ImGui::Dummy(ImVec2(columns * swatch_size, rows * swatch_size));
}

} // namespace ui
