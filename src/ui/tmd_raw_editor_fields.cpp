// TmdRawEditor's field editors: the single-item Vertex/Normal/Primitive
// editors (plus the primitive's UV/3D previews) and the batch editors for
// all three kinds. The tree/dispatch logic that picks which of these to
// show lives in tmd_raw_editor.cpp - split purely to keep any one file from
// growing unmanageably long, see docs/ARCHITECTURE.md.
#include "tmd_raw_editor.h"
#include "gl_image.h"
#include "zoom_pan.h"
#include "../gfx/math3d.h"
#include "../gfx/tmd_object_renderer.h"
#include "../gfx/tmd_space.h"
#include "imgui.h"
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui {

namespace {
using gfx::ToViewerSpace;

// float[0..1] <-> uint8[0..255] color conversion helpers, used throughout
// since ImGui's color widgets work in float but TMD_Polygon stores bytes.
void ColorToFloat(const uint8_t c[3], float out[3]) {
    out[0] = c[0] / 255.0f;
    out[1] = c[1] / 255.0f;
    out[2] = c[2] / 255.0f;
}
void FloatToColor(const float in[3], uint8_t out[3]) {
    out[0] = static_cast<uint8_t>(std::clamp(in[0], 0.0f, 1.0f) * 255.0f + 0.5f);
    out[1] = static_cast<uint8_t>(std::clamp(in[1], 0.0f, 1.0f) * 255.0f + 0.5f);
    out[2] = static_cast<uint8_t>(std::clamp(in[2], 0.0f, 1.0f) * 255.0f + 0.5f);
}

// Ray-casting point-in-polygon test over a primitive's UV footprint
// (perimeter-ordered points), used by GuessClutFromUv to rasterize which
// texels the UV actually covers rather than just its bounding box.
bool PointInUvPolygon(const ImVec2* pts, int n, float x, float y) {
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float xi = pts[i].x, yi = pts[i].y, xj = pts[j].x, yj = pts[j].y;
        if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) inside = !inside;
    }
    return inside;
}

// Guesses a textured primitive's CLUT by rasterizing its UV footprint into
// absolute VRAM word/line addresses (using its own texpage's origin and
// color mode - see VRAMManager::DecodeTexPage for the identical addressing
// math) and finding which loaded TIM image's pixel-data rectangle covers
// the most of those texels. Points `out_cba` at that image's first CLUT
// ("palette 0" - a TIM's own CLUTs are stacked consecutively starting at
// clut_header's origin, so that origin already *is* CLUT index 0).
// Returns false if the primitive isn't textured or nothing loaded overlaps.
bool GuessClutFromUv(const tmd::TMD_Polygon& p, const tim::Document& document, uint16_t& out_cba) {
    if (!p.textured) return false;
    const auto& images = document.Images();
    if (images.empty()) return false;

    int tpage_x_words = (p.tsb & 0xF) * VRAMManager::kTPageWidthWords;
    int tpage_y = ((p.tsb >> 4) & 0x1) * VRAMManager::kTPageHeight;
    int color_mode = (p.tsb >> 7) & 0x3;
    int texels_per_word = (color_mode == 0) ? 4 : (color_mode == 1) ? 2 : 1;

    int nverts = p.num_verts;
    ImVec2 uv_pts[4];
    for (int k = 0; k < nverts; k++) {
        int idx = (nverts == 4) ? tmd::kQuadPerimeterOrder[k] : k;
        uv_pts[k] = ImVec2(static_cast<float>(p.u[idx]), static_cast<float>(p.v[idx]));
    }

    float umin = 255, umax = 0, vmin = 255, vmax = 0;
    for (int k = 0; k < nverts; k++) {
        umin = std::min(umin, uv_pts[k].x); umax = std::max(umax, uv_pts[k].x);
        vmin = std::min(vmin, uv_pts[k].y); vmax = std::max(vmax, uv_pts[k].y);
    }
    int u0 = std::clamp(static_cast<int>(std::floor(umin)), 0, 255);
    int u1 = std::clamp(static_cast<int>(std::ceil(umax)), 0, 255);
    int v0 = std::clamp(static_cast<int>(std::floor(vmin)), 0, 255);
    int v1 = std::clamp(static_cast<int>(std::ceil(vmax)), 0, 255);

    std::vector<int> overlap(images.size(), 0);
    int total_samples = 0;
    for (int v = v0; v <= v1; v++) {
        for (int u = u0; u <= u1; u++) {
            if (!PointInUvPolygon(uv_pts, nverts, u + 0.5f, v + 0.5f)) continue;
            total_samples++;
            int abs_word = tpage_x_words + u / texels_per_word;
            int abs_line = tpage_y + v;
            for (size_t i = 0; i < images.size(); i++) {
                const auto& img = images[i];
                int ox = img.image_header.origin_x, oy = img.image_header.origin_y;
                int ow = img.image_header.width, oh = img.image_header.height;
                if (abs_word >= ox && abs_word < ox + ow && abs_line >= oy && abs_line < oy + oh) overlap[i]++;
            }
        }
    }
    if (total_samples == 0) return false;

    int best = -1, best_count = 0;
    for (size_t i = 0; i < overlap.size(); i++) {
        if (overlap[i] > best_count) { best_count = overlap[i]; best = static_cast<int>(i); }
    }
    if (best < 0 || !images[best].has_clut) return false;

    int clut_x = images[best].clut_header.origin_x;
    int clut_y = images[best].clut_header.origin_y;
    out_cba = static_cast<uint16_t>(clut_y * 64 + clut_x / 16);
    return true;
}

} // namespace

