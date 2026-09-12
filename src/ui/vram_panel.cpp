#include "vram_panel.h"
#include <GL/gl.h>
#include <cstdio>
#include <cmath>

namespace ui {

VRAMViewMode VRAMPanel::IndexToViewMode(int index) {
    switch (index) {
        case 0: return VRAMViewMode::Indexed4BPP;
        case 1: return VRAMViewMode::Indexed8BPP;
        default: return VRAMViewMode::Direct16BPP;
    }
}

int VRAMPanel::TPageWidthPixelsForMode(VRAMViewMode mode) {
    // A PS1 tpage is always 64 VRAM words wide.
    switch (mode) {
        case VRAMViewMode::Indexed4BPP: return 256;
        case VRAMViewMode::Indexed8BPP: return 128;
        default: return 64;
    }
}

void VRAMPanel::Render(tim::Document& document, VRAMManager& vram_manager) {
    ImGui::Text("VRAM reflects the PS1 Image Org and Palette Org addresses.");
    ImGui::TextDisabled("Click to select, Ctrl/Shift to multi-select, drag to move.");
    ImGui::Separator();

    const char* bpp_modes[] = { "4 BPP", "8 BPP", "16 BPP" };
    ImGui::SetNextItemWidth(150.0f);
    ImGui::Combo("VRAM BPP Mode", &bpp_mode_index, bpp_modes, 3);
    ImGui::SameLine();
    ImGui::SliderFloat("VRAM Zoom", &zoom, 0.5f, 4.0f, "%.1fx");
    ImGui::SameLine();
    ImGui::Checkbox("Snap to TPage grid / other TIMs", &snap_enabled);
    ImGui::Separator();

    // Images/CLUTs can shift or disappear from Images() when files are
    // closed; any cached index (selection, drag, anchors) is now stale.
    if (last_structure_version != document.GetStructureVersion()) {
        selected_cluts.clear();
        image_selection_anchor = -1;
        clut_selection_anchor = -1;
        drag = DragState{};
        last_structure_version = document.GetStructureVersion();
    }

    VRAMViewMode mode = IndexToViewMode(bpp_mode_index);
    vram_manager.SetViewMode(mode);

    // The document is the single source of truth for where everything sits
    // in VRAM; whenever it changes (load, move), rebuild the emulated VRAM
    // from scratch so moved images never leave a stale copy behind.
    if (last_vram_version != document.GetVramVersion()) {
        vram_manager.RebuildFromImages(document.Images());
        last_vram_version = document.GetVramVersion();
    }

    // Pixels-per-VRAM-word on screen: combines the current BPP mode's pixel
    // density with the zoom level, used to place/size every overlay region.
    float words_scale = (static_cast<float>(vram_manager.GetViewWidth()) / VRAMManager::kWidth) * zoom;
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

    ImGui::SetCursorScreenPos(canvas_p0);
    ImGui::Image((void*)(intptr_t)vram_tex, canvas_size);

    float tpage_w = TPageWidthPixelsForMode(mode) * zoom;
    float tpage_h = 256.0f * zoom;
    DrawTPageGrid(draw_list, canvas_p0, tpage_w, tpage_h);

    DrawRegions(document, canvas_p0, words_scale);
    UpdateDrag(document, words_scale);

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

void VRAMPanel::DrawRegions(tim::Document& document, ImVec2 canvas_origin, float words_scale) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    auto& images = document.Images();

    for (int i = 0; i < static_cast<int>(images.size()); i++) {
        TIM_Image& tim = images[i];

        // --- Image bounds ---
        {
            ImVec2 p0(canvas_origin.x + tim.image_header.origin_x * words_scale,
                      canvas_origin.y + tim.image_header.origin_y * zoom);
            ImVec2 p1(p0.x + std::max(tim.image_header.width * words_scale, 4.0f),
                      p0.y + std::max(tim.image_header.height * zoom, 4.0f));

            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(p0);
            ImGui::InvisibleButton("img_region", ImVec2(p1.x - p0.x, p1.y - p0.y));
            ImGui::PopID();

            if (ImGui::IsItemActivated()) {
                if (io.KeyShift && image_selection_anchor >= 0) {
                    int lo = std::min(image_selection_anchor, i), hi = std::max(image_selection_anchor, i);
                    for (auto& img : images) img.selected = false;
                    for (int k = lo; k <= hi; k++) images[k].selected = true;
                } else if (io.KeyCtrl) {
                    tim.selected = !tim.selected;
                    image_selection_anchor = i;
                } else if (tim.selected) {
                    // Already part of a multi-selection: don't collapse it yet,
                    // this click might be about to drag the whole group.
                    drag.pending_click_reset = true;
                    drag.click_reset_index = i;
                } else {
                    for (auto& img : images) img.selected = false;
                    tim.selected = true;
                    image_selection_anchor = i;
                }

                if (tim.selected) {
                    document.SetActiveIndex(i);
                    drag.target = DragTarget::Images;
                    drag.mouse_start = io.MousePos;
                    drag.original_origins.clear();
                    for (int idx = 0; idx < static_cast<int>(images.size()); idx++) {
                        if (images[idx].selected) {
                            drag.original_origins.push_back({ idx, ImVec2(images[idx].image_header.origin_x,
                                                                            images[idx].image_header.origin_y) });
                        }
                    }
                }
            }

            ImU32 color = tim.selected ? IM_COL32(80, 200, 255, 255) : IM_COL32(120, 120, 120, 180);
            draw_list->AddRect(p0, p1, color, 0.0f, 0, tim.selected ? 2.0f : 1.0f);
        }

        // --- CLUT bounds ---
        if (tim.has_clut) {
            ImVec2 p0(canvas_origin.x + tim.clut_header.origin_x * words_scale,
                      canvas_origin.y + tim.clut_header.origin_y * zoom);
            ImVec2 p1(p0.x + std::max(tim.clut_header.colors_per_clut * words_scale, 4.0f),
                      p0.y + std::max(tim.clut_header.num_cluts * zoom, 4.0f));

            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(p0);
            ImGui::InvisibleButton("clut_region", ImVec2(p1.x - p0.x, p1.y - p0.y));
            ImGui::PopID();

            bool is_selected = std::find(selected_cluts.begin(), selected_cluts.end(), i) != selected_cluts.end();

            if (ImGui::IsItemActivated()) {
                if (io.KeyShift && clut_selection_anchor >= 0) {
                    int lo = std::min(clut_selection_anchor, i), hi = std::max(clut_selection_anchor, i);
                    selected_cluts.clear();
                    for (int k = lo; k <= hi; k++) {
                        if (images[k].has_clut) selected_cluts.push_back(k);
                    }
                    is_selected = std::find(selected_cluts.begin(), selected_cluts.end(), i) != selected_cluts.end();
                } else if (io.KeyCtrl) {
                    auto it = std::find(selected_cluts.begin(), selected_cluts.end(), i);
                    if (it != selected_cluts.end()) { selected_cluts.erase(it); is_selected = false; }
                    else { selected_cluts.push_back(i); is_selected = true; }
                    clut_selection_anchor = i;
                } else if (is_selected) {
                    drag.pending_click_reset = true;
                    drag.click_reset_index = i;
                } else {
                    selected_cluts.assign(1, i);
                    clut_selection_anchor = i;
                    is_selected = true;
                }

                if (is_selected) {
                    drag.target = DragTarget::Cluts;
                    drag.mouse_start = io.MousePos;
                    drag.original_origins.clear();
                    for (int idx : selected_cluts) {
                        drag.original_origins.push_back({ idx, ImVec2(images[idx].clut_header.origin_x,
                                                                        images[idx].clut_header.origin_y) });
                    }
                }
            }

            ImU32 color = is_selected ? IM_COL32(255, 210, 80, 255) : IM_COL32(200, 160, 60, 160);
            draw_list->AddRect(p0, p1, color, 0.0f, 0, is_selected ? 2.0f : 1.0f);
        }
    }
}

int VRAMPanel::SnapToNearest(int candidate, const std::vector<int>& lines, float scale, float max_screen_px) {
    int best = candidate;
    float best_dist = max_screen_px;
    for (int line : lines) {
        float dist = std::abs(static_cast<float>(candidate - line)) * scale;
        if (dist < best_dist) {
            best_dist = dist;
            best = line;
        }
    }
    return best;
}

void VRAMPanel::UpdateDrag(tim::Document& document, float words_scale) {
    if (drag.target == DragTarget::None) return;

    auto& images = document.Images();
    ImVec2 mouse_pos = ImGui::GetIO().MousePos;
    float moved_dist = std::hypot(mouse_pos.x - drag.mouse_start.x, mouse_pos.y - drag.mouse_start.y);

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        // A plain click (no actual drag) on an already-selected item
        // collapses the selection down to just that item.
        if (moved_dist < 3.0f && drag.pending_click_reset) {
            if (drag.target == DragTarget::Images) {
                for (auto& img : images) img.selected = false;
                images[drag.click_reset_index].selected = true;
                image_selection_anchor = drag.click_reset_index;
                document.SetActiveIndex(drag.click_reset_index);
            } else {
                selected_cluts.assign(1, drag.click_reset_index);
                clut_selection_anchor = drag.click_reset_index;
            }
        }
        drag.target = DragTarget::None;
        drag.original_origins.clear();
        drag.pending_click_reset = false;
        drag.click_reset_index = -1;
        return;
    }

