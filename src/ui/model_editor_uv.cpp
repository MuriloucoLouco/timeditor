// ModelEditorPanel's per-face properties panel and UV workspace - split
// out of model_editor_panel.cpp (which keeps Render/state/toolbar) purely
// to keep any one file from growing unmanageably long. See
// docs/ARCHITECTURE.md and model_editor_internal.h's own comment.
#include "model_editor_panel.h"
#include "model_editor_internal.h"
#include "gl_image.h"
#include "icon_button.h"
#include "zoom_pan.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace ui {

using namespace model_editor_internal;

void ModelEditorPanel::RenderSidePanel(tmd::TMD_Object& obj, VRAMManager& vram_manager,
                                        gfx::TmdTextureCache& texture_cache, const std::function<void()>& push_undo,
                                        const std::function<void()>& mark_dirty) {
    if (select_mode != SelectMode::Face) {
        ImGui::TextWrapped(
            "Select one or more faces (switch to Face mode) to edit their texture reference, shading, color, and "
            "UVs here.");
        return;
    }
    std::vector<int> faces = SelectedFaceIndices();
    if (faces.empty()) {
        ImGui::TextWrapped("Select one or more faces to edit their properties here.");
        return;
    }

    float props_height = std::max(220.0f, ImGui::GetContentRegionAvail().y * 0.45f);
    ImGui::BeginChild("ModelEditorFaceProps", ImVec2(0, props_height), false);
    RenderFacePropertiesPanel(obj, faces, push_undo, mark_dirty);
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::BeginChild("ModelEditorUvWorkspace", ImVec2(0, 0), false);
    RenderUvWorkspace(obj, faces, vram_manager, texture_cache, push_undo, mark_dirty);
    ImGui::EndChild();
}