void TmdRawEditor::RenderVertexEditor(tmd::TMD_Object& obj, int index, const std::function<void()>& push_undo,
                                       const std::function<void()>& mark_dirty) {
    if (index < 0 || index >= static_cast<int>(obj.vertices.size())) return;
    tmd::TMD_Vertex& v = obj.vertices[index];

    ImGui::Text("Edit Vertex #%d", index);
    ImGui::Separator();

    int xyz[3] = { v.x, v.y, v.z };
    bool changed = ImGui::DragInt3("Position", xyz, 1.0f, -32768, 32767);
    if (changed) {
        if (ImGui::IsItemActivated()) push_undo();
        v.x = static_cast<int16_t>(std::clamp(xyz[0], -32768, 32767));
        v.y = static_cast<int16_t>(std::clamp(xyz[1], -32768, 32767));
        v.z = static_cast<int16_t>(std::clamp(xyz[2], -32768, 32767));
        mark_dirty();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Raw PS-X fixed-point units (no fixed real-world scale).");
}

void TmdRawEditor::RenderNormalEditor(tmd::TMD_Object& obj, int index, const std::function<void()>& push_undo,
                                       const std::function<void()>& mark_dirty) {
    if (index < 0 || index >= static_cast<int>(obj.normals.size())) return;
    tmd::TMD_Normal& n = obj.normals[index];

    ImGui::Text("Edit Normal #%d", index);
    ImGui::Separator();

    int xyz[3] = { n.x, n.y, n.z };
    bool changed = ImGui::DragInt3("Direction", xyz, 8.0f, -32768, 32767);
    if (changed) {
        if (ImGui::IsItemActivated()) push_undo();
        n.x = static_cast<int16_t>(std::clamp(xyz[0], -32768, 32767));
        n.y = static_cast<int16_t>(std::clamp(xyz[1], -32768, 32767));
        n.z = static_cast<int16_t>(std::clamp(xyz[2], -32768, 32767));
        mark_dirty();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("GTE fixed-point: 4096 = 1.0 along that axis.");
}

void TmdRawEditor::RenderPrimitiveEditor(tmd::TMD_Object& obj, int index, const tim::Document& document,
                                          VRAMManager& vram_manager, gfx::TmdTextureCache& texture_cache,
                                          const std::function<void()>& push_undo,
                                          const std::function<void()>& mark_dirty) {
    if (index < 0 || index >= static_cast<int>(obj.polygons.size())) return;

    // Fields fill the left column, full height. The whole right side is
    // one bordered panel with the UV preview on top and the 3D "primitive
    // in model" preview (highlighted green, camera aimed at it) stacked
    // below it.
    ImGui::BeginChild("TmdPrimFieldsCol", ImVec2(fields_width, 0), false);
    RenderPrimitiveFields(obj, index, document, push_undo, mark_dirty);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("TmdPrimPreviewPanel", ImVec2(0, 0), true);

    float uv_height = std::max(240.0f, ImGui::GetContentRegionAvail().y * 0.5f);
    ImGui::BeginChild("TmdPrimUvCol", ImVec2(0, uv_height), false);
    RenderUvPreview(obj, index, vram_manager, texture_cache, push_undo, mark_dirty);
    ImGui::EndChild();

    ImGui::SeparatorText("Primitive in Model (highlighted)");
    ImGui::BeginChild("TmdPrim3DCol", ImVec2(0, 0), true);
    RenderPrimitive3DPreview(obj, index, vram_manager, texture_cache);
    ImGui::EndChild();

    ImGui::EndChild();
}

void TmdRawEditor::RenderPrimitiveFields(tmd::TMD_Object& obj, int index, const tim::Document& document,
                                          const std::function<void()>& push_undo,
                                          const std::function<void()>& mark_dirty) {
    if (index < 0 || index >= static_cast<int>(obj.polygons.size())) return;
    tmd::TMD_Polygon& p = obj.polygons[index];
    int nverts = p.num_verts;

    ImGui::Text("Edit Primitive #%d", index);
    ImGui::Separator();

    // --- Mode/flag checkboxes ---
    // Any of these can change which documented olen value applies to this
    // primitive (see tmd_writer.cpp's FallbackOlen) - clearing the stored
    // one here forces the writer to recompute it fresh instead of writing
    // a now-stale value copied from before the edit, which is exactly the
    // class of bug (a wrong GPU packet size) that once crashed a real game
    // in this project.
    auto checkbox = [&](const char* label, bool& field) {
        bool v = field;
        if (ImGui::Checkbox(label, &v)) {
            push_undo();
            field = v;
            p.olen = 0;
            mark_dirty();
        }
    };

    bool is_quad = nverts == 4;
    if (ImGui::Checkbox("Quad", &is_quad)) {
        push_undo();
        p.olen = 0;
        if (is_quad && p.num_verts == 3) {
            // Growing 3->4: duplicate the last corner as a starting point.
            p.num_verts = 4;
            p.vert_idx[3] = p.vert_idx[2];
            p.norm_idx[3] = p.norm_idx[2];
            p.u[3] = p.u[2];
            p.v[3] = p.v[2];
            p.color[3][0] = p.color[2][0];
            p.color[3][1] = p.color[2][1];
            p.color[3][2] = p.color[2][2];
        } else if (!is_quad && p.num_verts == 4) {
            p.num_verts = 3;
        }
        nverts = p.num_verts;
        mark_dirty();
    }

    ImGui::SameLine();
    checkbox("Textured", p.textured);
    ImGui::SameLine();
    checkbox("Gouraud", p.gouraud);

    bool no_light_before = p.no_light;
    checkbox("No Light", p.no_light);
    if (no_light_before != p.no_light && !p.no_light) {
        // Lighting just got turned back on: any corner left pointing at
        // "no normal" needs a real index or the file would reference an
        // invalid normal - default to the first one in the table.
        for (int i = 0; i < nverts; i++) {
            if (p.norm_idx[i] == tmd::TMD_Polygon::kNoNormal) p.norm_idx[i] = 0;
        }
    }

    ImGui::SameLine();
    checkbox("Double-Sided", p.double_sided);
    ImGui::SameLine();
    checkbox("Semi-Transparent", p.semi_transparent);

    bool has_color = !p.textured || p.no_light;
    if (has_color) {
        bool per_vertex_before = p.color_per_vertex;
        checkbox("Per-Vertex Color", p.color_per_vertex);
        if (per_vertex_before && !p.color_per_vertex) {
            for (int i = 1; i < nverts; i++) {
                p.color[i][0] = p.color[0][0];
                p.color[i][1] = p.color[0][1];
                p.color[i][2] = p.color[0][2];
            }
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Vertex / Normal Indices");
    for (int i = 0; i < nverts; i++) {
        ImGui::PushID(i);
        int vi = p.vert_idx[i];
        ImGui::SetNextItemWidth(90.0f);
        char vlabel[16];
        snprintf(vlabel, sizeof(vlabel), "V%d", i);
        bool vchanged = ImGui::DragInt(vlabel, &vi, 0.2f, 0, static_cast<int>(obj.vertices.size()) - 1);
        if (vchanged) {
            if (ImGui::IsItemActivated()) push_undo();
            p.vert_idx[i] = static_cast<uint16_t>(std::clamp(vi, 0, std::max(0, static_cast<int>(obj.vertices.size()) - 1)));
            mark_dirty();
        }

        // For a flat (non-gouraud) primitive, only one shared normal exists
        // in the file - show its control just once, alongside V0, rather
        // than once per corner.
        if (!p.no_light && (p.gouraud || i == 0)) {
            ImGui::SameLine();
            int ni = (p.gouraud ? p.norm_idx[i] : p.norm_idx[0]);
            ImGui::SetNextItemWidth(90.0f);
            char nlabel[16];
            snprintf(nlabel, sizeof(nlabel), p.gouraud ? "N%d" : "Normal", i);
            bool nchanged = ImGui::DragInt(nlabel, &ni, 0.2f, 0, static_cast<int>(obj.normals.size()) - 1);
            if (nchanged) {
                if (ImGui::IsItemActivated()) push_undo();
                int clamped = std::clamp(ni, 0, std::max(0, static_cast<int>(obj.normals.size()) - 1));
                if (p.gouraud) {
                    p.norm_idx[i] = static_cast<uint16_t>(clamped);
                } else {
                    for (int k = 0; k < nverts; k++) p.norm_idx[k] = static_cast<uint16_t>(clamped);
                }
                mark_dirty();
            }
        }
        ImGui::PopID();
    }

    if (has_color) {
        ImGui::Spacing();
        ImGui::SeparatorText("Color");
        int color_count = p.color_per_vertex ? nverts : 1;
        for (int i = 0; i < color_count; i++) {
            ImGui::PushID(i);
            float col[3];
            ColorToFloat(p.color[i], col);
            char clabel[16];
            snprintf(clabel, sizeof(clabel), p.color_per_vertex ? "Color%d" : "Color", i);
            bool cchanged = ImGui::ColorEdit3(clabel, col);
            if (cchanged) {
                if (ImGui::IsItemActivated()) push_undo();
                if (p.color_per_vertex) {
                    FloatToColor(col, p.color[i]);
                } else {
                    uint8_t packed[3];
                    FloatToColor(col, packed);
                    for (int k = 0; k < nverts; k++) {
                        p.color[k][0] = packed[0];
                        p.color[k][1] = packed[1];
                        p.color[k][2] = packed[2];
                    }
                }
                mark_dirty();
            }
            ImGui::PopID();
        }
    }

    if (p.textured) {
        ImGui::Spacing();
        ImGui::SeparatorText("Texture Reference");

        int tpage_x = p.tsb & 0xF;
        int tpage_y = (p.tsb >> 4) & 0x1;
        int semi_rate = (p.tsb >> 5) & 0x3;
        int color_mode = (p.tsb >> 7) & 0x3;
        int clut_x = (p.cba % 64) * 16;
        int clut_y = p.cba / 64;

        auto repack = [&]() {
            p.tsb = static_cast<uint16_t>((tpage_x & 0xF) | ((tpage_y & 0x1) << 4) | ((semi_rate & 0x3) << 5) |
                                           ((color_mode & 0x3) << 7));
            p.cba = static_cast<uint16_t>(clut_y * 64 + clut_x / 16);
        };

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
        if (ImGui::Combo("Color Mode", &color_mode, modes, 3)) { push_undo(); touched = true; }

        const char* rates[] = { "0: 50%+50%", "1: 100%+100%", "2: 100%-100%", "3: 100%+25%" };
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::Combo("Semi-Trans Rate", &semi_rate, rates, 4)) { push_undo(); touched = true; }

        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragInt("CLUT X (words)", &clut_x, 4.0f, 0, 1008)) {
            if (ImGui::IsItemActivated()) push_undo();
            clut_x = (clut_x / 16) * 16;
            touched = true;
        }
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragInt("CLUT Y (line)", &clut_y, 1.0f, 0, 511)) { if (ImGui::IsItemActivated()) push_undo(); touched = true; }

        if (touched) {
            repack();
            mark_dirty();
        }

        ImGui::TextDisabled("tsb=0x%04X  cba=0x%04X", p.tsb, p.cba);

        ImGui::Spacing();
        if (ImGui::Button("Guess CLUT from Loaded Images")) {
            uint16_t guessed_cba;
            if (GuessClutFromUv(p, document, guessed_cba)) {
                push_undo();
                p.cba = guessed_cba;
                p.olen = 0;
                mark_dirty();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Finds which loaded TIM image's pixels this primitive's UV footprint covers the most,\n"
                "then points its CLUT at that image's first palette (CLUT index 0).");
        }
    }
}

void TmdRawEditor::RenderUvPreview(tmd::TMD_Object& obj, int index, VRAMManager& vram_manager,
                                    gfx::TmdTextureCache& texture_cache, const std::function<void()>& push_undo,
                                    const std::function<void()>& mark_dirty) {
    if (index < 0 || index >= static_cast<int>(obj.polygons.size())) return;
    tmd::TMD_Polygon& p = obj.polygons[index];
    int nverts = p.num_verts;
    if (!p.textured) {
        ImGui::TextDisabled("This primitive isn't textured - no UV to preview.");
        return;
    }

    ImGui::SeparatorText("UV Preview (drag corners)");
    ImGui::Checkbox("Show TPage Border/Number", &show_tpage_overlay);

    gfx::TmdTextureCache::Tile tile = texture_cache.GetTile(vram_manager, p.tsb, p.cba);
    ImGui::BeginChild("TmdPrimUvPreview", ImVec2(0, 0), true,
                       ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImVec2 tile_units(static_cast<float>(std::max(1, tile.width)), 256.0f);
        ImVec2 canvas_p0 = ZoomToCursor(uv_zoom, 0.5f, 16.0f, tile_units, [](float z) { return ImVec2(z, z); });
        PanWithMouseDrag(uv_panning_active);
        ImVec2 canvas_size(tile_units.x * uv_zoom, tile_units.y * uv_zoom);
        ImVec2 canvas_p1(canvas_p0.x + canvas_size.x, canvas_p0.y + canvas_size.y);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        // A solid black backdrop behind the tile - a texpage's unused
        // texels decode as (0,0,0,0) fully transparent, which otherwise
        // shows the ImGui window's own background through them instead of
        // the black VRAM actually holds there (matching the VRAM Viewer's
        // own canvas, which fills the same way before drawing its texture).
        draw_list->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(0, 0, 0, 255));

        if (tile.gl_tex) {
            ImGui::SetCursorScreenPos(canvas_p0);
            ImagePixelPerfect(tile.gl_tex, canvas_size);
        }

        if (show_tpage_overlay) {
            int tpage_index = ((p.tsb >> 4) & 0x1) * 16 + (p.tsb & 0xF);
            draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(255, 255, 255, 200));
            char tpage_str[16];
            snprintf(tpage_str, sizeof(tpage_str), "%d", tpage_index);
            draw_list->AddText(ImVec2(canvas_p0.x + 4, canvas_p0.y + 4), IM_COL32(255, 255, 255, 255), tpage_str);
        }

        ImVec2 pts[4];
        for (int i = 0; i < nverts; i++) {
            pts[i] = ImVec2(canvas_p0.x + p.u[i] * uv_zoom, canvas_p0.y + p.v[i] * uv_zoom);
        }
        // Connecting corners 0-1-2-3-0 directly draws a bowtie/hourglass
        // instead of the quad's actual outline - see tmd::kQuadPerimeterOrder.
        for (int k = 0; k < nverts; k++) {
            int a = (nverts == 4) ? tmd::kQuadPerimeterOrder[k] : k;
            int b = (nverts == 4) ? tmd::kQuadPerimeterOrder[(k + 1) % nverts] : (k + 1) % nverts;
            draw_list->AddLine(pts[a], pts[b], IM_COL32(255, 255, 0, 255), 2.0f);
        }
        for (int i = 0; i < nverts; i++) {
            ImGui::PushID(100 + i);
            ImGui::SetCursorScreenPos(ImVec2(pts[i].x - 6.0f, pts[i].y - 6.0f));
            ImGui::InvisibleButton("corner", ImVec2(12.0f, 12.0f));
            bool dragging = ImGui::IsItemActive();
            // push_undo() fires on activation (mouse-down) itself, not
            // gated behind IsMouseDragging: the drag-distance threshold
            // that flips IsMouseDragging to true is rarely crossed on the
            // exact same frame as activation, so requiring both on one
            // frame silently skipped the undo checkpoint on most real
            // drags (only the very first frame is ever "activated", but
            // the threshold is usually crossed a frame or two later).
            if (ImGui::IsItemActivated()) push_undo();
            if (dragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImGuiIO& io = ImGui::GetIO();
                int new_u = static_cast<int>(std::round(p.u[i] + io.MouseDelta.x / uv_zoom));
                int new_v = static_cast<int>(std::round(p.v[i] + io.MouseDelta.y / uv_zoom));
                p.u[i] = static_cast<uint8_t>(std::clamp(new_u, 0, 255));
                p.v[i] = static_cast<uint8_t>(std::clamp(new_v, 0, 255));
                mark_dirty();
            }
            ImU32 dot_color = dragging ? IM_COL32(255, 80, 80, 255) : IM_COL32(255, 255, 0, 255);
            draw_list->AddCircleFilled(pts[i], 5.0f, dot_color);
            if (ImGui::IsItemHovered() || dragging) {
                ImGui::SetTooltip("Corner %d: u=%d v=%d", i, p.u[i], p.v[i]);
            }
            ImGui::PopID();
        }

    ImGui::EndChild();
}

