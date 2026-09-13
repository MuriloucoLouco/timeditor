#include "tim_document.h"
#include "tim_writer.h"
#include "vram_manager.h"
#include <algorithm>

namespace tim {

int Document::AddImages(std::vector<TIM_Image>&& new_images) {
    if (new_images.empty()) return -1;
    DiscardPendingDelete(); // Indices are about to shift.
    int first_index = static_cast<int>(images.size());
    for (auto& img : new_images) {
        images.push_back(std::move(img));
    }
    vram_version++;
    structure_version++;
    undo_stack.clear(); // Snapshots are index-based; the layout just changed.
    content_undo_stack.clear();
    return first_index;
}

int Document::AddBlankImage(const std::string& filepath) {
    TIM_Image img;
    img.filename = filepath;

    int max_file_index = -1;
    for (const auto& existing : images) {
        if (existing.filename == filepath) max_file_index = std::max(max_file_index, existing.file_index);
    }
    img.file_index = max_file_index + 1;

    img.header.id = 0x00000010;
    img.type = 2; // Direct16BPP
    img.header.flag = 2; // pixel mode only, no CLUT bit
    img.has_clut = false;
    img.bpp = 16;
    img.real_width = 32;

    img.image_header.origin_x = 0;
    img.image_header.origin_y = 0;
    img.image_header.width = 32; // 1 word per pixel at 16bpp
    img.image_header.height = 32;
    img.image_data.assign(static_cast<size_t>(32) * 32 * 2, 0);
    img.image_header.size = static_cast<uint32_t>(12 + img.image_data.size());

    std::vector<TIM_Image> wrapper;
    wrapper.push_back(std::move(img));
    return AddImages(std::move(wrapper));
}

void Document::SetImageOrigin(int index, int x, int y) {
    if (index < 0 || index >= static_cast<int>(images.size())) return;
    TIM_Image& img = images[index];

    int max_x = std::max(0, VRAMManager::kWidth - static_cast<int>(img.image_header.width));
    int max_y = std::max(0, VRAMManager::kHeight - static_cast<int>(img.image_header.height));
    x = std::clamp(x, 0, max_x);
    y = std::clamp(y, 0, max_y);

    if (img.image_header.origin_x == x && img.image_header.origin_y == y) return;
    img.image_header.origin_x = static_cast<uint16_t>(x);
    img.image_header.origin_y = static_cast<uint16_t>(y);
    img.dirty = true;
    vram_version++;
}

void Document::SetClutOrigin(int index, int x, int y) {
    if (index < 0 || index >= static_cast<int>(images.size())) return;
    TIM_Image& img = images[index];
    if (!img.has_clut) return;

    int max_x = std::max(0, VRAMManager::kWidth - static_cast<int>(img.clut_header.colors_per_clut));
    int max_y = std::max(0, VRAMManager::kHeight - static_cast<int>(img.clut_header.num_cluts));
    x = std::clamp(x, 0, max_x);
    y = std::clamp(y, 0, max_y);

    if (img.clut_header.origin_x == x && img.clut_header.origin_y == y) return;
    img.clut_header.origin_x = static_cast<uint16_t>(x);
    img.clut_header.origin_y = static_cast<uint16_t>(y);
    img.dirty = true;
    vram_version++;
}

void Document::ReplaceImageContent(int index, ImageContent&& content) {
    if (index < 0 || index >= static_cast<int>(images.size())) return;
    TIM_Image& img = images[index];

    img.type = content.type;
    img.bpp = content.bpp;
    img.real_width = content.pixel_width;
    img.image_header.width = static_cast<uint16_t>(content.width_words);
    img.image_header.height = static_cast<uint16_t>(content.height);
    img.image_data = std::move(content.image_data);
    img.image_header.size = static_cast<uint32_t>(12 + img.image_data.size());

    img.has_clut = content.has_clut;
    if (content.has_clut) {
        img.clut_header.colors_per_clut = static_cast<uint16_t>(content.colors_per_clut);
        img.clut_header.num_cluts = static_cast<uint16_t>(content.num_cluts);
        img.clut_data = std::move(content.clut_data);
        img.clut_header.size = static_cast<uint32_t>(12 + img.clut_data.size() * sizeof(uint16_t));
        // origin_x/origin_y intentionally untouched: this CLUT keeps living
        // wherever this image's CLUT already was (or (0,0),
        // TIM_CLUT_Header's own default, if it never had one) - switching
        // to a direct-color mode and back must not lose its old spot.
        if (img.selected_clut >= img.clut_header.num_cluts) img.selected_clut = 0;
    } else {
        img.clut_data.clear();
        img.clut_header.colors_per_clut = 0;
        img.clut_header.num_cluts = 0;
        img.clut_header.size = 0;
        // origin_x/origin_y preserved here too, for the same reason.
    }

    img.header.flag = static_cast<uint32_t>(content.type) | (content.has_clut ? 0x08u : 0u);

    img.dirty = true;
    vram_version++;
}

void Document::ReplaceMasterImage(int index, MasterContent&& master) {
    if (index < 0 || index >= static_cast<int>(images.size())) return;
    TIM_Image& img = images[index];
    img.master_rgba = std::move(master.rgba);
    img.master_width = master.width;
    img.master_index_map = std::move(master.index_map);
    img.dirty = true;
    vram_version++;
}

void Document::MarkContentDirty(int index) {
    if (index < 0 || index >= static_cast<int>(images.size())) return;
    images[index].dirty = true;
    vram_version++;
}

bool Document::AddClutSlot(int index) {
    if (index < 0 || index >= static_cast<int>(images.size())) return false;
    TIM_Image& img = images[index];
    if (!img.has_clut) return false;

    int new_num_cluts = img.clut_header.num_cluts + 1;
    if (img.clut_header.origin_y + new_num_cluts > VRAMManager::kHeight) return false;

    int colors = img.clut_header.colors_per_clut;
    size_t old_row_count = img.clut_header.num_cluts;
    img.clut_data.resize(static_cast<size_t>(colors) * new_num_cluts);
    for (int i = 0; i < colors; i++) {
        img.clut_data[static_cast<size_t>(colors) * (new_num_cluts - 1) + i] =
            old_row_count > 0 ? img.clut_data[static_cast<size_t>(colors) * (old_row_count - 1) + i] : 0;
    }
    img.clut_header.num_cluts = static_cast<uint16_t>(new_num_cluts);
    img.clut_header.size = static_cast<uint32_t>(12 + img.clut_data.size() * sizeof(uint16_t));

    img.dirty = true;
    vram_version++;
    return true;
}

bool Document::RemoveClutSlot(int index, int slot) {
    if (index < 0 || index >= static_cast<int>(images.size())) return false;
    TIM_Image& img = images[index];
    if (!img.has_clut || img.clut_header.num_cluts <= 1) return false;
    if (slot < 0 || slot >= img.clut_header.num_cluts) return false;

    int colors = img.clut_header.colors_per_clut;
    img.clut_data.erase(img.clut_data.begin() + static_cast<long>(colors) * slot,
                         img.clut_data.begin() + static_cast<long>(colors) * (slot + 1));
    img.clut_header.num_cluts--;
    img.clut_header.size = static_cast<uint32_t>(12 + img.clut_data.size() * sizeof(uint16_t));
    if (img.selected_clut >= img.clut_header.num_cluts) img.selected_clut = img.clut_header.num_cluts - 1;

    img.dirty = true;
    vram_version++;
    return true;
}

bool Document::IsImageDirty(int index) const {
    if (index < 0 || index >= static_cast<int>(images.size())) return false;
    return images[index].dirty;
}

bool Document::IsFileDirty(const std::string& filepath) const {
    for (const auto& img : images) {
        if (img.filename == filepath && img.dirty) return true;
    }
    return false;
}

bool Document::CloseFile(const std::string& filepath) {
    std::vector<int> indices = IndicesForFile(filepath);
    if (indices.empty()) return false;
    DiscardPendingDelete(); // Indices are about to shift.

    // Remap active_index across the removal: -1 if it was one of the closed
    // images, otherwise shifted down by how many removed images preceded it.
    int new_active = -1;
    if (active_index >= 0) {
        bool active_removed = std::find(indices.begin(), indices.end(), active_index) != indices.end();
        if (!active_removed) {
            int shift = static_cast<int>(std::count_if(indices.begin(), indices.end(),
                                                         [&](int i) { return i < active_index; }));
            new_active = active_index - shift;
        }
    }

    std::vector<TIM_Image> kept;
    kept.reserve(images.size() - indices.size());
    for (int i = 0; i < static_cast<int>(images.size()); i++) {
        if (std::find(indices.begin(), indices.end(), i) == indices.end()) {
            kept.push_back(std::move(images[i]));
        }
    }
    images = std::move(kept);
    active_index = new_active;

    vram_version++;
    structure_version++;
    undo_stack.clear(); // Snapshots are index-based; the layout just changed.
    content_undo_stack.clear();
    return true;
}

bool Document::DeleteImage(int index) {
    if (index < 0 || index >= static_cast<int>(images.size())) return false;
    DiscardPendingDelete(); // Only one level of delete-undo is kept.

    PendingDelete rec;
    rec.image = std::move(images[index]);
    rec.original_index = index;
    rec.seq = next_action_seq++;
    images.erase(images.begin() + index);

    if (active_index == index) active_index = -1;
    else if (active_index > index) active_index--;

    structure_version++;
    vram_version++;
    undo_stack.clear(); // Snapshots are index-based; the layout just changed.
    content_undo_stack.clear();
    pending_delete = std::move(rec);
    return true;
}

bool Document::TakeDiscardedDelete(TIM_Image& out) {
    if (!discarded_delete) return false;
    out = std::move(*discarded_delete);
    discarded_delete.reset();
    return true;
}

void Document::DiscardPendingDelete() {
    if (!pending_delete) return;
    discarded_delete = std::move(pending_delete->image);
    pending_delete.reset();
}

void Document::PushUndoSnapshot() {
    UndoSnapshot snap;
    snap.image_x.reserve(images.size());
    snap.image_y.reserve(images.size());
    snap.clut_x.reserve(images.size());
    snap.clut_y.reserve(images.size());
    snap.image_dirty.reserve(images.size());
    for (const auto& img : images) {
        snap.image_x.push_back(img.image_header.origin_x);
        snap.image_y.push_back(img.image_header.origin_y);
        snap.clut_x.push_back(img.clut_header.origin_x);
        snap.clut_y.push_back(img.clut_header.origin_y);
        snap.image_dirty.push_back(img.dirty);
    }
    snap.seq = next_action_seq++;

    undo_stack.push_back(std::move(snap));
    constexpr size_t kMaxUndoDepth = 100;
    if (undo_stack.size() > kMaxUndoDepth) undo_stack.erase(undo_stack.begin());
}

void Document::DiscardLastUndo() {
    if (!undo_stack.empty()) undo_stack.pop_back();
}

void Document::PushContentUndoSnapshot(int index) {
    if (index < 0 || index >= static_cast<int>(images.size())) return;
    const TIM_Image& img = images[index];

    ContentSnapshot snap;
    snap.index = index;
    snap.type = img.type;
    snap.bpp = img.bpp;
    snap.pixel_width = img.real_width;
    snap.width_words = img.image_header.width;
    snap.height = img.image_header.height;
    snap.image_data = img.image_data;
    snap.has_clut = img.has_clut;
    snap.colors_per_clut = img.clut_header.colors_per_clut;
    snap.num_cluts = img.clut_header.num_cluts;
    snap.clut_data = img.clut_data;
    snap.master_rgba = img.master_rgba;
    snap.master_width = img.master_width;
    snap.master_index_map = img.master_index_map;
    snap.dirty = img.dirty;
    snap.seq = next_action_seq++;

    content_undo_stack.push_back(std::move(snap));
    constexpr size_t kMaxContentUndoDepth = 50;
    if (content_undo_stack.size() > kMaxContentUndoDepth) content_undo_stack.erase(content_undo_stack.begin());
}

bool Document::Undo(int& content_rebuild_index) {
    content_rebuild_index = -1;

    bool have_move = !undo_stack.empty();
    bool have_delete = pending_delete.has_value();
    bool have_content = !content_undo_stack.empty();
    if (!have_move && !have_delete && !have_content) return false;

    int move_seq = have_move ? undo_stack.back().seq : -1;
    int delete_seq = have_delete ? pending_delete->seq : -1;
    int content_seq = have_content ? content_undo_stack.back().seq : -1;

    if (have_content && content_seq >= move_seq && content_seq >= delete_seq) {
        ContentSnapshot snap = std::move(content_undo_stack.back());
        content_undo_stack.pop_back();
        if (snap.index < 0 || snap.index >= static_cast<int>(images.size())) return false;

        TIM_Image& img = images[snap.index];
        img.type = snap.type;
        img.bpp = snap.bpp;
        img.real_width = snap.pixel_width;
        img.image_header.width = static_cast<uint16_t>(snap.width_words);
        img.image_header.height = static_cast<uint16_t>(snap.height);
        img.image_data = std::move(snap.image_data);
        img.image_header.size = static_cast<uint32_t>(12 + img.image_data.size());
        img.has_clut = snap.has_clut;
        img.clut_header.colors_per_clut = static_cast<uint16_t>(snap.colors_per_clut);
        img.clut_header.num_cluts = static_cast<uint16_t>(snap.num_cluts);
        img.clut_data = std::move(snap.clut_data);
        img.clut_header.size = static_cast<uint32_t>(12 + img.clut_data.size() * sizeof(uint16_t));
        img.master_rgba = std::move(snap.master_rgba);
        img.master_width = snap.master_width;
        img.master_index_map = std::move(snap.master_index_map);
        if (img.selected_clut >= img.clut_header.num_cluts) img.selected_clut = std::max(0, img.clut_header.num_cluts - 1);
        img.dirty = snap.dirty;

        vram_version++;
        content_rebuild_index = snap.index;
        return true;
    }

    if (have_delete && delete_seq >= move_seq) {
        int idx = std::clamp(pending_delete->original_index, 0, static_cast<int>(images.size()));
        images.insert(images.begin() + idx, std::move(pending_delete->image));
        pending_delete.reset();
        active_index = idx;
        structure_version++;
        vram_version++;
        undo_stack.clear(); // Indices just shifted again.
        content_undo_stack.clear();
        return true;
    }

    UndoSnapshot snap = std::move(undo_stack.back());
    undo_stack.pop_back();

    // The image set changed since this snapshot was taken - it no longer
    // lines up with `images`, so there's nothing safe to restore.
    if (snap.image_x.size() != images.size()) return false;

    for (size_t i = 0; i < images.size(); i++) {
        images[i].image_header.origin_x = snap.image_x[i];
        images[i].image_header.origin_y = snap.image_y[i];
        images[i].clut_header.origin_x = snap.clut_x[i];
        images[i].clut_header.origin_y = snap.clut_y[i];
        images[i].dirty = snap.image_dirty[i];
    }
    vram_version++;
    return true;
}

std::vector<int> Document::IndicesForFile(const std::string& filepath) const {
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(images.size()); i++) {
        if (images[i].filename == filepath) indices.push_back(i);
    }
    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
        return images[a].file_index < images[b].file_index;
    });
    return indices;
}

bool Document::Save(const std::string& filepath) {
    std::vector<int> indices = IndicesForFile(filepath);
    if (indices.empty()) return false;

    std::vector<const TIM_Image*> to_write;
    for (int i : indices) to_write.push_back(&images[i]);

    if (!Writer::WriteToFile(filepath, to_write)) return false;
    for (int i : indices) images[i].dirty = false;
    return true;
}

bool Document::SaveAs(const std::string& filepath, const std::string& new_filepath) {
    std::vector<int> indices = IndicesForFile(filepath);
    if (indices.empty()) return false;

    std::vector<const TIM_Image*> to_write;
    for (int i : indices) to_write.push_back(&images[i]);

    if (!Writer::WriteToFile(new_filepath, to_write)) return false;

    for (int i : indices) {
        images[i].filename = new_filepath;
        images[i].dirty = false; // Freshly written, nothing pending
    }
    return true;
}

} // namespace tim
