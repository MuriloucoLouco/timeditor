#pragma once
#include "../core/tmd_format.h"
#include "../core/tmd_mesh_ops.h"
#include "../core/vram_manager.h"
#include "../gfx/framebuffer.h"
#include "../gfx/math3d.h"
#include "../gfx/tmd_texture_cache.h"
#include "imgui.h"
#include <functional>
#include <set>
#include <vector>

namespace ui {

// The TMD Editor's "Model Editor" tab: a real interactive 3D mesh editor
// (select vertices/edges/faces, move/rotate/scale them with an on-screen
// gizmo) alongside the read-only "3D View" and the field-level "Raw
// Editor". Doesn't know about TmdPanel's LoadedModel/undo-stack
// bookkeeping - same calling convention as TmdRawEditor: the caller passes
// the object to edit directly plus push_undo/mark_dirty callbacks.
//
// Controls: left-click selects (click empty space to clear, Shift+click to
// add/remove one element), click-drag from empty space (or over an
// unselected element) box-selects. The moment 1+ elements are selected, a
// gizmo (ImGuizmo) appears at the selection's centroid - dragging one of
// its handles is the only way to move/rotate/scale the selection (matching
// Maya/Unity/Unreal's persistent-gizmo convention, rather than a bare
// click-drag on the geometry itself); W/E/R or the toolbar's Move/Rotate/
// Scale buttons switch which the gizmo shows. Middle-drag orbits the
// camera, Shift+middle-drag pans, scroll zooms - deliberately different
// from "3D View"'s left-drag-orbits scheme, since here left-click is busy
// selecting.
class ModelEditorPanel {
public:
    void Render(int model_index, int object_index, tmd::TMD_Object& obj, VRAMManager& vram_manager,
                gfx::TmdTextureCache& texture_cache, const std::function<void()>& push_undo,
                const std::function<void()>& mark_dirty);

private:
    enum class SelectMode { Vertex, Edge, Face };
    SelectMode select_mode = SelectMode::Face;
    bool wireframe = false; // GL_LINE polygon mode instead of filled - see RenderViewport

    int last_model_index = -1;
    int last_object_index = -1;

    std::vector<bool> selected_vertices; // parallel to obj.vertices
    std::vector<bool> selected_faces;    // parallel to obj.polygons
    std::set<tmd::Edge> selected_edges;  // edges are derived, not stored - see tmd_mesh_ops.h

    // Own orbit camera, independent of TmdPanel's "3D View" tab.
    float cam_yaw = 0.6f;
    float cam_pitch = 0.35f;
    float cam_distance = 400.0f;
    gfx::Vec3 cam_target{ 0, 0, 0 };

    gfx::Framebuffer framebuffer;
    float side_width = 340.0f;

    // UV workspace state (Phase 8) - which texpage tile is currently shown
    // (a TMD primitive's UV always addresses one specific tile, so unlike
    // a conventional UV editor's single shared 0-1 atlas, this workspace is
    // tile-scoped - see RenderUvWorkspace).
    uint16_t uv_tile_tsb = 0;
    uint16_t uv_tile_cba = 0;
    bool uv_tile_valid = false;
    float uv_zoom = 4.0f;
    float uv_scale_input = 1.0f;
    float uv_rotate_input_deg = 0.0f;
    bool uv_panning_active = false; // right-drag-to-pan session - see zoom_pan.h's PanWithMouseDrag

    // What the mouse was over when the current click/drag started, and
    // whether it's turned into an actual drag yet - both persist across
    // frames for the duration of one press-to-release input gesture (set
    // on IsItemActivated, read/updated while active, resolved on
    // IsItemDeactivated). See ModelEditorPanel.cpp's HandleSelectionInput
    // for the exact click-vs-drag-vs-box-select decision tree. Note this
    // never *moves* anything itself anymore - dragging is exclusively the
    // gizmo's job (see below); a drag that isn't on a gizmo handle is
    // always a box-select attempt, even if it started on an already-
    // selected element, matching how Maya/Unity/Unreal behave once a
    // persistent transform gizmo is in the picture.
    enum class PressTarget { None, Vertex, Edge, Face };
    struct PressState {
        bool active = false; // a gesture this code itself started is in progress (see note below)
        PressTarget target = PressTarget::None;
        int vertex_index = -1;
        tmd::Edge edge{};
        int face_index = -1;
        bool drag_moved = false;
        ImVec2 press_screen_pos{};
    };
    PressState press;

    // Middle-mouse orbit/pan session, tracked the same explicit way as
    // `press` above and for the identical reason - see HandleCameraInput.
    bool middle_orbit_active = false;