void TmdRawEditor::RenderPrimitive3DPreview(tmd::TMD_Object& obj, int index, VRAMManager& vram_manager,
                                             gfx::TmdTextureCache& texture_cache) {
    if (index < 0 || index >= static_cast<int>(obj.polygons.size())) return;
    const tmd::TMD_Polygon& p = obj.polygons[index];

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1 || avail.y < 1) return;
    preview_framebuffer.EnsureSize(static_cast<int>(avail.x), static_cast<int>(avail.y));

    // Bounding box of the *whole* object, purely to pick a framing distance -
    // the point is to see this primitive highlighted in context on the
    // model, not isolated from it.
    gfx::Vec3 lo{ 1e9f, 1e9f, 1e9f }, hi{ -1e9f, -1e9f, -1e9f };
    for (const auto& v : obj.vertices) {
        gfx::Vec3 pos = ToViewerSpace(v.x, v.y, v.z);
        lo.x = std::min(lo.x, pos.x); lo.y = std::min(lo.y, pos.y); lo.z = std::min(lo.z, pos.z);
        hi.x = std::max(hi.x, pos.x); hi.y = std::max(hi.y, pos.y); hi.z = std::max(hi.z, pos.z);
    }
    float extent = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1.0f });

    // The primitive's own centroid, so the camera looks straight at it.
    gfx::Vec3 centroid{ 0, 0, 0 };
    for (int i = 0; i < p.num_verts; i++) {
        uint16_t vi = p.vert_idx[i];
        gfx::Vec3 pos = (vi < obj.vertices.size())
                            ? ToViewerSpace(obj.vertices[vi].x, obj.vertices[vi].y, obj.vertices[vi].z)
                            : gfx::Vec3{ 0, 0, 0 };
        centroid = centroid + pos * (1.0f / static_cast<float>(p.num_verts));
    }

    // The primitive's facing direction, so the camera sits on the side its
    // front face points to - a real normal for lit primitives (averaged
    // for gouraud, single shared one for flat), a fresh geometric normal
    // for no_light (same approach as the OBJ exporter's reference normals).
    gfx::Vec3 normal{ 0, 0, 1 };
    if (!p.no_light) {
        gfx::Vec3 sum{ 0, 0, 0 };
        int count = 0;
        for (int i = 0; i < p.num_verts; i++) {
            uint16_t ni = p.norm_idx[i];
            if (ni != tmd::TMD_Polygon::kNoNormal && ni < obj.normals.size()) {
                sum = sum + ToViewerSpace(obj.normals[ni].x, obj.normals[ni].y, obj.normals[ni].z);
                count++;
            }
            if (!p.gouraud) break; // one shared normal - a single sample is enough
        }
        if (count > 0) normal = (sum * (1.0f / static_cast<float>(count))).Normalized();
    } else if (p.num_verts >= 3) {
        uint16_t i0 = p.vert_idx[0], i1 = p.vert_idx[1], i2 = p.vert_idx[2];
        if (i0 < obj.vertices.size() && i1 < obj.vertices.size() && i2 < obj.vertices.size()) {
            gfx::Vec3 v0 = ToViewerSpace(obj.vertices[i0].x, obj.vertices[i0].y, obj.vertices[i0].z);
            gfx::Vec3 v1 = ToViewerSpace(obj.vertices[i1].x, obj.vertices[i1].y, obj.vertices[i1].z);
            gfx::Vec3 v2 = ToViewerSpace(obj.vertices[i2].x, obj.vertices[i2].y, obj.vertices[i2].z);
            normal = (v1 - v0).Cross(v2 - v0).Normalized();
        }
    }

    gfx::Vec3 up{ 0, 1, 0 };
    if (std::abs(normal.Dot(up)) > 0.99f) up = gfx::Vec3{ 0, 0, 1 }; // avoid a degenerate LookAt basis

    float distance = std::max(extent * 1.3f, 10.0f);
    gfx::Vec3 eye = centroid + normal * distance;
    gfx::Mat4 view = gfx::Mat4::LookAt(eye, centroid, up);
    gfx::Mat4 proj = gfx::Mat4::Perspective(60.0f * (3.14159265f / 180.0f), avail.x / avail.y, 1.0f, 1000000.0f);

    preview_framebuffer.Bind();
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.m);

    gfx::TmdObjectRenderer::Options options;
    options.textured = true;
    options.cull_backfaces = false;
    options.highlight_primitive = index;
    gfx::TmdObjectRenderer::DrawObject(obj, gfx::Mat4::Identity(), vram_manager, texture_cache, options);

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // wireframe (if ever used here) must never leak into ImGui's own rendering
    preview_framebuffer.Unbind();

    ImGui::Image((void*)(intptr_t)preview_framebuffer.ColorTexture(), avail, ImVec2(0, 1), ImVec2(1, 0));
}