void ModelEditorPanel::RenderFacePropertiesPanel(tmd::TMD_Object& obj, const std::vector<int>& faces,
                                                  const std::function<void()>& push_undo,
                                                  const std::function<void()>& mark_dirty) {
    ImGui::Text("Face Properties (%zu selected)", faces.size());
    ImGui::TextDisabled("Fields show the first selected face's values; changing one applies to all selected.");
    ImGui::Separator();

    tmd::TMD_Polygon& first = obj.polygons[faces[0]];

    auto ApplyToSelected = [&](const std::function<void(tmd::TMD_Polygon&)>& fn) {
        for (int fi : faces) {
            tmd::TMD_Polygon& p = obj.polygons[fi];
            fn(p);
            p.olen = 0; // any of these fields can change which documented olen applies - force a fresh recompute
        }
        mark_dirty();
    };

    bool textured = first.textured;
    if (ImGui::Checkbox("Textured", &textured)) {
        push_undo();
        ApplyToSelected([&](tmd::TMD_Polygon& p) { p.textured = textured; });
    }
    ImGui::SameLine();
    bool gouraud = first.gouraud;
    if (ImGui::Checkbox("Gouraud", &gouraud)) {
        push_undo();
        ApplyToSelected([&](tmd::TMD_Polygon& p) { p.gouraud = gouraud; });
    }

    bool no_light = first.no_light;
    if (ImGui::Checkbox("No Light", &no_light)) {
        push_undo();
        ApplyToSelected([&](tmd::TMD_Polygon& p) { p.no_light = no_light; });
    }
    ImGui::SameLine();
    bool double_sided = first.double_sided;
    if (ImGui::Checkbox("Double-Sided", &double_sided)) {
        push_undo();
        ApplyToSelected([&](tmd::TMD_Polygon& p) { p.double_sided = double_sided; });
    }

    bool semi_transparent = first.semi_transparent;
    if (ImGui::Checkbox("Semi-Transparent", &semi_transparent)) {
        push_undo();
        ApplyToSelected([&](tmd::TMD_Polygon& p) { p.semi_transparent = semi_transparent; });
    }

    if (first.textured) {
        ImGui::Spacing();
        ImGui::SeparatorText("Texture Reference");
        int tpage_x = first.tsb & 0xF;
        int tpage_y = (first.tsb >> 4) & 0x1;
        int semi_rate = (first.tsb >> 5) & 0x3;
        int color_mode = (first.tsb >> 7) & 0x3;
        int clut_x = (first.cba % 64) * 16;
        int clut_y = first.cba / 64;
        bool touched = false;

        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::InputInt("TPage X", &tpage_x, 1, 1)) {
            if (ImGui::IsItemActivated()) push_undo();
            tpage_x = std::clamp(tpage_x, 0, 15);
            touched = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::InputInt("TPage Y", &tpage_y, 1, 1)) {
            if (ImGui::IsItemActivated()) push_undo();
            tpage_y = std::clamp(tpage_y, 0, 1);
            touched = true;
        }

        const char* modes[] = { "4bpp (CLUT)", "8bpp (CLUT)", "16bpp (Direct)" };
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::Combo("Color Mode", &color_mode, modes, 3)) {
            push_undo();
            touched = true;
        }

        const char* rates[] = { "0: 50%+50%", "1: 100%+100%", "2: 100%-100%", "3: 100%+25%" };
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::Combo("Semi-Trans Rate", &semi_rate, rates, 4)) {
            push_undo();
            touched = true;
        }

        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragInt("CLUT X (words)", &clut_x, 4.0f, 0, 1008)) {
            if (ImGui::IsItemActivated()) push_undo();
            clut_x = (clut_x / 16) * 16;
            touched = true;
        }
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragInt("CLUT Y (line)", &clut_y, 1.0f, 0, 511)) {
            if (ImGui::IsItemActivated()) push_undo();
            touched = true;
        }

        if (touched) {
            uint16_t new_tsb = static_cast<uint16_t>((tpage_x & 0xF) | ((tpage_y & 0x1) << 4) |
                                                      ((semi_rate & 0x3) << 5) | ((color_mode & 0x3) << 7));
            uint16_t new_cba = static_cast<uint16_t>(clut_y * 64 + clut_x / 16);
            ApplyToSelected([&](tmd::TMD_Polygon& p) {
                p.tsb = new_tsb;
                p.cba = new_cba;
            });
        }

        ImGui::TextDisabled("tsb=0x%04X  cba=0x%04X (first selected face)", first.tsb, first.cba);
    }

    bool has_color = !first.textured || first.no_light;
    if (has_color) {
        ImGui::Spacing();
        ImGui::SeparatorText("Flat Color");
        float col[3] = { first.color[0][0] / 255.0f, first.color[0][1] / 255.0f, first.color[0][2] / 255.0f };
        if (ImGui::SliderFloat3("Color (RGB)", col, 0.0f, 1.0f)) {
            if (ImGui::IsItemActivated()) push_undo();
            uint8_t r = static_cast<uint8_t>(std::clamp(col[0], 0.0f, 1.0f) * 255.0f + 0.5f);
            uint8_t g = static_cast<uint8_t>(std::clamp(col[1], 0.0f, 1.0f) * 255.0f + 0.5f);
            uint8_t b = static_cast<uint8_t>(std::clamp(col[2], 0.0f, 1.0f) * 255.0f + 0.5f);
            ApplyToSelected([&](tmd::TMD_Polygon& p) {
                for (int k = 0; k < 4; k++) {
                    p.color[k][0] = r;
                    p.color[k][1] = g;
                    p.color[k][2] = b;
                }
            });
        }
    }
}