    // Gizmo (ImGuizmo) state. The pivot matrix (translation = selection
    // centroid, identity rotation/scale - a "world-aligned" pivot) is
    // recomputed fresh every frame the gizmo ISN'T being dragged, so it
    // tracks a changing selection/geometry live; the moment a drag starts,
    // it's left alone (ImGuizmo owns writing to it) and a snapshot of the
    // affected vertices' original positions is taken so each frame's delta
    // - which ImGuizmo reports as the absolute transform since the drag
    // started, not a per-frame increment - can be applied fresh rather
    // than accumulated (avoiding float drift over a long drag).
    enum class GizmoOp { Translate, Rotate, Scale };
    GizmoOp gizmo_op = GizmoOp::Translate;
    float gizmo_matrix[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    bool gizmo_was_using = false;
    bool gizmo_capturing_input = false; // set by RenderGizmo, read by HandleSelectionInput to avoid double-handling a click
    std::vector<int> gizmo_affected_vertices;
    std::vector<gfx::Vec3> gizmo_original_positions; // parallel to gizmo_affected_vertices, viewer (Y-up) space
    gfx::Vec3 gizmo_drag_start_centroid{}; // pivot position when the current drag began - see RenderGizmo

    void ResizeSelectionToObject(const tmd::TMD_Object& obj);
    void ClearSelection();
    void SetSelectMode(SelectMode mode);

    // `hovered` is computed via ImGui::IsWindowHovered() on the viewport's
    // own child window, NOT an ImGui item (no InvisibleButton exists for
    // the viewport at all) - and every press/drag/release below is read
    // straight from io.MouseDown/IsMouseClicked/IsMouseReleased rather than
    // an item's IsItemActive()/IsItemActivated()/IsItemDeactivated().
    // This is deliberate, not a style choice: ImGuizmo internally refuses
    // to start a manipulation at all while *any* ImGui item is hovered or
    // active (ImGuizmo.cpp's CanActivate()), and an item's hover is sticky
    // across the one-frame lag in ImGui's own hover resolution - so a
    // viewport-sized InvisibleButton (tried first, then reordered before
    // the gizmo, neither of which was enough) permanently blocked the
    // gizmo the instant the mouse had hovered it for even one prior frame.
    // Plain window-hover and raw mouse queries never touch ImGui's
    // item/active-id system at all, so they can never conflict with it.
    void HandleCameraInput(bool hovered);
    void HandleSelectionInput(tmd::TMD_Object& obj, ImVec2 viewport_pos, ImVec2 viewport_size, const gfx::Mat4& view,
                               const gfx::Mat4& proj, bool hovered);
    void DeleteSelection(tmd::TMD_Object& obj, const std::function<void()>& push_undo,
                          const std::function<void()>& mark_dirty);

    // Computes the current selection's centroid (in viewer/Y-up space) and
    // the vertex indices it implies (mode-aware: a face selection expands
    // to its corners, an edge selection to its two endpoints, etc.) -
    // shared by the gizmo pivot and its drag-start snapshot.
    bool ComputeSelectionPivot(const tmd::TMD_Object& obj, gfx::Vec3& out_centroid, std::vector<int>& out_vertices) const;
    void RenderGizmo(tmd::TMD_Object& obj, ImVec2 viewport_pos, ImVec2 viewport_size, const gfx::Mat4& view,
                      const gfx::Mat4& proj, const std::function<void()>& push_undo,
                      const std::function<void()>& mark_dirty);

    // QoL: WASD+Q/E fly camera (matching "3D View"'s own convention),
    // arrow-key/PageUp/PageDown nudge of the current selection along the
    // camera's own screen-relative axes (Shift for a bigger step, one
    // undo entry per press-and-hold gesture), and "F" to frame the camera
    // on the current selection.
    void HandleFlyCamera();
    void HandleKeyboardNudge(tmd::TMD_Object& obj, const gfx::Mat4& view, const std::function<void()>& push_undo,
                              const std::function<void()>& mark_dirty);
    void FrameSelection(const tmd::TMD_Object& obj);
    bool nudge_active = false; // whether a nudge key is currently held, for the one-undo-per-hold rule above

    void RenderToolbar(tmd::TMD_Object& obj, const std::function<void()>& push_undo,
                        const std::function<void()>& mark_dirty);
    void RenderStatusLine(const tmd::TMD_Object& obj) const;

    std::vector<int> SelectedVertexIndices() const;
    std::vector<int> SelectedFaceIndices() const;
    std::vector<tmd::Edge> SelectedEdgeList() const;
    void RenderViewport(tmd::TMD_Object& obj, VRAMManager& vram_manager, gfx::TmdTextureCache& texture_cache,
                         const std::function<void()>& push_undo, const std::function<void()>& mark_dirty);

    // Right-hand column: contextual field editing for the current face
    // selection (Face mode only - vertices/edges have no primitive fields
    // of their own to show). Unlike the Raw Editor's opt-in-checkbox batch
    // editor (built for an arbitrary, possibly heterogeneous tree
    // selection), this applies every change live to the whole selection
    // immediately - a face selection made here is normally a small,
    // deliberate, already-coherent group (e.g. "this door panel"), so a
    // direct properties-panel feel is the more natural, less fiddly fit.
    void RenderSidePanel(tmd::TMD_Object& obj, VRAMManager& vram_manager, gfx::TmdTextureCache& texture_cache,
                         const std::function<void()>& push_undo, const std::function<void()>& mark_dirty);
    void RenderFacePropertiesPanel(tmd::TMD_Object& obj, const std::vector<int>& faces,
                                    const std::function<void()>& push_undo, const std::function<void()>& mark_dirty);

    // The tile-scoped multi-face UV workspace (Phase 8) - see the class
    // comment in model_editor_panel.cpp's RenderUvWorkspace for the exact
    // design (tile picker, island-move drag, numeric scale/rotate).
    void RenderUvWorkspace(tmd::TMD_Object& obj, const std::vector<int>& faces, VRAMManager& vram_manager,
                            gfx::TmdTextureCache& texture_cache, const std::function<void()>& push_undo,
                            const std::function<void()>& mark_dirty);
};

} // namespace ui
