// ModelEditorPanel's top-level Render/state/toolbar/viewport-rendering
// methods. Camera/selection-input/gizmo logic lives in
// model_editor_input.cpp, the face-properties panel and UV workspace in
// model_editor_uv.cpp - split purely to keep any one file from growing
// unmanageably long; see docs/ARCHITECTURE.md and model_editor_internal.h.
#include "model_editor_panel.h"
#include "model_editor_internal.h"
#include "../gfx/gl_ext.h"
#include "../gfx/tmd_object_renderer.h"
#include "IconsFontAwesome6.h"
#include "icon_button.h"
#include "splitter.h"
#include "../core/gl_compat.h"
#include <algorithm>
#include <cmath>

namespace ui {

using namespace model_editor_internal;

namespace {

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
    // The vertex/face count check (beyond just the model/object index) is
    // needed because the object's contents can change out from under this
    // panel without going through any of its own mesh-editing calls, which
    // are the only place that otherwise keeps selected_vertices/faces sized
    // to match - e.g. Undo/Redo (TmdPanel::Undo swaps the whole TMD_Object
    // wholesale) or an OBJ/glTF reimport. Without this, a stale, wrong-sized
    // selection bitset silently indexed out of bounds (vector<bool> gives no
    // bounds error, just corrupts an unrelated bit) - symptoms ranged from
    // picking a face doing nothing to it staying that way even after
    // closing and reopening the file, since the model/object index the
    // panel last saw never actually changed across that.
    if (model_index != last_model_index || object_index != last_object_index ||
        obj.vertices.size() != selected_vertices.size() || obj.polygons.size() != selected_faces.size()) {
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
        if (IconButton(icon, tooltip, kIconBtn, select_mode == mode)) SetSelectMode(mode);
    };
    auto IconGizmoButton = [&](const char* icon, const char* tooltip, GizmoOp op) {
        if (IconButton(icon, tooltip, kIconBtn, gizmo_op == op)) gizmo_op = op;
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
    // A plain (never-highlighted) action button at this toolbar's icon
    // size - see icon_button.h for id_suffix's purpose (two buttons using
    // the same icon in one frame, like the two Merge buttons below, would
    // otherwise collide on the same ImGui ID).
    auto IconButton = [&](const char* icon, const char* tooltip, const char* id_suffix = nullptr) {
        return ui::IconButton(icon, tooltip, kIconBtn, false, id_suffix);
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
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(faces.empty());
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
    gfx::gl::LogGLErrors("ModelEditorPanel::RenderViewport");

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

} // namespace ui
