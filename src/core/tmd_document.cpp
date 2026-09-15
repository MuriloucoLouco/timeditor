#include "tmd_document.h"
#include "tmd_parser.h"
#include "tmd_writer.h"

namespace tmd {

bool TmdDocument::LoadFile(const std::string& path) {
    for (const auto& loaded : models) {
        if (loaded.model.filename == path) return false;
    }

    TMD_Model model;
    if (!Parser::LoadFromFile(path, model)) return false;

    LoadedModel loaded;
    loaded.model = std::move(model);
    loaded.transforms.resize(loaded.model.objects.size());
    loaded.visible.assign(loaded.model.objects.size(), true);
    models.push_back(std::move(loaded));

    active_model = static_cast<int>(models.size()) - 1;
    active_object = -1;
    return true;
}

void TmdDocument::NewModelAt(const std::string& path) {
    for (size_t i = 0; i < models.size(); i++) {
        if (models[i].model.filename == path) {
            active_model = static_cast<int>(i);
            active_object = -1;
            return;
        }
    }

    LoadedModel loaded;
    loaded.model.filename = path;
    loaded.dirty = true; // nothing written to `path` yet - there's real state to save
    models.push_back(std::move(loaded));

    active_model = static_cast<int>(models.size()) - 1;
    active_object = -1;
}

void TmdDocument::AddObject(int model_index) {
    if (model_index < 0 || model_index >= static_cast<int>(models.size())) return;
    LoadedModel& loaded = models[model_index];

    // A default-constructed TMD_Object is a perfectly valid empty object
    // (0 verts/normals/polygons) - there's nothing else to fill in before
    // it can be selected and built up from scratch in the Model Editor tab.
    loaded.model.objects.emplace_back();
    loaded.transforms.emplace_back();
    loaded.visible.push_back(true);
    loaded.dirty = true;

    active_model = model_index;
    active_object = static_cast<int>(loaded.model.objects.size()) - 1;
}

void TmdDocument::CloseModel(int index) {
    if (index < 0 || index >= static_cast<int>(models.size())) return;
    models.erase(models.begin() + index);
    if (active_model == index) {
        active_model = -1;
        active_object = -1;
    } else if (active_model > index) {
        active_model--;
    }
}

void TmdDocument::PushUndo(int model_index, int object_index) {
    if (model_index < 0 || model_index >= static_cast<int>(models.size())) return;
    const auto& objects = models[model_index].model.objects;
    if (object_index < 0 || object_index >= static_cast<int>(objects.size())) return;

    if (undo_stack.size() >= kMaxUndo) undo_stack.erase(undo_stack.begin());
    undo_stack.push_back({ model_index, object_index, objects[object_index] });
    redo_stack.clear(); // A new edit - any pending redo is now stale.
}

void TmdDocument::Undo() {
    if (undo_stack.empty()) return;
    ObjectSnapshot snap = std::move(undo_stack.back());
    undo_stack.pop_back();

    if (snap.model_index < 0 || snap.model_index >= static_cast<int>(models.size())) return;
    auto& objects = models[snap.model_index].model.objects;
    if (snap.object_index < 0 || snap.object_index >= static_cast<int>(objects.size())) return;

    if (redo_stack.size() >= kMaxUndo) redo_stack.erase(redo_stack.begin());
    redo_stack.push_back({ snap.model_index, snap.object_index, objects[snap.object_index] });

    objects[snap.object_index] = std::move(snap.object);
    models[snap.model_index].dirty = true;
}

void TmdDocument::Redo() {
    if (redo_stack.empty()) return;
    ObjectSnapshot snap = std::move(redo_stack.back());
    redo_stack.pop_back();

    if (snap.model_index < 0 || snap.model_index >= static_cast<int>(models.size())) return;
    auto& objects = models[snap.model_index].model.objects;
    if (snap.object_index < 0 || snap.object_index >= static_cast<int>(objects.size())) return;

    if (undo_stack.size() >= kMaxUndo) undo_stack.erase(undo_stack.begin());
    undo_stack.push_back({ snap.model_index, snap.object_index, objects[snap.object_index] });

    objects[snap.object_index] = std::move(snap.object);
    models[snap.model_index].dirty = true;
}

bool TmdDocument::SaveActiveModel() {
    if (!HasActiveModel()) return false;
    LoadedModel& loaded = models[active_model];
    if (!Writer::WriteToFile(loaded.model.filename, loaded.model)) return false;
    loaded.dirty = false;
    return true;
}

bool TmdDocument::SaveActiveModelAs(const std::string& new_path) {
    if (!HasActiveModel()) return false;
    LoadedModel& loaded = models[active_model];
    if (!Writer::WriteToFile(new_path, loaded.model)) return false;
    loaded.model.filename = new_path;
    loaded.dirty = false;
    return true;
}

bool TmdDocument::AnyModelDirty() const {
    for (const auto& loaded : models) {
        if (loaded.dirty) return true;
    }
    return false;
}

void TmdDocument::SaveAllDirtyModels() {
    for (auto& loaded : models) {
        if (loaded.dirty && Writer::WriteToFile(loaded.model.filename, loaded.model)) {
            loaded.dirty = false;
        }
    }
}

} // namespace tmd
