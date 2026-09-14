#include "inspector_panel.h"
#include "palette_view.h"
#include "splitter.h"
#include "gl_image.h"
#include "zoom_pan.h"
#include "text_utils.h"
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

} // namespace

bool InspectorPanel::ConsumePendingVramFocus(int& index, bool& is_clut) {
    if (!pending_vram_focus.pending) return false;
    index = pending_vram_focus.index;
    is_clut = pending_vram_focus.is_clut;
    pending_vram_focus.pending = false;
    return true;
}

void InspectorPanel::FocusImage() { view_mode = ViewMode::Image; }

void InspectorPanel::Render(tim::Document& document) {
    // A deletion evicted by a newer one (or by loading/closing a file) still
    // held its GL textures alive in case of undo; once it's truly gone,
    // release them here so they don't leak.
    TIM_Image discarded;
    if (document.TakeDiscardedDelete(discarded)) {
        gfx::TIMTextureBuilder::DeleteTextures(discarded);
    }

    ImGui::BeginChild("List", ImVec2(list_width, 0), true);
    RenderFileList(document);
    ImGui::EndChild();

    // Dragging the splitter right grows the list (it's the left pane).
    list_width += ui::VerticalSplitter("InspectorSplitter");
    list_width = std::clamp(list_width, kListMinWidth, kListMaxWidth);

    ImGui::BeginChild("Viewer", ImVec2(0, 0), true);

    int active = document.GetActiveIndex();
    if (view_mode == ViewMode::Image && (active < 0 || active >= static_cast<int>(document.Images().size()))) {
        view_mode = ViewMode::Empty; // e.g. this image was just deleted
    }

    if (view_mode == ViewMode::FileOverview) {
        RenderFileOverview(document, overview_file);
    } else if (view_mode == ViewMode::Image) {
        TIM_Image& tim = document.Images()[active];
        if (ImGui::BeginTabBar("ImageViewTabs")) {
            if (ImGui::BeginTabItem("Info")) {
                RenderPreview(document, tim);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Image Editor")) {
                image_editor.Render(document, active);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    } else {
        ImGui::TextDisabled("Select a file or image from the list.");
    }

    ImGui::EndChild();
}

void InspectorPanel::RenderFileList(tim::Document& document) {
    auto& tims = document.Images();

    ImGui::Text("Loaded Files:");
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

        // GetWindowContentRegionMax() (unlike GetWindowWidth()) already
        // excludes a visible scrollbar's width, so the close button lands
        // to its left instead of underneath it once the file list
        // overflows. The label itself is truncated to never reach that
        // column in the first place - an unframed TreeNodeEx's clickable
        // area follows its rendered text width, and since overlapping
        // ImGui items resolve first-submitted-wins, a long label would
        // otherwise swallow clicks meant for the button drawn after it.
        float close_btn_x = ImGui::GetWindowContentRegionMax().x - 22.0f;
        float label_start_x = ImGui::GetCursorPosX();
        const ImGuiStyle& style = ImGui::GetStyle();
        float max_label_width = close_btn_x - label_start_x - ImGui::GetFontSize() - style.FramePadding.x * 2 -
                                 style.ItemSpacing.x * 2 - 4.0f;
        header = TruncateToWidth(header, max_label_width);

        // OpenOnArrow: clicking the label itself (below) opens the file
        // overview instead of just expanding/collapsing the children.
        bool node_open = ImGui::TreeNodeEx("##group_node", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow,
                                            "%s", header.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", filepath.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            view_mode = ViewMode::FileOverview;
            overview_file = filepath;
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(close_btn_x);
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

                std::string label = Fmt("[%d BPP] Image #%d", img.bpp, img.file_index) + (img.dirty ? " *" : "") +
                                     "##sel_" + std::to_string(idx);
                if (ImGui::Selectable(label.c_str(), document.GetActiveIndex() == idx)) {
                    document.SetActiveIndex(idx);
                    view_mode = ViewMode::Image;
                }
            }
            ImGui::Unindent();
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (!close_now.empty()) {
        if (view_mode == ViewMode::FileOverview && overview_file == close_now) view_mode = ViewMode::Empty;
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
            if (view_mode == ViewMode::FileOverview && overview_file == pending_close_file) view_mode = ViewMode::Empty;
            pending_close_file.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard")) {
            ReleaseTexturesForFile(document, pending_close_file);
            document.CloseFile(pending_close_file);
            if (view_mode == ViewMode::FileOverview && overview_file == pending_close_file) view_mode = ViewMode::Empty;
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

void InspectorPanel::RenderFileOverview(tim::Document& document, const std::string& filepath) {
    auto& tims = document.Images();
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(tims.size()); i++) {
        if (tims[i].filename == filepath) indices.push_back(i);
    }

    std::string filename = filepath.substr(filepath.find_last_of("/\\") + 1);
    bool dirty = document.IsFileDirty(filepath);

    ImGui::TextUnformatted(filepath.c_str());
    if (dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "(unsaved changes)");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (ImGui::SmallButton("Save")) document.Save(filepath);
    ImGui::EndDisabled();
    ImGui::Separator();

    if (indices.empty()) {
        ImGui::TextDisabled("This file has no images left (closed or all deleted?).");
        return;
    }

    int bpp_seen[4] = { 0, 0, 0, 0 }; // 4, 8, 16, 24
    int total_colors = 0;
    for (int idx : indices) {
        TIM_Image& t = tims[idx];
        if (t.bpp == 4) bpp_seen[0]++;
        else if (t.bpp == 8) bpp_seen[1]++;
        else if (t.bpp == 16) bpp_seen[2]++;
        else if (t.bpp == 24) bpp_seen[3]++;
        if (t.has_clut) total_colors += t.clut_header.colors_per_clut * t.clut_header.num_cluts;
    }

    if (ImGui::BeginTable("FileStats", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                            ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        PropertyRow("Images", Fmt("%d", static_cast<int>(indices.size())));

        std::string bpp_summary;
        const int bpp_values[4] = { 4, 8, 16, 24 };
        for (int i = 0; i < 4; i++) {
            if (bpp_seen[i] == 0) continue;
            if (!bpp_summary.empty()) bpp_summary += ", ";
            bpp_summary += Fmt("%d BPP x%d", bpp_values[i], bpp_seen[i]);
        }
        PropertyRow("Depths in use", bpp_summary.empty() ? "-" : bpp_summary);
        PropertyRow("Total palette colors", Fmt("%d", total_colors));

        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button("+ Add New Image")) {
        int new_idx = document.AddBlankImage(filepath);
        if (new_idx >= 0) {
            gfx::TIMTextureBuilder::BuildTextures(document.Images()[new_idx]);
            document.SetActiveIndex(new_idx);
            view_mode = ViewMode::Image;
            return; // Jump straight into it; the grid below is now stale anyway.
        }
    }
    ImGui::Separator();

    ImGui::Text("Images:");
    ImGui::BeginChild("FileOverviewGrid", ImVec2(0, 0), false);

    const float kThumbSize = 64.0f;
    float window_visible_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    for (int idx : indices) {
        TIM_Image& t = tims[idx];
        ImGui::PushID(idx);
        ImGui::BeginGroup();

        bool clicked_thumb = false;
        if (!t.opengl_texture_ids.empty()) {
            uint32_t tex = t.opengl_texture_ids[t.selected_clut];
            float aspect = t.image_header.height > 0
                ? static_cast<float>(t.real_width) / static_cast<float>(t.image_header.height)
                : 1.0f;
            ImVec2 size = aspect >= 1.0f ? ImVec2(kThumbSize, kThumbSize / aspect)
                                          : ImVec2(kThumbSize * aspect, kThumbSize);
            clicked_thumb = ImageButtonPixelPerfect("##thumb", tex, size);
        } else {
            clicked_thumb = ImGui::Button("##thumb", ImVec2(kThumbSize, kThumbSize));
        }
        if (clicked_thumb) {
            document.SetActiveIndex(idx);
            view_mode = ViewMode::Image;
        }
        ImGui::Text("Image #%d%s", t.file_index, t.dirty ? " *" : "");
        ImGui::EndGroup();
        ImGui::PopID();

        float next_x2 = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + kThumbSize;
        if (idx != indices.back() && next_x2 < window_visible_x2) ImGui::SameLine();
    }

    ImGui::EndChild();
}

void InspectorPanel::RenderPreview(tim::Document& document, TIM_Image& tim) {
    // Shown here: whether THIS image specifically was edited (its own
    // origin/pixels/CLUT) - a file can hold several images, and this should
    // only light up for the one you're actually looking at.
    ImGui::TextUnformatted(tim.filename.c_str());
    if (tim.dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "(unsaved changes)");
    }

    // Saving here writes the whole file this image belongs to (that's the
    // unit the TIM format saves in) but is scoped to whichever image you're
    // currently inspecting, rather than requiring a trip back to the file list -
    // so it stays enabled whenever ANY image in the file needs saving, not
    // just this one.
    bool file_dirty = document.IsFileDirty(tim.filename);
    ImGui::SameLine();
    ImGui::BeginDisabled(!file_dirty);
    if (ImGui::SmallButton("Save")) {
        document.Save(tim.filename);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::SmallButton("Delete Image (Ctrl+Z to undo)")) {
        document.DeleteImage(document.GetActiveIndex());
        return; // `tim` may now be a dangling reference into a shifted vector.
    }

    ImGui::Separator();

    ImGui::SeparatorText("Image");
    if (ImGui::BeginTable("ImageProperties", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                  ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        PropertyRow("Dimensions", Fmt("%d x %d px", tim.real_width, tim.image_header.height));
        PropertyRow("Depth", Fmt("%d BPP", tim.bpp));

        TPageLocation img_loc = VRAMManager::ComputeTPageLocation(tim.image_header.origin_x, tim.image_header.origin_y);
        PropertyRow("VRAM Origin", Fmt("x = %d words, y = %d px", tim.image_header.origin_x, tim.image_header.origin_y));
        PropertyRow("TPage", Fmt("#%d  (local x = %d, y = %d)", img_loc.tpage_id, img_loc.local_x_words, img_loc.local_y));

        ImGui::EndTable();
    }
    if (ImGui::SmallButton("Go to VRAM (Image)##goto_img")) {
        pending_vram_focus = { true, document.GetActiveIndex(), false };
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
        if (ImGui::SmallButton("Go to VRAM (Palette)##goto_clut")) {
            pending_vram_focus = { true, document.GetActiveIndex(), true };
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

    ImVec2 content_units((float)tim.real_width, (float)tim.image_header.height);
    ImVec2 canvas_p0 = ZoomToCursor(zoom_level, 1.0f, 8.0f, content_units, [](float z) { return ImVec2(z, z); });

    ImVec2 tex_size(content_units.x * zoom_level, content_units.y * zoom_level);
    if (!tim.opengl_texture_ids.empty()) {
        uint32_t active_tex = tim.opengl_texture_ids[tim.selected_clut];
        ImGui::SetCursorScreenPos(canvas_p0);
        ImagePixelPerfect(active_tex, tex_size);
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
