#include "tim_document.h"
#include "tim_writer.h"
#include "vram_manager.h"
#include <algorithm>

namespace tim {

int Document::AddImages(std::vector<TIM_Image>&& new_images) {
    if (new_images.empty()) return -1;
    int first_index = static_cast<int>(images.size());
    for (auto& img : new_images) {
        images.push_back(std::move(img));
    }
    vram_version++;
    structure_version++;
    return first_index;
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
    dirty_files.insert(img.filename);
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
    dirty_files.insert(img.filename);
    vram_version++;
}

bool Document::IsFileDirty(const std::string& filepath) const {
    return dirty_files.count(filepath) != 0;
}

bool Document::CloseFile(const std::string& filepath) {
    std::vector<int> indices = IndicesForFile(filepath);
    if (indices.empty()) return false;

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

    dirty_files.erase(filepath);
    vram_version++;
    structure_version++;
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
    dirty_files.erase(filepath);
    return true;
}

bool Document::SaveAs(const std::string& filepath, const std::string& new_filepath) {
    std::vector<int> indices = IndicesForFile(filepath);
    if (indices.empty()) return false;

    std::vector<const TIM_Image*> to_write;
    for (int i : indices) to_write.push_back(&images[i]);

    if (!Writer::WriteToFile(new_filepath, to_write)) return false;

    for (int i : indices) images[i].filename = new_filepath;
    dirty_files.erase(filepath);
    dirty_files.erase(new_filepath); // Freshly written, nothing pending
    return true;
}

} // namespace tim
