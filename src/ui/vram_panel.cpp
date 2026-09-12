#include "vram_panel.h"
#include <GL/gl.h>
#include <cstdio>

namespace ui {

VRAMViewMode VRAMPanel::IndexToViewMode(int index) {
    switch (index) {
        case 0: return VRAMViewMode::Indexed4BPP;
        case 1: return VRAMViewMode::Indexed8BPP;
        default: return VRAMViewMode::Direct16BPP;
    }
}

int VRAMPanel::TPageWidthPixelsForMode(VRAMViewMode mode) {
    // Uma tpage do PS1 sempre ocupa 64 "words" de VRAM; em pixels isso
    // equivale a 256px (4 BPP), 128px (8 BPP) ou 64px (16 BPP).
    switch (mode) {
        case VRAMViewMode::Indexed4BPP: return 256;
        case VRAMViewMode::Indexed8BPP: return 128;
        case VRAMViewMode::Direct16BPP:
        default: return 64;
    }
}

void VRAMPanel::Render(VRAMManager& vram_manager) {
    ImGui::Text("VRAM reflects the PS1 Image Org and Palette Org addresses.");
    ImGui::Separator();

    const char* bpp_modes[] = { "4 BPP", "8 BPP", "16 BPP" };
    ImGui::SetNextItemWidth(150.0f);
    ImGui::Combo("VRAM BPP Mode", &bpp_mode_index, bpp_modes, 3);
    ImGui::SameLine();
    ImGui::SliderFloat("VRAM Zoom", &zoom, 0.5f, 4.0f, "%.1fx");
    ImGui::Separator();

    // Antes, o modo só mudava o tamanho do canvas e esticava a mesma
    // textura de 1024x512 em 16 BPP — agora ele manda a VRAMManager
    // reinterpretar e regenerar a textura de fato (uma textura maior e
    // "achatada" em 4/8 BPP, com cada índice cru mostrado em cinza).
    VRAMViewMode mode = IndexToViewMode(bpp_mode_index);
    vram_manager.SetViewMode(mode); // Não faz nada se o modo já for o mesmo

    ImVec2 canvas_size(vram_manager.GetViewWidth() * zoom, vram_manager.GetViewHeight() * zoom);

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

    float tpage_w = TPageWidthPixelsForMode(mode) * zoom;
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
