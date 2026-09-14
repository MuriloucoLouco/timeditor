#pragma once
#include "imgui.h"
#include "../core/tim_document.h"
#include "../core/tmd_format.h"
#include "../core/vram_manager.h"
#include "../gfx/framebuffer.h"
#include "../gfx/math3d.h"
#include "../gfx/tmd_texture_cache.h"
#include "tmd_raw_editor.h"
#include "model_editor_panel.h"
#include "model_export_dialog.h"
#include "model_import_dialog.h"
#include <string>
#include <vector>

namespace ui {

// The "TMD Editor" tab: loads .tmd files (independently of the .tim
// Document's file list) and renders their objects using the shared
// VRAMManager as the texture source, the same way a real PS1 would - a
// polygon's texpage/CLUT fields address VRAM directly, so whatever TIMs
// are loaded and positioned there is what shows up on the model.
class TmdPanel {
public:
    void Render(tim::Document& document, VRAMManager& vram_manager);
    void LoadFile(const std::string& path, const tim::Document& document);

    // Creates a brand-new, empty .tmd (0 objects) at a path the user picks
    // via a save dialog (there's nothing on disk yet to "open" - this is
    // the TMD-side equivalent of Document::AddBlankImage/EditorApp::NewTim)
    // and makes it the active model. Use the sidebar's own "+ New Object"
    // row (per model, once it has at least this) to start building a mesh.
    void NewModel();

    // Pushes a snapshot of models[model_index].objects[object_index] onto
    // the undo stack. Call before mutating an object's fields.
    void PushUndo(int model_index, int object_index);
    bool CanUndo() const { return !undo_stack.empty(); }
    void Undo();

    // Mirrors tim::Document's Undo/Redo design: every Undo() moves the
    // object state it's about to overwrite onto redo_stack, so Redo() can
    // put it straight back; PushUndo (any new edit) clears redo_stack.
    bool CanRedo() const { return !redo_stack.empty(); }
    void Redo();

    bool HasActiveModel() const { return active_model >= 0 && active_model < static_cast<int>(models.size()); }
    bool ActiveModelDirty() const { return HasActiveModel() && models[active_model].dirty; }
    std::string ActiveModelFilename() const { return HasActiveModel() ? models[active_model].model.filename : ""; }
    bool SaveActiveModel();
    bool SaveActiveModelAs(const std::string& new_path);

    bool AnyModelDirty() const;
    void SaveAllDirtyModels();

    // Opens the Export/Import Model dialogs for the active model - shared
    // by the sidebar's own buttons and the top menu bar's "Export"/"Import
    // Model..." items, so both stay in sync with whatever's "active" here.
    void OpenExportDialog();
    void OpenImportDialog();

private:
    // Per-object viewer-only placement. TMD stores no object hierarchy/
    // placement of its own (see tmd_format.h), so this is just a manual
    // convenience the user can adjust, not data read from the file.
    struct ObjectTransform {
        float pos[3] = { 0, 0, 0 };
        float rot_deg[3] = { 0, 0, 0 };
        float scale = 1.0f;
    };

    struct LoadedModel {
        tmd::TMD_Model model;
        std::vector<ObjectTransform> transforms;
        std::vector<bool> visible; // which objects are included in the render, like TIM's per-image checkboxes
        bool dirty = false;
    };

    std::vector<LoadedModel> models;
    int active_model = -1;
    int active_object = -1; // which object's stats/transform show in the info bar; -1 = whole-file aggregate

    // Snapshots a whole TMD_Object before some editing operation mutates it
    // (a raw-editor field edit, or an OBJ sync apply) - simpler than TIM's
    // undo (tim_document.h) since there's no VRAM/texture coupling to
    // invalidate here, just object data. Capped so it can't grow unbounded.
    struct ObjectSnapshot {
        int model_index;
        int object_index;
        tmd::TMD_Object object;
    };
    static constexpr size_t kMaxUndo = 20;
    std::vector<ObjectSnapshot> undo_stack;
    std::vector<ObjectSnapshot> redo_stack;

    float sidebar_width = 220.0f;
    // File bar (filename/dirty/Save/Undo) is model-level state that
    // applies no matter which sub-tab is open, so it stays above the tab
    // bar; the render-toggle/camera/transform info bar is 3D-View-only
    // (Model Editor and Raw Editor render at identity transform and have
    // their own toggles/camera) so it now lives inside that tab alone -
    // see RenderFileBar/RenderInfoBar and RenderMainArea.
    static constexpr float kFileBarHeight = 40.0f;
    static constexpr float kInfoBarHeight = 70.0f; // 2 rows: stats+toggles, transform

    bool textured = true;
    bool cull_backfaces = false;
    // A freshly loaded model should show its actual shaded/textured
    // surface, not just outlines - wireframe is an inspection mode you opt
    // into, matching gfx::TmdObjectRenderer::Options' own default and every
    // other renderer in this app (Model Editor, Raw Editor's 3D preview,
    // neither of which even offer a wireframe toggle, both effectively
    // "off"). This one previously defaulted to true, so a textured model
    // loaded looking like bare wireframe until you noticed and unchecked it.
    bool wireframe = false;

    // Orbit camera, in spherical coordinates around `cam_target`.
    float cam_yaw = 0.6f;
    float cam_pitch = 0.35f;
    float cam_distance = 400.0f;
    gfx::Vec3 cam_target{ 0, 0, 0 };

    gfx::Framebuffer framebuffer;
    gfx::TmdTextureCache texture_cache;
    int last_vram_version = -1;

    TmdRawEditor raw_editor;
    ModelEditorPanel model_editor;
    ModelExportDialog export_dialog;
    ModelImportDialog import_dialog;

    int pending_close_index = -1;
    bool open_close_confirm = false;

    void RenderSidebar(const tim::Document& document);
    void RenderCloseConfirmPopup();
    void RenderFileBar();
    void RenderInfoBar();
    void RenderMainArea(const tim::Document& document, VRAMManager& vram_manager);
    void RenderViewport(VRAMManager& vram_manager);
    void HandleCameraInput(bool hovered, bool dragging);
    void DrawModel(const LoadedModel& loaded, VRAMManager& vram_manager);
    void DrawObject(const tmd::TMD_Object& obj, const ObjectTransform& xform, VRAMManager& vram_manager);
    gfx::Mat4 BuildModelMatrix(const ObjectTransform& t) const;
    void FrameSelection();
};

} // namespace ui
