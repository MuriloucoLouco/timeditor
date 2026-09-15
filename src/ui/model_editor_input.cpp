// ModelEditorPanel's camera, selection-picking/input, and gizmo methods -
// split out of model_editor_panel.cpp (which keeps Render/state/toolbar)
// purely to keep any one file from growing unmanageably long. See
// docs/ARCHITECTURE.md and model_editor_internal.h's own comment.
#include "model_editor_panel.h"
#include "model_editor_internal.h"
#include "../gfx/ray.h"
#include "ImGuizmo.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ui {

namespace {
using namespace model_editor_internal;

constexpr float kPickPixelRadius = 10.0f;
constexpr float kDragThresholdPixels = 4.0f;

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

} // namespace

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

} // namespace ui
