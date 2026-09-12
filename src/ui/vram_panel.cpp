#include "vram_panel.h"
#include "splitter.h"
#include <GL/gl.h>
#include <cstdio>
#include <cmath>

namespace ui {

const VRAMPanel::FramebufferPreset VRAMPanel::kFramebufferPresets[] = {
    { "None",              0,   0   },
    { "NTSC 256x240",      256, 240 },
    { "NTSC 320x240",      320, 240 },
    { "NTSC 384x240",      384, 240 },
    { "NTSC 512x240",      512, 240 },
    { "NTSC 640x240",      640, 240 },
    { "NTSC 256x480i",     256, 480 },
    { "NTSC 320x480i",     320, 480 },
    { "NTSC 384x480i",     384, 480 },
    { "NTSC 512x480i",     512, 480 },
    { "NTSC 640x480i",     640, 480 },
    { "PAL 256x256",       256, 256 },
    { "PAL 320x256",       320, 256 },
    { "PAL 384x256",       384, 256 },
    { "PAL 512x256",       512, 256 },
    { "PAL 640x256",       640, 256 },
    { "PAL 256x512i",      256, 512 },
    { "PAL 320x512i",      320, 512 },
    { "PAL 384x512i",      384, 512 },
    { "PAL 512x512i",      512, 512 },
    { "PAL 640x512i",      640, 512 },
};

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
    ImGui::TextDisabled("Click to select (click again to deselect), Ctrl/Shift to multi-select, drag to move.");
    ImGui::Separator();

    const char* bpp_modes[] = { "4 BPP", "8 BPP", "16 BPP" };
    ImGui::SetNextItemWidth(150.0f);
    ImGui::Combo("VRAM BPP Mode", &bpp_mode_index, bpp_modes, 3);
    ImGui::SameLine();
    ImGui::SliderFloat("VRAM Zoom", &zoom, kMinZoom, kMaxZoom, "%.2fx");
    ImGui::SameLine();
    ImGui::Checkbox("Snap to TPage grid / other TIMs", &snap_enabled);

    const char* preset_labels[kFramebufferPresetCount];
    for (int i = 0; i < kFramebufferPresetCount; i++) preset_labels[i] = kFramebufferPresets[i].label;
    ImGui::SetNextItemWidth(150.0f);
    ImGui::Combo("Display Buffer", &framebuffer_preset_index, preset_labels, kFramebufferPresetCount);
    if (framebuffer_preset_index > 0) {
        ImGui::SameLine();
        ImGui::Checkbox("Double Buffered", &framebuffer_double_buffered);
        if (framebuffer_double_buffered) {
            ImGui::SameLine();
            ImGui::Checkbox("Stack Horizontally", &framebuffer_stack_horizontal);
        }
    }

    ImGui::TextDisabled("Scroll to zoom. Select something and use arrow keys to nudge it.");
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

