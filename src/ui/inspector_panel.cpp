#include "inspector_panel.h"
#include "imgui.h"
#include "../core/vram_manager.h"
#include <GL/gl.h>
#include <string>

namespace ui {

namespace {

struct TPageLocation {
    int tpage_id;
    int local_x_words; // 0-63: posição dentro da tpage, em words
    int local_y;        // 0-255: posição dentro da tpage, em pixels
};

// Traduz a coordenada de VRAM de uma imagem (Image Org, armazenada em
// "words" no eixo X e pixels no eixo Y, como o próprio formato TIM guarda)
// para o índice da tpage do PS1 e a posição relativa dentro dela.
TPageLocation ComputeTPageLocation(int origin_x_words, int origin_y) {
    constexpr int kTPageWidthWords = 64;
    constexpr int kTPageHeight = 256;
    constexpr int kTPagesPerRow = VRAMManager::kWidth / kTPageWidthWords; // 16

    TPageLocation loc;
    int col = origin_x_words / kTPageWidthWords;
    int row = origin_y / kTPageHeight;
    loc.tpage_id = row * kTPagesPerRow + col;
    loc.local_x_words = origin_x_words % kTPageWidthWords;
    loc.local_y = origin_y % kTPageHeight;
    return loc;
}

} // namespace

void InspectorPanel::Render(std::vector<TIM_Image>& tims) {
    ImGui::BeginChild("List", ImVec2(240, 0), true);
    RenderFileList(tims);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("Viewer", ImVec2(0, 0), true);
    if (selected_index >= 0 && selected_index < static_cast<int>(tims.size())) {
        RenderPreview(tims[selected_index]);
    } else {
        ImGui::TextDisabled("No TIM file selected.");
    }
    ImGui::EndChild();
}

void InspectorPanel::RenderFileList(std::vector<TIM_Image>& tims) {
    ImGui::Text("Loaded Files:");
    ImGui::Separator();

    bool all_selected = !tims.empty();
    for (const auto& tim : tims) {
        if (!tim.selected) {
            all_selected = false;
            break;
        }
    }

    if (ImGui::Checkbox("Select All", &all_selected)) {
        for (auto& tim : tims) {
            tim.selected = all_selected;
        }
    }
    ImGui::Separator();

    for (size_t i = 0; i < tims.size(); i++) {
        std::string full_path = tims[i].filename;
        std::string filename = full_path.substr(full_path.find_last_of("/\\") + 1);

        std::string chk_id = "##chk_" + std::to_string(i);
        ImGui::Checkbox(chk_id.c_str(), &tims[i].selected);

        ImGui::SameLine();

        std::string label = "[" + std::to_string(tims[i].bpp) + "BPP] " + filename +
                             "[" + std::to_string(tims[i].file_index) + "]##" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), selected_index == static_cast<int>(i))) {
            selected_index = static_cast<int>(i);
        }
    }
}

void InspectorPanel::RenderPreview(TIM_Image& tim) {
    if (ImGui::BeginTable("TIMProperties", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Real Dimensions");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%dx%d pixels", tim.real_width, tim.image_header.height);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("Depth");
        ImGui::TableSetColumnIndex(1); ImGui::Text("%d BPP", tim.bpp);

        TPageLocation img_loc = ComputeTPageLocation(tim.image_header.origin_x, tim.image_header.origin_y);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("VRAM Position (Image)");
        ImGui::TableSetColumnIndex(1); ImGui::Text("x=%d words, y=%d px", tim.image_header.origin_x, tim.image_header.origin_y);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("TPage");
        ImGui::TableSetColumnIndex(1); ImGui::Text("#%d (local %d, %d)", img_loc.tpage_id, img_loc.local_x_words, img_loc.local_y);

        if (tim.has_clut) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("Palettes (CLUTs)");
            ImGui::TableSetColumnIndex(1); ImGui::Text("%d Found", tim.clut_header.num_cluts);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("VRAM Position (CLUT)");
            ImGui::TableSetColumnIndex(1); ImGui::Text("x=%d, y=%d px", tim.clut_header.origin_x, tim.clut_header.origin_y);
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();

    if (tim.has_clut && tim.clut_header.num_cluts > 1) {
        RenderClutSelector(tim);
        ImGui::Separator();
    }

    ImGui::SliderFloat("Zoom", &zoom_level, 1.0f, 8.0f, "%.1fx");

    ImVec2 tex_size(tim.real_width * zoom_level, tim.image_header.height * zoom_level);
    ImGui::BeginChild("ScrollArea", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    if (!tim.opengl_texture_ids.empty()) {
        uint32_t active_tex = tim.opengl_texture_ids[tim.selected_clut];
        // Reforça o sampler nearest bem no momento do desenho: a textura já
        // é criada com esse filtro (ver TIMTextureBuilder), mas garantir de
        // novo aqui custa nada e evita qualquer imagem borrada por
        // interpolação caso o estado do sampler seja alterado em outro
        // lugar do app entre a criação da textura e este frame.
        glBindTexture(GL_TEXTURE_2D, active_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        ImGui::Image((void*)(intptr_t)active_tex, tex_size);
    }
    ImGui::EndChild();
}

void InspectorPanel::RenderClutSelector(TIM_Image& tim) {
    ImGui::Text("Select Palette:");
    for (int c = 0; c < tim.clut_header.num_cluts; c++) {
        if (c > 0 && c % 8 != 0) ImGui::SameLine();
        std::string btn_label = std::to_string(c) + "##clut" + std::to_string(c);
        bool is_selected = (tim.selected_clut == c);

        if (is_selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.7f, 0.4f, 1.0f));
        }
        if (ImGui::Button(btn_label.c_str())) {
            tim.selected_clut = c;
        }
        if (is_selected) {
            ImGui::PopStyleColor();
        }
    }
}

} // namespace ui
