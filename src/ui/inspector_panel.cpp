#include "inspector_panel.h"
#include "palette_view.h"
#include "splitter.h"
#include "imgui.h"
#include "../core/vram_manager.h"
#include "../gfx/tim_texture_builder.h"
#include <GL/gl.h>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <cstdio>
#include <cstdarg>

namespace ui {

namespace {

std::string Fmt(const char* fmt, ...) {
    char buf[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

// Groups image indices by their source file, preserving first-seen order,
// so the file list can show each .tim as a parent with its images nested
// underneath as children.
std::vector<std::pair<std::string, std::vector<int>>> GroupByFile(const std::vector<TIM_Image>& tims) {
    std::vector<std::pair<std::string, std::vector<int>>> groups;
    for (int i = 0; i < static_cast<int>(tims.size()); i++) {
        const std::string& filename = tims[i].filename;
        auto it = std::find_if(groups.begin(), groups.end(),
                                [&](const auto& g) { return g.first == filename; });
        if (it == groups.end()) groups.push_back({ filename, { i } });
        else it->second.push_back(i);
    }
    return groups;
}

// Releases the GL textures of every image belonging to filepath. Must run
// before Document::CloseFile() drops them, or their GPU textures leak.
void ReleaseTexturesForFile(tim::Document& document, const std::string& filepath) {
    for (auto& img : document.Images()) {
        if (img.filename == filepath) gfx::TIMTextureBuilder::DeleteTextures(img);
    }
}

// One row of the properties table: a label in column 0, a value in column 1.
void PropertyRow(const char* label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value.c_str());
}

struct TPageLocation {
    int tpage_id;
    int local_x_words; // 0-63 within the tpage
    int local_y;        // 0-255 within the tpage
};

// Converts an Image Org VRAM coordinate (words on X, pixels on Y, as stored
// by the TIM format) into a PS1 tpage index and the position inside it.
TPageLocation ComputeTPageLocation(int origin_x_words, int origin_y) {
    constexpr int kTPageWidthWords = 64;
    constexpr int kTPageHeight = 256;
    constexpr int kTPagesPerRow = VRAMManager::kWidth / kTPageWidthWords; // 16

    TPageLocation loc;
    loc.tpage_id = (origin_y / kTPageHeight) * kTPagesPerRow + (origin_x_words / kTPageWidthWords);
    loc.local_x_words = origin_x_words % kTPageWidthWords;
    loc.local_y = origin_y % kTPageHeight;
    return loc;
}

} // namespace

void InspectorPanel::Render(tim::Document& document) {
    ImGui::BeginChild("List", ImVec2(list_width, 0), true);
    RenderFileList(document);
    ImGui::EndChild();

    // Dragging the splitter right grows the list (it's the left pane).
    list_width += ui::VerticalSplitter("InspectorSplitter");
    list_width = std::clamp(list_width, kListMinWidth, kListMaxWidth);

    ImGui::BeginChild("Viewer", ImVec2(0, 0), true);
    int active = document.GetActiveIndex();
    auto& tims = document.Images();
    if (active >= 0 && active < static_cast<int>(tims.size())) {
        RenderPreview(document, tims[active]);
    } else {
        ImGui::TextDisabled("No TIM file selected.");
    }
    ImGui::EndChild();
}

void InspectorPanel::RenderFileList(tim::Document& document) {
    auto& tims = document.Images();

    ImGui::Text("Loaded Files:");
    ImGui::Separator();

    bool all_selected = !tims.empty();
    for (const auto& tim : tims) {
        if (!tim.selected) { all_selected = false; break; }
    }

    if (ImGui::Checkbox("Select All", &all_selected)) {
        for (auto& tim : tims) tim.selected = all_selected;
    }
    ImGui::Separator();

    // Closing mutates document.Images() (indices shift or vanish), which
    // would corrupt the rest of this loop if done mid-iteration - so a click
    // on "x" is only recorded here and actually applied once the loop (and
    // the snapshot of file groups it walks) is done with.
    std::string close_now;

    for (auto& group : GroupByFile(tims)) {
        const std::string& filepath = group.first;
        std::vector<int>& indices = group.second;
        std::string filename = filepath.substr(filepath.find_last_of("/\\") + 1);
        bool dirty = document.IsFileDirty(filepath);

        bool group_selected = true;
        for (int idx : indices) {
            if (!tims[idx].selected) { group_selected = false; break; }
        }

        ImGui::PushID(filepath.c_str());

        // Checking this checkbox selects/deselects every image that belongs
        // to this file - selecting the "file" is a proxy for selecting all
        // of its children at once.
        if (ImGui::Checkbox("##group_chk", &group_selected)) {
            for (int idx : indices) tims[idx].selected = group_selected;
        }
        ImGui::SameLine();

        std::string header = filename + (dirty ? " *" : "") +
                              "  (" + std::to_string(indices.size()) + (indices.size() == 1 ? " image)" : " images)");
        bool node_open = ImGui::TreeNodeEx("##group_node", ImGuiTreeNodeFlags_DefaultOpen, "%s", header.c_str());

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 26.0f);
        if (ImGui::SmallButton("x")) {
            if (dirty) {
                pending_close_file = filepath;
                open_close_confirm = true;
            } else {
                close_now = filepath;
            }
        }

        if (node_open) {
            ImGui::Indent();
            for (int idx : indices) {
                TIM_Image& img = tims[idx];
                std::string chk_id = "##chk_" + std::to_string(idx);
                ImGui::Checkbox(chk_id.c_str(), &img.selected);
                ImGui::SameLine();

                std::string label = Fmt("[%d BPP] Image #%d", img.bpp, img.file_index) + "##sel_" + std::to_string(idx);
                if (ImGui::Selectable(label.c_str(), document.GetActiveIndex() == idx)) {
                    document.SetActiveIndex(idx);
                }
            }
            ImGui::Unindent();
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (!close_now.empty()) {
        ReleaseTexturesForFile(document, close_now);
        document.CloseFile(close_now);
    }

    RenderCloseConfirmPopup(document);
}

void InspectorPanel::RenderCloseConfirmPopup(tim::Document& document) {
    if (open_close_confirm) {
        ImGui::OpenPopup("Close File?");
        open_close_confirm = false;
    }

    if (ImGui::BeginPopupModal("Close File?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string filename = pending_close_file.substr(pending_close_file.find_last_of("/\\") + 1);
        ImGui::Text("\"%s\" has unsaved changes.", filename.c_str());
        ImGui::Text("Save changes before closing?");
        ImGui::Separator();

        if (ImGui::Button("Save")) {
            document.Save(pending_close_file);
            ReleaseTexturesForFile(document, pending_close_file);
            document.CloseFile(pending_close_file);
            pending_close_file.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) {
            ReleaseTexturesForFile(document, pending_close_file);
            document.CloseFile(pending_close_file);
            pending_close_file.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            pending_close_file.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void InspectorPanel::RenderPreview(tim::Document& document, TIM_Image& tim) {
    bool dirty = document.IsFileDirty(tim.filename);
    ImGui::TextUnformatted(tim.filename.c_str());
    if (dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "(unsaved changes)");
    }

    // Saving here writes the whole file this image belongs to (that's the
    // unit the TIM format saves in) but is scoped to whichever image you're
    // currently inspecting, rather than requiring a trip back to the file list.
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (ImGui::SmallButton("Save")) {
        document.Save(tim.filename);
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    ImGui::SeparatorText("Image");
    if (ImGui::BeginTable("ImageProperties", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                  ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        PropertyRow("Dimensions", Fmt("%d x %d px", tim.real_width, tim.image_header.height));
        PropertyRow("Depth", Fmt("%d BPP", tim.bpp));

        TPageLocation img_loc = ComputeTPageLocation(tim.image_header.origin_x, tim.image_header.origin_y);
        PropertyRow("VRAM Origin", Fmt("x = %d words, y = %d px", tim.image_header.origin_x, tim.image_header.origin_y));
        PropertyRow("TPage", Fmt("#%d  (local x = %d, y = %d)", img_loc.tpage_id, img_loc.local_x_words, img_loc.local_y));

        ImGui::EndTable();
    }

    if (tim.has_clut) {
        ImGui::SeparatorText("Palette");
        if (ImGui::BeginTable("ClutProperties", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                     ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            PropertyRow("CLUTs", Fmt("%d", tim.clut_header.num_cluts));
            PropertyRow("Colors per CLUT", Fmt("%d", tim.clut_header.colors_per_clut));
            PropertyRow("VRAM Origin", Fmt("x = %d words, y = %d px", tim.clut_header.origin_x, tim.clut_header.origin_y));

            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    ImGui::SliderFloat("Zoom", &zoom_level, 1.0f, 8.0f, "%.1fx");
    ImGui::TextDisabled("Scroll over the image to zoom.");
    ImGui::Spacing();

    // Palette gets a fixed-width column on the right; the image takes
    // whatever space is left (a negative BeginChild size means "stretch to
    // available minus this many pixels").
    const float kPalettePanelWidth = 176.0f;
    ImVec2 image_area_size = tim.has_clut ? ImVec2(-kPalettePanelWidth, 0) : ImVec2(0, 0);

    // NoScrollWithMouse: the wheel drives zoom below instead of panning.
    ImGui::BeginChild("ScrollArea", image_area_size, true,
                       ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // GetCursorScreenPos()/GetScrollX() still reflect the scroll from BEFORE
    // this frame's SetScrollX/Y call below (that only takes effect for the
    // window's *next* layout), so canvas_p0 is computed by hand instead of
    // re-querying ImGui after changing zoom - otherwise the image would
    // render one frame out of place, flashing at the old position on every
    // wheel tick.
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    if (ImGui::IsWindowHovered() && io.MouseWheel != 0.0f) {
        ImVec2 scroll_before(ImGui::GetScrollX(), ImGui::GetScrollY());
        ImVec2 window_origin(canvas_p0.x + scroll_before.x, canvas_p0.y + scroll_before.y);
        float content_x = (io.MousePos.x - canvas_p0.x) / zoom_level;
        float content_y = (io.MousePos.y - canvas_p0.y) / zoom_level;

        zoom_level = std::clamp(zoom_level * (1.0f + io.MouseWheel * 0.1f), 1.0f, 8.0f);

        canvas_p0 = ImVec2(io.MousePos.x - content_x * zoom_level, io.MousePos.y - content_y * zoom_level);
        ImGui::SetScrollX(window_origin.x - canvas_p0.x);
        ImGui::SetScrollY(window_origin.y - canvas_p0.y);
    }

    ImVec2 tex_size(tim.real_width * zoom_level, tim.image_header.height * zoom_level);
    if (!tim.opengl_texture_ids.empty()) {
        uint32_t active_tex = tim.opengl_texture_ids[tim.selected_clut];
        glBindTexture(GL_TEXTURE_2D, active_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        ImGui::SetCursorScreenPos(canvas_p0);
        // The backend's default sampler is linear and overrides the NEAREST
        // texture parameters set above; force point-sampling for this one
        // draw so zoomed-in pixels stay crisp instead of blurring.
        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        if (platform_io.DrawCallback_SetSamplerNearest) draw_list->AddCallback(platform_io.DrawCallback_SetSamplerNearest);
        ImGui::Image((void*)(intptr_t)active_tex, tex_size);
        if (platform_io.DrawCallback_SetSamplerLinear) draw_list->AddCallback(platform_io.DrawCallback_SetSamplerLinear);
    }
    ImGui::EndChild();

    if (tim.has_clut) {
        ImGui::SameLine();
        ImGui::BeginChild("PaletteSide", ImVec2(kPalettePanelWidth, 0), true);
        if (tim.clut_header.num_cluts > 1) {
            RenderClutSelector(tim);
            ImGui::Separator();
        }
        ImGui::Text("Palette");
        ImGui::Separator();
        // Size swatches to fill this fixed-width panel exactly, matching
        // PaletteView's own column count (16 wide for 256-color CLUTs, 8
        // otherwise), so nothing gets clipped or needs a second scrollbar.
        int columns = (tim.clut_header.colors_per_clut >= 256) ? 16 : 8;
        float swatch_size = (kPalettePanelWidth - 16.0f) / columns;
        PaletteView::Draw(tim, tim.selected_clut, swatch_size);
        ImGui::EndChild();
    }
}

void InspectorPanel::RenderClutSelector(TIM_Image& tim) {
    ImGui::Text("Select Palette:");
    ImGuiStyle& style = ImGui::GetStyle();
    float window_visible_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

    for (int c = 0; c < tim.clut_header.num_cluts; c++) {
        std::string btn_label = std::to_string(c) + "##clut" + std::to_string(c);
        bool is_selected = (tim.selected_clut == c);

        if (is_selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.7f, 0.4f, 1.0f));
        ImGui::Button(btn_label.c_str());
        if (ImGui::IsItemClicked()) tim.selected_clut = c;
        if (is_selected) ImGui::PopStyleColor();

        // Manual wrap: keep placing buttons on the same line as long as the
        // next one would still fit in this (comparatively narrow) side panel.
        float next_button_x2 = ImGui::GetItemRectMax().x + style.ItemSpacing.x + ImGui::GetItemRectSize().x;
        if (c + 1 < tim.clut_header.num_cluts && next_button_x2 < window_visible_x2) {
            ImGui::SameLine();
        }
    }
}

} // namespace ui