    // NoScrollWithMouse: the wheel drives zoom below instead of panning.
    // Negative width reserves the (user-resizable) sidebar drawn after this child.
    ImGui::BeginChild("VRAMScroll", ImVec2(-sidebar_width, 0), true,
                       ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // GetCursorScreenPos()/GetScrollX() still reflect the scroll from BEFORE
    // this frame's SetScrollX/Y call below (that only takes effect for the
    // window's *next* layout), so canvas_p0 is computed by hand instead of
    // re-querying ImGui after changing zoom - otherwise the view would
    // render one frame out of place, flashing at the old position/zoom on
    // every wheel tick.
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    canvas_p0.x += kCanvasMargin; // Breathing room above/left of word/row 0 (see kCanvasMargin).
    canvas_p0.y += kCanvasMargin;
    bool canvas_hovered = ImGui::IsWindowHovered();
    if (canvas_hovered && io.MouseWheel != 0.0f) {
        ImVec2 scroll_before(ImGui::GetScrollX(), ImGui::GetScrollY());
        ImVec2 window_origin(canvas_p0.x + scroll_before.x, canvas_p0.y + scroll_before.y);
        float content_x = (io.MousePos.x - canvas_p0.x) / words_scale;
        float content_y = (io.MousePos.y - canvas_p0.y) / zoom;

        zoom = std::clamp(zoom * (1.0f + io.MouseWheel * 0.1f), kMinZoom, kMaxZoom);
        words_scale = (static_cast<float>(vram_manager.GetViewWidth()) / VRAMManager::kWidth) * zoom;
        canvas_size = ImVec2(vram_manager.GetViewWidth() * zoom, vram_manager.GetViewHeight() * zoom);

        canvas_p0 = ImVec2(io.MousePos.x - content_x * words_scale, io.MousePos.y - content_y * zoom);
        ImGui::SetScrollX(window_origin.x - canvas_p0.x);
        ImGui::SetScrollY(window_origin.y - canvas_p0.y);
    }

    ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_size.x, canvas_p0.y + canvas_size.y);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(0, 0, 0, 255));

    uint32_t vram_tex = vram_manager.GetVRAMTextureID();
    glBindTexture(GL_TEXTURE_2D, vram_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    ImGui::SetCursorScreenPos(canvas_p0);
    // The backend's default sampler is linear and overrides the NEAREST
    // texture parameters above; force point-sampling for this one draw so
    // zoomed-in VRAM pixels stay sharp instead of blurring.
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    if (platform_io.DrawCallback_SetSamplerNearest) draw_list->AddCallback(platform_io.DrawCallback_SetSamplerNearest);
    ImGui::Image((void*)(intptr_t)vram_tex, canvas_size);
    if (platform_io.DrawCallback_SetSamplerLinear) draw_list->AddCallback(platform_io.DrawCallback_SetSamplerLinear);

    DrawFramebufferOverlay(draw_list, canvas_p0, words_scale);

    float tpage_w = TPageWidthPixelsForMode(mode) * zoom;
    float tpage_h = 256.0f * zoom;
    DrawTPageGrid(draw_list, canvas_p0, tpage_w, tpage_h);

    DrawRegions(document, canvas_p0, words_scale);
    UpdateDrag(document, words_scale);
    HandleKeyboardNudge(document);

    int mouse_word_x = static_cast<int>((io.MousePos.x - canvas_p0.x) / words_scale);
    int mouse_line_y = static_cast<int>((io.MousePos.y - canvas_p0.y) / zoom);
    bool mouse_in_vram = canvas_hovered && mouse_word_x >= 0 && mouse_word_x < VRAMManager::kWidth &&
                          mouse_line_y >= 0 && mouse_line_y < VRAMManager::kHeight;

    ImGui::EndChild();

    // Dragging the splitter right shrinks the sidebar (it's the right pane).
    sidebar_width -= ui::VerticalSplitter("VRAMSplitter");
    sidebar_width = std::clamp(sidebar_width, kSidebarMinWidth, kSidebarMaxWidth);

    RenderSidebar(document, mouse_word_x, mouse_line_y, mouse_in_vram);
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

    // Draw (and hit-test) order: the active image - and by extension its
    // CLUT - goes last, so it renders on top and wins ties when several
    // regions overlap. "Active" already tracks whichever image was most
    // recently selected here or in the Inspector.
    std::vector<int> draw_order(n);
    for (int i = 0; i < n; i++) draw_order[i] = i;
    int active = document.GetActiveIndex();
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

        // --- Image bounds ---
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

            // Green highlights this image: bright when it's directly
            // selected, dim when it isn't but its CLUT is (so you can see
            // which image a selected palette belongs to). Both are an actual
            // fill, not just a border color, so the highlight reads clearly.
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

        // --- CLUT bounds ---
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
        if (moved_dist < 3.0f && drag.pending_click_reset) {
            if (drag.target == DragTarget::Images) {
                if (drag.click_deselect_if_sole) {
                    images[drag.click_reset_index].selected = false;
                    image_selection_anchor = -1;
                } else {
                    for (auto& img : images) img.selected = false;
                    images[drag.click_reset_index].selected = true;
                    image_selection_anchor = drag.click_reset_index;
                    document.SetActiveIndex(drag.click_reset_index);
                }
            } else {
                if (drag.click_deselect_if_sole) {
                    selected_cluts.clear();
                    clut_selection_anchor = -1;
                } else {
                    selected_cluts.assign(1, drag.click_reset_index);
                    clut_selection_anchor = drag.click_reset_index;
                }
            }
        }

        // If nothing actually moved (a plain click, or a drag that snapped/
        // clamped right back to where it started), the undo snapshot pushed
        // at drag-start would just be a no-op entry - drop it.
        bool any_moved = std::any_of(drag.original_origins.begin(), drag.original_origins.end(),
                                      [&](const std::pair<int, ImVec2>& entry) {
                                          ImVec2 now = (drag.target == DragTarget::Images)
                                              ? ImVec2(images[entry.first].image_header.origin_x,
                                                       images[entry.first].image_header.origin_y)
                                              : ImVec2(images[entry.first].clut_header.origin_x,
                                                       images[entry.first].clut_header.origin_y);
                                          return now.x != entry.second.x || now.y != entry.second.y;
                                      });
        if (!any_moved) document.DiscardLastUndo();

        drag.target = DragTarget::None;
        drag.original_origins.clear();
        drag.pending_click_reset = false;
        drag.click_reset_index = -1;
        drag.click_deselect_if_sole = false;
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

void VRAMPanel::HandleKeyboardNudge(tim::Document& document) {
    if (drag.target != DragTarget::None) return; // Don't fight an active mouse drag.
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) return;

    int dx = 0, dy = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) dx -= 1;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) dx += 1;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) dy -= 1;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) dy += 1;
    if (dx == 0 && dy == 0) return;

    // Bypasses snapping on purpose - arrow keys are for pixel-precise
    // adjustment, snapping is for coarse mouse placement.
    int step = ImGui::GetIO().KeyShift ? 10 : 1;
    dx *= step;
    dy *= step;

    auto& images = document.Images();
    bool has_image_selection = std::any_of(images.begin(), images.end(),
                                            [](const TIM_Image& t) { return t.selected; });

    // Images and CLUTs are never selected at the same time, so exactly one
    // of these branches can ever apply.
    if (has_image_selection) {
        document.PushUndoSnapshot();
        bool moved = false;
        for (int i = 0; i < static_cast<int>(images.size()); i++) {
            if (!images[i].selected) continue;
            int old_x = images[i].image_header.origin_x, old_y = images[i].image_header.origin_y;
            document.SetImageOrigin(i, old_x + dx, old_y + dy);
            moved |= (images[i].image_header.origin_x != old_x || images[i].image_header.origin_y != old_y);
        }
        if (!moved) document.DiscardLastUndo();
    } else if (!selected_cluts.empty()) {
        document.PushUndoSnapshot();
        bool moved = false;
        for (int idx : selected_cluts) {
            int old_x = images[idx].clut_header.origin_x, old_y = images[idx].clut_header.origin_y;
            document.SetClutOrigin(idx, old_x + dx, old_y + dy);
            moved |= (images[idx].clut_header.origin_x != old_x || images[idx].clut_header.origin_y != old_y);
        }
        if (!moved) document.DiscardLastUndo();
    }
}