    int dx_words = static_cast<int>((mouse_pos.x - drag.mouse_start.x) / words_scale);
    int dy_units = static_cast<int>((mouse_pos.y - drag.mouse_start.y) / zoom);

    if (snap_enabled && !drag.original_origins.empty()) {
        // Snap candidates: every TPage grid line, plus the edges of every
        // region not currently being dragged (so dragged items can align to
        // the TPage grid or to any other TIM already placed in VRAM).
        std::vector<int> x_lines, y_lines;
        for (int x = 0; x <= VRAMManager::kWidth; x += 64) x_lines.push_back(x);
        for (int y = 0; y <= VRAMManager::kHeight; y += 256) y_lines.push_back(y);

        auto is_dragged = [this](int idx) {
            return std::any_of(drag.original_origins.begin(), drag.original_origins.end(),
                                [idx](const std::pair<int, ImVec2>& e) { return e.first == idx; });
        };

        for (int idx = 0; idx < static_cast<int>(images.size()); idx++) {
            if (!(drag.target == DragTarget::Images && is_dragged(idx))) {
                x_lines.push_back(images[idx].image_header.origin_x);
                x_lines.push_back(images[idx].image_header.origin_x + images[idx].image_header.width);
                y_lines.push_back(images[idx].image_header.origin_y);
                y_lines.push_back(images[idx].image_header.origin_y + images[idx].image_header.height);
            }
            if (images[idx].has_clut && !(drag.target == DragTarget::Cluts && is_dragged(idx))) {
                x_lines.push_back(images[idx].clut_header.origin_x);
                x_lines.push_back(images[idx].clut_header.origin_x + images[idx].clut_header.colors_per_clut);
                y_lines.push_back(images[idx].clut_header.origin_y);
                y_lines.push_back(images[idx].clut_header.origin_y + images[idx].clut_header.num_cluts);
            }
        }

        // Snap the first dragged item's origin, then apply the same offset
        // to the whole group so a multi-selection keeps moving together.
        const auto& primary = drag.original_origins.front();
        int candidate_x = static_cast<int>(primary.second.x) + dx_words;
        int candidate_y = static_cast<int>(primary.second.y) + dy_units;
        int snapped_x = SnapToNearest(candidate_x, x_lines, words_scale, 8.0f);
        int snapped_y = SnapToNearest(candidate_y, y_lines, zoom, 8.0f);
        dx_words = snapped_x - static_cast<int>(primary.second.x);
        dy_units = snapped_y - static_cast<int>(primary.second.y);
    }

    for (auto& entry : drag.original_origins) {
        int index = entry.first;
        int new_x = static_cast<int>(entry.second.x) + dx_words;
        int new_y = static_cast<int>(entry.second.y) + dy_units;
        if (drag.target == DragTarget::Images) document.SetImageOrigin(index, new_x, new_y);
        else document.SetClutOrigin(index, new_x, new_y);
    }
}

} // namespace ui
