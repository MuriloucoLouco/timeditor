#pragma once
#include "tmd_format.h"
#include <string>
#include <vector>

namespace tmd {

// Owns every loaded .tmd file (independently of tim::Document's TIM file
// list) and the undo/redo history for edits to their objects - the TMD-side
// counterpart to tim::Document, giving TmdPanel the same document/UI split
// tim::Document/InspectorPanel already have. Stays free of ImGui/OpenGL on
// purpose, same as tim::Document: TmdPanel (and the Model Editor/Raw Editor
// it hosts) own every rendering-only concern themselves (camera, render
// toggles, GL framebuffers, texture cache).
class TmdDocument {
public:
    // Per-object viewer-only placement. TMD stores no object hierarchy/
    // placement of its own (see tmd_format.h), so this is just a manual
    // convenience the user can adjust, not data read from the file.
    struct ObjectTransform {
        float pos[3] = { 0, 0, 0 };
        float rot_deg[3] = { 0, 0, 0 };
        float scale = 1.0f;
    };

    struct LoadedModel {
        TMD_Model model;
        std::vector<ObjectTransform> transforms;
        std::vector<bool> visible; // which objects are included in the render, like TIM's per-image checkboxes
        bool dirty = false;
    };

    std::vector<LoadedModel>& Models() { return models; }
    const std::vector<LoadedModel>& Models() const { return models; }

    int ActiveModel() const { return active_model; }
    int ActiveObject() const { return active_object; }
    void SetActive(int model_index, int object_index) {
        active_model = model_index;
        active_object = object_index;
    }

    bool HasActiveModel() const { return active_model >= 0 && active_model < static_cast<int>(models.size()); }
    bool HasActiveObject() const {
        return HasActiveModel() && active_object >= 0 &&
               active_object < static_cast<int>(models[active_model].model.objects.size());
    }
    bool ActiveModelDirty() const { return HasActiveModel() && models[active_model].dirty; }
    std::string ActiveModelFilename() const { return HasActiveModel() ? models[active_model].model.filename : ""; }

    // Loads `path` and makes it the active model. If `path` is already
    // loaded, this is a no-op (matches the editor's prior behavior of
    // silently ignoring a repeat "Open" of an already-open file rather than
    // re-focusing it) and returns false. Returns true if a new model was
    // actually loaded (and thus became active).
    bool LoadFile(const std::string& path);

    // Creates a brand-new, empty .tmd (0 objects) at `path` and makes it the
    // active model - the caller (TmdPanel::NewModel) is responsible for
    // asking the user for `path` first (a save dialog, a UI concern this
    // class has no business with). If `path` is already loaded, just makes
    // it active instead of creating a duplicate entry.
    void NewModelAt(const std::string& path);

    // Appends one new, empty object to the given model and makes it active -
    // the sidebar's "+ Add Object" row.
    void AddObject(int model_index);

    // Discards the model at `index` (in-memory, unsaved changes and all -
    // the caller is responsible for any "are you sure?" confirmation) and
    // fixes up ActiveModel()/ActiveObject() if needed.
    void CloseModel(int index);

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

    bool SaveActiveModel();
    bool SaveActiveModelAs(const std::string& new_path);

    bool AnyModelDirty() const;
    void SaveAllDirtyModels();

private:
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
        TMD_Object object;
    };
    static constexpr size_t kMaxUndo = 20;
    std::vector<ObjectSnapshot> undo_stack;
    std::vector<ObjectSnapshot> redo_stack;
};

} // namespace tmd