// No single absolute position makes sense across an arbitrary set of
// vertices with different coordinates - a relative offset is the one
// operation that's well-defined for all of them at once.
void TmdRawEditor::RenderBatchVertexEditor(tmd::TMD_Object& obj, const std::vector<int>& indices,
                                            const std::function<void()>& push_undo,
                                            const std::function<void()>& mark_dirty) {
    ImGui::Text("Move the selected vertices by an offset:");
    ImGui::DragFloat3("Offset", batch_vertex_offset, 1.0f);

    if (ImGui::Button(("Apply to " + std::to_string(indices.size()) + " Vertices").c_str())) {
        push_undo();
        for (int i : indices) {
            if (i < 0 || i >= static_cast<int>(obj.vertices.size())) continue;
            auto& v = obj.vertices[i];
            v.x = static_cast<int16_t>(std::clamp(static_cast<int>(v.x) + static_cast<int>(batch_vertex_offset[0]), -32768, 32767));
            v.y = static_cast<int16_t>(std::clamp(static_cast<int>(v.y) + static_cast<int>(batch_vertex_offset[1]), -32768, 32767));
            v.z = static_cast<int16_t>(std::clamp(static_cast<int>(v.z) + static_cast<int>(batch_vertex_offset[2]), -32768, 32767));
        }
        mark_dirty();
        batch_vertex_offset[0] = batch_vertex_offset[1] = batch_vertex_offset[2] = 0.0f;
    }
}