// A tile-scoped multi-face UV workspace: a TMD primitive's UV always
// addresses one specific 256x256-texel texpage tile (never a shared 0-1
// atlas across the whole model), so unlike a conventional UV editor this
// shows exactly one tile at a time - whichever `uv_tile_tsb/cba` currently
// is - and draws every SELECTED face that happens to use that same tile.
// Selected faces using a different tile are called out with a one-click
// switch instead of being silently hidden. Dragging any drawn UV corner
// moves every one of those corners together by the same delta (the
// "island" convenience a real UV editor offers) unless Alt is held, which
// moves just the grabbed corner - there's no shared-UV-vertex concept to
// preserve here (each primitive's u/v are its own), so "the island" is
// simply "every UV corner currently shown".
void ModelEditorPanel::RenderUvWorkspace(tmd::TMD_Object& obj, const std::vector<int>& faces,
                                          VRAMManager& vram_manager, gfx::TmdTextureCache& texture_cache,
                                          const std::function<void()>& push_undo,
                                          const std::function<void()>& mark_dirty) {
    std::vector<int> textured_faces;
    for (int fi : faces) if (obj.polygons[fi].textured) textured_faces.push_back(fi);

    if (textured_faces.empty()) {
        ImGui::TextWrapped("None of the selected faces are textured - nothing to unwrap.");
        return;
    }

    // Distinct (tsb,cba) tiles among the selection, and default to one of
    // them the first time (or if the current tile no longer applies).
    std::set<std::pair<uint16_t, uint16_t>> tiles;
    for (int fi : textured_faces) tiles.insert({ obj.polygons[fi].tsb, obj.polygons[fi].cba });
    if (!uv_tile_valid || !tiles.count({ uv_tile_tsb, uv_tile_cba })) {
        auto first = *tiles.begin();
        uv_tile_tsb = first.first;
        uv_tile_cba = first.second;
        uv_tile_valid = true;
    }

    ImGui::SeparatorText("UV Workspace");
    if (tiles.size() > 1) {
        ImGui::TextWrapped("The selection spans %zu texpage tiles - showing one at a time:", tiles.size());
        for (auto& t : tiles) {
            ImGui::PushID(static_cast<int>(t.first) << 16 | t.second);
            bool active = t.first == uv_tile_tsb && t.second == uv_tile_cba;
            char label[32];
            snprintf(label, sizeof(label), "tsb=0x%04X cba=0x%04X", t.first, t.second);
            if (ui::IconButton(label, nullptr, ImVec2(0, 0), active)) {
                uv_tile_tsb = t.first;
                uv_tile_cba = t.second;
            }
            ImGui::PopID();
        }
    }

    std::vector<int> matching;
    for (int fi : textured_faces) {
        if (obj.polygons[fi].tsb == uv_tile_tsb && obj.polygons[fi].cba == uv_tile_cba) matching.push_back(fi);
    }
    if (textured_faces.size() > matching.size()) {
        ImGui::TextDisabled("%zu of %zu selected textured faces use a different tile (switch above to reach them).",
                            textured_faces.size() - matching.size(), textured_faces.size());
    }

    // Numeric scale/rotate around the shown corners' own centroid - a
    // one-shot "apply this factor" control (resets after each Apply),
    // for precision a freehand drag can't easily give.
    ImGui::SetNextItemWidth(100.0f);
    ImGui::DragFloat("##uv_scale", &uv_scale_input, 0.01f, 0.01f, 10.0f, "x%.2f");
    ImGui::SameLine();
    if (ImGui::Button("Apply Scale")) {
        push_undo();
        float cu = 0, cv = 0;
        int n = 0;
        for (int fi : matching) {
            auto& p = obj.polygons[fi];
            for (int k = 0; k < p.num_verts; k++) { cu += p.u[k]; cv += p.v[k]; n++; }
        }
        if (n > 0) {
            cu /= n; cv /= n;
            for (int fi : matching) {
                auto& p = obj.polygons[fi];
                for (int k = 0; k < p.num_verts; k++) {
                    p.u[k] = static_cast<uint8_t>(std::clamp(cu + (p.u[k] - cu) * uv_scale_input, 0.0f, 255.0f));
                    p.v[k] = static_cast<uint8_t>(std::clamp(cv + (p.v[k] - cv) * uv_scale_input, 0.0f, 255.0f));
                }
            }
        }
        uv_scale_input = 1.0f;
        mark_dirty();
    }

    ImGui::SetNextItemWidth(100.0f);
    ImGui::DragFloat("##uv_rotate", &uv_rotate_input_deg, 1.0f, -180.0f, 180.0f, "%.0f deg");
    ImGui::SameLine();
    if (ImGui::Button("Apply Rotate")) {
        push_undo();
        float cu = 0, cv = 0;
        int n = 0;
        for (int fi : matching) {
            auto& p = obj.polygons[fi];
            for (int k = 0; k < p.num_verts; k++) { cu += p.u[k]; cv += p.v[k]; n++; }
        }
        if (n > 0) {
            cu /= n; cv /= n;
            float rad = uv_rotate_input_deg * kDegToRad;
            float c = std::cos(rad), s = std::sin(rad);
            for (int fi : matching) {
                auto& p = obj.polygons[fi];
                for (int k = 0; k < p.num_verts; k++) {
                    float du = p.u[k] - cu, dv = p.v[k] - cv;
                    p.u[k] = static_cast<uint8_t>(std::clamp(cu + du * c - dv * s, 0.0f, 255.0f));
                    p.v[k] = static_cast<uint8_t>(std::clamp(cv + du * s + dv * c, 0.0f, 255.0f));
                }
            }
        }
        uv_rotate_input_deg = 0.0f;
        mark_dirty();
    }

    gfx::TmdTextureCache::Tile tile = texture_cache.GetTile(vram_manager, uv_tile_tsb, uv_tile_cba);
    ImGui::BeginChild("ModelEditorUvCanvas", ImVec2(0, 0), true,
                       ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 tile_units(static_cast<float>(std::max(1, tile.width)), 256.0f);
    ImVec2 canvas_p0 = ZoomToCursor(uv_zoom, 0.5f, 16.0f, tile_units, [](float z) { return ImVec2(z, z); });
    PanWithMouseDrag(uv_panning_active);
    ImVec2 canvas_size(tile_units.x * uv_zoom, tile_units.y * uv_zoom);
    ImVec2 canvas_p1(canvas_p0.x + canvas_size.x, canvas_p0.y + canvas_size.y);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(0, 0, 0, 255));
    if (tile.gl_tex) {
        ImGui::SetCursorScreenPos(canvas_p0);
        ImagePixelPerfect(tile.gl_tex, canvas_size);
    }

    for (int fi : matching) {
        auto& p = obj.polygons[fi];
        ImVec2 pts[4];
        for (int k = 0; k < p.num_verts; k++) {
            pts[k] = ImVec2(canvas_p0.x + p.u[k] * uv_zoom, canvas_p0.y + p.v[k] * uv_zoom);
        }
        for (int k = 0; k < p.num_verts; k++) {
            int a = (p.num_verts == 4) ? tmd::kQuadPerimeterOrder[k] : k;
            int b = (p.num_verts == 4) ? tmd::kQuadPerimeterOrder[(k + 1) % 4] : (k + 1) % p.num_verts;
            draw_list->AddLine(pts[a], pts[b], IM_COL32(255, 165, 0, 255), 2.0f);
        }
        for (int k = 0; k < p.num_verts; k++) {
            ImGui::PushID(fi * 8 + k);
            ImGui::SetCursorScreenPos(ImVec2(pts[k].x - 6.0f, pts[k].y - 6.0f));
            ImGui::InvisibleButton("uv_corner", ImVec2(12.0f, 12.0f));
            // Captured immediately after submission, not re-queried below -
            // same defensive pattern as the 3D viewport's gizmo fix, so
            // nothing later in this scope can ever change what "the last
            // item" means out from under this check.
            bool corner_activated = ImGui::IsItemActivated();
            bool dragging = ImGui::IsItemActive();
            // Fires on activation itself, NOT gated behind IsMouseDragging:
            // that gate's drag-distance threshold is essentially never
            // crossed on the very same frame as activation (activation is
            // only ever true on the mouse-down frame, before the mouse has
            // moved), so requiring both together meant this checkpoint was
            // silently skipped on almost every real drag.
            if (corner_activated) push_undo();
            if (dragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImGuiIO& io = ImGui::GetIO();
                int du = static_cast<int>(std::round(io.MouseDelta.x / uv_zoom));
                int dv = static_cast<int>(std::round(io.MouseDelta.y / uv_zoom));
                if (io.KeyAlt) {
                    p.u[k] = static_cast<uint8_t>(std::clamp(static_cast<int>(p.u[k]) + du, 0, 255));
                    p.v[k] = static_cast<uint8_t>(std::clamp(static_cast<int>(p.v[k]) + dv, 0, 255));
                } else {
                    // Island move: every corner of every matching face moves together.
                    for (int mfi : matching) {
                        auto& mp = obj.polygons[mfi];
                        for (int mk = 0; mk < mp.num_verts; mk++) {
                            mp.u[mk] = static_cast<uint8_t>(std::clamp(static_cast<int>(mp.u[mk]) + du, 0, 255));
                            mp.v[mk] = static_cast<uint8_t>(std::clamp(static_cast<int>(mp.v[mk]) + dv, 0, 255));
                        }
                    }
                }
                mark_dirty();
            }
            draw_list->AddCircleFilled(pts[k], 5.0f, dragging ? IM_COL32(255, 80, 80, 255) : IM_COL32(255, 165, 0, 255));
            if (ImGui::IsItemHovered() || dragging) ImGui::SetTooltip("Face %d, corner %d: u=%d v=%d", fi, k, p.u[k], p.v[k]);
            ImGui::PopID();
        }
    }

    ImGui::EndChild();
}

} // namespace ui