void VRAMPanel::RenderSidebar(tim::Document& document, int mouse_word_x, int mouse_line_y, bool mouse_in_vram) {
    auto& images = document.Images();
    std::vector<int> selected;
    for (int i = 0; i < static_cast<int>(images.size()); i++) {
        if (images[i].selected) selected.push_back(i);
    }

    ImGui::BeginChild("VRAMSidebar", ImVec2(sidebar_width, 0), true);

    // Always exactly two lines here, valid or not, so the separator and
    // everything below never shift as the mouse moves in and out of VRAM.
    if (mouse_in_vram) {
        int tpage_id = (mouse_line_y / 256) * 16 + (mouse_word_x / 64);
        ImGui::Text("Cursor: x=%d words, y=%d px", mouse_word_x, mouse_line_y);
        ImGui::Text("TPage #%d (local %d, %d)", tpage_id, mouse_word_x % 64, mouse_line_y % 256);
    } else {
        ImGui::TextDisabled("Cursor: outside VRAM");
        ImGui::TextDisabled("TPage: -");
    }
    ImGui::Separator();

    ImGui::Text("Selected images: %d", static_cast<int>(selected.size()));
    ImGui::Separator();

    ImGui::BeginChild("VRAMSidebarSelList", ImVec2(0, 0), false);
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    if (!selected.empty() && platform_io.DrawCallback_SetSamplerNearest) {
        ImGui::GetWindowDrawList()->AddCallback(platform_io.DrawCallback_SetSamplerNearest);
    }

    for (int idx : selected) {
        TIM_Image& tim = images[idx];
        ImGui::PushID(idx);

        // Every entry gets the same shape - name, then preview, then palette
        // row - regardless of whether this image actually has multiple
        // palettes, so the list doesn't reflow unevenly as you scroll it.
        ImGui::Text("Image #%d", tim.file_index);

        const float kThumbHeight = 40.0f;
        float aspect = tim.image_header.height > 0
            ? static_cast<float>(tim.real_width) / static_cast<float>(tim.image_header.height)
            : 1.0f;
        ImVec2 thumb_size(kThumbHeight * aspect, kThumbHeight);
        if (!tim.opengl_texture_ids.empty()) {
            uint32_t tex = tim.opengl_texture_ids[tim.selected_clut];
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            ImGui::Image((void*)(intptr_t)tex, thumb_size);
        } else {
            ImGui::Dummy(thumb_size); // Keep row height identical even with no texture yet.
        }

        ImGui::Text("Palette:");
        ImGui::SameLine();
        bool has_multi_clut = tim.has_clut && tim.clut_header.num_cluts > 1;
        ImGui::BeginDisabled(!has_multi_clut);
        if (ImGui::ArrowButton("##pal_prev", ImGuiDir_Left) && tim.selected_clut > 0) tim.selected_clut--;
        ImGui::SameLine();
        if (tim.has_clut) ImGui::Text("%d / %d", tim.selected_clut, tim.clut_header.num_cluts - 1);
        else ImGui::TextUnformatted("-");
        ImGui::SameLine();
        if (ImGui::ArrowButton("##pal_next", ImGuiDir_Right) && tim.selected_clut < tim.clut_header.num_cluts - 1) {
            tim.selected_clut++;
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::PopID();
    }

    if (!selected.empty() && platform_io.DrawCallback_SetSamplerLinear) {
        ImGui::GetWindowDrawList()->AddCallback(platform_io.DrawCallback_SetSamplerLinear);
    }
    ImGui::EndChild();

    ImGui::EndChild();
}

} // namespace ui
