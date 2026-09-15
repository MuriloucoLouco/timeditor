// VRAMPanel's region-drawing helpers: the TPage grid overlay, the display-
// buffer preset overlay, and the per-image/per-CLUT selection/overlap
// rectangles (plus their click/drag input handling, since each region's
// hit-test area is defined right alongside how it's drawn). Everything
// else (the scroll/zoom canvas, sidebar, keyboard nudge) lives in
// vram_panel.cpp - split purely to keep any one file from growing
// unmanageably long, see docs/ARCHITECTURE.md.
#include "vram_panel.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace ui {

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

void VRAMPanel::DrawFramebufferOverlay(ImDrawList* draw_list, ImVec2 canvas_origin, float words_scale) const {
    if (framebuffer_preset_index <= 0 || framebuffer_preset_index >= kFramebufferPresetCount) return;
    const FramebufferPreset& preset = kFramebufferPresets[framebuffer_preset_index];

    // A display buffer is always 16bpp direct color in VRAM, so its width in
    // words equals its pixel width - the same word/line addressing every
    // other region here uses.
    float buf_w = preset.width * words_scale;
    float buf_h = preset.height * zoom;

    auto draw_box = [&](float word_x, float line_y) {
        ImVec2 p0(canvas_origin.x + word_x, canvas_origin.y + line_y);
        ImVec2 p1(p0.x + buf_w, p0.y + buf_h);
        draw_list->AddRectFilled(p0, p1, IM_COL32(0, 210, 100, 55));
        draw_list->AddRect(p0, p1, IM_COL32(0, 230, 110, 210), 0.0f, 0, 2.0f);
    };

    draw_box(0.0f, 0.0f);
    if (framebuffer_double_buffered) {
        if (framebuffer_stack_horizontal) {
            if (preset.width * 2 <= VRAMManager::kWidth) draw_box(buf_w, 0.0f);
        } else {
            if (preset.height * 2 <= VRAMManager::kHeight) draw_box(0.0f, buf_h);
        }
    }
}

namespace {

std::array<int, 4> RectKey(ImVec2 p0, ImVec2 p1) {
    return { static_cast<int>(std::round(p0.x)), static_cast<int>(std::round(p0.y)),
             static_cast<int>(std::round(p1.x)), static_cast<int>(std::round(p1.y)) };
}

// Overlap must be a property of the data (VRAM word/line extents), not of
// how it's currently drawn on screen - using screen-space rects instead
// would false-positive on tiny regions inflated by the on-screen minimum
// size, and would flicker at zoom levels where floating-point rounding
// makes two logically-adjacent (but not overlapping) edges jitter by a
// fraction of a pixel. Plain integer VRAM coordinates have neither problem.
struct RectI { int x0, y0, x1, y1; };

bool RectsOverlapI(const RectI& a, const RectI& b) {
    return a.x0 < b.x1 && a.x1 > b.x0 && a.y0 < b.y1 && a.y1 > b.y0;
}

} // namespace

