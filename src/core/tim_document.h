#pragma once
#include "tim_format.h"
#include <vector>
#include <string>
#include <set>

namespace tim {

// Owns every loaded TIM_Image and is the only place allowed to mutate them.
// Centralizing edits here (instead of poking TIM_Image fields from UI code)
// keeps dirty-tracking and future editing features (palette edits, pixel
// edits, etc.) consistent no matter which panel triggers them.
class Document {
public:
    std::vector<TIM_Image>& Images() { return images; }
    const std::vector<TIM_Image>& Images() const { return images; }

    // Appends newly loaded images; returns the index of the first one
    // added (handy for auto-selecting it), or -1 if none.
    int AddImages(std::vector<TIM_Image>&& new_images);

    int GetActiveIndex() const { return active_index; }
    void SetActiveIndex(int index) { active_index = index; }

    // Moves an image/CLUT block within VRAM, clamped to stay in bounds.
    // No-op (and doesn't dirty the file) if the position doesn't change.
    void SetImageOrigin(int index, int x, int y);
    void SetClutOrigin(int index, int x, int y);

    bool IsFileDirty(const std::string& filepath) const;

    // Removes every image belonging to filepath from the document (e.g. the
    // user closed that file's tab). Returns false if no image had that path.
    bool CloseFile(const std::string& filepath);

    // Undo support for VRAM origin edits (image/CLUT moves). Call
    // PushUndoSnapshot() before starting an edit gesture (e.g. a drag);
    // if the gesture turns out to have changed nothing, call
    // DiscardLastUndo() instead of leaving a no-op entry on the stack.
    // Undo() restores the most recent snapshot and returns whether it did
    // anything. The stack is cleared whenever images are added or removed,
    // since a snapshot's per-index layout no longer applies after that.
    void PushUndoSnapshot();
    void DiscardLastUndo();
    bool Undo();
    bool CanUndo() const { return !undo_stack.empty(); }

    // Bumped whenever the loaded set or any image/CLUT VRAM origin changes.
    // VRAMPanel compares this against its own last-seen value to know when
    // the emulated VRAM needs to be rebuilt from scratch.
    int GetVramVersion() const { return vram_version; }

    // Bumped only when images are added or removed (not on a plain move).
    // Panels that cache indices into Images() (selection lists, drag state)
    // must reset that cache when this changes, since indices can shift.
    int GetStructureVersion() const { return structure_version; }

    // Re-serializes every image belonging to filepath (in file order) and overwrites it.
    bool Save(const std::string& filepath);

    // Same as Save, but writes to new_filepath and re-points those images at it.
    bool SaveAs(const std::string& filepath, const std::string& new_filepath);

private:
    struct UndoSnapshot {
        std::vector<uint16_t> image_x, image_y, clut_x, clut_y; // parallel to `images`
        std::set<std::string> dirty_files;
    };

    std::vector<TIM_Image> images;
    int active_index = -1;
    std::set<std::string> dirty_files;
    int vram_version = 0;
    int structure_version = 0;
    std::vector<UndoSnapshot> undo_stack;

    std::vector<int> IndicesForFile(const std::string& filepath) const;
};

} // namespace tim