// Overwrites every selected normal with the same exact direction - useful
// for forcing a group of faces to share one shading normal.
void TmdRawEditor::RenderBatchNormalEditor(tmd::TMD_Object& obj, const std::vector<int>& indices,
                                            const std::function<void()>& push_undo,
                                            const std::function<void()>& mark_dirty) {
    ImGui::Text("Set the selected normals to:");
    ImGui::DragInt3("Direction", batch_normal_dir, 8.0f, -32768, 32767);
    ImGui::TextDisabled("GTE fixed-point: 4096 = 1.0 along that axis.");

    if (ImGui::Button(("Apply to " + std::to_string(indices.size()) + " Normals").c_str())) {
        push_undo();
        for (int i : indices) {
            if (i < 0 || i >= static_cast<int>(obj.normals.size())) continue;
            auto& n = obj.normals[i];
            n.x = static_cast<int16_t>(std::clamp(batch_normal_dir[0], -32768, 32767));
            n.y = static_cast<int16_t>(std::clamp(batch_normal_dir[1], -32768, 32767));
            n.z = static_cast<int16_t>(std::clamp(batch_normal_dir[2], -32768, 32767));
        }
        mark_dirty();
    }
}

// Every field gets its own opt-in checkbox ("Copy to Selected"-style) -
// only ticked fields get written to every selected primitive when
// applied; per-vertex/per-corner data (vertex indices, per-corner UV,
// per-vertex color) is deliberately not offered here - it's inherently
// specific to each primitive's own shape and doesn't generalize.
void TmdRawEditor::RenderBatchPrimitiveEditor(tmd::TMD_Object& obj, const std::vector<int>& indices,
                                               const tim::Document& document,
                                               const std::function<void()>& push_undo,
                                               const std::function<void()>& mark_dirty) {
    BatchPrimitiveState& b = batch_primitive;
    ImGui::TextWrapped("Tick a field to apply it to every selected primitive; untouched fields are left as-is.");
    ImGui::Separator();

    auto BoolField = [&](const char* label, bool& enable, bool& value) {
        ImGui::Checkbox((std::string("##en_") + label).c_str(), &enable);
        ImGui::SameLine();
        ImGui::BeginDisabled(!enable);
        ImGui::Checkbox(label, &value);
        ImGui::EndDisabled();
    };

    BoolField("Textured", b.set_textured, b.textured_value);
    ImGui::SameLine(220.0f);
    BoolField("Gouraud", b.set_gouraud, b.gouraud_value);
    BoolField("No Light", b.set_no_light, b.no_light_value);
    ImGui::SameLine(220.0f);
    BoolField("Double-Sided", b.set_double_sided, b.double_sided_value);
    BoolField("Semi-Transparent", b.set_semi_transparent, b.semi_transparent_value);

    ImGui::Spacing();
    ImGui::SeparatorText("Texture Reference");

    ImGui::Checkbox("##en_tpx", &b.set_tpage_x);
    ImGui::SameLine();
    ImGui::BeginDisabled(!b.set_tpage_x);
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputInt("TPage X", &b.tpage_x_value, 1, 1);
    b.tpage_x_value = std::clamp(b.tpage_x_value, 0, 15);
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Checkbox("##en_tpy", &b.set_tpage_y);
    ImGui::SameLine();
    ImGui::BeginDisabled(!b.set_tpage_y);
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputInt("TPage Y", &b.tpage_y_value, 1, 1);
    b.tpage_y_value = std::clamp(b.tpage_y_value, 0, 1);
    ImGui::EndDisabled();

    const char* modes[] = { "4bpp (CLUT)", "8bpp (CLUT)", "16bpp (Direct)" };
    ImGui::Checkbox("##en_cm", &b.set_color_mode);
    ImGui::SameLine();
    ImGui::BeginDisabled(!b.set_color_mode);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::Combo("Color Mode", &b.color_mode_value, modes, 3);
    ImGui::EndDisabled();

    const char* rates[] = { "0: 50%+50%", "1: 100%+100%", "2: 100%-100%", "3: 100%+25%" };
    ImGui::Checkbox("##en_sr", &b.set_semi_rate);
    ImGui::SameLine();
    ImGui::BeginDisabled(!b.set_semi_rate);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::Combo("Semi-Trans Rate", &b.semi_rate_value, rates, 4);
    ImGui::EndDisabled();

    ImGui::BeginDisabled(b.guess_clut);
    ImGui::Checkbox("##en_cx", &b.set_clut_x);
    ImGui::SameLine();
    ImGui::BeginDisabled(!b.set_clut_x);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragInt("CLUT X (words)", &b.clut_x_value, 4.0f, 0, 1008);
    b.clut_x_value = (b.clut_x_value / 16) * 16;
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Checkbox("##en_cy", &b.set_clut_y);
    ImGui::SameLine();
    ImGui::BeginDisabled(!b.set_clut_y);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragInt("CLUT Y (line)", &b.clut_y_value, 1.0f, 0, 511);
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::BeginDisabled(b.set_clut_x || b.set_clut_y);
    ImGui::Checkbox("Guess CLUT from Loaded Images (per-primitive)", &b.guess_clut);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "For each selected textured primitive, finds which loaded TIM image its own UV footprint\n"
            "covers the most and points its CLUT at that image's first palette - independently per primitive,\n"
            "unlike CLUT X/Y above which would write the same single value to all of them (mutually exclusive).");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Color");
    ImGui::Checkbox("##en_color", &b.set_color);
    ImGui::SameLine();
    ImGui::BeginDisabled(!b.set_color);
    ImGui::ColorButton("##color_preview", ImVec4(b.color_value[0], b.color_value[1], b.color_value[2], 1.0f),
                        ImGuiColorEditFlags_NoTooltip, ImVec2(24, 24));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::SliderFloat3("Flat Color (RGB)", b.color_value, 0.0f, 1.0f);
    ImGui::EndDisabled();

    ImGui::Separator();
    if (ImGui::Button(("Apply to " + std::to_string(indices.size()) + " Primitives").c_str())) {
        push_undo();
        for (int i : indices) {
            if (i < 0 || i >= static_cast<int>(obj.polygons.size())) continue;
            tmd::TMD_Polygon& p = obj.polygons[i];

            if (b.set_textured) p.textured = b.textured_value;
            if (b.set_gouraud) p.gouraud = b.gouraud_value;
            if (b.set_no_light) p.no_light = b.no_light_value;
            if (b.set_double_sided) p.double_sided = b.double_sided_value;
            if (b.set_semi_transparent) p.semi_transparent = b.semi_transparent_value;

            if (b.set_tpage_x || b.set_tpage_y || b.set_color_mode || b.set_semi_rate) {
                int tpage_x = b.set_tpage_x ? b.tpage_x_value : (p.tsb & 0xF);
                int tpage_y = b.set_tpage_y ? b.tpage_y_value : ((p.tsb >> 4) & 0x1);
                int semi_rate = b.set_semi_rate ? b.semi_rate_value : ((p.tsb >> 5) & 0x3);
                int color_mode = b.set_color_mode ? b.color_mode_value : ((p.tsb >> 7) & 0x3);
                p.tsb = static_cast<uint16_t>((tpage_x & 0xF) | ((tpage_y & 0x1) << 4) | ((semi_rate & 0x3) << 5) |
                                               ((color_mode & 0x3) << 7));
            }
            if (b.guess_clut) {
                uint16_t guessed_cba;
                if (GuessClutFromUv(p, document, guessed_cba)) p.cba = guessed_cba;
            } else if (b.set_clut_x || b.set_clut_y) {
                int clut_x = b.set_clut_x ? b.clut_x_value : (p.cba % 64) * 16;
                int clut_y = b.set_clut_y ? b.clut_y_value : p.cba / 64;
                p.cba = static_cast<uint16_t>(clut_y * 64 + clut_x / 16);
            }
            if (b.set_color) {
                uint8_t packed[3];
                FloatToColor(b.color_value, packed);
                for (int k = 0; k < 4; k++) {
                    p.color[k][0] = packed[0];
                    p.color[k][1] = packed[1];
                    p.color[k][2] = packed[2];
                }
            }

            // Any of the above can change which documented olen value
            // applies (see tmd_writer.cpp's FallbackOlen) - force a fresh
            // recompute rather than keep a value stale to the pre-edit mode.
            p.olen = 0;
        }
        mark_dirty();
    }
}

} // namespace ui