void VRAMPanel::DrawRegions(tim::Document& document, ImVec2 canvas_origin, float words_scale) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    auto& images = document.Images();
    int n = static_cast<int>(images.size());

    // Pre-pass: every region's screen rect (for drawing/hit-testing) and its
    // true VRAM-space rect (for overlap testing only - see RectsOverlapI).
    std::vector<ImVec2> img_p0(n), img_p1(n), clut_p0(n), clut_p1(n);
    std::vector<RectI> img_rect(n), clut_rect(n);
    for (int i = 0; i < n; i++) {
        TIM_Image& tim = images[i];
        img_p0[i] = ImVec2(canvas_origin.x + tim.image_header.origin_x * words_scale,
                            canvas_origin.y + tim.image_header.origin_y * zoom);
        img_p1[i] = ImVec2(img_p0[i].x + std::max(tim.image_header.width * words_scale, 4.0f),
                            img_p0[i].y + std::max(tim.image_header.height * zoom, 4.0f));
        img_rect[i] = { tim.image_header.origin_x, tim.image_header.origin_y,
                         tim.image_header.origin_x + tim.image_header.width,
                         tim.image_header.origin_y + tim.image_header.height };
        if (tim.has_clut) {
            clut_p0[i] = ImVec2(canvas_origin.x + tim.clut_header.origin_x * words_scale,
                                 canvas_origin.y + tim.clut_header.origin_y * zoom);
            clut_p1[i] = ImVec2(clut_p0[i].x + std::max(tim.clut_header.colors_per_clut * words_scale, 4.0f),
                                 clut_p0[i].y + std::max(tim.clut_header.num_cluts * zoom, 4.0f));
            clut_rect[i] = { tim.clut_header.origin_x, tim.clut_header.origin_y,
                              tim.clut_header.origin_x + tim.clut_header.colors_per_clut,
                              tim.clut_header.origin_y + tim.clut_header.num_cluts };
        }
    }

    struct RegionRef { int index; bool is_clut; };
    std::vector<RegionRef> all_regions;
    all_regions.reserve(n * 2);
    for (int i = 0; i < n; i++) {
        all_regions.push_back({ i, false });
        if (images[i].has_clut) all_regions.push_back({ i, true });
    }

    auto has_overlap = [&](int self_idx, bool self_clut) {
        const RectI& a = self_clut ? clut_rect[self_idx] : img_rect[self_idx];
        for (const auto& r : all_regions) {
            if (r.index == self_idx && r.is_clut == self_clut) continue;
            const RectI& b = r.is_clut ? clut_rect[r.index] : img_rect[r.index];
            if (RectsOverlapI(a, b)) return true;
        }
        return false;
    };

    int active = document.GetActiveIndex();

    // --- Pass 1: input. When two widgets overlap, ImGui resolves hover to
    // whichever was SUBMITTED FIRST this frame (the first candidate claims
    // g.HoveredId; anything else at the same spot is refused unless it
    // opts into AllowOverlap) - so the active image/CLUT must be submitted
    // first here for a click to reliably land on it instead of on some
    // other region that happens to sit underneath it. This is the opposite
    // order from pass 2's painting below, so it's a separate pass rather
    // than the same loop.
    std::vector<int> input_order(n);
    for (int i = 0; i < n; i++) input_order[i] = i;
    if (active >= 0 && active < n) {
        auto it = std::find(input_order.begin(), input_order.end(), active);
        if (it != input_order.end()) {
            input_order.erase(it);
            input_order.insert(input_order.begin(), active);
        }
    }

    for (int i : input_order) {
        TIM_Image& tim = images[i];

        // --- Image input ---
        {
            ImVec2 p0 = img_p0[i], p1 = img_p1[i];

            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(p0);
            ImGui::InvisibleButton("img_region", ImVec2(p1.x - p0.x, p1.y - p0.y));
            ImGui::PopID();

            if (ImGui::IsItemActivated()) {
                int selected_count = static_cast<int>(std::count_if(images.begin(), images.end(),
                                                                      [](const TIM_Image& t) { return t.selected; }));
                if (io.KeyShift && image_selection_anchor >= 0) {
                    int lo = std::min(image_selection_anchor, i), hi = std::max(image_selection_anchor, i);
                    for (auto& img : images) img.selected = false;
                    for (int k = lo; k <= hi; k++) images[k].selected = true;
                    selected_cluts.clear(); // Images and CLUTs never selected together.
                    clut_selection_anchor = -1;
                } else if (io.KeyCtrl) {
                    tim.selected = !tim.selected;
                    image_selection_anchor = i;
                    selected_cluts.clear();
                    clut_selection_anchor = -1;
                } else if (tim.selected) {
                    // Already selected: don't change selection yet, this
                    // click might be about to drag the whole group. On
                    // release: deselect if it was the only one selected,
                    // otherwise collapse down to just this item.
                    drag.pending_click_reset = true;
                    drag.click_reset_index = i;
                    drag.click_deselect_if_sole = (selected_count == 1);
                } else {
                    for (auto& img : images) img.selected = false;
                    tim.selected = true;
                    image_selection_anchor = i;
                    selected_cluts.clear();
                    clut_selection_anchor = -1;
                }

                if (tim.selected) {
                    document.SetActiveIndex(i);
                    drag.target = DragTarget::Images;
                    drag.mouse_start = io.MousePos;
                    drag.original_origins.clear();
                    for (int idx = 0; idx < n; idx++) {
                        if (images[idx].selected) {
                            drag.original_origins.push_back({ idx, ImVec2(images[idx].image_header.origin_x,
                                                                            images[idx].image_header.origin_y) });
                        }
                    }
                    document.PushUndoSnapshot();
                }
            }

        }

        // --- CLUT input ---
        if (tim.has_clut) {
            ImVec2 p0 = clut_p0[i], p1 = clut_p1[i];

            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(p0);
            ImGui::InvisibleButton("clut_region", ImVec2(p1.x - p0.x, p1.y - p0.y));
            ImGui::PopID();

            bool is_selected = std::find(selected_cluts.begin(), selected_cluts.end(), i) != selected_cluts.end();

            if (ImGui::IsItemActivated()) {
                int selected_count = static_cast<int>(selected_cluts.size());
                if (io.KeyShift && clut_selection_anchor >= 0) {
                    int lo = std::min(clut_selection_anchor, i), hi = std::max(clut_selection_anchor, i);
                    selected_cluts.clear();
                    for (int k = lo; k <= hi; k++) {
                        if (images[k].has_clut) selected_cluts.push_back(k);
                    }
                    is_selected = std::find(selected_cluts.begin(), selected_cluts.end(), i) != selected_cluts.end();
                    for (auto& img : images) img.selected = false; // Images and CLUTs never selected together.
                    image_selection_anchor = -1;
                } else if (io.KeyCtrl) {
                    auto it = std::find(selected_cluts.begin(), selected_cluts.end(), i);
                    if (it != selected_cluts.end()) { selected_cluts.erase(it); is_selected = false; }
                    else { selected_cluts.push_back(i); is_selected = true; }
                    clut_selection_anchor = i;
                    for (auto& img : images) img.selected = false;
                    image_selection_anchor = -1;
                } else if (is_selected) {
                    drag.pending_click_reset = true;
                    drag.click_reset_index = i;
                    drag.click_deselect_if_sole = (selected_count == 1);
                } else {
                    selected_cluts.assign(1, i);
                    clut_selection_anchor = i;
                    is_selected = true;
                    for (auto& img : images) img.selected = false;
                    image_selection_anchor = -1;
                }

                if (is_selected) {
                    drag.target = DragTarget::Cluts;
                    drag.mouse_start = io.MousePos;
                    drag.original_origins.clear();
                    for (int idx : selected_cluts) {
                        drag.original_origins.push_back({ idx, ImVec2(images[idx].clut_header.origin_x,
                                                                        images[idx].clut_header.origin_y) });
                    }
                    document.PushUndoSnapshot();
                }
            }

        }
    }

    // --- Pass 2: visuals. Drawn in the opposite order from pass 1 above -
    // the active region is painted LAST here so its highlight color ends
    // up on top and is never hidden by an overlapping duplicate underneath
    // it, even though it had to be the FIRST one submitted for input.
    std::vector<int> draw_order(n);
    for (int i = 0; i < n; i++) draw_order[i] = i;
    if (active >= 0 && active < n) {
        auto it = std::find(draw_order.begin(), draw_order.end(), active);
        if (it != draw_order.end()) {
            draw_order.erase(it);
            draw_order.push_back(active);
        }
    }

    // When several regions occupy the exact same rect, only the first one
    // gets its border drawn - otherwise identical borders stack and read as
    // one abnormally thick line. The active region is always force-drawn
    // (on top, last) regardless, so its highlight is never hidden by an
    // earlier duplicate.
    std::vector<std::array<int, 4>> drawn_image_borders, drawn_clut_borders;

    for (int i : draw_order) {
        TIM_Image& tim = images[i];
        bool is_active = (i == active);

        // --- Image visuals ---
        {
            ImVec2 p0 = img_p0[i], p1 = img_p1[i];

            // Green highlights this image: bright when it's directly
            // selected, dim when it isn't but its CLUT is (so you can see
            // which image a selected palette belongs to). Both are an
            // actual fill, not just a border color, so it reads clearly.
            bool clut_selected_here = tim.has_clut &&
                std::find(selected_cluts.begin(), selected_cluts.end(), i) != selected_cluts.end();
            ImU32 color;
            float thickness;
            ImU32 fill = 0;
            if (tim.selected) { color = IM_COL32(60, 220, 110, 255); thickness = 2.5f; fill = IM_COL32(60, 220, 110, 130); }
            else if (clut_selected_here) { color = IM_COL32(60, 200, 110, 170); thickness = 1.5f; fill = IM_COL32(60, 200, 110, 90); }
            else { color = IM_COL32(120, 120, 120, 180); thickness = 1.0f; }

            if (fill != 0) draw_list->AddRectFilled(p0, p1, fill);
            // Overlap warning tint goes on top of the highlight fill so it's
            // always noticeable even on a selected/highlighted region.
            if (has_overlap(i, false)) {
                draw_list->AddRectFilled(p0, p1, IM_COL32(255, 40, 40, 140));
            }

            auto key = RectKey(p0, p1);
            bool already_drawn = std::find(drawn_image_borders.begin(), drawn_image_borders.end(), key) !=
                                  drawn_image_borders.end();
            if (is_active || !already_drawn) {
                draw_list->AddRect(p0, p1, color, 0.0f, 0, thickness);
            }
            drawn_image_borders.push_back(key);
        }

        // --- CLUT visuals ---
        if (tim.has_clut) {
            ImVec2 p0 = clut_p0[i], p1 = clut_p1[i];
            bool is_selected = std::find(selected_cluts.begin(), selected_cluts.end(), i) != selected_cluts.end();

            // Yellow highlights this CLUT: bright when directly selected,
            // dim when its owning image is selected instead (so you can see
            // which palette a selected image is using). Both are an actual
            // fill, not just a border color, so the highlight reads clearly.
            ImU32 color;
            float thickness;
            ImU32 fill = 0;
            if (is_selected) { color = IM_COL32(255, 210, 80, 255); thickness = 2.5f; fill = IM_COL32(255, 210, 80, 130); }
            else if (tim.selected) { color = IM_COL32(255, 225, 140, 170); thickness = 1.5f; fill = IM_COL32(255, 225, 140, 90); }
            else { color = IM_COL32(200, 160, 60, 160); thickness = 1.0f; }

            if (fill != 0) draw_list->AddRectFilled(p0, p1, fill);

            // No overlap tint here on purpose: CLUTs stacking on top of each
            // other is normal (e.g. sharing one uploaded palette by address),
            // unlike an image overlapping something, which is flagged above.

            auto key = RectKey(p0, p1);
            bool already_drawn = std::find(drawn_clut_borders.begin(), drawn_clut_borders.end(), key) !=
                                  drawn_clut_borders.end();
            if (is_active || !already_drawn) {
                draw_list->AddRect(p0, p1, color, 0.0f, 0, thickness);
            }
            drawn_clut_borders.push_back(key);
        }
    }
}

} // namespace ui
