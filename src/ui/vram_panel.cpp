#include "vram_panel.h"
#include <GL/gl.h>
#include <cstdio>

namespace ui {

int VRAMPanel::BppMultiplier() const {
    switch (bpp_mode) {
        case 0: return 4; // 4 BPP
        case 1: return 2; // 8 BPP
        default: return 1; // 16 BPP
    }
}

void VRAMPanel::Render(VRAMManager& vram_manager) {
    ImGui::Text("VRAM reflects the PS1 Image Org and Palette Org addresses.");
    ImGui::Separator();

    const char* bpp_modes[] = { "4 BPP", "8 BPP", "16 BPP" };
    ImGui::SetNextItemWidth(150.0f);
    ImGui::Combo("VRAM BPP Mode", &bpp_mode, bpp_modes, 3);
    ImGui::SameLine();
    ImGui::SliderFloat("VRAM Zoom", &zoom, 0.5f, 4.0f, "%.1fx");
    ImGui::Separator();

    int bpp_multiplier = BppMultiplier();
    float base_width = static_cast<float>(VRAMManager::kWidth) * bpp_multiplier;
    float base_height = static_cast<float>(VRAMManager::kHeight);
    ImVec2 canvas_size(base_width * zoom, base_height * zoom);

    ImGui::BeginChild("VRAMScroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_size.x, canvas_p0.y + canvas_size.y);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(0, 0, 0, 255));

    uint32_t vram_tex = vram_manager.GetVRAMTextureID();
    glBindTexture(GL_TEXTURE_2D, vram_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    ImGui::Image((void*)(intptr_t)vram_tex, canvas_size);

    float tpage_w = 64.0f * bpp_multiplier * zoom;
    float tpage_h = 256.0f * zoom;
    DrawTPageGrid(draw_list, canvas_p0, tpage_w, tpage_h);

    ImGui::EndChild();
}

void VRAMPanel::DrawTPageGrid(ImDrawList* draw_list, ImVec2 origin, float tpage_w, float tpage_h) const {
    for (int i = 0; i < 32; i++) {
        int col = i % 16;
        int row = i / 16;

        ImVec2 tp_p0(origin.x + col * tpage_w, origin.y + row * tpage_h);
        ImVec2 tp_p1(tp_p0.x + tpage_w, tp_p0.y + tpage_h);

        draw_list->AddRect(tp_p0, tp_p1, IM_COL32(255, 255, 255, 200));

        char tpage_str[16];
        snprintf(tpage_str, sizeof(tpage_str), "%d", i);
        draw_list->AddText(ImVec2(tp_p0.x + 4, tp_p0.y + 4), IM_COL32(255, 255, 255, 255), tpage_str);
    }
}

} // namespace ui
