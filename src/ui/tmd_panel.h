#pragma once
#include "imgui.h"
#include "../core/tim_document.h"
#include "../core/tmd_document.h"
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

// The "TMD Editor" tab: a thin UI layer over tmd::TmdDocument (which owns
// every loaded .tmd file, independently of the .tim Document's file list,
// and its undo/redo history - see tmd_document.h). Renders objects using
// the shared VRAMManager as the texture source, the same way a real PS1
// would - a polygon's texpage/CLUT fields address VRAM directly, so
// whatever TIMs are loaded and positioned there is what shows up on the
// model. Everything here (camera, render toggles, GL framebuffer, texture
// cache, sidebar/dialogs) is rendering/UI state that has no business living
// on the document itself.
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
    void PushUndo(int model_index, int object_index) { doc.PushUndo(model_index, object_index); }
    bool CanUndo() const { return doc.CanUndo(); }
    void Undo() { doc.Undo(); }

    // Mirrors tim::Document's Undo/Redo design: every Undo() moves the
    // object state it's about to overwrite onto redo_stack, so Redo() can
    // put it straight back; PushUndo (any new edit) clears redo_stack.
    bool CanRedo() const { return doc.CanRedo(); }
    void Redo() { doc.Redo(); }

    bool HasActiveModel() const { return doc.HasActiveModel(); }
    bool ActiveModelDirty() const { return doc.ActiveModelDirty(); }
    std::string ActiveModelFilename() const { return doc.ActiveModelFilename(); }
    bool SaveActiveModel() { return doc.SaveActiveModel(); }
    bool SaveActiveModelAs(const std::string& new_path) { return doc.SaveActiveModelAs(new_path); }

    bool AnyModelDirty() const { return doc.AnyModelDirty(); }
    void SaveAllDirtyModels() { doc.SaveAllDirtyModels(); }

    // Opens the Export/Import Model dialogs for the active model - shared
    // by the sidebar's own buttons and the top menu bar's "Export"/"Import
    // Model..." items, so both stay in sync with whatever's "active" here.
    void OpenExportDialog();
    void OpenImportDialog();

private:
    using LoadedModel = tmd::TmdDocument::LoadedModel;
    using ObjectTransform = tmd::TmdDocument::ObjectTransform;

    tmd::TmdDocument doc;

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
