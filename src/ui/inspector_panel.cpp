#include "inspector_panel.h"
#include "imgui.h"
#include <string>

namespace ui {

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

        if (tim.has_clut) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("Palettes (CLUTs)");
            ImGui::TableSetColumnIndex(1); ImGui::Text("%d Found", tim.clut_header.num_cluts);
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
