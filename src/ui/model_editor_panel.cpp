#include "model_editor_panel.h"
#include "../gfx/ray.h"
#include "../gfx/tmd_object_renderer.h"
#include "IconsFontAwesome6.h"
#include "ImGuizmo.h"
#include "gl_image.h"
#include "splitter.h"
#include "zoom_pan.h"
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace ui {

namespace {
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
constexpr float kPickPixelRadius = 10.0f;
constexpr float kDragThresholdPixels = 4.0f;

gfx::Vec3 ToViewerSpace(int16_t x, int16_t y, int16_t z) {
    return { static_cast<float>(x), -static_cast<float>(y), static_cast<float>(z) };
}

gfx::Vec3 VertexViewerPos(const tmd::TMD_Object& obj, int vi) {
    const auto& v = obj.vertices[vi];
    return ToViewerSpace(v.x, v.y, v.z);
}

// Nearest vertex to `mouse` (screen space) within a small pixel radius, or
// -1 if none qualifies - simpler and more robust than ray-casting a
// zero-size point, and the standard approach in modeling tools.
int PickVertex(const tmd::TMD_Object& obj, ImVec2 mouse, ImVec2 viewport_pos, ImVec2 viewport_size,
               const gfx::Mat4& view_proj) {
    int best = -1;
    float best_dist = kPickPixelRadius;
    for (size_t i = 0; i < obj.vertices.size(); i++) {
        gfx::Vec3 screen;
        if (!gfx::ProjectPoint(view_proj, VertexViewerPos(obj, static_cast<int>(i)), viewport_size.x, viewport_size.y,
                                screen))
            continue;
        float dx = mouse.x - (viewport_pos.x + screen.x), dy = mouse.y - (viewport_pos.y + screen.y);
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < best_dist) {
            best_dist = dist;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// Closest single face to the ray under the cursor - a real ray/triangle
// test, unlike vertex/edge picking, since a face has real area to hit
// precisely rather than needing a screen-space fallback radius.
int PickFace(const tmd::TMD_Object& obj, const gfx::Ray& ray) {
    int best = -1;
    float best_t = 1e30f;
    for (size_t pi = 0; pi < obj.polygons.size(); pi++) {
        const auto& p = obj.polygons[pi];
        gfx::Vec3 v[4];
        bool valid = true;
        for (int k = 0; k < p.num_verts; k++) {
            if (p.vert_idx[k] >= obj.vertices.size()) {
                valid = false;
                break;
            }
            v[k] = VertexViewerPos(obj, p.vert_idx[k]);
        }
        if (!valid) continue;
        float t;
        if (gfx::RayHitsTriangle(ray.origin, ray.dir, v[0], v[1], v[2], t) && t < best_t) {
            best_t = t;
            best = static_cast<int>(pi);
        }
        // Same two triangles GL_TRIANGLE_STRIP actually renders for a
        // strip-order quad (see tmd_object_renderer.cpp).
        if (p.num_verts == 4 && gfx::RayHitsTriangle(ray.origin, ray.dir, v[1], v[3], v[2], t) && t < best_t) {
            best_t = t;
            best = static_cast<int>(pi);
        }
    }
    return best;
}

bool PickEdge(const tmd::TMD_Object& obj, ImVec2 mouse, ImVec2 viewport_pos, ImVec2 viewport_size,
              const gfx::Mat4& view_proj, tmd::Edge& out_edge) {
    auto edges = tmd::BuildEdgeList(obj);
    float best_dist = kPickPixelRadius;
    bool found = false;
    for (auto& eu : edges) {
        if (eu.edge.a >= obj.vertices.size() || eu.edge.b >= obj.vertices.size()) continue;
        gfx::Vec3 sa, sb;
        if (!gfx::ProjectPoint(view_proj, VertexViewerPos(obj, eu.edge.a), viewport_size.x, viewport_size.y, sa))
            continue;
        if (!gfx::ProjectPoint(view_proj, VertexViewerPos(obj, eu.edge.b), viewport_size.x, viewport_size.y, sb))
            continue;
        float dist = gfx::DistancePointToSegment2D(mouse.x - viewport_pos.x, mouse.y - viewport_pos.y, sa.x, sa.y,
                                                    sb.x, sb.y);
        if (dist < best_dist) {
            best_dist = dist;
            out_edge = eu.edge;
            found = true;
        }
    }
    return found;
}

// A simple fixed ground grid on the XZ plane, centered at the world
// origin - not adaptive/infinite (that would need a custom shader, out of
// reach for this project's immediate-mode GL rendering), but still the
// classic "this is a real 3D viewport" spatial reference every modeling
// tool has, which this one previously had none of at all. Brighter lines
// mark the X (red-ish) and Z (blue-ish) axes through the origin.
void DrawGroundGrid(float extent, float spacing) {
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // faint lines need real alpha blending, not just a low RGB

    glColor4ub(255, 255, 255, 35);
    glBegin(GL_LINES);
    for (float x = -extent; x <= extent; x += spacing) {
        glVertex3f(x, 0, -extent);
        glVertex3f(x, 0, extent);
    }
    for (float z = -extent; z <= extent; z += spacing) {
        glVertex3f(-extent, 0, z);
        glVertex3f(extent, 0, z);
    }
    glEnd();

    glLineWidth(1.5f);
    glBegin(GL_LINES);
    glColor4ub(200, 80, 80, 130);
    glVertex3f(-extent, 0, 0);
    glVertex3f(extent, 0, 0);
    glColor4ub(80, 110, 220, 130);
    glVertex3f(0, 0, -extent);
    glVertex3f(0, 0, extent);
    glEnd();
    glLineWidth(1.0f);
}

// A small fixed-corner "which way am I looking" indicator - three short
// axis lines built from the SAME view matrix the camera actually uses (via
// TransformDirection, the 3x3-only helper already in math3d.h), so it's
// always exactly consistent with the real camera rather than a separately
// hand-derived approximation. Drawn back-to-front so a nearer axis is
// never hidden behind a farther one at the shared origin point.
void DrawOrientationIndicator(ImDrawList* draw_list, ImVec2 center, float radius, const gfx::Mat4& view) {
    struct Axis {
        gfx::Vec3 dir;
        const char* label;
        ImU32 color;
    };
    Axis axes[3] = {
        { { 1, 0, 0 }, "X", IM_COL32(220, 70, 70, 255) },
        { { 0, 1, 0 }, "Y", IM_COL32(90, 200, 90, 255) },
        { { 0, 0, 1 }, "Z", IM_COL32(90, 130, 230, 255) },
    };
    std::sort(std::begin(axes), std::end(axes), [&](const Axis& a, const Axis& b) {
        return gfx::TransformDirection(view, a.dir).z > gfx::TransformDirection(view, b.dir).z;
    });

    draw_list->AddCircleFilled(center, radius + 8.0f, IM_COL32(20, 20, 24, 140));
    for (const auto& ax : axes) {
        gfx::Vec3 v = gfx::TransformDirection(view, ax.dir);
        ImVec2 p2d(center.x + v.x * radius, center.y - v.y * radius); // screen Y grows downward
        bool behind = v.z > 0;                                        // OpenGL view space looks down -Z
        ImU32 col = behind ? IM_COL32(120, 120, 125, 150) : ax.color;
        draw_list->AddLine(center, p2d, col, 2.0f);
        draw_list->AddCircleFilled(p2d, 5.0f, col);
        if (!behind) draw_list->AddText(ImVec2(p2d.x + 5, p2d.y - 7), col, ax.label);
    }
}

} // namespace

void ModelEditorPanel::ResizeSelectionToObject(const tmd::TMD_Object& obj) {
    selected_vertices.assign(obj.vertices.size(), false);
    selected_faces.assign(obj.polygons.size(), false);
    selected_edges.clear();
}

void ModelEditorPanel::ClearSelection() {
    std::fill(selected_vertices.begin(), selected_vertices.end(), false);
    std::fill(selected_faces.begin(), selected_faces.end(), false);
    selected_edges.clear();
}

void ModelEditorPanel::SetSelectMode(SelectMode mode) {
    if (mode == select_mode) return;
    select_mode = mode;
    // Selection semantics differ per mode (a vertex index vs. an edge vs. a
    // face index) - kept simple for now rather than converting a selection
    // across modes.
    ClearSelection();
}

void ModelEditorPanel::Render(int model_index, int object_index, tmd::TMD_Object& obj, VRAMManager& vram_manager,
                               gfx::TmdTextureCache& texture_cache, const std::function<void()>& push_undo,
                               const std::function<void()>& mark_dirty) {
    if (model_index != last_model_index || object_index != last_object_index) {
        ResizeSelectionToObject(obj);
        last_model_index = model_index;
        last_object_index = object_index;
    }

    RenderToolbar(obj, push_undo, mark_dirty);
    RenderStatusLine(obj);
    ImGui::Separator();

    ImGui::BeginChild("ModelEditorViewportCol", ImVec2(-side_width, 0), false, ImGuiWindowFlags_NoScrollbar |
                                                                                    ImGuiWindowFlags_NoScrollWithMouse);
    RenderViewport(obj, vram_manager, texture_cache, push_undo, mark_dirty);
    ImGui::EndChild();

    // Dragging the splitter right shrinks the side column (it's the right pane).
    side_width -= ui::VerticalSplitter("ModelEditorSplitter");
    side_width = std::clamp(side_width, 260.0f, 600.0f);

    ImGui::BeginChild("ModelEditorSideCol", ImVec2(0, 0), true);
    RenderSidePanel(obj, vram_manager, texture_cache, push_undo, mark_dirty);
    ImGui::EndChild();
}

std::vector<int> ModelEditorPanel::SelectedVertexIndices() const {
    std::vector<int> result;
    for (size_t i = 0; i < selected_vertices.size(); i++) {
        if (selected_vertices[i]) result.push_back(static_cast<int>(i));
    }
    return result;
}

std::vector<int> ModelEditorPanel::SelectedFaceIndices() const {
    std::vector<int> result;
    for (size_t i = 0; i < selected_faces.size(); i++) {
        if (selected_faces[i]) result.push_back(static_cast<int>(i));
    }
    return result;
}

std::vector<tmd::Edge> ModelEditorPanel::SelectedEdgeList() const {
    return { selected_edges.begin(), selected_edges.end() };
}

void ModelEditorPanel::RenderToolbar(tmd::TMD_Object& obj, const std::function<void()>& push_undo,
                                      const std::function<void()>& mark_dirty) {
    const ImVec2 kIconBtn(32.0f, 32.0f);
    auto IconModeButton = [&](const char* icon, const char* tooltip, SelectMode mode) {
        bool active = select_mode == mode;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        if (ImGui::Button(icon, kIconBtn)) SetSelectMode(mode);
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    };
    auto IconGizmoButton = [&](const char* icon, const char* tooltip, GizmoOp op) {
        bool active = gizmo_op == op;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        if (ImGui::Button(icon, kIconBtn)) gizmo_op = op;
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    };

    IconModeButton(ICON_FA_CIRCLE_DOT, "Select Vertices (1)", SelectMode::Vertex);
    ImGui::SameLine();
    IconModeButton(ICON_FA_SLASH, "Select Edges (2)", SelectMode::Edge);
    ImGui::SameLine();
    IconModeButton(ICON_FA_VECTOR_SQUARE, "Select Faces (3)", SelectMode::Face);

    ImGui::SameLine(0, 16.0f);
    ImGui::TextUnformatted("|");
    ImGui::SameLine(0, 16.0f);

    IconGizmoButton(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, "Move (W)", GizmoOp::Translate);
    ImGui::SameLine();
    IconGizmoButton(ICON_FA_ROTATE, "Rotate (E)", GizmoOp::Rotate);
    ImGui::SameLine();
    IconGizmoButton(ICON_FA_EXPAND, "Scale (R)", GizmoOp::Scale);

    ImGui::SameLine(0, 16.0f);
    ImGui::TextUnformatted("|");
    ImGui::SameLine(0, 16.0f);

    auto ViewButton = [&](const char* icon, const char* tooltip, float yaw, float pitch) {
        if (ImGui::Button(icon, kIconBtn)) {
            cam_yaw = yaw;
            cam_pitch = pitch;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    };
    ViewButton(ICON_FA_CUBE, "Perspective View", 0.6f, 0.35f);
    ImGui::SameLine();
    ViewButton(ICON_FA_CAMERA, "Front View", 0.0f, 0.0f);
    ImGui::SameLine();
    ViewButton(ICON_FA_BORDER_ALL, "Top View", 0.0f, 1.5f);
    ImGui::SameLine();
    ViewButton(ICON_FA_SQUARE_FULL, "Side View", 3.14159265f * 0.5f, 0.0f);

    ImGui::SameLine(0, 16.0f);
    ImGui::Checkbox("Wireframe", &wireframe);

    ImGui::SameLine(0, 16.0f);
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Left-click selects (Shift adds/removes one element, drag from empty space box-selects).\n"
            "The gizmo appears at the selection's center - drag its handles to Move/Rotate/Scale.\n"
            "Middle-drag orbits, Shift+Middle-drag pans, scroll zooms.\n"
            "1/2/3 switch Vertex/Edge/Face mode, W/E/R switch Move/Rotate/Scale, Delete or X deletes.");
    }

    ImGui::SameLine(0, 16.0f);
    ImGui::TextUnformatted("|");
    ImGui::SameLine(0, 16.0f);

    // Mode-specific topology tools - every button pushes undo once, applies
    // its tmd_mesh_ops operation, then adjusts the selection to whatever
    // makes sense to keep working with next (e.g. Extrude keeps the moved
    // cap selected in Face mode; Duplicate selects the new copy).
    auto action = [&](const std::function<void()>& fn) {
        push_undo();
        fn();
        mark_dirty();
    };
    // id_suffix disambiguates two buttons that would otherwise share the
    // exact same ImGui ID (derived from the label, which for an icon
    // button is just the glyph itself) - needed wherever the same icon is
    // used twice in one frame, like the two Merge buttons below, which
    // otherwise triggered Dear ImGui's "2 visible items with conflicting
    // ID" warning and made only one of the pair actually clickable.
    auto IconButton = [&](const char* icon, const char* tooltip, const char* id_suffix = nullptr) {
        if (id_suffix) ImGui::PushID(id_suffix);
        bool clicked = ImGui::Button(icon, kIconBtn);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
        if (id_suffix) ImGui::PopID();
        return clicked;
    };

    if (select_mode == SelectMode::Vertex) {
        std::vector<int> verts = SelectedVertexIndices();
        ImGui::BeginDisabled(verts.size() < 2);
        if (IconButton(ICON_FA_CODE_MERGE, "Merge at Average Position", "merge_avg")) {
            action([&] {
                tmd::MergeVertices(obj, verts, tmd::MergeTarget::Average);
                ResizeSelectionToObject(obj);
                if (!verts.empty()) selected_vertices[verts[0]] = true;
            });
        }
        ImGui::SameLine();
        if (IconButton(ICON_FA_CODE_MERGE, "Merge at Last-Selected Position", "merge_last")) {
            action([&] {
                tmd::MergeVertices(obj, verts, tmd::MergeTarget::Last);
                ResizeSelectionToObject(obj);
                if (!verts.empty()) selected_vertices[verts[0]] = true;
            });
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(verts.size() != 3 && verts.size() != 4);
        if (IconButton(ICON_FA_DRAW_POLYGON, "Make Face from Selected Vertices")) {
            action([&] {
                // Auto-orders the selected points around their centroid (a
                // best-fit-plane angular sort) so the user doesn't have to
                // click corners in a specific order - a small convenience,
                // reliable for the common near-planar/convex case a "make
                // a face from these points" tool is actually used for.
                gfx::Vec3 centroid{ 0, 0, 0 };
                for (int vi : verts) centroid = centroid + VertexViewerPos(obj, vi);
                centroid = centroid * (1.0f / static_cast<float>(verts.size()));
                gfx::Vec3 normal = (VertexViewerPos(obj, verts[0]) - centroid)
                                        .Cross(VertexViewerPos(obj, verts[1]) - centroid)
                                        .Normalized();
                gfx::Vec3 arbitrary = std::abs(normal.y) < 0.99f ? gfx::Vec3{ 0, 1, 0 } : gfx::Vec3{ 1, 0, 0 };
                gfx::Vec3 u = normal.Cross(arbitrary).Normalized();
                gfx::Vec3 v = normal.Cross(u);
                std::vector<std::pair<float, int>> angled;
                for (int vi : verts) {
                    gfx::Vec3 d = VertexViewerPos(obj, vi) - centroid;
                    angled.push_back({ std::atan2(d.Dot(v), d.Dot(u)), vi });
                }
                std::sort(angled.begin(), angled.end());
                std::vector<int> ordered;
                for (auto& [a, vi] : angled) ordered.push_back(vi);
                int new_face = tmd::AddFace(obj, ordered);
                ResizeSelectionToObject(obj);
                if (new_face >= 0) {
                    select_mode = SelectMode::Face;
                    selected_faces[new_face] = true;
                }
            });
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (IconButton(ICON_FA_PLUS, "Add Vertex (at selection's center, or the view center if none)")) {
            action([&] {
                gfx::Vec3 at = cam_target;
                if (!verts.empty()) {
                    gfx::Vec3 c{ 0, 0, 0 };
                    for (int vi : verts) c = c + VertexViewerPos(obj, vi);
                    at = c * (1.0f / static_cast<float>(verts.size()));
                }
                // Stored TMD space is Y-down; `at` is in Y-up viewer space.
                int nv = tmd::AddVertex(obj, at.x, -at.y, at.z);
                ResizeSelectionToObject(obj);
                selected_vertices[nv] = true;
            });
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(verts.empty());
        if (IconButton(ICON_FA_CLONE, "Duplicate Selected Vertices")) {
            action([&] {
                tmd::DuplicateResult r = tmd::DuplicateSelection(obj, verts, {});
                ResizeSelectionToObject(obj);
                for (int nv : r.new_vertex_indices) if (nv >= 0) selected_vertices[nv] = true;
            });
        }
        ImGui::SameLine();
        if (IconButton(ICON_FA_TRASH, "Delete Selected Vertices (and every face touching them)")) {
            DeleteSelection(obj, push_undo, mark_dirty);
        }
        ImGui::EndDisabled();
    } else if (select_mode == SelectMode::Edge) {
        std::vector<tmd::Edge> edges = SelectedEdgeList();
        ImGui::BeginDisabled(edges.size() != 1);
        if (IconButton(ICON_FA_ARROW_UP_FROM_BRACKET, "Extrude Edge (select exactly 1 edge)")) {
            action([&] {
                tmd::Edge e = edges[0];
                tmd::ExtrudeResult r = tmd::ExtrudeEdge(obj, e.a, e.b);
                if (r.new_vertex_indices.size() == 2) {
                    select_mode = SelectMode::Vertex;
                    ResizeSelectionToObject(obj);
                    selected_vertices[r.new_vertex_indices[0]] = true;
                    selected_vertices[r.new_vertex_indices[1]] = true;
                } else {
                    ResizeSelectionToObject(obj);
                }
            });
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(edges.empty());
        // TMD has no standalone edge primitive - "deleting an edge" means
        // deleting every face that uses it (see DeleteSelection).
        if (IconButton(ICON_FA_TRASH, "Delete Selected Edges (removes every face using them)")) {
            DeleteSelection(obj, push_undo, mark_dirty);
        }
        ImGui::EndDisabled();
    } else {
        std::vector<int> faces = SelectedFaceIndices();
        // Restricted to exactly one face at a time (mirroring the Edge
        // tool's own "exactly 1" restriction above) - multi-face extrude
        // was removed at the user's request.
        ImGui::BeginDisabled(faces.size() != 1);
        if (IconButton(ICON_FA_ARROW_UP_FROM_BRACKET, "Extrude Face (select exactly 1 face)")) {
            action([&] {
                tmd::ExtrudeResult r = tmd::ExtrudeFaces(obj, faces);
                // The original primitive index is re-pointed to the new
                // (extruded) vertices and stays the moved cap, so it's
                // still correctly selected - but selected_vertices/
                // selected_faces must still grow to cover the new
                // vertices/wall faces ExtrudeFaces just added (resize(),
                // not ResizeSelectionToObject's full reset, so the
                // existing cap selection above survives). Skipping this
                // left both vectors undersized relative to obj.vertices/
                // obj.polygons - anything that indexed them by the object's
                // *new*, larger count without bounds-checking (e.g. the
                // viewport's selection-outline overlay) read past the
                // vector's real end, which is exactly what made a stray
                // outline "stick" to an unrelated face after extruding.
                selected_vertices.resize(obj.vertices.size(), false);
                selected_faces.resize(obj.polygons.size(), false);
                (void)r;
            });
        }
        ImGui::SameLine();
        if (IconButton(ICON_FA_CLONE, "Duplicate Selected Faces")) {
            action([&] {
                std::vector<int> verts = SelectedVertexIndices(); // empty in Face mode; gather from faces instead
                verts.clear();
                std::vector<bool> mark(obj.vertices.size(), false);
                for (int fi : faces) {
                    const auto& p = obj.polygons[fi];
                    for (int k = 0; k < p.num_verts; k++) {
                        if (!mark[p.vert_idx[k]]) { mark[p.vert_idx[k]] = true; verts.push_back(p.vert_idx[k]); }
                    }
                }
                tmd::DuplicateResult r = tmd::DuplicateSelection(obj, verts, faces);
                ResizeSelectionToObject(obj);
                for (int nf : r.new_primitive_indices) if (nf >= 0) selected_faces[nf] = true;
            });
        }
        ImGui::SameLine();
        if (IconButton(ICON_FA_ROTATE_RIGHT, "Flip Normal")) {
            action([&] { for (int fi : faces) tmd::FlipNormal(obj, fi); });
        }
        ImGui::SameLine();
        if (IconButton(ICON_FA_WAND_MAGIC, "Recalculate Normal from Geometry")) {
            action([&] { for (int fi : faces) tmd::RecalculateNormal(obj, fi); });
        }
        ImGui::SameLine();
        if (IconButton(ICON_FA_TRASH, "Delete Selected Faces")) DeleteSelection(obj, push_undo, mark_dirty);
        ImGui::EndDisabled();
    }

    ImGui::SameLine(0, 16.0f);
    ImGui::TextUnformatted("|");
    ImGui::SameLine(0, 16.0f);
    if (IconButton(ICON_FA_BROOM, "Remove Unused Vertices/Normals")) {
        action([&] {
            tmd::RemoveUnusedVerticesAndNormals(obj);
            ResizeSelectionToObject(obj);
        });
    }
}

void ModelEditorPanel::RenderStatusLine(const tmd::TMD_Object& obj) const {
    int count = 0;
    const char* noun = "vertices";
    switch (select_mode) {
        case SelectMode::Vertex:
            for (bool b : selected_vertices) if (b) count++;
            noun = count == 1 ? "vertex" : "vertices";
            break;
        case SelectMode::Edge:
            count = static_cast<int>(selected_edges.size());
            noun = count == 1 ? "edge" : "edges";
            break;
        case SelectMode::Face:
            for (bool b : selected_faces) if (b) count++;
            noun = count == 1 ? "face" : "faces";
            break;
    }
    if (count == 0) ImGui::TextDisabled("Nothing selected.");
    else ImGui::Text("%d %s selected.", count, noun);
    (void)obj;
}

void ModelEditorPanel::HandleCameraInput(bool hovered) {
    ImGuiIO& io = ImGui::GetIO();

    // An explicit session flag, started only by a fresh click while
    // hovering, held for as long as the button stays down (regardless of
    // whether the mouse drifts outside the viewport mid-drag, matching
    // ordinary drag behavior) - deliberately not ImGui's own item-active
    // tracking; see the header comment on HandleCameraInput for why.
    if (!middle_orbit_active && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        middle_orbit_active = true;
    }
    if (middle_orbit_active && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        middle_orbit_active = false;
    }

    if (middle_orbit_active) {
        if (io.KeyShift) {
            gfx::Vec3 forward{ -std::cos(cam_pitch) * std::sin(cam_yaw), -std::sin(cam_pitch),
                                -std::cos(cam_pitch) * std::cos(cam_yaw) };
            gfx::Vec3 right = forward.Cross({ 0, 1, 0 }).Normalized();
            gfx::Vec3 up = right.Cross(forward).Normalized();
            float pan_speed = cam_distance * 0.0015f;
            cam_target = cam_target - right * (io.MouseDelta.x * pan_speed) + up * (io.MouseDelta.y * pan_speed);
        } else {
            cam_yaw -= io.MouseDelta.x * 0.01f;
            cam_pitch = std::clamp(cam_pitch - io.MouseDelta.y * 0.01f, -1.5f, 1.5f);
        }
    }
    if (hovered && io.MouseWheel != 0.0f) {
        cam_distance = std::clamp(cam_distance * (1.0f - io.MouseWheel * 0.1f), 1.0f, 1000000.0f);
    }
}

void ModelEditorPanel::HandleFlyCamera() {
    ImGuiIO& io = ImGui::GetIO();
    gfx::Vec3 forward{ -std::cos(cam_pitch) * std::sin(cam_yaw), -std::sin(cam_pitch),
                        -std::cos(cam_pitch) * std::cos(cam_yaw) };
    gfx::Vec3 right = forward.Cross({ 0, 1, 0 }).Normalized();
    float speed = cam_distance * 0.8f * io.DeltaTime;

    if (ImGui::IsKeyDown(ImGuiKey_W)) cam_target = cam_target + forward * speed;
    if (ImGui::IsKeyDown(ImGuiKey_S)) cam_target = cam_target - forward * speed;
    if (ImGui::IsKeyDown(ImGuiKey_A)) cam_target = cam_target - right * speed;
    if (ImGui::IsKeyDown(ImGuiKey_D)) cam_target = cam_target + right * speed;
    if (ImGui::IsKeyDown(ImGuiKey_E)) cam_target.y += speed;
    if (ImGui::IsKeyDown(ImGuiKey_Q)) cam_target.y -= speed;
}

void ModelEditorPanel::HandleKeyboardNudge(tmd::TMD_Object& obj, const gfx::Mat4& view,
                                            const std::function<void()>& push_undo,
                                            const std::function<void()>& mark_dirty) {
    gfx::Vec3 centroid;
    std::vector<int> verts;
    if (!ComputeSelectionPivot(obj, centroid, verts)) {
        nudge_active = false;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    float speed = (io.KeyShift ? 200.0f : 40.0f) * io.DeltaTime;

    // World-space camera basis, read straight out of the view matrix's
    // rows (its rotation part is orthogonal, so its inverse is its own
    // transpose) - so a nudge always moves the selection relative to how
    // it's currently being looked at, not fixed world axes.
    gfx::Vec3 right{ view.m[0], view.m[4], view.m[8] };
    gfx::Vec3 up{ view.m[1], view.m[5], view.m[9] };
    gfx::Vec3 forward{ -view.m[2], -view.m[6], -view.m[10] };

    gfx::Vec3 delta{ 0, 0, 0 };
    bool any = false;
    if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) { delta = delta - right * speed; any = true; }
    if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) { delta = delta + right * speed; any = true; }
    if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) { delta = delta + up * speed; any = true; }
    if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) { delta = delta - up * speed; any = true; }
    if (ImGui::IsKeyDown(ImGuiKey_PageUp)) { delta = delta + forward * speed; any = true; }
    if (ImGui::IsKeyDown(ImGuiKey_PageDown)) { delta = delta - forward * speed; any = true; }

    if (!any) {
        nudge_active = false;
        return;
    }

    // One undo entry for the whole press-and-hold gesture, not one per
    // frame - matches the gizmo's own drag convention.
    if (!nudge_active) {
        push_undo();
        nudge_active = true;
    }

    for (int vi : verts) {
        if (vi < 0 || vi >= static_cast<int>(obj.vertices.size())) continue;
        gfx::Vec3 p = VertexViewerPos(obj, vi) + delta;
        long nx = std::lround(p.x);
        long ny = std::lround(-p.y); // viewer (Y-up) -> TMD storage (Y-down)
        long nz = std::lround(p.z);
        obj.vertices[vi].x = static_cast<int16_t>(std::clamp(nx, -32768L, 32767L));
        obj.vertices[vi].y = static_cast<int16_t>(std::clamp(ny, -32768L, 32767L));
        obj.vertices[vi].z = static_cast<int16_t>(std::clamp(nz, -32768L, 32767L));
    }
    mark_dirty();
}

void ModelEditorPanel::FrameSelection(const tmd::TMD_Object& obj) {
    gfx::Vec3 centroid;
    std::vector<int> verts;
    if (!ComputeSelectionPivot(obj, centroid, verts)) return;

    gfx::Vec3 lo{ 1e9f, 1e9f, 1e9f }, hi{ -1e9f, -1e9f, -1e9f };
    for (int vi : verts) {
        gfx::Vec3 p = VertexViewerPos(obj, vi);
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
    }
    float extent = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 10.0f });
    cam_target = centroid;
    cam_distance = std::clamp(extent * 2.0f, 20.0f, 1000000.0f);
}

void ModelEditorPanel::HandleSelectionInput(tmd::TMD_Object& obj, ImVec2 viewport_pos, ImVec2 viewport_size,
                                             const gfx::Mat4& view, const gfx::Mat4& proj, bool hovered) {
    if (gizmo_capturing_input) return; // the gizmo owns this frame's click/drag instead

    ImGuiIO& io = ImGui::GetIO();
    gfx::Mat4 view_proj = proj * view;

    // Only START a fresh press while hovered and not already mid-gesture;
    // once started, the drag/release below tracks it regardless of
    // whether the mouse drifts outside the viewport, matching ordinary
    // drag behavior (and regardless of `hovered` on later frames).
    bool start_press = !press.active && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (start_press) {
        press = PressState{};
        press.active = true;
        press.press_screen_pos = ImGui::GetMousePos();

        float ndc_x = ((press.press_screen_pos.x - viewport_pos.x) / viewport_size.x) * 2.0f - 1.0f;
        float ndc_y = 1.0f - ((press.press_screen_pos.y - viewport_pos.y) / viewport_size.y) * 2.0f;
        gfx::Ray ray = gfx::RayFromNDC(view_proj.Inverse(), ndc_x, ndc_y);

        switch (select_mode) {
            case SelectMode::Vertex: {
                int vi = PickVertex(obj, press.press_screen_pos, viewport_pos, viewport_size, view_proj);
                if (vi >= 0) {
                    press.target = PressTarget::Vertex;
                    press.vertex_index = vi;
                }
                break;
            }
            case SelectMode::Edge: {
                tmd::Edge e;
                if (PickEdge(obj, press.press_screen_pos, viewport_pos, viewport_size, view_proj, e)) {
                    press.target = PressTarget::Edge;
                    press.edge = e;
                }
                break;
            }
            case SelectMode::Face: {
                int fi = PickFace(obj, ray);
                if (fi >= 0) {
                    press.target = PressTarget::Face;
                    press.face_index = fi;
                }
                break;
            }
        }
    }

    if (press.active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImVec2 cur = ImGui::GetMousePos();
        float dx = cur.x - press.press_screen_pos.x;
        float dy = cur.y - press.press_screen_pos.y;
        if (std::abs(dx) > kDragThresholdPixels || std::abs(dy) > kDragThresholdPixels) {
            press.drag_moved = true;
        }
    }

    if (press.active && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (!press.drag_moved) {
            if (press.target == PressTarget::None) {
                if (!io.KeyShift) ClearSelection();
            } else {
                switch (press.target) {
                    case PressTarget::Vertex: {
                        bool now = selected_vertices[press.vertex_index];
                        if (io.KeyShift) selected_vertices[press.vertex_index] = !now;
                        else {
                            ClearSelection();
                            selected_vertices[press.vertex_index] = true;
                        }
                        break;
                    }
                    case PressTarget::Edge: {
                        bool now = selected_edges.count(press.edge) > 0;
                        if (io.KeyShift) {
                            if (now) selected_edges.erase(press.edge);
                            else selected_edges.insert(press.edge);
                        } else {
                            ClearSelection();
                            selected_edges.insert(press.edge);
                        }
                        break;
                    }
                    case PressTarget::Face: {
                        bool now = selected_faces[press.face_index];
                        if (io.KeyShift) selected_faces[press.face_index] = !now;
                        else {
                            ClearSelection();
                            selected_faces[press.face_index] = true;
                        }
                        break;
                    }
                    default: break;
                }
            }
        } else {
            // Any drag not captured by the gizmo is a box-select, no
            // matter what (if anything) was under the initial press -
            // moving now happens exclusively via the gizmo.
            ImVec2 p0 = press.press_screen_pos, p1 = ImGui::GetMousePos();
            float minx = std::min(p0.x, p1.x), maxx = std::max(p0.x, p1.x);
            float miny = std::min(p0.y, p1.y), maxy = std::max(p0.y, p1.y);
            if (!io.KeyShift) ClearSelection();

            auto inside = [&](gfx::Vec3 world) {
                gfx::Vec3 screen;
                if (!gfx::ProjectPoint(view_proj, world, viewport_size.x, viewport_size.y, screen)) return false;
                float sx = viewport_pos.x + screen.x, sy = viewport_pos.y + screen.y;
                return sx >= minx && sx <= maxx && sy >= miny && sy <= maxy;
            };

            if (select_mode == SelectMode::Vertex) {
                for (size_t i = 0; i < obj.vertices.size(); i++) {
                    if (inside(VertexViewerPos(obj, static_cast<int>(i)))) selected_vertices[i] = true;
                }
            } else if (select_mode == SelectMode::Edge) {
                for (auto& eu : tmd::BuildEdgeList(obj)) {
                    gfx::Vec3 mid = (VertexViewerPos(obj, eu.edge.a) + VertexViewerPos(obj, eu.edge.b)) * 0.5f;
                    if (inside(mid)) selected_edges.insert(eu.edge);
                }
            } else {
                for (size_t pi = 0; pi < obj.polygons.size(); pi++) {
                    const auto& p = obj.polygons[pi];
                    gfx::Vec3 c{ 0, 0, 0 };
                    int n = 0;
                    for (int k = 0; k < p.num_verts; k++) {
                        if (p.vert_idx[k] >= obj.vertices.size()) continue;
                        c = c + VertexViewerPos(obj, p.vert_idx[k]);
                        n++;
                    }
                    if (n == 0) continue;
                    c = c * (1.0f / static_cast<float>(n));
                    if (pi < selected_faces.size() && inside(c)) selected_faces[pi] = true;
                }
            }
        }
        press = PressState{};
    }
}

void ModelEditorPanel::DeleteSelection(tmd::TMD_Object& obj, const std::function<void()>& push_undo,
                                        const std::function<void()>& mark_dirty) {
    switch (select_mode) {
        case SelectMode::Vertex: {
            std::vector<int> verts = SelectedVertexIndices();
            if (verts.empty()) return;
            push_undo();
            tmd::DeleteVertices(obj, verts);
            ResizeSelectionToObject(obj);
            mark_dirty();
            break;
        }
        case SelectMode::Edge: {
            std::vector<tmd::Edge> edges = SelectedEdgeList();
            if (edges.empty()) return;
            push_undo();
            // TMD has no standalone edge primitive - deleting an edge means
            // deleting every face that uses it.
            auto all_edges = tmd::BuildEdgeList(obj);
            std::vector<int> faces_to_delete;
            for (auto& sel : edges) {
                for (auto& eu : all_edges) {
                    if (eu.edge == sel) {
                        faces_to_delete.insert(faces_to_delete.end(), eu.primitive_indices.begin(),
                                                eu.primitive_indices.end());
                    }
                }
            }
            tmd::DeleteFaces(obj, faces_to_delete);
            ResizeSelectionToObject(obj);
            mark_dirty();
            break;
        }
        case SelectMode::Face: {
            std::vector<int> faces = SelectedFaceIndices();
            if (faces.empty()) return;
            push_undo();
            tmd::DeleteFaces(obj, faces);
            ResizeSelectionToObject(obj);
            mark_dirty();
            break;
        }
    }
}

bool ModelEditorPanel::ComputeSelectionPivot(const tmd::TMD_Object& obj, gfx::Vec3& out_centroid,
                                              std::vector<int>& out_vertices) const {
    out_vertices.clear();
    std::vector<bool> mark(obj.vertices.size(), false);
    auto add = [&](int vi) {
        if (vi >= 0 && vi < static_cast<int>(mark.size()) && !mark[vi]) {
            mark[vi] = true;
            out_vertices.push_back(vi);
        }
    };
    if (select_mode == SelectMode::Vertex) {
        for (size_t i = 0; i < selected_vertices.size(); i++)
            if (selected_vertices[i]) add(static_cast<int>(i));
    } else if (select_mode == SelectMode::Edge) {
        for (const auto& e : selected_edges) {
            add(e.a);
            add(e.b);
        }
    } else {
        for (size_t i = 0; i < obj.polygons.size(); i++) {
            if (i >= selected_faces.size() || !selected_faces[i]) continue;
            const auto& p = obj.polygons[i];
            for (int k = 0; k < p.num_verts; k++) add(p.vert_idx[k]);
        }
    }
    if (out_vertices.empty()) return false;

    gfx::Vec3 sum{ 0, 0, 0 };
    for (int vi : out_vertices) sum = sum + VertexViewerPos(obj, vi);
    out_centroid = sum * (1.0f / static_cast<float>(out_vertices.size()));
    return true;
}

void ModelEditorPanel::RenderGizmo(tmd::TMD_Object& obj, ImVec2 viewport_pos, ImVec2 viewport_size,
                                    const gfx::Mat4& view, const gfx::Mat4& proj,
                                    const std::function<void()>& push_undo,
                                    const std::function<void()>& mark_dirty) {
    gfx::Vec3 centroid;
    std::vector<int> verts;
    bool has_selection = ComputeSelectionPivot(obj, centroid, verts);

    if (!has_selection) {
        gizmo_capturing_input = false;
        gizmo_was_using = false;
        return;
    }

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::BeginFrame();
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(viewport_pos.x, viewport_pos.y, viewport_size.x, viewport_size.y);

    if (!ImGuizmo::IsUsing()) {
        // Not currently being dragged - keep the pivot tracking the live
        // selection centroid (world-aligned: identity rotation/scale).
        gfx::Mat4 pivot = gfx::Mat4::Translate(centroid);
        std::memcpy(gizmo_matrix, pivot.m, sizeof(gizmo_matrix));
    }

    ImGuizmo::OPERATION op = gizmo_op == GizmoOp::Translate ? ImGuizmo::TRANSLATE
                             : gizmo_op == GizmoOp::Rotate  ? ImGuizmo::ROTATE
                                                             : ImGuizmo::SCALE;
    float delta[16];
    ImGuizmo::Manipulate(view.m, proj.m, op, ImGuizmo::WORLD, gizmo_matrix, delta);

    bool using_now = ImGuizmo::IsUsing();
    gizmo_capturing_input = using_now || ImGuizmo::IsOver();

    if (using_now && !gizmo_was_using) {
        // Drag just started: snapshot the affected vertices' current
        // positions once and push undo once. NOTE: `delta` (ImGuizmo's
        // deltaMatrix out-param) is NOT the total transform since the drag
        // started - reading ImGuizmo.cpp's ComputeContext() shows it
        // re-derives mModelSource from `gizmo_matrix` itself every single
        // call, so deltaMatrix is only the incremental change since the
        // *previous frame*. Applying that to a drag-start snapshot (as an
        // earlier version of this code did) made the mesh track the mouse
        // only while it was actively moving and snap back the instant it
        // stopped (the last frame's delta collapses to ~identity right
        // before release). Instead, `gizmo_matrix` itself is what ImGuizmo
        // keeps as the running absolute pivot transform - our own pivot
        // always starts a fresh drag as a pure translation (identity
        // rotation/scale, see above), so the total transform since drag
        // start is simply current_pivot * Translate(-start_centroid).
        push_undo();
        gizmo_affected_vertices = verts;
        gizmo_original_positions.clear();
        gizmo_original_positions.reserve(verts.size());
        for (int vi : verts) gizmo_original_positions.push_back(VertexViewerPos(obj, vi));
        gizmo_drag_start_centroid = centroid;
    }

    if (using_now) {
        gfx::Mat4 current_pivot;
        std::memcpy(current_pivot.m, gizmo_matrix, sizeof(current_pivot.m));
        gfx::Mat4 world_delta = current_pivot * gfx::Mat4::Translate(gizmo_drag_start_centroid * -1.0f);
        for (size_t i = 0; i < gizmo_affected_vertices.size(); i++) {
            int vi = gizmo_affected_vertices[i];
            if (vi < 0 || vi >= static_cast<int>(obj.vertices.size())) continue;
            gfx::Vec3 new_pos = gfx::TransformPoint(world_delta, gizmo_original_positions[i]);
            long nx = std::lround(new_pos.x);
            long ny = std::lround(-new_pos.y); // viewer (Y-up) -> TMD storage (Y-down)
            long nz = std::lround(new_pos.z);
            obj.vertices[vi].x = static_cast<int16_t>(std::clamp(nx, -32768L, 32767L));
            obj.vertices[vi].y = static_cast<int16_t>(std::clamp(ny, -32768L, 32767L));
            obj.vertices[vi].z = static_cast<int16_t>(std::clamp(nz, -32768L, 32767L));
        }
        mark_dirty();
    }

    gizmo_was_using = using_now;
}

void ModelEditorPanel::RenderViewport(tmd::TMD_Object& obj, VRAMManager& vram_manager,
                                       gfx::TmdTextureCache& texture_cache, const std::function<void()>& push_undo,
                                       const std::function<void()>& mark_dirty) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1 || avail.y < 1) return;
    framebuffer.EnsureSize(static_cast<int>(avail.x), static_cast<int>(avail.y));

    ImVec2 viewport_pos = ImGui::GetCursorScreenPos();
    ImGui::Image((void*)(intptr_t)framebuffer.ColorTexture(), avail, ImVec2(0, 1), ImVec2(1, 0));

    gfx::Vec3 eye{ cam_target.x + cam_distance * std::cos(cam_pitch) * std::sin(cam_yaw),
                   cam_target.y + cam_distance * std::sin(cam_pitch),
                   cam_target.z + cam_distance * std::cos(cam_pitch) * std::cos(cam_yaw) };
    gfx::Mat4 view = gfx::Mat4::LookAt(eye, cam_target, { 0, 1, 0 });
    gfx::Mat4 proj = gfx::Mat4::Perspective(60.0f * kDegToRad, avail.x / avail.y, 1.0f, 1000000.0f);

    // No ImGui item (InvisibleButton or otherwise) is ever submitted for
    // the viewport's own input - see the header comment on
    // HandleCameraInput/HandleSelectionInput for why: ImGuizmo refuses to
    // start a manipulation at all while any ImGui item is hovered/active
    // (ImGuizmo.cpp's CanActivate()), and that hover is sticky across
    // ImGui's own one-frame hover-resolution lag, so even submitting such
    // a button *after* RenderGizmo (tried previously) still blocked it on
    // any frame following one where the mouse had already hovered it.
    // IsWindowHovered() reads g.HoveredWindow, never g.HoveredId, so it
    // can never contend with ImGuizmo's check regardless of ordering.
    RenderGizmo(obj, viewport_pos, avail, view, proj, push_undo, mark_dirty);

    // Just this child window (ModelEditorViewportCol, see Render()) - not
    // its parent or siblings, so hovering the side panel never counts.
    bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    HandleCameraInput(hovered);

    // Blender-style 1/2/3 mode switch, 4/5/6 for the gizmo's Move/Rotate/
    // Scale operation (kept off WASD/QE deliberately - those fly the
    // camera below, matching "3D View"'s own convention), Delete/X to
    // remove the current selection - only while the viewport itself is
    // hovered, so these don't steal keys meant for the rest of the app.
    if (hovered) {
        if (ImGui::IsKeyPressed(ImGuiKey_1)) SetSelectMode(SelectMode::Vertex);
        if (ImGui::IsKeyPressed(ImGuiKey_2)) SetSelectMode(SelectMode::Edge);
        if (ImGui::IsKeyPressed(ImGuiKey_3)) SetSelectMode(SelectMode::Face);
        if (ImGui::IsKeyPressed(ImGuiKey_4)) gizmo_op = GizmoOp::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_5)) gizmo_op = GizmoOp::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_6)) gizmo_op = GizmoOp::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_X)) {
            DeleteSelection(obj, push_undo, mark_dirty);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F)) FrameSelection(obj);
    }

    // WASD+Q/E fly camera while hovering (matching "3D View"'s own
    // convention) - independent of any mouse button, so it works
    // alongside orbiting/selecting.
    if (hovered) HandleFlyCamera();

    // Arrow-key nudge for the current selection - a small, precise
    // keyboard alternative to dragging the gizmo, held-repeat friendly
    // (Shift for a bigger step) - see HandleKeyboardNudge.
    if (hovered) HandleKeyboardNudge(obj, view, push_undo, mark_dirty);

    // Only left-drag drives selection - middle-drag is reserved for the
    // camera above.
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        HandleSelectionInput(obj, viewport_pos, avail, view, proj, hovered);
    }

    framebuffer.Bind();
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.m);

    DrawGroundGrid(1000.0f, 50.0f);

    gfx::TmdObjectRenderer::Options options;
    options.textured = true;
    options.cull_backfaces = false;
    options.wireframe = wireframe;
    if (select_mode == SelectMode::Face) options.selected_polygons = &selected_faces;
    gfx::TmdObjectRenderer::DrawObject(obj, gfx::Mat4::Identity(), vram_manager, texture_cache, options);

    if (select_mode == SelectMode::Vertex) {
        std::vector<gfx::TmdObjectRenderer::OverlayPoint> points;
        points.reserve(obj.vertices.size());
        for (size_t i = 0; i < obj.vertices.size(); i++) {
            points.push_back({ VertexViewerPos(obj, static_cast<int>(i)), selected_vertices[i] });
        }
        gfx::TmdObjectRenderer::DrawSelectionOverlay(gfx::Mat4::Identity(), points, {});
    } else if (select_mode == SelectMode::Edge) {
        std::vector<gfx::TmdObjectRenderer::OverlayLine> lines;
        for (auto& eu : tmd::BuildEdgeList(obj)) {
            bool sel = selected_edges.count(eu.edge) > 0;
            lines.push_back({ VertexViewerPos(obj, eu.edge.a), VertexViewerPos(obj, eu.edge.b), sel });
        }
        gfx::TmdObjectRenderer::DrawSelectionOverlay(gfx::Mat4::Identity(), {}, lines);
    } else if (select_mode == SelectMode::Face) {
        // The flat orange tint DrawObject applies via options.selected_polygons
        // is modulated against the face's own texture/lighting, so it can
        // wash out on a bright texture or strong light - an outline drawn
        // through this same un-lit, always-orange overlay path (like
        // Vertex/Edge mode's dots/lines) makes a face selection unambiguous
        // regardless of what's under it.
        std::vector<gfx::TmdObjectRenderer::OverlayLine> lines;
        for (size_t pi = 0; pi < obj.polygons.size(); pi++) {
            if (pi >= selected_faces.size() || !selected_faces[pi]) continue;
            const auto& p = obj.polygons[pi];
            for (int k = 0; k < p.num_verts; k++) {
                int a = (p.num_verts == 4) ? tmd::kQuadPerimeterOrder[k] : k;
                int b = (p.num_verts == 4) ? tmd::kQuadPerimeterOrder[(k + 1) % 4] : (k + 1) % p.num_verts;
                lines.push_back({ VertexViewerPos(obj, p.vert_idx[a]), VertexViewerPos(obj, p.vert_idx[b]), true });
            }
        }
        gfx::TmdObjectRenderer::DrawSelectionOverlay(gfx::Mat4::Identity(), {}, lines);
    }

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    framebuffer.Unbind();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Box-select drag rectangle - any ongoing drag not captured by the
    // gizmo is a box-select now (see HandleSelectionInput).
    if (press.drag_moved) {
        draw_list->AddRectFilled(press.press_screen_pos, ImGui::GetMousePos(), IM_COL32(255, 165, 0, 40));
        draw_list->AddRect(press.press_screen_pos, ImGui::GetMousePos(), IM_COL32(255, 165, 0, 200));
    }

    ImVec2 orientation_center(viewport_pos.x + avail.x - 40.0f, viewport_pos.y + 40.0f);
    DrawOrientationIndicator(draw_list, orientation_center, 24.0f, view);
}

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
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            char label[32];
            snprintf(label, sizeof(label), "tsb=0x%04X cba=0x%04X", t.first, t.second);
            if (ImGui::Button(label)) {
                uv_tile_tsb = t.first;
                uv_tile_cba = t.second;
            }
            if (active) ImGui::PopStyleColor();
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
